#include "internal.hpp"
#include "pen_stream/stream.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>
using namespace pen::stream;
static void require(bool b, const char *s) {
    if (!b)
        throw std::runtime_error(s);
}
struct Probe {
    std::atomic<unsigned> calls{0}, opens{0}, closes{0};
    unsigned delay = 0;
    bool fail = false;
};
class Fake final : public Inference {
    std::shared_ptr<Probe> p;

  public:
    explicit Fake(std::shared_ptr<Probe> q) : p(q) {}
    void open() override {
        ++p->opens;
    }
    void close() noexcept override {
        ++p->closes;
    }
    std::size_t managedBytes() const noexcept override {
        return 0;
    }
    void infer(const NetworkBatch &b, Heads &h) override {
        ++p->calls;
        if (p->delay)
            std::this_thread::sleep_for(std::chrono::milliseconds(p->delay));
        if (p->fail)
            throw std::runtime_error("injected_npu_failure");
        require(b.validCount > 1 && b.validCount <= 32, "valid count");
        for (unsigned i = 0; i < b.validCount; ++i)
            require(b.gray[i], "frame view alive");
        const unsigned bins[9] = {384, 384, 512, 512, 512, 512, 512, 512, 1};
        for (unsigned k = 0; k < 9; ++k) {
            h[k].assign(32 * bins[k], -80);
            for (unsigned t = 0; t < 32; ++t) {
                unsigned at = bins[k] / 2;
                if (k == 0)
                    at += 2;
                h[k][t * bins[k] + at] = 0;
            }
        }
    }
};
static Config config(int w = 64, int h = 48) {
    Config c;
    c.width = w;
    c.height = h;
    c.rotation = Rotation::None;
    c.diagnosticAcceptAll = true;
    auto k = std::make_shared<Calibration>();
    k->version = "synthetic-constant";
    k->sensorWidth = w;
    k->sensorHeight = h;
    k->rotation = c.rotation;
    k->gain.fill(1);
    c.calibration = k;
    return c;
}
static std::vector<std::uint8_t> image(const Config &c) {
    std::vector<std::uint8_t> v(c.width * c.height, 210);
    for (int y = 10; y < c.height - 10; ++y)
        for (int x = 8; x < c.width - 8; ++x)
            if ((x + y) % 11 < 3)
                v[y * c.width + x] = 30;
    return v;
}
static void boundaries() {
    for (unsigned n = 0; n <= 96; ++n) {
        auto probe = std::make_shared<Probe>();
        Engine e(std::make_unique<Fake>(probe));
        auto c = config();
        auto im = image(c);
        std::vector<std::uint64_t> commits;
        auto s = e.beginScan(c, {[&](const Event &v) {
                                 if (v.kind == Event::Kind::Submitted)
                                     commits.push_back(v.selectedIndex);
                             }});
        require(s.code == Code::Ok, "begin");
        for (unsigned i = 0; i < n; ++i) {
            auto p = e.pushFrame(s.scan, im.data(), c.width, c.height, c.width, 100 + i, i * 10540);
            require(p.accepted, "boundary push");
        }
        e.endScan(s.scan);
        auto r = e.wait(s.scan);
        require(r && r->status() == (n ? Status::Complete : Status::Empty), "boundary status");
        auto count = r->counts();
        require(count.received == n && count.selected == n && count.committed == n &&
                    !count.pending && !count.unlocated,
                "boundary accounting");
        require(commits.size() == n, "commit event count");
        for (unsigned i = 0; i < n; ++i)
            require(commits[i] == i, "commit exactly once ordered");
        unsigned expected = n <= 1 ? 0 : (n <= 32 ? 1 : 1 + (n - 32 + 23) / 24);
        require(probe->calls == expected, "no redundant overlap inference");
        if (n == 1) {
            auto b = r->bounds();
            require(b.width == c.width && b.height == c.height, "single full view");
            std::vector<std::uint8_t> g(b.width), m(b.width);
            for (int y = 0; y < b.height; ++y) {
                require(r->readRow(y, g.data(), m.data(), m.size()) == Code::Ok, "row");
                for (auto mask : m)
                    require(mask == 255, "single complete mask");
            }
        }
        e.releaseResult(s.scan);
        r.reset();
        e.shutdown();
        require(e.metrics().frames == 0 && e.metrics().sessions == 0,
                "prompt frame/session cleanup");
    }
}
static void coordinate() {
    for (auto r : {Rotation::None, Rotation::Clockwise90, Rotation::HalfTurn,
                   Rotation::CounterClockwise90}) {
        int w = 496, h = 360, ow = w, oh = h;
        if (r == Rotation::Clockwise90 || r == Rotation::CounterClockwise90)
            std::swap(ow, oh);
        auto S = rawToWork(w, h, r), R = rawToWork(w, h, r, ow, oh);
        double sx = double(ow) / 160, sy = double(oh) / 288;
        cv::Matx33d D(sx, 0, (sx - 1) * .5, 0, sy, (sy - 1) * .5, 0, 0, 1);
        auto H = D * S;
        for (int y = 0; y < h; y += 29)
            for (int x = 0; x < w; x += 31) {
                auto a = H * cv::Vec3d(x, y, 1), b = R * cv::Vec3d(x, y, 1);
                require(cv::norm(a - b) < 1e-9, "coordinate round trip < 1px");
            }
    }
    WindowGeometry g;
    std::vector<pen::y07::DecodedFrame> f(32);
    for (auto &v : f) {
        v.dx = 2;
        v.dy = 0;
    }
    f[0].dx = 0;
    auto first = g.consume(0, f, 32, false);
    auto next = g.consume(24, f, 32, false);
    auto tail = g.flush();
    require(first.size() == 24 && next.size() == 24 && tail.size() == 8, "window output counts");
    require(std::abs(next[0].work(0, 2) - 96) < 1e-4, "overlap boundary accumulated once");
    require(std::abs(tail.back().work(0, 2) - 220) < 1e-4, "tail coordinate");
}
static void pressure() {
    {
        auto check = std::make_shared<Probe>();
        Limits tight;
        tight.managedBytes = 10 * MiB;
        Engine admission(std::make_unique<Fake>(check), tight);
        require(admission.beginScan(config(496, 360)).code == Code::Busy,
                "begin requires 32-frame managed headroom");
        admission.shutdown();
    }
    auto p = std::make_shared<Probe>();
    p->delay = 100;
    Limits l;
    l.maxFrames = 40;
    l.highFrames = 36;
    Engine e(std::make_unique<Fake>(p), l);
    auto c = config(496, 360);
    auto im = image(c);
    auto s = e.beginScan(c);
    unsigned accepted = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        auto out = e.pushFrame(s.scan, im.data(), c.width, c.height, c.width, i, i);
        if (out.accepted)
            ++accepted;
        if (out.code == Code::StopCapture)
            break;
    }
    require(accepted >= 36 && accepted <= 40, "bounded high water");
    require(e.beginScan(c).code == Code::Busy, "stop refuses new scan");
    for (unsigned i = 0; i < 4; ++i) {
        auto tail = e.pushFrame(s.scan, im.data(), c.width, c.height, c.width, 1000 + i, i);
        require(tail.accepted && tail.code == Code::StopCapture,
                "reserved in-flight frames accepted");
        ++accepted;
    }
    require(!e.pushFrame(s.scan, im.data(), c.width, c.height, c.width, 2000, 0).accepted,
            "reserve exhaustion seals scan");
    e.endScan(s.scan);
    auto r = e.wait(s.scan);
    require(r->status() == Status::CapacityStopped, "capacity never complete");
    auto n = r->counts();
    require(n.received == accepted && n.committed + n.unlocated == accepted,
            "accepted data retained under pressure");
    require(e.metrics().managedBytes <= 96 * MiB, "managed budget");
    e.releaseResult(s.scan);
    require(e.beginScan(c).code == Code::Busy, "consumer handle still counts");
    r.reset();
    for (unsigned k = 0;
         k < 100 && (e.metrics().frames || e.metrics().canvasBytes || e.metrics().sessions); ++k)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto next = e.beginScan(c);
    if (next.code != Code::Ok) {
        auto m = e.metrics();
        std::cerr << "pressure frames=" << m.frames << " bytes=" << m.frameBytes
                  << " canvas=" << m.canvasBytes << " sessions=" << m.sessions
                  << " rss=" << m.residentBytes << "\n";
    }
    require(next.code == Code::Ok, "capacity recovers after release");
    e.endScan(next.scan);
    e.wait(next.scan);
    e.releaseResult(next.scan);
    e.shutdown();
    require(!e.metrics().frames, "pressure cleanup");
}
static void sessionsAndFailure() {
    auto p = std::make_shared<Probe>();
    p->delay = 5;
    Limits l;
    l.idleModelMs = 30;
    Engine e(std::make_unique<Fake>(p), l);
    auto c = config();
    auto im = image(c);
    Config bad = c;
    bad.calibration.reset();
    require(e.beginScan(bad).code == Code::MissingCalibration, "calibration required");
    std::array<ScanId, 4> ids;
    for (auto &id : ids) {
        auto b = e.beginScan(c);
        id = b.scan;
        require(b.code == Code::Ok, "queued begin");
        require(e.beginScan(c).code == Code::Busy, "one acquisition");
        for (unsigned j = 0; j < 33; ++j)
            require(e.pushFrame(id, im.data(), c.width, c.height, c.width, j, j).accepted,
                    "queued push");
        e.endScan(id);
    }
    require(e.beginScan(c).code == Code::Busy, "four session cap");
    for (auto id : ids) {
        auto r = e.wait(id);
        require(r->counts().committed == 33, "sessions independent");
        e.releaseResult(id);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    require(!e.metrics().modelLoaded, "idle context freed");
    e.shutdown();
    auto fail = std::make_shared<Probe>();
    fail->fail = true;
    Engine f(std::make_unique<Fake>(fail));
    auto b = f.beginScan(c);
    for (unsigned j = 0; j < 65; ++j)
        f.pushFrame(b.scan, im.data(), c.width, c.height, c.width, j, j);
    f.endScan(b.scan);
    auto r = f.wait(b.scan);
    require(r->status() == Status::LocalizationFailed && r->unlocatedCount() == 65,
            "failure retains source views");
    for (unsigned i = 0; i < 65; ++i)
        require(r->unlocatedFrame(i).sequence == i, "retained source identity");
    f.releaseResult(b.scan);
    f.shutdown();
    require(f.metrics().frames == 65, "external result survives shutdown");
    r.reset();
    require(!f.metrics().frames, "external release frees frames");
}
static void canvasAndCopies() {
    Limits limits;
    limits.canvasBytes = 240 * 1024;
    auto b = std::make_shared<Budget>(limits);
    MemoryCanvas canvas(b);
    cv::Mat image(48, 64, CV_8UC1, cv::Scalar(137));
    auto identity = cv::Matx33d::eye();
    bool stopped = false;
    for (int i = 0; i < 200; ++i) {
        auto transform = identity;
        transform(0, 2) = i * 128;
        auto before = canvas.bounds();
        if (!canvas.draw(image, transform, identity, i == 0)) {
            require(canvas.bounds().width == before.width, "failed canvas commit is atomic");
            stopped = true;
            break;
        }
    }
    bool rejected = false;
    try {
        canvas.draw(image, cv::Matx33d(1, 0, 0, 0, 0, 1, 0, 0, 1), identity, false);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    require(rejected, "singular homography rejected safely");
    require(stopped && b->canvas <= limits.canvasBytes, "lossless cold tiles obey global budget");
    auto bounds = canvas.bounds();
    std::vector<std::uint8_t> g(bounds.width), m(bounds.width);
    require(canvas.row(20, g.data(), m.data(), g.size()) == Code::Ok, "read compressed row");
    for (int x = 0; x < 64; ++x)
        require(m[x] == 255 && g[x] == 137, "lossless compressed pixels preserved");
    auto probe = std::make_shared<Probe>();
    probe->fail = true;
    Limits small;
    small.canvasBytes = 64 * 1024;
    Engine engine(std::make_unique<Fake>(probe), small);
    auto c = config();
    auto source = image.clone();
    auto scan = engine.beginScan(c);
    for (unsigned i = 0; i < 33; ++i) {
        source.setTo(137);
        require(engine.pushFrame(scan.scan, source.ptr<std::uint8_t>(), 64, 48, source.step, i, i)
                    .accepted,
                "copy source");
        source.setTo(10);
    }
    engine.endScan(scan.scan);
    auto result = engine.wait(scan.scan);
    require(result->unlocatedCount() == 33, "unrenderable retained");
    for (unsigned i = 0; i < 33; ++i)
        require(result->unlocatedFrame(i).y[0] == 137,
                "camera buffer returned independently of workers");
    engine.releaseResult(result);
    engine.shutdown();
    auto slow = std::make_shared<Probe>();
    slow->delay = 50;
    Limits high;
    high.maxFrames = 40;
    high.highFrames = 32;
    Engine e(std::make_unique<Fake>(slow), high);
    std::atomic<bool> called{false}, wrongThread{false};
    auto caller = std::this_thread::get_id();
    auto input = config();
    auto pixels = image;
    auto id = e.beginScan(input, {[&](const Event &v) {
                              if (v.kind == Event::Kind::StopRequested) {
                                  called = true;
                                  wrongThread = std::this_thread::get_id() == caller;
                              }
                          }});
    for (unsigned i = 0; i < 100; ++i) {
        auto r = e.pushFrame(id.scan, pixels.ptr<std::uint8_t>(), 64, 48, pixels.step, i, i);
        if (r.code == Code::StopCapture)
            break;
    }
    e.endScan(id.scan);
    auto r = e.wait(id.scan);
    require(called && !wrongThread, "stop notification never runs in ingress callback");
    e.releaseResult(r);
    e.shutdown();
}
static void synchronousFacade() {
    auto probe = std::make_shared<Probe>();
    Engine engine(std::make_unique<Fake>(probe));
    SynchronousScan sync(engine);
    auto cfg = config();
    auto pixels = image(cfg);
    for (unsigned n : {0u, 1u, 33u}) {
        require(sync.beginScan(cfg).code == Code::Ok, "synchronous begin");
        for (unsigned i = 0; i < n; ++i)
            require(sync.pushFrame(pixels.data(), cfg.width, cfg.height, cfg.width, std::int64_t(i))
                        .accepted,
                    "synchronous push");
        auto result = sync.endScan();
        require(result && result->counts().committed == n, "synchronous end waits for drain");
        engine.releaseResult(result);
    }
    engine.shutdown();
}
int main() {
    try {
        std::fprintf(stderr, "TEST coordinates\n");
        coordinate();
        std::fprintf(stderr, "TEST boundaries\n");
        boundaries();
        std::fprintf(stderr, "TEST pressure\n");
        pressure();
        std::fprintf(stderr, "TEST sessions\n");
        sessionsAndFailure();
        std::fprintf(stderr, "TEST canvas and ownership\n");
        canvasAndCopies();
        synchronousFacade();
        std::puts("STREAM32_CONTRACTS_OK");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
