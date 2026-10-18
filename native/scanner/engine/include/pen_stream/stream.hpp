#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pen::stream {
using ScanId = std::uint64_t;
constexpr std::size_t MiB = 1024 * 1024;
enum class Code {
    Ok,
    Busy,
    StopCapture,
    InvalidArgument,
    MissingCalibration,
    Closed,
    NotReady,
    NotFound
};
enum class Status { Processing, Complete, Empty, Segmented, LocalizationFailed, CapacityStopped };
enum class Rotation { None, Clockwise90, HalfTurn, CounterClockwise90 };
struct Calibration {
    // Frozen camera-coordinate gain map after configured rotation, 160 x 288.
    std::string version;
    int sensorWidth = 0, sensorHeight = 0;
    Rotation rotation = Rotation::CounterClockwise90;
    std::array<float, 160 * 288> gain{};
};
struct Config {
    int width = 496, height = 360;
    Rotation rotation = Rotation::CounterClockwise90;
    std::shared_ptr<const Calibration> calibration;
    // Diagnostic only: bypass selection to exercise exact window boundaries.
    bool diagnosticAcceptAll = false;
};
struct Limits {
    std::size_t frameBytes = 48 * MiB, canvasBytes = 32 * MiB;
    std::size_t managedBytes = 96 * MiB, rssSoftBytes = 240 * MiB;
    unsigned maxFrames = 256, highFrames = 240, maxSessions = 4;
    unsigned idleModelMs = 5000;
};
struct Counts {
    std::uint64_t received = 0, filtered = 0, selected = 0, committed = 0;
    std::uint64_t unlocated = 0, pending = 0, inferenceBatches = 0;
};
struct Event {
    enum class Kind { Filtered, Selected, Submitted, StopRequested, Preview, Finished, Error } kind;
    ScanId scan = 0;
    std::uint64_t sequence = 0, selectedIndex = 0;
    std::int64_t timestamp = 0;
    std::array<double, 9> rawToCanvas{1, 0, 0, 0, 1, 0, 0, 0, 1};
    const char *reason = ""; // Static text; copy if retained by the application.
};
struct Callbacks {
    std::function<void(const Event &)> event;
};
struct PushResult {
    Code code = Code::Ok;
    bool accepted = false;
};
struct BeginResult {
    Code code = Code::Ok;
    ScanId scan = 0;
};
struct Bounds {
    int x = 0, y = 0, width = 0, height = 0;
};
struct Metrics {
    std::size_t frameBytes = 0, canvasBytes = 0, managedBytes = 0, residentBytes = 0,
                peakResidentBytes = 0;
    unsigned frames = 0, sessions = 0;
    bool modelLoaded = false;
};
struct RawFrameView {
    const std::uint8_t *y = nullptr;
    int width = 0, height = 0, stride = 0;
    std::uint64_t sequence = 0;
    std::int64_t timestamp = 0;
};
class Result {
  public:
    ~Result();
    ScanId scanId() const;
    Status status() const;
    Counts counts() const;
    Bounds bounds() const;
    std::string reason() const;
    // Caller supplies width bytes each. Invalid pixels are white, mask is zero.
    Code readRow(int relativeY, std::uint8_t *gray, std::uint8_t *mask, std::size_t capacity) const;
    // Keeps accepted but unpainted source frames alive. Valid until Result release.
    Code readPreview(int width, int height, std::uint8_t *gray, std::uint8_t *mask,
                     std::size_t capacity) const;
    std::size_t unlocatedCount() const;
    RawFrameView unlocatedFrame(std::size_t index) const;

  private:
    struct Impl;
    explicit Result(std::shared_ptr<Impl>);
    std::shared_ptr<Impl> p_;
    friend class Engine;
};
using ResultHandle = std::shared_ptr<const Result>;
// A backend sees 32 frame slots, validCount tells it where RGB [124,116,104]
// padding begins. Its output vectors have 32*bins elements. One owner thread.
struct NetworkBatch {
    std::array<const std::uint8_t *, 32> gray{}; // each 80 x 144
    unsigned validCount = 0;
};
using Heads = std::array<std::vector<float>, 9>;
class Inference {
  public:
    virtual ~Inference() = default;
    virtual void open() = 0;
    virtual void infer(const NetworkBatch &, Heads &) = 0;
    virtual void close() noexcept = 0;
    virtual std::size_t managedBytes() const noexcept = 0;
};
// Dynamically loads only public RKNN ABI; no vendor stitch library or GPU.
std::unique_ptr<Inference> makeRknn32(std::string modelPath,
                                      std::string runtimePath = "librknnrt.so");
class Engine {
  public:
    explicit Engine(std::unique_ptr<Inference>, Limits = {});
    ~Engine();
    BeginResult beginScan(const Config &, Callbacks = {});
    PushResult pushFrame(ScanId, const std::uint8_t *y, int width, int height, std::size_t stride,
                         std::uint64_t sourceSequence, std::int64_t timestamp);
    Code endScan(ScanId); // Seal only; processing continues.
    Code query(ScanId, Status &, Counts &) const;
    ResultHandle preview(ScanId) const; // Read-only live handle; still counts until released.
    ResultHandle result(ScanId) const;  // Null until complete.
    ResultHandle wait(ScanId);          // Synchronous compatibility wrapper.
    Code releaseResult(ResultHandle &);
    Code releaseResult(ScanId); // Outstanding shared handles still count.
    Metrics metrics() const;
    void shutdown(); // Seals and drains accepted work, joins.
  private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
// Opt-in waiting facade for callers using the previous synchronous call style.
// It shares the Engine and its process-wide session/frame budgets.
class SynchronousScan {
    Engine &engine_;
    std::atomic<ScanId> active_{0};
    std::atomic<std::uint64_t> sequence_{0};

  public:
    explicit SynchronousScan(Engine &engine) : engine_(engine) {}
    ~SynchronousScan() {
        auto id = active_.exchange(0);
        if (id)
            engine_.endScan(id);
    }
    BeginResult beginScan(const Config &config, Callbacks callbacks = {}) {
        if (active_.load())
            return {Code::Busy, 0};
        auto result = engine_.beginScan(config, std::move(callbacks));
        if (result.code == Code::Ok) {
            sequence_ = 0;
            active_ = result.scan;
        }
        return result;
    }
    PushResult pushFrame(const std::uint8_t *y, int width, int height, std::size_t stride,
                         std::int64_t timestamp = 0) {
        return pushFrame(y, width, height, stride, sequence_.fetch_add(1), timestamp);
    }
    PushResult pushFrame(const std::uint8_t *y, int width, int height, std::size_t stride,
                         std::uint64_t source, std::int64_t timestamp) {
        auto id = active_.load();
        return id ? engine_.pushFrame(id, y, width, height, stride, source, timestamp)
                  : PushResult{Code::Closed, false};
    }
    ResultHandle endScan() {
        auto id = active_.exchange(0);
        if (!id)
            return {};
        engine_.endScan(id);
        return engine_.wait(id);
    }
};
} // namespace pen::stream
