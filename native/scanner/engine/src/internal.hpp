#pragma once
#include "geometry_decoder.hpp"
#include "pen_stream/stream.hpp"
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <opencv2/core.hpp>
#include <sys/mman.h>
#include <unistd.h>

namespace pen::stream {
struct Budget {
    Limits limits;
    std::recursive_mutex admission;
    std::atomic<std::size_t> frames{0}, canvas{0}, other{0};
    std::atomic<std::size_t> observedRss{0};
    std::atomic<unsigned> frameCount{0}, sessions{0};
    std::atomic<bool> loaded{false};
    explicit Budget(Limits l) : limits(l) {}
    std::size_t total() const {
        return frames + canvas + other;
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
            munmap(data_, n_);
            *category_ -= n_;
        }
        data_ = nullptr;
        n_ = 0;
    }
    void allocate(std::size_t n, const std::shared_ptr<Budget> &b, std::atomic<std::size_t> &c,
                  std::size_t limit) {
        std::lock_guard<std::recursive_mutex> guard(b->admission);
        n = (n + 4095) & ~std::size_t(4095);
        // Category admission is serialized by its owner (frame ingress or renderer).
        if (c + n > limit || b->total() + n > b->limits.managedBytes)
            throw std::bad_alloc();
        void *p = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
            throw std::bad_alloc();
        clear();
        data_ = p;
        n_ = n;
        budget_ = b;
        category_ = &c;
        c += n;
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
    Pages storage;
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
        return y() + std::size_t(width) * height;
    }
};
using FramePtr = std::shared_ptr<Frame>;
std::pair<std::size_t, std::size_t> rss();
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
            if (budget->canvas + 256 > budget->limits.canvasBytes ||
                budget->total() + 256 > budget->limits.managedBytes)
                throw std::bad_alloc();
            budget->canvas += 256;
        }
        ~Tile() {
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
    Code preview(int, int, std::uint8_t *, std::uint8_t *, std::size_t);
    Code row(int, std::uint8_t *, std::uint8_t *, std::size_t);
};
} // namespace pen::stream
