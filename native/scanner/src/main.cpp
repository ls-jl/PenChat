#include "hardware.hpp"
#include "png.hpp"
#include "preview.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <pen_stream/stream.hpp>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
using namespace penchat_scan;
using namespace pen::stream;
namespace {
volatile sig_atomic_t running = 1;
void stop(int) { running = 0; }
struct ControlDisconnected : std::runtime_error {
  ControlDisconnected() : std::runtime_error("scanner control disconnected") {}
};
struct DrainDeadline {
  DrainDeadline() { alarm(3); }
  ~DrainDeadline() { alarm(0); }
};
int64_t clockUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
class Light {
  std::mutex mutex_;
  std::condition_variable cv_;
  bool desired_ = false, stopped_ = false;
  uint64_t requested_ = 0, done_ = 0;
  std::string error_;
  bool mock_;
  std::thread thread_;

public:
  explicit Light(bool mock)
      : mock_(mock), thread_([this] {
          for (;;) {
            bool on;
            uint64_t ticket;
            {
              std::unique_lock<std::mutex> l(mutex_);
              cv_.wait(l, [&] { return stopped_ || requested_ != done_; });
              if (stopped_)
                break;
              on = desired_;
              ticket = requested_;
            }
            std::string error;
            try {
              if (!mock_)
                setScanLight(on);
            } catch (const std::exception &e) {
              error = e.what();
            }
            {
              std::lock_guard<std::mutex> l(mutex_);
              error_ = error;
              done_ = ticket;
              cv_.notify_all();
            }
          }
          try {
            if (!mock_ && desired_)
              setScanLight(false);
          } catch (...) {
          }
        }) {}
  ~Light() {
    {
      std::lock_guard<std::mutex> l(mutex_);
      stopped_ = true;
      cv_.notify_all();
    }
    thread_.join();
  }
  void request(bool on) {
    std::lock_guard<std::mutex> l(mutex_);
    desired_ = on;
    ++requested_;
    cv_.notify_all();
  }
  void off() {
    {
      std::lock_guard<std::mutex> l(mutex_);
      if (!requested_)
        return;
    }
    request(false);
    std::unique_lock<std::mutex> l(mutex_);
    cv_.wait(l, [&] { return done_ == requested_; });
  }
  std::string error() {
    std::lock_guard<std::mutex> l(mutex_);
    return error_;
  }
};
#ifdef SCANNER_TEST_SOURCE
class TestInference final : public Inference {
public:
  void open() override {}
  void close() noexcept override {}
  size_t managedBytes() const noexcept override { return 0; }
  void infer(const NetworkBatch &, Heads &h) override {
    const unsigned bins[9] = {384, 384, 512, 512, 512, 512, 512, 512, 1};
    for (unsigned k = 0; k < 9; ++k) {
      h[k].assign(32 * bins[k], -80);
      for (unsigned t = 0; t < 32; ++t)
        h[k][t * bins[k] + bins[k] / 2 + (k == 0 ? 2 : 0)] = 0;
    }
  }
};
#endif
class ObservedInference final : public Inference {
  std::unique_ptr<Inference> backend_;
  std::function<void()> started_;

public:
  ObservedInference(std::unique_ptr<Inference> b, std::function<void()> f)
      : backend_(std::move(b)), started_(std::move(f)) {}
  void open() override { backend_->open(); }
  void close() noexcept override { backend_->close(); }
  size_t managedBytes() const noexcept override {
    return backend_->managedBytes();
  }
  void infer(const NetworkBatch &b, Heads &h) override {
    started_();
    backend_->infer(b, h);
  }
};
class Worker {
  int fd_;
  PreviewSlot *slot_;
  std::string resources_, directory_, session_, state_ = "disarmed";
  uint64_t generation_ = 0, request_ = 0, scan_ = 0;
  bool mock_ = false, armed_ = false, capturing_ = false, finished_ = false;
  bool requireRelease_ = false, keyDown_ = false, draining_ = false;
  unsigned afterStop_ = 0;
  bool haveSequence_ = false;
  uint64_t lastSequence_ = 0, droppedRaw_ = 0;
  std::string captureFailure_;
  std::atomic<int64_t> firstSelectedUs_{0}, firstBatchUs_{0};
  std::atomic<unsigned> warnings_{0};
  int64_t lightRequestedUs_ = 0;
  int64_t ownerDeadline_ = 0, nextOwner_ = 0;
  int64_t nextPreview_ = 0, lastFrame_ = 0, pressTime_ = 0, firstFrame_ = 0,
          drainDeadline_ = 0;
  Json lease_, exported_;
  std::mutex eventMutex_;
  std::deque<Event> events_;
  Hardware hardware_;
  Light light_;
  Config config_;
  std::unique_ptr<Engine> engine_;
  ResultHandle result_;
  PreviewWorker preview_;
  int64_t lastPublishedUs_ = 0, lastCaptureTickUs_ = 0, maxCaptureLoopUs_ = 0;
  Json counts() {
    auto r = scan_ ? engine_->preview(scan_) : ResultHandle{};
    Counts c = r ? r->counts() : Counts{};
    return {{"received", c.received},        {"filtered", c.filtered},
            {"selected", c.selected},        {"committed", c.committed},
            {"pending", c.pending},          {"unlocated", c.unlocated},
            {"batches", c.inferenceBatches}, {"dropped_raw", droppedRaw_},
            {"warnings", warnings_.load()}};
  }
  void emit(const std::string &type, Json data = Json::object()) {
    data["v"] = ProtocolVersion;
    data["type"] = type;
    data["generation"] = generation_;
    data["requestId"] = request_;
    data["scanId"] = scan_;
    data["session"] = session_;
    if (!sendPacket(fd_, data))
      throw ControlDisconnected();
  }
  void calibration() {
    std::ifstream meta(resources_ + "/demo_gain.json");
    Json j;
    meta >> j;
    if (j.value("sensor_width", 0) != 496 ||
        j.value("sensor_height", 0) != 360 ||
        j.value("rotation", std::string()) != "counterclockwise90" ||
        j.value("gain_shape", Json::array()) != Json::array({288, 160}) ||
        j.value("gain_dtype", std::string()) != "float32 little endian")
      throw std::runtime_error("scan calibration profile mismatch");
    auto k = std::make_shared<Calibration>();
    k->version = j.at("version").get<std::string>();
    k->sensorWidth = 496;
    k->sensorHeight = 360;
    k->rotation = Rotation::CounterClockwise90;
    std::ifstream gain(resources_ + "/demo_gain_160x288.f32", std::ios::binary);
    gain.read(reinterpret_cast<char *>(k->gain.data()),
              k->gain.size() * sizeof(float));
    if (!gain || gain.peek() != EOF)
      throw std::runtime_error("scan calibration length mismatch");
    for (float value : k->gain)
      if (!std::isfinite(value) || value < .8f || value > 1.4f)
        throw std::runtime_error("invalid scan calibration value");
    config_.calibration = k;
    config_.diagnosticAcceptAll = mock_;
  }
  void removeExport() {
    for (const char *key : {"path", "previewPath"})
      if (exported_.contains(key)) {
        auto path = exported_[key].get<std::string>();
        unlink(path.c_str());
        unlink((path + ".part").c_str());
      }
    exported_ = Json::object();
  }
  void release() {
    preview_.cancelAndWait();
    if (scan_) {
      engine_->endScan(scan_);
      auto r = engine_->wait(scan_);
      result_.reset();
      engine_->releaseResult(r);
      engine_->releaseResult(scan_);
    }
    for (int n = 0; n < 200 && engine_->metrics().sessions; ++n)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (engine_->metrics().sessions)
      throw std::runtime_error("scan result still referenced");
    scan_ = 0;
    capturing_ = false;
    finished_ = false;
    draining_ = false;
    removeExport();
    std::lock_guard<std::mutex> l(eventMutex_);
    events_.clear();
  }
  void disarm() {
    light_.off();
    hardware_.close();
    armed_ = false;
    state_ = "disarmed";
    // Camera, key and guardian are restored before imposing an exit bound on
    // cancelled work. A stuck NPU call must not leave a detached worker alive.
    DrainDeadline deadline;
    release();
    lease_ = Json();
  }
  void begin() {
    if (!armed_ || scan_ || requireRelease_)
      return;
    const auto begin = engine_->beginScan(
        config_, {[this](const Event &e) {
          // State/preview counts are polled and coalesced; only
          // terminal/control events enter this small queue. Never call the UI
          // or block on IPC here.
          if (e.kind == Event::Kind::Selected) {
            int64_t zero = 0;
            firstSelectedUs_.compare_exchange_strong(zero, clockUs());
          }
          if (e.kind == Event::Kind::Error) {
            ++warnings_;
            return;
          }
          if (e.kind == Event::Kind::StopRequested ||
              e.kind == Event::Kind::Finished) {
            std::lock_guard<std::mutex> l(eventMutex_);
            events_.push_back(e);
          }
        }});
    if (begin.code != Code::Ok) {
      fail("scan engine unavailable: " + std::to_string(int(begin.code)));
      return;
    }
    firstSelectedUs_ = 0;
    firstBatchUs_ = 0;
    warnings_ = 0;
    scan_ = begin.scan;
    pressTime_ = clockUs();
    lastCaptureTickUs_ = pressTime_;
    maxCaptureLoopUs_ = 0;
    firstFrame_ = 0;
    lastFrame_ = pressTime_;
    haveSequence_ = false;
    droppedRaw_ = 0;
    captureFailure_.clear();
    afterStop_ = 0;
    capturing_ = true;
    finished_ = false;
    draining_ = false;
    state_ = "capturing";
    lightRequestedUs_ = clockUs();
    light_.request(true);
    emit("started",
         {{"pressedUs", pressTime_}, {"lightRequestedUs", lightRequestedUs_}});
  }
  void seal() {
    if (!capturing_)
      return;
    capturing_ = false;
    draining_ = false;
    light_.request(false);
    engine_->endScan(scan_);
    state_ = "processing";
    emit("processing", {{"counts", counts()}});
  }
  void drain() {
    if (!capturing_ || draining_)
      return;
    light_.request(false);
    if (mock_) {
      seal();
      return;
    }
    hardware_.exposure(false);
    draining_ = true;
    afterStop_ = 16;
    drainDeadline_ = clockUs() + 50000;
    state_ = "processing";
    emit("processing", {{"counts", counts()}});
  }
  void requestPreview(bool final = false) {
    auto r = scan_ ? engine_->preview(scan_) : ResultHandle{};
    if (!r)
      return;
    const auto generation = generation_, scan = scan_;
    preview_.request([r, generation, scan] {
      auto b = r->bounds();
      if (b.width < 1 || b.height < 1)
        return std::unique_ptr<PreviewFrame>{};
      double scale = std::min(1., std::min(double(PreviewWidth) / b.width,
                                           double(PreviewHeight) / b.height));
      std::unique_ptr<PreviewFrame> frame(new PreviewFrame);
      frame->generation = generation;
      frame->scan = scan;
      frame->width = std::max(1, int(b.width * scale));
      frame->height = std::max(1, int(b.height * scale));
      frame->canvasWidth = b.width;
      frame->canvasHeight = b.height;
      frame->pixels.resize(size_t(frame->width) * frame->height);
      std::vector<uint8_t> mask(frame->pixels.size());
      if (r->readPreview(frame->width, frame->height, frame->pixels.data(),
                          mask.data(), frame->pixels.size()) != Code::Ok)
        return std::unique_ptr<PreviewFrame>{};
      return frame;
    }, final);
  }
  void publishPreview() {
    if (clockUs() - lastPublishedUs_ < 100000)
      return;
    auto frame = preview_.take();
    if (!frame || frame->generation != generation_ || frame->scan != scan_)
      return;
    slot_->sequence.fetch_add(1, std::memory_order_acq_rel);
    slot_->width = frame->width;
    slot_->height = frame->height;
    slot_->generation = frame->generation;
    slot_->scan = frame->scan;
    memcpy(slot_->pixels, frame->pixels.data(), frame->pixels.size());
    slot_->sequence.fetch_add(1, std::memory_order_release);
    lastPublishedUs_ = clockUs();
    emit("preview", {{"counts", counts()}, {"width", frame->canvasWidth},
                      {"height", frame->canvasHeight}});
  }
  void exportResult() {
    if (!exported_.empty()) {
      emit("exported", {{"image", exported_}});
      return;
    }
    if (!finished_ || !result_)
      return;
    auto b = result_->bounds();
    if (b.width < 1 || b.height < 1 || b.width > 1024 * 1024)
      throw std::runtime_error(
          "scan has no usable image or row exceeds export limit");
    mkdir(directory_.c_str(), 0700);
    const auto stem = directory_ + "/scan_" + std::to_string(getpid()) + "_" +
                      std::to_string(generation_) + "_" + std::to_string(scan_);
    exported_ = {{"path", stem + ".png"},
                 {"previewPath", stem + ".preview.png"},
                 {"width", b.width},
                 {"height", b.height},
                 {"source", "scan"}};
    auto c = counts();
    exported_["metadata"] = {
        {"status", captureFailure_.empty() ? int(result_->status())
                                           : int(Status::Segmented)},
        {"counts", c},
        {"reason",
         captureFailure_.empty() ? result_->reason() : captureFailure_},
        {"calibration", config_.calibration->version},
        {"timing",
         {{"pressedUs", pressTime_},
          {"firstFrameUs", firstFrame_},
          {"lightRequestedUs", lightRequestedUs_},
          {"firstSelectedUs", firstSelectedUs_.load()},
          {"firstBatchUs", firstBatchUs_.load()},
          {"maxCaptureLoopUs", maxCaptureLoopUs_}}}};
    std::vector<uint8_t> mask(b.width);
    writeGrayPng(
        exported_["path"], b.width, b.height, [&](int y, uint8_t *pixels) {
          if (result_->readRow(y, pixels, mask.data(), b.width) != Code::Ok)
            throw std::runtime_error("scan row unavailable");
        });
    double scale = std::min(1., std::min(1024. / b.width, 256. / b.height));
    int w = std::max(1, int(b.width * scale)),
        h = std::max(1, int(b.height * scale));
    std::vector<uint8_t> preview(size_t(w) * h), valid(preview.size());
    if (result_->readPreview(w, h, preview.data(), valid.data(),
                             preview.size()) != Code::Ok)
      throw std::runtime_error("scan preview unavailable");
    writeGrayPng(exported_["previewPath"], w, h, [&](int y, uint8_t *row) {
      memcpy(row, preview.data() + size_t(y) * w, w);
    });
    emit("exported", {{"image", exported_}});
  }

public:
  Worker(int fd, PreviewSlot *slot, std::string resources,
         std::string directory, bool mock)
      : fd_(fd), slot_(slot), resources_(std::move(resources)),
        directory_(std::move(directory)), mock_(mock), light_(mock) {
    std::unique_ptr<Inference> backend;
#ifdef SCANNER_TEST_SOURCE
    if (mock_)
      backend = std::make_unique<TestInference>();
#endif
    if (!backend)
      backend = makeRknn32(resources_ + "/pintu_32.rknn");
    Limits limits;
    limits.maxSessions = 1;
    backend = std::make_unique<ObservedInference>(std::move(backend), [this] {
      int64_t zero = 0;
      firstBatchUs_.compare_exchange_strong(zero, clockUs());
    });
    engine_ = std::make_unique<Engine>(std::move(backend), limits);
    calibration();
  }
  ~Worker() {
    try {
      disarm();
    } catch (...) {
    }
    DrainDeadline deadline;
    result_.reset();
    engine_.reset();
  }
  void command(const Json &j) {
    auto type = j.value("type", std::string());
    auto generation = j.value("generation", uint64_t(0));
    if (type == "arm") {
      if (generation <= generation_)
        return;
      disarm();
      generation_ = generation;
      session_ = j.at("session").get<std::string>();
      request_ = j.value("requestId", uint64_t(0));
      if (mock_) {
        armed_ = true;
        state_ = "ready";
        emit("ready");
      } else {
        state_ = "waiting_owner";
        ownerDeadline_ = clockUs() + 5000000;
        nextOwner_ = 0;
        emit("preparing");
      }
      return;
    }
    if (generation != generation_ ||
        j.value("session", std::string()) != session_)
      return;
    request_ = j.value("requestId", request_);
    if (type == "lease_ack" && state_ == "preparing") {
      hardware_.open(lease_);
      armed_ = true;
      requireRelease_ = false;
      state_ = "ready";
      emit("ready");
    } else if (type == "disarm" || type == "cancel") {
      disarm();
      emit("released");
    } else if (type == "confirm" && j.value("scanId", uint64_t(0)) == scan_) {
      try {
        exportResult();
      } catch (...) {
        removeExport();
        throw;
      }
    } else if (type == "discard" && j.value("scanId", uint64_t(0)) == scan_) {
      release();
      requireRelease_ = keyDown_;
      if (!mock_ && armed_)
        hardware_.exposure(true);
      state_ = "ready";
      emit("ready");
    }
#ifdef SCANNER_TEST_SOURCE
    else if (mock_ && type == "test.press") {
      bool down = j.value("pressed", false);
      keyDown_ = down;
      if (down)
        begin();
      else {
        requireRelease_ = false;
        drain();
      }
    } else if (mock_ && type == "test.frame" && capturing_) {
      std::vector<uint8_t> pixels(496 * 360, 210);
      for (int y = 20; y < 340; ++y)
        for (int x = 20; x < 476; ++x)
          if ((x + y) % 11 < 3)
            pixels[size_t(y) * 496 + x] = 30;
      frame(pixels.data(), 496,
            j.value("rawSequence", haveSequence_ ? lastSequence_ + 1 : uint64_t(0)),
            clockUs());
    }
#endif
  }
  void frame(const uint8_t *pixels, int stride, uint64_t sequence,
             int64_t timestamp) {
    lastFrame_ = clockUs();
    if (!capturing_)
      return;
    if (!mock_ && timestamp < pressTime_)
      return;

    if (haveSequence_ && uint32_t(sequence) != uint32_t(lastSequence_ + 1)) {
      const auto distance = uint32_t(sequence) - uint32_t(lastSequence_);
      // A backwards/reset sequence is not billions of missing frames.
      if (distance > 1 && distance < 0x80000000u)
        droppedRaw_ += distance - 1;
      captureFailure_ = distance > 1 && distance < 0x80000000u
                            ? "sensor_sequence_gap"
                            : "sensor_sequence_reset";
      requireRelease_ = true;
      drain();
      seal();
      return;
    }
    haveSequence_ = true;
    lastSequence_ = sequence;
    const bool wasDraining = draining_;
    auto p = engine_->pushFrame(scan_, pixels, 496, 360, stride, sequence,
                                timestamp);
    if (p.accepted && !firstFrame_)
      firstFrame_ = lastFrame_;
    if (p.code == Code::StopCapture) {
      requireRelease_ = true;
      drain();
      if (!p.accepted)
        seal();
    } else if (!p.accepted)
      throw std::runtime_error("pen frame rejected");
    if (wasDraining && capturing_ && afterStop_ && --afterStop_ == 0)
      seal();
  }
  void tick() {
    if (capturing_) {
      const auto now = clockUs();
      maxCaptureLoopUs_ = std::max(maxCaptureLoopUs_, now - lastCaptureTickUs_);
      lastCaptureTickUs_ = now;
    }
    if (state_ == "waiting_owner" && clockUs() >= nextOwner_) {
      try {
        lease_ = scanLeaseSnapshot();
        state_ = "preparing";
        emit("lease", {{"lease", lease_}});
      } catch (const std::exception &e) {
        if (std::string(e.what()) != "system capture preparing" ||
            clockUs() >= ownerDeadline_)
          throw;
        nextOwner_ = clockUs() + 50000;
      }
    }
    if (armed_ && !mock_) {
      bool down = false;
      while (hardware_.keyEvent(down)) {
        keyDown_ = down;
        if (down)
          begin();
        else {
          requireRelease_ = false;
          drain();
        }
      }
      // Always dequeue standby frames, but copy only while capturing.
      for (int n = 0; n < 16; ++n)
        if (!hardware_.readFrame(
                [&](const uint8_t *y, int stride, uint64_t seq, int64_t ts) {
                  frame(y, stride, seq, ts);
                }))
          break;
      if (draining_ && clockUs() >= drainDeadline_)
        seal();
      if (capturing_ && clockUs() - lastFrame_ > 5000000)
        throw std::runtime_error("pen camera stalled");
    }
    std::deque<Event> events;
    {
      std::lock_guard<std::mutex> l(eventMutex_);
      events.swap(events_);
    }
    for (auto &e : events) {
      if (e.scan != scan_)
        continue;
      if (e.kind == Event::Kind::StopRequested) {
        requireRelease_ = true;
        drain();
        emit("stopped", {{"reason", e.reason}});
      }
      if (e.kind == Event::Kind::Finished) {
        result_ = engine_->result(scan_);
        finished_ = true;
        state_ = "result";
        requestPreview(true);
        auto b = result_->bounds();
        emit("finished",
             {{"status", captureFailure_.empty() ? int(result_->status())
                                                 : int(Status::Segmented)},
              {"reason",
               captureFailure_.empty() ? result_->reason() : captureFailure_},
              {"usable", b.width > 0 && b.height > 0},
              {"counts", counts()},
              {"pressedUs", pressTime_},
              {"firstFrameUs", firstFrame_},
              {"lightRequestedUs", lightRequestedUs_},
              {"firstSelectedUs", firstSelectedUs_.load()},
              {"firstBatchUs", firstBatchUs_.load()},
              {"maxCaptureLoopUs", maxCaptureLoopUs_}});
      }
    }
    if (scan_ && !finished_ && clockUs() >= nextPreview_) {
      nextPreview_ = clockUs() + 100000;
      requestPreview();
      emit("progress", {{"counts", counts()}});
    }
    publishPreview();
    if (armed_ || capturing_) {
      auto error = light_.error();
      if (!error.empty())
        throw std::runtime_error(error);
    }
  }
  void fail(const std::string &why) {
    if (scan_ && !finished_) {
      captureFailure_ = why;
      light_.off();
      hardware_.close();
      armed_ = false;
      capturing_ = false;
      draining_ = false;
      engine_->endScan(scan_);
      state_ = "processing";
      emit("stopped", {{"reason", why}});
    } else {
      light_.off();
      hardware_.close();
      armed_ = false;
      state_ = "error";
      emit("error", {{"error", why}});
    }
  }
};
} // namespace
int main(int argc, char **argv) {
  int fd = -1, preview = -1, processLock = -1;
  std::string resources, directory;
  bool mock = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
#ifdef SCANNER_TEST_SOURCE
    if (a == "--simulate") {
      mock = true;
      continue;
    }
#endif
    if (i + 1 >= argc)
      return 2;
    std::string v = argv[++i];
    if (a == "--bridge-fd")
      fd = std::stoi(v);
    else if (a == "--preview-fd")
      preview = std::stoi(v);
    else if (a == "--lease-fd")
      processLock = std::stoi(v);
    else if (a == "--resources")
      resources = v;
    else if (a == "--data-dir")
      directory = v;
    else
      return 2;
  }
  if (fd < 0 || preview < 0 || resources.empty() || directory.empty())
    return 2;
  signal(SIGTERM, stop);
  signal(SIGINT, stop);
  signal(SIGHUP, stop);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_DFL);
  struct stat st {};
  if (fstat(preview, &st) || st.st_size < ssize_t(sizeof(PreviewSlot)))
    return 2;
  auto slot = static_cast<PreviewSlot *>(mmap(nullptr, sizeof(PreviewSlot),
                                              PROT_READ | PROT_WRITE,
                                              MAP_SHARED, preview, 0));
  if (slot == MAP_FAILED)
    return 2;
  try {
    if (!mock) {
      if (processLock < 0)
        processLock = acquireScanProcessLock();
      if (!validScanProcessLock(processLock))
        throw std::runtime_error("invalid scanner process lock");
      fcntl(processLock, F_SETFD, FD_CLOEXEC);
    }
    Worker worker(fd, slot, resources, directory, mock);
    while (running) {
      pollfd p{fd, POLLIN, 0};
      poll(&p, 1, 5);
      if (p.revents & (POLLHUP | POLLERR | POLLNVAL))
        break;
      try {
        for (int n = 0; n < 32; ++n) {
          auto j = receivePacket(fd);
          if (j.is_boolean()) {
            running = 0;
            break;
          }
          if (j.is_null())
            break;
          worker.command(j);
        }
        if (running)
          worker.tick();
      } catch (const ControlDisconnected &) {
        running = 0;
      } catch (const std::exception &e) {
        worker.fail(e.what());
      }
    }
  } catch (const ControlDisconnected &) {
    // The peer may close between poll and an asynchronous preview/error send.
    // Worker unwinding has released the sensor and all result readers.
  } catch (const std::exception &e) {
    sendPacket(
        fd, {{"v", ProtocolVersion}, {"type", "fatal"}, {"error", e.what()}});
    munmap(slot, sizeof(PreviewSlot));
    if (processLock >= 0)
      close(processLock);
    return 1;
  }
  munmap(slot, sizeof(PreviewSlot));
  close(preview);
  close(fd);
  if (processLock >= 0)
    close(processLock);
  return 0;
}
