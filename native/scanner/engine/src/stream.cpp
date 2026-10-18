#include "internal.hpp"
#include "photo_cpu.hpp"
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <opencv2/core/ocl.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <thread>

namespace pen::stream {
struct Result::Impl {
    mutable std::mutex mutex, canvasMutex;
    std::shared_ptr<Budget> budget;
    ScanId scan = 0;
    std::size_t reservation = 0;
    Config config;
    std::vector<float> rawGain;
    MemoryCanvas canvas;
    std::vector<FramePtr> unlocated;
    Counts count;
    Status state = Status::Processing;
    std::string failure;
    explicit Impl(std::shared_ptr<Budget> b) : budget(b), canvas(std::move(b)) {
        ++budget->sessions;
    }
    ~Impl() {
        std::vector<FramePtr>().swap(unlocated);
        std::vector<float>().swap(rawGain);
        canvas.clear();
        config.calibration.reset();
        budget->other -= reservation;
        --budget->sessions;
    }
};
Result::Result(std::shared_ptr<Impl> p) : p_(std::move(p)) {}
Result::~Result() = default;
ScanId Result::scanId() const {
    return p_->scan;
}
Status Result::status() const {
    std::lock_guard<std::mutex> l(p_->mutex);
    return p_->state;
}
Counts Result::counts() const {
    std::lock_guard<std::mutex> l(p_->mutex);
    auto c = p_->count;
    c.pending = c.received - c.filtered - c.committed - c.unlocated;
    return c;
}
Bounds Result::bounds() const {
    std::lock_guard<std::mutex> l(p_->canvasMutex);
    return p_->canvas.bounds();
}
std::string Result::reason() const {
    std::lock_guard<std::mutex> l(p_->mutex);
    return p_->failure;
}
Code Result::readRow(int y, std::uint8_t *g, std::uint8_t *m, std::size_t n) const {
    std::lock_guard<std::mutex> l(p_->canvasMutex);
    return p_->canvas.row(y, g, m, n);
}
Code Result::readPreview(int w, int h, std::uint8_t *g, std::uint8_t *m, std::size_t n) const {
    std::lock_guard<std::mutex> l(p_->canvasMutex);
    return p_->canvas.preview(w, h, g, m, n);
}
std::size_t Result::unlocatedCount() const {
    std::lock_guard<std::mutex> l(p_->mutex);
    return p_->unlocated.size();
}
RawFrameView Result::unlocatedFrame(std::size_t i) const {
    std::lock_guard<std::mutex> l(p_->mutex);
    if (i >= p_->unlocated.size())
        return {};
    auto &f = *p_->unlocated[i];
    return {f.y(), f.width, f.height, f.width, f.sequence, f.timestamp};
}

struct Engine::Impl {
    struct Scan {
        ScanId id = 0;
        Callbacks cb;
        std::shared_ptr<Result::Impl> result;
        std::deque<FramePtr> raw, window;
        FramePtr first;
        Selection selector;
        WindowGeometry geometry;
        bool sealed = false, preprocessed = false, capacity = false, haveBatch = false;
        unsigned reserveRemaining = 0;
        std::atomic<bool> broken{false};
        const char *pendingStop = nullptr;
        const char *capacityReason = nullptr;
        std::uint64_t seen = 0, selected = 0, start = 0;
    };
    struct Job {
        std::shared_ptr<Scan> scan;
        std::vector<FramePtr> frames;
        std::uint64_t start = 0;
        bool last = false, flush = false;
    };
    struct Render {
        Job job;
        std::vector<y07::DecodedFrame> decoded;
        std::string error;
    };
    std::shared_ptr<Budget> budget;
    std::unique_ptr<Inference> inference;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::map<ScanId, std::shared_ptr<Scan>> scans;
    std::deque<Job> jobs;
    std::deque<Render> renders;
    ScanId next = 1, capturing = 0;
    bool stopping = false, preDone = false, npuDone = false, blocked = false;
    bool inferBusy = false, renderBusy = false, warmRequested = false;
    std::thread preThread, npuThread, renderThread;
    std::vector<float> brightnessScratch;
    explicit Impl(std::unique_ptr<Inference> i, Limits l)
        : budget(std::make_shared<Budget>(l)), inference(std::move(i)) {
        if (!inference || l.maxFrames < 32 || l.maxFrames > 256 || l.managedBytes > 96 * MiB ||
            l.rssSoftBytes > 240 * MiB || l.highFrames < 32 || l.highFrames > l.maxFrames ||
            !l.maxSessions || l.maxSessions > 4 || l.managedBytes < 10 * MiB ||
            l.frameBytes > 48 * MiB || l.canvasBytes > 32 * MiB)
            throw std::invalid_argument("invalid_stream_limits");
        // Conservative fixed reservation: 5.625 MiB feature buffer, two nine-
        // head sets, CPU scratch, bounded queues and thread-local working maps.
        budget->other = 8 * MiB;
        brightnessScratch.reserve(160 * 288);
        cv::setNumThreads(1);
        cv::ocl::setUseOpenCL(false);
        try {
            preThread = std::thread([this] { preLoop(); });
            npuThread = std::thread([this] { npuLoop(); });
            renderThread = std::thread([this] { renderLoop(); });
        } catch (...) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                stopping = true;
                cv.notify_all();
            }
            if (preThread.joinable())
                preThread.join();
            if (npuThread.joinable())
                npuThread.join();
            if (renderThread.joinable())
                renderThread.join();
            throw;
        }
    }
    ~Impl() {
        stop();
        budget->other -= 8 * MiB;
    }
    static void emit(const std::shared_ptr<Scan> &s, Event::Kind k, const char *why = "",
                     const FramePtr &f = {}, const cv::Matx33d *h = nullptr) noexcept {
        if (!s->cb.event)
            return;
        Event e{};
        e.kind = k;
        e.scan = s->id;
        e.reason = why;
        if (f) {
            e.sequence = f->sequence;
            e.timestamp = f->timestamp;
            e.selectedIndex = f->index;
        }
        if (h)
            std::copy(std::begin(h->val), std::end(h->val), e.rawToCanvas.begin());
        try {
            s->cb.event(e);
        } catch (...) { /* Callback exceptions never kill processing. */
        }
    }
    void stopCapture(const std::shared_ptr<Scan> &s, const char *reason, bool hard = false) {
        std::lock_guard<std::mutex> l(mutex);
        blocked = true;
        auto request = [&](const std::shared_ptr<Scan> &target) {
            if (!target->capacity) {
                target->capacity = true;
                target->pendingStop = reason;
                target->capacityReason = reason;
                target->reserveRemaining =
                    std::min(16u, budget->limits.maxFrames - budget->limits.highFrames);
                if (!target->reserveRemaining) {
                    target->sealed = true;
                    if (capturing == target->id)
                        capturing = 0;
                }
            }
        };
        request(s);
        if (hard) {
            s->sealed = true;
            if (capturing == s->id)
                capturing = 0;
        }
        if (capturing)
            request(scans.at(capturing));
        cv.notify_all();
    }
    void enqueue(Job job) {
        std::unique_lock<std::mutex> l(mutex);
        cv.wait(l, [&] { return jobs.size() < 8; });
        jobs.push_back(std::move(job));
        cv.notify_all();
    }
    void select(const std::shared_ptr<Scan> &s, FramePtr f, const cv::Mat &upright) {
        f->index = s->selected++;
        cv::Mat match(144, 80, CV_8UC1, f->match());
        resizeCamera(upright, match, 80, 144);
        cv::Mat small;
        resizeCamera(upright, small, 160, 288);
        try {
            f->brightness = pen_photo::brightness_neon(small.ptr<std::uint8_t>(), 160, 288,
                                                       s->result->config.calibration->gain.data(),
                                                       brightnessScratch);
        } catch (const std::runtime_error &) {
            f->photoValid = false;
            f->brightness = 1;
            emit(s, Event::Kind::Error, "insufficient_paper_background_retained", f);
        }
        {
            std::lock_guard<std::mutex> l(s->result->mutex);
            ++s->result->count.selected;
        }
        emit(s, Event::Kind::Selected, "", f);
        s->window.push_back(std::move(f));
        if (s->window.size() == 32) {
            Job job{s, {s->window.begin(), s->window.end()}, s->start, false, false};
            enqueue(std::move(job));
            for (unsigned i = 0; i < 24; ++i)
                s->window.pop_front();
            s->start += 24;
            s->haveBatch = true;
        }
    }
    void filtered(const std::shared_ptr<Scan> &s, const FramePtr &f) {
        {
            std::lock_guard<std::mutex> l(s->result->mutex);
            ++s->result->count.filtered;
        }
        emit(s, Event::Kind::Filtered, "original_selection", f);
    }
    void preLoop() {
        for (;;) {
            std::shared_ptr<Scan> s;
            FramePtr f;
            bool finish = false;
            const char *notice = nullptr;
            {
                std::unique_lock<std::mutex> l(mutex);
                cv.wait(l, [&] {
                    for (auto &e : scans)
                        if (e.second->pendingStop || (!e.second->preprocessed &&
                                                      (!e.second->raw.empty() || e.second->sealed)))
                            return true;
                    return stopping;
                });
                for (auto &e : scans)
                    if (e.second->pendingStop) {
                        s = e.second;
                        notice = s->pendingStop;
                        s->pendingStop = nullptr;
                        break;
                    }
                for (auto &e : scans)
                    if (!s && !e.second->preprocessed) {
                        s = e.second;
                        break;
                    }
                if (!s) {
                    if (stopping)
                        break;
                    continue;
                }
                if (notice) {
                } else if (!s->raw.empty()) {
                    f = std::move(s->raw.front());
                    s->raw.pop_front();
                } else if (s->sealed)
                    finish = true;
                else {
                    cv.wait(l);
                    continue;
                }
            }
            if (notice) {
                {
                    std::lock_guard<std::mutex> lock(s->result->mutex);
                    if (s->result->failure.empty())
                        s->result->failure = notice;
                }
                emit(s, Event::Kind::StopRequested, notice);
                continue;
            }
            try {
                if (f) {
                    auto memory = rss();
                    budget->observedRss = memory.first;
                    if (memory.first >= budget->limits.rssSoftBytes)
                        stopCapture(s, "rss_soft_high_water");
                    auto upright = rotateY(cv::Mat(f->height, f->width, CV_8UC1, f->y()),
                                           s->result->config.rotation);
                    bool accepted = s->selector.accept(upright),
                         all = s->result->config.diagnosticAcceptAll;
                    if (s->first) {
                        filtered(s, s->first);
                        s->first.reset();
                    }
                    if (s->seen++ == 0 && !all)
                        s->first = f;
                    else if (all || accepted)
                        select(s, f, upright);
                    else
                        filtered(s, f);
                } else if (finish) {
                    if (s->first) {
                        auto raw = s->first;
                        auto upright = rotateY(cv::Mat(raw->height, raw->width, CV_8UC1, raw->y()),
                                               s->result->config.rotation);
                        if (Selection::texture(s->selector.previous) >= 100)
                            select(s, raw, upright);
                        else
                            filtered(s, raw);
                        s->first.reset();
                    }
                    bool flush = s->haveBatch && s->window.size() == 8;
                    Job tail{s, {s->window.begin(), s->window.end()}, s->start, true, flush};
                    s->window.clear();
                    s->selector.previous.release();
                    enqueue(std::move(tail));
                    {
                        std::lock_guard<std::mutex> l(mutex);
                        s->preprocessed = true;
                        cv.notify_all();
                    }
                }
            } catch (const std::exception &e) {
                // Retain uniquely tagged sources and clear a failed window;
                // it must never grow past 32 after an allocation exception.
                s->broken = true;
                auto keep = [&](const FramePtr &frame) {
                    if (!frame)
                        return;
                    if (frame->index == std::uint64_t(-1))
                        frame->index = s->selected++;
                    {
                        std::lock_guard<std::mutex> lock(s->result->mutex);
                        s->result->count.selected = s->selected;
                    }
                    retain(s, frame);
                };
                keep(f);
                keep(s->first);
                for (const auto &item : s->window)
                    keep(item);
                s->first.reset();
                s->window.clear();
                stopCapture(s, "preprocessing_allocation_or_conversion_failure");
                emit(s, Event::Kind::Error, "preprocessing_failure");
            }
        }
        {
            std::lock_guard<std::mutex> l(mutex);
            preDone = true;
            cv.notify_all();
        }
    }
    bool idleLocked() const {
        if (capturing || !jobs.empty() || !renders.empty() || inferBusy || renderBusy)
            return false;
        for (auto &e : scans)
            if (!e.second->preprocessed)
                return false;
        return true;
    }
    void npuLoop() {
        bool opened = false, idleArmed = false;
        auto idleSince = std::chrono::steady_clock::now();
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> l(mutex);
                while (jobs.empty() && !preDone) {
                    if (warmRequested) {
                        warmRequested = false;
                        inferBusy = true;
                        l.unlock();
                        try {
                            if (!opened) {
                                inference->open();
                                opened = true;
                                budget->loaded = true;
                            }
                        } catch (...) {
                            inference->close();
                            opened = false;
                            budget->loaded = false;
                        }
                        l.lock();
                        inferBusy = false;
                        idleArmed = false;
                        cv.notify_all();
                        continue;
                    }
                    if (opened && idleLocked()) {
                        if (!idleArmed) {
                            idleSince = std::chrono::steady_clock::now();
                            idleArmed = true;
                        }
                        auto deadline =
                            idleSince + std::chrono::milliseconds(budget->limits.idleModelMs);
                        if (cv.wait_until(l, deadline) == std::cv_status::timeout && idleLocked()) {
                            l.unlock();
                            inference->close();
                            opened = false;
                            budget->loaded = false;
                            l.lock();
                        }
                    } else {
                        idleArmed = false;
                        cv.wait(l);
                    }
                }
                if (jobs.empty() && preDone)
                    break;
                job = std::move(jobs.front());
                jobs.pop_front();
                idleArmed = false;
                inferBusy = true;
                cv.notify_all();
            }
            Render render;
            render.job = std::move(job);
            if (!render.job.scan->broken && !render.job.flush && render.job.frames.size() > 1) {
                try {
                    if (render.job.frames.size() > 32)
                        throw std::runtime_error("internal_batch_overflow");
                    if (!opened) {
                        inference->open();
                        opened = true;
                        budget->loaded = true;
                    }
                    NetworkBatch batch;
                    batch.validCount = render.job.frames.size();
                    for (unsigned i = 0; i < batch.validCount; ++i)
                        batch.gray[i] = render.job.frames[i]->match();
                    Heads heads;
                    inference->infer(batch, heads);
                    if (inference->managedBytes() > 6 * MiB)
                        throw std::runtime_error("backend_feature_budget_exceeded");
                    std::array<y07::FloatHead, 9> views;
                    for (unsigned h = 0; h < 9; ++h)
                        views[h] = {heads[h].data(), heads[h].size()};
                    render.decoded = y07::decodeBatch(views, 32);
                    {
                        std::lock_guard<std::mutex> l(render.job.scan->result->mutex);
                        ++render.job.scan->result->count.inferenceBatches;
                    }
                } catch (const std::exception &e) {
                    render.error = e.what();
                    inference->close();
                    opened = false;
                    budget->loaded = false;
                }
            }
            {
                std::unique_lock<std::mutex> l(mutex);
                cv.wait(l, [&] { return renders.size() < 2; });
                renders.push_back(std::move(render));
                inferBusy = false;
                idleSince = std::chrono::steady_clock::now();
                cv.notify_all();
            }
        }
        inference->close();
        budget->loaded = false;
        {
            std::lock_guard<std::mutex> l(mutex);
            npuDone = true;
            cv.notify_all();
        }
    }
    void retain(const std::shared_ptr<Scan> &s, const FramePtr &f) {
        auto &r = *s->result;
        std::lock_guard<std::mutex> l(r.mutex);
        if (f->index < r.count.committed)
            return;
        auto it = std::lower_bound(r.unlocated.begin(), r.unlocated.end(), f->index,
                                   [](auto &a, std::uint64_t i) { return a->index < i; });
        if (it == r.unlocated.end() || (*it)->index != f->index) {
            r.unlocated.insert(it, f);
            ++r.count.unlocated;
        }
    }
    void renderLoop() {
        for (;;) {
            Render render;
            {
                std::unique_lock<std::mutex> l(mutex);
                cv.wait(l, [&] { return !renders.empty() || npuDone; });
                if (renders.empty())
                    break;
                render = std::move(renders.front());
                renders.pop_front();
                renderBusy = true;
                cv.notify_all();
            }
            auto &job = render.job;
            auto s = job.scan;
            auto &r = *s->result;
            if (!render.error.empty()) {
                s->broken = true;
                std::lock_guard<std::mutex> l(r.mutex);
                if (r.failure.empty())
                    r.failure = render.error;
            }
            try {
                std::vector<Pose> poses;
                if (!s->broken) {
                    if (job.flush)
                        poses = s->geometry.flush();
                    else if (job.frames.size() == 1)
                        poses.push_back({job.start, cv::Matx33d::eye()});
                    else if (!job.frames.empty())
                        poses = s->geometry.consume(job.start, render.decoded, job.frames.size(),
                                                    job.last);
                }
                for (const auto &pose : poses) {
                    if (s->broken)
                        break;
                    if (pose.index < job.start || pose.index - job.start >= job.frames.size())
                        throw std::runtime_error("commit_frame_range_mismatch");
                    auto f = job.frames[pose.index - job.start];
                    cv::Mat corrected(f->height, f->width, CV_8UC1);
                    pen_photo::apply_neon(f->y(), corrected.ptr<std::uint8_t>(),
                                          std::size_t(f->width) * f->height, r.rawGain.data(),
                                          f->brightness);
                    auto small = rawToWork(f->width, f->height, r.config.rotation);
                    int ow = f->width, oh = f->height;
                    if (r.config.rotation == Rotation::Clockwise90 ||
                        r.config.rotation == Rotation::CounterClockwise90)
                        std::swap(ow, oh);
                    double sx = double(ow) / 160, sy = double(oh) / 288;
                    cv::Matx33d enlarge(sx, 0, (sx - 1) * .5, 0, sy, (sy - 1) * .5, 0, 0, 1);
                    auto h = enlarge * pose.work * small;
                    bool ok;
                    bool first;
                    {
                        std::lock_guard<std::mutex> l(r.mutex);
                        if (f->index != r.count.committed)
                            throw std::runtime_error("non_sequential_commit");
                        first = r.count.committed == 0;
                    }
                    {
                        std::lock_guard<std::mutex> l(r.canvasMutex);
                        ok = r.canvas.draw(corrected, h, small, first);
                    }
                    if (ok) {
                        std::lock_guard<std::mutex> l(r.mutex);
                        ++r.count.committed;
                    }

                    if (!ok) {
                        s->broken = true;
                        stopCapture(s, "canvas_capacity");
                        break;
                    }
                    emit(s, Event::Kind::Submitted, "", f, &h);
                }
            } catch (const std::bad_alloc &) {
                s->broken = true;
                stopCapture(s, "render_memory_capacity");
            } catch (const std::exception &e) {
                s->broken = true;
                std::lock_guard<std::mutex> l(r.mutex);
                if (r.failure.empty())
                    r.failure = e.what();
            }
            if (s->broken)
                for (auto &f : job.frames)
                    retain(s, f);
            emit(s, Event::Kind::Preview);
            if (job.last) {
                bool capacity;
                const char *capacityReason;
                {
                    std::lock_guard<std::mutex> g(mutex);
                    capacity = s->capacity;
                    capacityReason = s->capacityReason;
                }
                {
                    std::lock_guard<std::mutex> l(r.mutex);
                    if (capacity && r.failure.empty() && capacityReason)
                        r.failure = capacityReason;
                    r.state = capacity ? Status::CapacityStopped
                                       : (r.count.selected == 0
                                              ? Status::Empty
                                              : (!s->broken ? Status::Complete
                                                            : (r.count.committed
                                                                   ? Status::Segmented
                                                                   : Status::LocalizationFailed)));
                }
                // Gain/calibration and frame pages are no longer needed once
                // all queued jobs for this scan have drained.
                r.rawGain.clear();
                r.rawGain.shrink_to_fit();
                r.config.calibration.reset();
                emit(s, Event::Kind::Finished);
            }
            {
                std::lock_guard<std::mutex> l(mutex);
                renderBusy = false;
                cv.notify_all();
            }
        }
    }
    void stop() {
        {
            std::lock_guard<std::mutex> l(mutex);
            if (stopping)
                return;
            stopping = true;
            capturing = 0;
            for (auto &e : scans)
                e.second->sealed = true;
            cv.notify_all();
        }
        if (preThread.joinable())
            preThread.join();
        if (npuThread.joinable())
            npuThread.join();
        if (renderThread.joinable())
            renderThread.join();
    }
};

Engine::Engine(std::unique_ptr<Inference> i, Limits l)
    : p_(std::make_unique<Impl>(std::move(i), l)) {}
Engine::~Engine() = default;
BeginResult Engine::beginScan(const Config &c, Callbacks cb) {
    if (int(c.rotation) < 0 || int(c.rotation) > 3 || c.width < 2 || c.height < 2 ||
        std::size_t(c.width) * c.height > 1024 * 1024)
        return {Code::InvalidArgument, 0};
    auto k = c.calibration;
    if (!k || k->version.empty() || k->sensorWidth != c.width || k->sensorHeight != c.height ||
        k->rotation != c.rotation)
        return {Code::MissingCalibration, 0};
    for (float v : k->gain)
        if (!std::isfinite(v) || v < .8f || v > 1.4f)
            return {Code::MissingCalibration, 0};
    std::lock_guard<std::mutex> l(p_->mutex);
    auto b = p_->budget;
    std::size_t frame = (std::size_t(c.width) * c.height + 80 * 144 + 4095) & ~std::size_t(4095);
    if (p_->stopping)
        return {Code::Closed, 0};
    if (p_->blocked && b->frameCount == 0 && b->canvas == 0)
        p_->blocked = false;
    if (p_->capturing || p_->blocked || b->sessions >= b->limits.maxSessions ||
        b->frameCount + 32 > b->limits.maxFrames || b->frames + frame * 32 > b->limits.frameBytes ||
        rss().first >= b->limits.rssSoftBytes)
        return {Code::Busy, 0};
    auto reserve = ((std::size_t(c.width) * c.height * sizeof(float) + sizeof(Calibration) +
                     128 * 128 * 7 + 80 * 1024 + 4095) &
                    ~std::size_t(4095));
    std::lock_guard<std::recursive_mutex> admission(b->admission);
    if (b->total() + reserve + frame * 32 > b->limits.managedBytes)
        return {Code::Busy, 0};
    try {
        auto r = std::make_shared<Result::Impl>(b);
        r->reservation = reserve;
        b->other += reserve;
        r->config = c;
        r->config.calibration = std::make_shared<const Calibration>(*k);
        r->unlocated.reserve(b->limits.maxFrames);
        int ow = c.width, oh = c.height;
        if (c.rotation == Rotation::Clockwise90 || c.rotation == Rotation::CounterClockwise90)
            std::swap(ow, oh);
        cv::Mat g(288, 160, CV_32F, const_cast<float *>(k->gain.data())), up;
        cv::resize(g, up, {ow, oh});
        auto inverse =
            c.rotation == Rotation::Clockwise90
                ? Rotation::CounterClockwise90
                : (c.rotation == Rotation::CounterClockwise90 ? Rotation::Clockwise90 : c.rotation);
        auto raw = rotateY(up, inverse);
        r->rawGain.assign(raw.ptr<float>(), raw.ptr<float>() + std::size_t(c.width) * c.height);
        auto s = std::make_shared<Impl::Scan>();
        s->id = p_->next++;
        r->scan = s->id;
        s->cb = std::move(cb);
        s->result = std::move(r);
        p_->scans.emplace(s->id, s);
        p_->capturing = s->id;
        p_->warmRequested = true;
        p_->cv.notify_all();
        return {Code::Ok, s->id};
    } catch (const std::bad_alloc &) {
        return {Code::Busy, 0};
    }
}
PushResult Engine::pushFrame(ScanId id, const std::uint8_t *y, int w, int h, std::size_t stride,
                             std::uint64_t seq, std::int64_t ts) {
    std::shared_ptr<Impl::Scan> s;
    bool high = false, accepted = false, hadCapacity = false;
    {
        std::lock_guard<std::mutex> l(p_->mutex);
        auto it = p_->scans.find(id);
        if (it == p_->scans.end())
            return {Code::NotFound, false};
        s = it->second;
        if (!y || w != s->result->config.width || h != s->result->config.height ||
            stride < std::size_t(w) || stride > std::size_t(-1) / std::size_t(h))
            return {Code::InvalidArgument, false};
        if (s->sealed || p_->stopping)
            return {s->capacity ? Code::StopCapture : Code::Closed, false};
        auto b = p_->budget;
        hadCapacity = s->capacity;
        try {
            if (b->frameCount >= b->limits.maxFrames)
                throw std::bad_alloc();
            auto f = std::make_shared<Frame>();
            f->width = w;
            f->height = h;
            f->sequence = seq;
            f->timestamp = ts;
            f->storage.allocate(std::size_t(w) * h + 80 * 144, b, b->frames, b->limits.frameBytes);
            f->budget = b;
            ++b->frameCount;
            for (int row = 0; row < h; ++row)
                std::memcpy(f->y() + std::size_t(row) * w, y + std::size_t(row) * stride, w);
            s->raw.push_back(std::move(f));
            accepted = true;
            if (hadCapacity && s->reserveRemaining && --s->reserveRemaining == 0) {
                s->sealed = true;
                if (p_->capturing == id)
                    p_->capturing = 0;
            }
            {
                std::lock_guard<std::mutex> r(s->result->mutex);
                ++s->result->count.received;
            }
            high = b->frameCount >= b->limits.highFrames ||
                   b->observedRss.load() >= b->limits.rssSoftBytes;
            p_->cv.notify_all();
        } catch (const std::bad_alloc &) {
            high = true;
        }
        if (hadCapacity && accepted)
            return {Code::StopCapture, true};
        if (!high)
            return {Code::Ok, true};
    }
    // The high-water frame has been copied. A hard allocation failure did not
    // accept the submitted frame, and the caller retains its original buffer.
    p_->stopCapture(s, "ingress_capacity_or_rss_high_water", !accepted);
    return {Code::StopCapture, accepted};
}
Code Engine::endScan(ScanId id) {
    std::lock_guard<std::mutex> l(p_->mutex);
    auto it = p_->scans.find(id);
    if (it == p_->scans.end())
        return Code::NotFound;
    it->second->sealed = true;
    if (p_->capturing == id)
        p_->capturing = 0;
    p_->cv.notify_all();
    return Code::Ok;
}
Code Engine::query(ScanId id, Status &state, Counts &counts) const {
    auto result = this->result(id);
    if (result) {
        state = result->status();
        counts = result->counts();
        return Code::Ok;
    }
    std::lock_guard<std::mutex> l(p_->mutex);
    auto it = p_->scans.find(id);
    if (it == p_->scans.end())
        return Code::NotFound;
    std::lock_guard<std::mutex> r(it->second->result->mutex);
    state = it->second->result->state;
    counts = it->second->result->count;
    counts.pending = counts.received - counts.filtered - counts.committed - counts.unlocated;
    return Code::Ok;
}
ResultHandle Engine::preview(ScanId id) const {
    std::lock_guard<std::mutex> l(p_->mutex);
    auto it = p_->scans.find(id);
    return it == p_->scans.end() ? ResultHandle{}
                                 : std::shared_ptr<const Result>(new Result(it->second->result));
}
ResultHandle Engine::result(ScanId id) const {
    std::lock_guard<std::mutex> l(p_->mutex);
    auto it = p_->scans.find(id);
    if (it == p_->scans.end())
        return {};
    std::lock_guard<std::mutex> r(it->second->result->mutex);
    if (it->second->result->state == Status::Processing)
        return {};
    return std::shared_ptr<const Result>(new Result(it->second->result));
}
ResultHandle Engine::wait(ScanId id) {
    std::unique_lock<std::mutex> l(p_->mutex);
    p_->cv.wait(l, [&] {
        auto it = p_->scans.find(id);
        if (it == p_->scans.end())
            return true;
        std::lock_guard<std::mutex> r(it->second->result->mutex);
        return it->second->result->state != Status::Processing;
    });
    auto it = p_->scans.find(id);
    return it == p_->scans.end() ? ResultHandle{}
                                 : std::shared_ptr<const Result>(new Result(it->second->result));
}
Code Engine::releaseResult(ResultHandle &h) {
    if (!h)
        return Code::InvalidArgument;
    auto code = releaseResult(h->scanId());
    if (code == Code::Ok || code == Code::NotFound)
        h.reset();
    return code;
}
Code Engine::releaseResult(ScanId id) {
    std::shared_ptr<Impl::Scan> old;
    {
        std::lock_guard<std::mutex> l(p_->mutex);
        auto it = p_->scans.find(id);
        if (it == p_->scans.end())
            return Code::NotFound;
        {
            std::lock_guard<std::mutex> r(it->second->result->mutex);
            if (it->second->result->state == Status::Processing)
                return Code::NotReady;
        }
        old = std::move(it->second);
        p_->scans.erase(it);
        p_->cv.notify_all();
    }
    old.reset();
    return Code::Ok;
}
Metrics Engine::metrics() const {
    auto b = p_->budget;
    auto mem = rss();
    b->observedRss = mem.first;
    return {b->frames,  b->canvas,     b->total(),  mem.first,
            mem.second, b->frameCount, b->sessions, b->loaded};
}
void Engine::shutdown() {
    p_->stop();
}
} // namespace pen::stream
