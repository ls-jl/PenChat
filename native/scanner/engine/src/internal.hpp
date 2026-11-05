#pragma once
#include "geometry_decoder.hpp"
#include "pen_stream/stream.hpp"
#include <atomic>
#include <deque>
#include <condition_variable>
#include <thread>
#include <limits>
#include <map>
#include <mutex>
#include <opencv2/core.hpp>
#include <sys/mman.h>
#include <unistd.h>

namespace pen::stream {
struct MemoryError : std::bad_alloc {
    const char *reason;
    explicit MemoryError(const char *r) : reason(r) {}
    const char *what() const noexcept override { return reason; }
};
struct Budget {
    Limits limits;
    std::recursive_mutex admission;
    std::atomic<std::size_t> frames{0}, canvas{0}, other{0};
    std::atomic<std::size_t> framePeak{0}, canvasPeak{0};
    std::atomic<unsigned> frameCount{0}, sessions{0};
    std::atomic<bool> loaded{false};
    std::atomic<std::size_t> available{0}, reserve{0};
    std::size_t ceiling = 0;
    std::chrono::steady_clock::time_point lastGood;
    bool hasSample = false;
    std::mutex monitorMutex;
    std::condition_variable monitorCv;
    bool monitorStopped = false;
    std::thread monitor;
    explicit Budget(Limits l) : limits(std::move(l)) {
        refresh();
        monitor = std::thread([this] {
            std::unique_lock<std::mutex> lock(monitorMutex);
            while (!monitorCv.wait_for(lock, std::chrono::milliseconds(100),
                                       [this] { return monitorStopped; })) {
                lock.unlock(); refresh(); lock.lock();
            }
        });
    }
    ~Budget() {
        { std::lock_guard<std::mutex> lock(monitorMutex); monitorStopped = true; }
        monitorCv.notify_all();
        if (monitor.joinable()) monitor.join();
    }
    std::size_t total() const { return frames + canvas + other; }
    void refresh() noexcept {
        try {
            // Serialize the sample with page allocation/free so allocations made
            // after a sample cannot all spend the same unreserved headroom.
            std::lock_guard<std::recursive_mutex> lock(admission);
            const auto m = limits.memoryProbe ? limits.memoryProbe() : systemMemory();
            if (!m.valid || !m.total || m.available > m.total) return;
            available = m.available;
            reserve = std::max(std::size_t(32 * MiB), m.total / 20);
            const auto headroom = m.available > reserve ? m.available - reserve : 0;
            ceiling = total() + headroom;
            hasSample = true; lastGood = std::chrono::steady_clock::now();
        } catch (...) { /* A failed sample expires, never grants more memory. */ }
    }
    const char *reason(std::size_t extra = 0) {
        std::lock_guard<std::recursive_mutex> lock(admission);
        if (!hasSample || std::chrono::steady_clock::now() - lastGood >= std::chrono::seconds(1))
            return "system_memory_unavailable";
        if (available <= reserve || total() > ceiling || extra > ceiling - total())
            return "system_memory_low";
        return "";
    }
    void require(std::size_t extra) {
        const auto why = reason(extra);
        if (*why) throw MemoryError(why);
    }
    void peak() {
        framePeak.store(std::max(framePeak.load(), frames.load()));
        canvasPeak.store(std::max(canvasPeak.load(), canvas.load()));
    }
};
class Pages {
    void *data_ = nullptr;
    std::size_t n_ = 0;
    std::shared_ptr<Budget> budget_;
    std::atomic<std::size_t> *category_ = nullptr;

  public:
    Pages() = default;
    Pages(const Pages &) = delete;
    Pages &operator=(const Pages &) = delete;
    ~Pages() {
        clear();
    }
    void clear() {
        if (data_) {
            std::lock_guard<std::recursive_mutex> guard(budget_->admission);
            munmap(data_, n_);
            *category_ -= n_;
        }
        data_ = nullptr;
        n_ = 0;
    }
    void allocate(std::size_t n, const std::shared_ptr<Budget> &b, std::atomic<std::size_t> &c) {
        std::lock_guard<std::recursive_mutex> guard(b->admission);
        if (!n || n > std::numeric_limits<std::size_t>::max() - 4095)
            throw MemoryError("memory_allocation_failed");
        n = (n + 4095) & ~std::size_t(4095);
        b->require(n);
        if (b->limits.allocationProbe && !b->limits.allocationProbe(n))
            throw MemoryError("memory_allocation_failed");
        void *p = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
            throw MemoryError("memory_allocation_failed");
        clear();
        data_ = p;
        n_ = n;
        budget_ = b;
        category_ = &c;
        c += n;
        b->peak();
    }
    std::uint8_t *bytes() {
        return static_cast<std::uint8_t *>(data_);
    }
    const std::uint8_t *bytes() const {
        return static_cast<const std::uint8_t *>(data_);
    }
    std::size_t size() const {
        return n_;
    }
};
struct Frame {
    std::shared_ptr<Budget> budget;
    Pages storage, model;
    int width = 0, height = 0;
    std::uint64_t sequence = 0, index = std::uint64_t(-1);
    std::int64_t timestamp = 0;
    float brightness = 1;
    bool photoValid = true;
    ~Frame() {
        if (budget)
            --budget->frameCount;
    }
    std::uint8_t *y() {
        return storage.bytes();
    }
    std::uint8_t *match() {
        return model.bytes();
    }
};
using FramePtr = std::shared_ptr<Frame>;
std::pair<std::size_t, std::size_t> rss(std::size_t *swap = nullptr);
void resizeCamera(const cv::Mat &, cv::Mat &, int, int);
cv::Mat rotateY(const cv::Mat &, Rotation);
// Independently specified pixel-center mapping; raw sensor -> model work coords.
cv::Matx33d rawToWork(int width, int height, Rotation, int workWidth = 160, int workHeight = 288);
struct Selection {
    cv::Mat previous;
    bool accept(const cv::Mat &upright);
    static double texture(const cv::Mat &small);
};
struct Pose {
    std::uint64_t index;
    cv::Matx33d work;
};
class WindowGeometry {
    std::array<y07::DecodedFrame, 32> previous_{};
    std::uint64_t previousStart_ = 0, next_ = 0;
    bool hasPrevious_ = false;
    int carryX_ = 0, carryY_ = 0, carryScale_ = 0;

  public:
    std::vector<Pose> consume(std::uint64_t start, const std::vector<y07::DecodedFrame> &,
                              unsigned valid, bool last);
    std::vector<Pose> flush();
    static cv::Matx33d construct(const y07::DecodedFrame &, float x, float y, float scale);
};
// Validate before painting: a plausible finite homography can still fold or
// misalign a lifted/turning tail over already committed text. Only small model
// images are retained; no source-frame ownership or model output is changed.
class GeometryQuality {
    static constexpr unsigned History = 8, Pixels = 80 * 144;
    Pages images_;
    std::array<cv::Matx33d, History> poses_;
    unsigned used_ = 0, next_ = 0;

  public:
    const char *accept(const cv::Matx33d &, const std::uint8_t *, const std::shared_ptr<Budget> &);
    void clear() { images_.clear(); used_ = next_ = 0; }
};
class MemoryCanvas {
    static constexpr int T = 128, N = T * T;
    struct Tile {
        std::shared_ptr<Budget> budget;
        Pages hot, cold;
        std::size_t coldSize = 0;
        std::uint64_t age = 0;
        bool pinned = false;
        explicit Tile(std::shared_ptr<Budget> b) : budget(std::move(b)) {
            std::lock_guard<std::recursive_mutex> guard(budget->admission);
            budget->require(256);
            budget->canvas += 256;
        }
        ~Tile() {
            std::lock_guard<std::recursive_mutex> guard(budget->admission);
            budget->canvas -= 256;
        }
    };
    std::shared_ptr<Budget> budget_;
    std::map<std::pair<int, int>, std::unique_ptr<Tile>> tiles_;
    std::uint64_t clock_ = 0;
    Bounds bounds_{};
    alignas(float) std::array<std::uint8_t, N * 7> scratch_{};
    bool compress(Tile &);
    bool room(std::size_t);
    bool heat(Tile &);
    static int divide(int x) {
        return x >= 0 ? x / T : (x + 1) / T - 1;
    }

  public:
    explicit MemoryCanvas(std::shared_ptr<Budget> b) : budget_(std::move(b)) {}
    bool draw(const cv::Mat &gray, const cv::Matx33d &rawToCanvas, const cv::Matx33d &rawToSmall,
              bool first);
    Bounds bounds() const {
        return bounds_;
    }
    void clear() {
        tiles_.clear();
        bounds_ = {};
    }
    void compact() noexcept;
    Code preview(int, int, std::uint8_t *, std::uint8_t *, std::size_t);
    Code row(int, std::uint8_t *, std::uint8_t *, std::size_t);
};
} // namespace pen::stream
