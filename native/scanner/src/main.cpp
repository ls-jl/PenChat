#include "hardware.hpp"
#include "memory_policy.hpp"
#include "runtime_io.hpp"
#include "png.hpp"
#include "preview.hpp"
#include "protocol.hpp"
#include "sequence_gap.hpp"
#include "capture_timing.hpp"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <pen_stream/stream.hpp>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#ifdef SCANNER_DIAGNOSTIC_TRACE
#include "diagnostic_trace.hpp"
#endif
using namespace penchat_scan;
using namespace pen::stream;
namespace {
const char *rotationName(Rotation rotation) {
  switch (rotation) {
  case Rotation::None: return "none";
  case Rotation::Clockwise90: return "clockwise90";
  case Rotation::HalfTurn: return "halfturn";
  case Rotation::CounterClockwise90: return "counterclockwise90";
  }
  return "unknown";
}
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
  bool attempted_ = false;
  unsigned calls_ = 0;
public:
  void open() override {
    const bool first = !attempted_;
    attempted_ = true;
    if (first && getenv("PENCHAT_TEST_SCAN_FAIL_PREPARE"))
      throw std::runtime_error("injected_model_preparation_failure");
  }
  void close() noexcept override {}
  size_t managedBytes() const noexcept override { return 0; }
  void infer(const NetworkBatch &, Heads &h) override {
    const bool badTail = calls_++ && getenv("PENCHAT_TEST_SCAN_BAD_TAIL");
    if (const char *delay = getenv("PENCHAT_TEST_SCAN_INFER_MS"))
      std::this_thread::sleep_for(std::chrono::milliseconds(std::max(0, std::min(500, atoi(delay)))));
    const unsigned bins[9] = {384, 384, 512, 512, 512, 512, 512, 512, 1};
    for (unsigned k = 0; k < 9; ++k) {
      h[k].assign(32 * bins[k], -80);
      for (unsigned t = 0; t < 32; ++t)
        h[k][t * bins[k] + bins[k] / 2 + (k == 0 ? 2 : (badTail && k == 2 && t >= 30 ? 160 : 0))] = 0;
    }
  }
};
#endif
class ObservedInference final : public Inference {
  std::unique_ptr<Inference> backend_;
  std::function<void()> started_;
  bool sliceSync_ = false;

public:
  ObservedInference(std::unique_ptr<Inference> b, std::function<void()> f, bool sliceSync)
      : backend_(std::move(b)), started_(std::move(f)), sliceSync_(sliceSync) {}
  void open() override {
    RuntimeIoScope scope(sliceSync_);
    backend_->open();
  }
  void close() noexcept override {
    RuntimeIoScope scope(sliceSync_);
    backend_->close();
  }
  size_t managedBytes() const noexcept override {
    return backend_->managedBytes();
  }
  void infer(const NetworkBatch &b, Heads &h) override {
    started_();
    RuntimeIoScope scope(sliceSync_);
    backend_->infer(b, h);
  }
};
class Worker {
#ifdef SCANNER_DIAGNOSTIC_TRACE
  DiagnosticTrace trace_;
#endif
  int fd_;
  PreviewSlot *slot_;
  std::string resources_, directory_, session_, state_ = "disarmed";
  uint64_t generation_ = 0, request_ = 0, scan_ = 0;
  bool ocrKeys_ = false;
  unsigned ocrSequence_ = 0;
  bool mock_ = false, armed_ = false, capturing_ = false, finished_ = false;
#ifdef SCANNER_TEST_SOURCE
  std::atomic<int> mockMemory_{0};
#endif
  CaptureProfile profile_;
  std::deque<Json> deferredControl_;
  uint64_t mediaToken_=0;
  void yieldMedia() {
    if(!running)return;
    const auto token=++mediaToken_;emit("media_yield",{{"token",token}});
    const auto end=clockUs()+3000000;
    while(clockUs()<end) {
      const auto packet=receivePacket(fd_);
      if(packet.is_boolean())throw ControlDisconnected();
      if(packet.is_null()){pollfd p{fd_,POLLIN,0};poll(&p,1,10);continue;}
      if(packet.value("type",std::string())=="media_ack"&&packet.value("token",uint64_t(0))==token)return;
      if(deferredControl_.size()>=32)throw std::runtime_error("media handoff command queue full");
      deferredControl_.push_back(packet);
    }
    throw std::runtime_error("native display release timed out");
  }
  bool requireRelease_ = false, keyDown_ = false, draining_ = false;
  bool retryWhenEmpty_ = false;
  unsigned afterStop_ = 0;
  int64_t releaseObservedUs_ = 0;
  uint64_t rejectedAfterRelease_ = 0;
  bool haveSequence_ = false;
  uint64_t lastSequence_ = 0, droppedRaw_ = 0, recoveredGaps_ = 0;
  uint64_t recoveredFrames_ = 0;
  struct GapDetail {
    uint32_t before, after;
    int64_t elapsedUs;
    unsigned stableFrames;
    bool recovered;
  };
  std::array<GapDetail, 16> gapDetails_{};
  size_t gapDetailCount_ = 0;
  unsigned framesSinceGap_ = 16;
  int64_t lastRawTimestamp_ = 0;
  std::string captureFailure_;
  std::atomic<int64_t> firstSelectedUs_{0}, firstBatchUs_{0};
  std::atomic<unsigned> warnings_{0};
  int64_t lightRequestedUs_ = 0;
  int64_t ownerDeadline_ = 0, nextOwner_ = 0;
  int64_t nextPreview_ = 0, lastFrame_ = 0, pressTime_ = 0, firstFrame_ = 0,
          drainDeadline_ = 0, sourceCutoffUs_ = 0;
  std::future<Json> exportTask_;
  std::atomic<bool> exportCancel_{false};
  bool exportReview_ = false;
  uint64_t exportRequest_ = 0;
  Json lease_, exported_, memoryAtStop_ = Json::object();
  std::mutex eventMutex_;
  std::deque<Event> events_;
  Hardware hardware_;
  bool engineSealed_ = false, reclaimed_ = false;
  uint64_t discardedBacklog_ = 0;
  Light light_;
  Config config_;
  std::unique_ptr<Engine> engine_;
  ResultHandle result_;
  PreviewWorker preview_;
  int64_t lastPublishedUs_ = 0, lastCaptureTickUs_ = 0, maxCaptureLoopUs_ = 0;
  Json counts() {
    auto r = scan_ ? engine_->preview(scan_) : ResultHandle{};
    Counts c = r ? r->counts() : Counts{};
    auto m = engine_->metrics();
    const auto io = runtimeIoMetrics();
    const auto queued = m.rawQueued + m.selectedQueued;
    return {{"received", c.received},        {"filtered", c.filtered},
            {"selected", c.selected},        {"committed", c.committed},
            {"pending", c.pending},          {"unlocated", c.unlocated},
            {"batches", c.inferenceBatches}, {"dropped_raw", droppedRaw_},
            {"cache_sync_operations", io.operations}, {"cache_sync_slices", io.slices},
            {"cache_sync_max_call_us", io.maxSliceUs},
            {"recovered_gaps", recoveredGaps_},
            {"recovered_frames", recoveredFrames_},
            {"queued", queued},
            {"raw_queued", m.rawQueued}, {"selected_queued", m.selectedQueued},
            {"queue_peak", m.queuedPeak},
            {"raw_peak", m.rawPeak}, {"selected_peak", m.selectedPeak},
            {"frame_bytes", m.frameBytes}, {"canvas_bytes", m.canvasBytes},
            {"peak_frame_bytes", m.peakFrameBytes}, {"peak_canvas_bytes", m.peakCanvasBytes},
            {"rss_bytes", m.residentBytes}, {"rss_peak_bytes", m.peakResidentBytes}, {"swap_bytes", m.swapBytes},
            {"system_available_bytes", m.systemAvailableBytes}, {"system_reserve_bytes", m.systemReserveBytes},
            {"spilled", 0},
            {"discarded_backlog", discardedBacklog_},
            {"post_release_rejected", rejectedAfterRelease_},
            {"memory_stop_code", captureFailure_ == "system_memory_low" ? 1 :
                captureFailure_ == "system_memory_unavailable" ? 2 :
                captureFailure_ == "memory_allocation_failed" ? 3 :
                captureFailure_ == "canvas_allocation_failed" ? 4 :
                captureFailure_ == "render_memory_allocation_failed" ? 5 : 0},
            {"stop_rss_bytes", memoryAtStop_.value("rss_bytes", std::size_t(0))},
            {"stop_swap_bytes", memoryAtStop_.value("swap_bytes", std::size_t(0))},
            {"stop_frame_bytes", memoryAtStop_.value("frame_bytes", std::size_t(0))},
            {"stop_canvas_bytes", memoryAtStop_.value("canvas_bytes", std::size_t(0))},
            {"warnings", warnings_.load()}};
  }
  void rememberStop() {
    if (!memoryAtStop_.empty()) return;
    const auto m = engine_->metrics();
    memoryAtStop_ = {{"rss_bytes", m.residentBytes}, {"swap_bytes", m.swapBytes},
      {"system_available_bytes", m.systemAvailableBytes}, {"system_reserve_bytes", m.systemReserveBytes},
      {"frame_bytes", m.frameBytes}, {"canvas_bytes", m.canvasBytes},
      {"raw_queued", m.rawQueued}, {"selected_queued", m.selectedQueued}};
  }
  void emit(const std::string &type, Json data = Json::object(), uint64_t request = 0) {
    data["v"] = ProtocolVersion;
    data["type"] = type;
    data["generation"] = generation_;
    data["requestId"] = request ? request : request_;
    data["scanId"] = scan_;
    data["session"] = session_;
    data["profile"] = profile_.id;
    data["sensor_width"] = profile_.width;
    data["sensor_height"] = profile_.height;
    const auto region=sensorRegion(profile_);
    data["sensor_region"] = {region.left,region.top,region.width,region.height};
    data["processing_width"] = config_.width;
    data["processing_height"] = config_.height;
    data["sensor_rotation"] = rotationName(config_.rotation);
    data["hardware_armed"] = armed_;
    if (!sendPacket(fd_, data))
      throw ControlDisconnected();
  }
  void calibration() {
    const auto region=sensorRegion(profile_);
    config_.width = region.width;
    config_.height = region.height;
    // Optical mounting is independent of display/touch rotation. Apply this
    // once in the engine, shared by selection, model input and raw canvas.
    // S6: clockwise90 relative to the original CCW90 view cancels to None.
    config_.rotation = profile_.id == "s6-pag7920"
        ? Rotation::None : Rotation::CounterClockwise90;
    if(profile_.id=="y07-sc2356") {
      std::ifstream source(resources_+"/y07-sc2356.json");Json j;source>>j;
      if(j.value("profile",std::string())!=profile_.id || j.value("sensor_width",0)!=profile_.width ||
         j.value("sensor_height",0)!=profile_.height || j.value("rotation",std::string())!="counterclockwise90")
        throw std::runtime_error("Y07 scan calibration profile mismatch");
      const float gain=j.at("gain_constant").get<float>();
      if(!std::isfinite(gain)||gain<.8f||gain>1.4f)throw std::runtime_error("invalid Y07 scan calibration");
      auto k=std::make_shared<Calibration>();k->version=j.at("version").get<std::string>();k->sensorWidth=profile_.width;
      k->sensorHeight=profile_.height;k->rotation=config_.rotation;k->gain.fill(gain);
      config_.calibration=k;config_.diagnosticAcceptAll=mock_;return;
    }
    if (profile_.id == "y02-sc035gs" || profile_.id == "melon-ov7251" || profile_.cvi) {
      // Independent neutral flat-field, NOT the ORANGE illumination resource.
      // Keep raw SC035GS detail; per-frame photometric estimation still runs.
      auto k = std::make_shared<Calibration>();
      k->version = profile_.id == "s6-pag7920"
          ? "s6-pag7920-optical-region-neutral-v4" : profile_.id+"-native-neutral-v1";
      k->sensorWidth = config_.width;
      k->sensorHeight = config_.height;
      k->rotation = config_.rotation;
      k->gain.fill(1.f);
      config_.calibration = k;
      config_.diagnosticAcceptAll = mock_;
      return;
    }
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
  void release(bool cancel = false) {
#ifdef SCANNER_DIAGNOSTIC_TRACE
    trace_.cancel();
#endif
    cancelExport();
    preview_.cancelAndWait();
    if (scan_) {
      if (cancel) engine_->cancelScan(scan_);
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
    engine_->reclaimIdle();
    scan_ = 0;
    capturing_ = false;
    finished_ = false;
    draining_ = false;
    retryWhenEmpty_ = false;
    removeExport();
    std::lock_guard<std::mutex> l(eventMutex_);
    events_.clear();
  }
  void disarm() {
    light_.off();
    hardware_.close();
    armed_ = false;
    ocrKeys_ = false;
    keyDown_ = false;
    state_ = "disarmed";
    // Camera, key and guardian are restored before imposing an exit bound on
    // cancelled work. A stuck NPU call must not leave a detached worker alive.
    DrainDeadline deadline;
    release(true);
    lease_ = Json();
  }
  void begin(int64_t pressedUs = 0) {
    if (!armed_ || scan_ || requireRelease_)
      return;
    mkdir(directory_.c_str(), 0700);
    engineSealed_ = false;
    reclaimed_ = false;
    memoryAtStop_ = Json::object();
    discardedBacklog_ = 0;
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
      fail(*begin.reason ? begin.reason : "scan engine unavailable");
      return;
    }
    firstSelectedUs_ = 0;
    firstBatchUs_ = 0;
    warnings_ = 0;
    scan_ = begin.scan;
#ifdef SCANNER_DIAGNOSTIC_TRACE
    auto traceConfig=config_;
    traceConfig.width=profile_.width;traceConfig.height=profile_.height;
    trace_.begin(directory_,traceConfig);
#endif
    const auto now = clockUs();
    pressTime_ = pressedUs > 0 && pressedUs <= now ? pressedUs : now;
    releaseObservedUs_ = 0; rejectedAfterRelease_ = 0;
    lastCaptureTickUs_ = pressTime_;
    maxCaptureLoopUs_ = 0;
    firstFrame_ = 0;
    lastFrame_ = pressTime_;
    haveSequence_ = false;
    droppedRaw_ = recoveredGaps_ = recoveredFrames_ = 0;
    gapDetailCount_ = 0;
    framesSinceGap_ = 16;
    lastRawTimestamp_ = 0;
    captureFailure_.clear();
    afterStop_ = 0;
    sourceCutoffUs_ = 0;
    capturing_ = true;
    finished_ = false;
    draining_ = false;
    state_ = "capturing";
    lightRequestedUs_ = clockUs();
    light_.request(true);
    emit("started",
         {{"pressedUs", pressTime_}, {"lightRequestedUs", lightRequestedUs_}});
  }
  bool retryEmpty(int64_t pressedUs = 0) {
    if (!armed_ || !finished_ || !result_)
      return false;
    const auto bounds = result_->bounds();
    if (bounds.width > 0 && bounds.height > 0)
      return false; // Never discard an image waiting for user review.
    release();
    requireRelease_ = false;
    if (!mock_)
      hardware_.exposure(true);
    begin(pressedUs);
    return true;
  }
  void keyEvent(bool down, int64_t eventUs = 0) {
    if (!armed_ || down == keyDown_)
      return;
    keyDown_ = down;
    if (down) {
      if (retryEmpty(eventUs))
        return;
      // Finish deciding whether there is an image before discarding anything.
      retryWhenEmpty_ = scan_ && !capturing_ && !finished_;
      begin(eventUs);
    } else {
      retryWhenEmpty_ = false;
      requireRelease_ = false;
      releaseObservedUs_ = clockUs();
      drain(eventUs);
    }
  }
  void seal() {
    if (!capturing_)
      return;
    capturing_ = false;
    draining_ = false;
    light_.request(false);
    state_ = "processing";
    emit("processing", {{"counts", counts()}});
  }
  void drain(int64_t releasedUs = 0) {
    if (!capturing_ || draining_)
      return;
    const auto now = clockUs();
    sourceCutoffUs_ = releasedUs > 0 ? std::max(pressTime_, std::min(releasedUs, now)) : now;
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
  void cancelExport() {
    exportCancel_=true;
    if(exportTask_.valid()) {try{exported_=exportTask_.get();}catch(...) {}}
    exportCancel_=false;
  }
  void pollExport() {
    if(!exportTask_.valid() || exportTask_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    try {
      exported_=exportTask_.get();
      emit(exportReview_?"review_ready":"exported",{{"image",exported_}},exportRequest_);
    } catch(const std::exception &e) {
      emit(exportReview_?"review_error":"error",{{"error",e.what()}},exportRequest_);
    }
  }
  void exportResult(bool review = false) {
    if (!exported_.empty()) {
      emit(review?"review_ready":"exported", {{"image", exported_}});
      return;
    }
    if (!finished_ || !result_ || exportTask_.valid())
      return;
    auto b = result_->bounds();
    if (b.width < 1 || b.height < 1 || b.width > 1024 * 1024)
      throw std::runtime_error(
          "scan has no usable image or row exceeds export limit");
    mkdir(directory_.c_str(), 0700);
    const auto stem = directory_ + "/scan_" + std::to_string(getpid()) + "_" +
                      std::to_string(generation_) + "_" + std::to_string(scan_);
    Json image = {{"path", stem + ".png"},
                 {"previewPath", stem + ".preview.png"},
                 {"width", b.width},
                 {"height", b.height},
                 {"source", "scan"}};
    auto c = counts();
    image["metadata"] = {
        {"status", captureFailure_.empty() ? int(result_->status())
                                           : int(Status::Segmented)},
        {"counts", c}, {"memory_at_stop", memoryAtStop_},
        {"reason",
         captureFailure_.empty() ? result_->reason() : captureFailure_},
        {"calibration", config_.calibration->version},
        {"profile", profile_.id}, {"sensor_width", profile_.width},
        {"sensor_height", profile_.height},
        {"sensor_region", {sensorRegion(profile_).left,sensorRegion(profile_).top,
            config_.width,config_.height}},
        {"sensor_rotation", rotationName(config_.rotation)},
        {"timing",
         {{"pressedUs", pressTime_},
          {"firstFrameUs", firstFrame_},
          {"lightRequestedUs", lightRequestedUs_},
          {"firstSelectedUs", firstSelectedUs_.load()},
          {"firstBatchUs", firstBatchUs_.load()},
          {"maxCaptureLoopUs", maxCaptureLoopUs_},
          {"captureEndedUs", sourceCutoffUs_},
          {"releaseObservedUs", releaseObservedUs_}}}};
    const auto result=result_;
    exportCancel_=false;exportReview_=review;exportRequest_=request_;
    exportTask_=std::async(std::launch::async,[this,result,image,b]() mutable {
      try {
        std::vector<uint8_t> mask(b.width);
        writeGrayPng(image["path"],b.width,b.height,[&](int y,uint8_t *pixels) {
          if(exportCancel_ || result->readRow(y,pixels,mask.data(),b.width)!=Code::Ok)
            throw std::runtime_error("scan image preparation cancelled or unavailable");
        });
        if(exportCancel_)throw std::runtime_error("scan image preparation cancelled");
        const double scale=std::min(1.,std::min(1024./b.width,256./b.height));
        const int w=std::max(1,int(b.width*scale)),h=std::max(1,int(b.height*scale));
        std::vector<uint8_t> preview(size_t(w)*h),valid(preview.size());
        if(result->readPreview(w,h,preview.data(),valid.data(),preview.size())!=Code::Ok)
          throw std::runtime_error("scan preview unavailable");
        writeGrayPng(image["previewPath"],w,h,[&](int y,uint8_t *row) {
          if(exportCancel_)throw std::runtime_error("scan image preparation cancelled");
          memcpy(row,preview.data()+size_t(y)*w,w);
        });
        if(exportCancel_)throw std::runtime_error("scan image preparation cancelled");
        return image;
      } catch(...) {
        for(const char *key:{"path","previewPath"}) {
          const auto path=image.at(key).get<std::string>();unlink(path.c_str());unlink((path+".part").c_str());
        }
        throw;
      }
    });
  }

public:
  Worker(int fd, PreviewSlot *slot, std::string resources,
         std::string directory, bool mock, const CaptureProfile &profile)
      : fd_(fd), slot_(slot), resources_(std::move(resources)),
        directory_(std::move(directory)), mock_(mock), profile_(profile), light_(mock) {
    if (!profile_.supported())
      throw std::runtime_error("unsupported scan board or pen sensor");
    if(profile_.cvi)hardware_.beforeRestore([this]{yieldMedia();});
    std::unique_ptr<Inference> backend;
#ifdef SCANNER_TEST_SOURCE
    if (mock_)
      backend = std::make_unique<TestInference>();
#endif
    if (!backend) {
      // Y07 firmware ships RKNN 1.4.0, which aborts on this model's CPU
      // MaxPool dilations. Load the pinned private runtime only on that board.
      const auto runtime = profile_.privateRuntime.empty()
          ? std::string("librknnrt.so")
          : resources_ + "/lib/" + profile_.privateRuntime;
#ifdef PENCHAT_CVI_CAPTURE
      backend = makeCvi32(resources_ + "/pintu_rgb32_int8.cvimodel", "/oem/YoudaoDictPen/output/libs/libcviruntime.so");
#else
      backend = makeRknn32(resources_ + "/pintu_32.rknn", runtime);
#endif
    }
    Limits limits;
    limits.maxSessions = 1;
    limits.idleModelMs = 0;
#ifdef SCANNER_TEST_SOURCE
    if (mock_) limits.memoryProbe = [this] {
      const auto state = mockMemory_.load();
      if (state == 1) return SystemMemory{1024 * MiB, 0, true};
      if (state == 2) return SystemMemory{};
      return systemMemory();
    };
#endif
    backend = std::make_unique<ObservedInference>(std::move(backend), [this] {
      int64_t zero = 0;
      firstBatchUs_.compare_exchange_strong(zero, clockUs());
    }, profile_.id == "y02-sc035gs");
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
  void drainDeferred() {
    while(!deferredControl_.empty()){auto commandPacket=std::move(deferredControl_.front());deferredControl_.pop_front();command(commandPacket);}
  }
  void command(const Json &j) {
    auto type = j.value("type", std::string());
    auto generation = j.value("generation", uint64_t(0));
#ifdef SCANNER_TEST_SOURCE
    if(mock_&&type=="test.media_disarm") {
      yieldMedia();disarm();emit("released");return;
    }
#endif
    if (type == "ocr_keys" && profile_.id != "y07-sc2356")
      throw std::runtime_error("key-only OCR is not supported on this profile");
    if (type == "ocr_keys" && generation > generation_ && armed_) {
      light_.off();
      if (!mock_) hardware_.cameraToOcr();
      armed_ = false; keyDown_ = false;
      { DrainDeadline deadline; release(true); }
      lease_ = Json(); generation_ = generation;
      session_ = j.at("session").get<std::string>();
      request_ = j.value("requestId", uint64_t(0));
      ocrKeys_ = true; state_ = "ocr_keys"; emit("released", {{"ocr_keys", true}});
      return;
    }
    if (type == "arm" || type == "ocr_keys") {
      if (generation <= generation_)
        return;
      disarm();
      ocrKeys_ = type == "ocr_keys";
      generation_ = generation;
      session_ = j.at("session").get<std::string>();
      request_ = j.value("requestId", uint64_t(0));
      if (mock_) {
        if (ocrKeys_) { state_="ocr_keys"; emit("released",{{"ocr_keys",true}}); return; }
        if (engine_->prepareModel() != Code::Ok)
          throw std::runtime_error("model preparation unavailable");
        state_ = "warming";
        emit("preparing", {{"phase", "model_warmup"}});
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
      // Reserve the key and pause the stock capture owner before model work,
      // but do not start our sensor. The parent already retained the lease.
      hardware_.reserve(lease_);
      if (ocrKeys_) {
        hardware_.cameraToOcr(); lease_ = Json();
        state_ = "ocr_keys"; emit("released", {{"ocr_keys", true}}); return;
      }
      if (engine_->prepareModel() != Code::Ok)
        throw std::runtime_error("model preparation unavailable");
      state_ = "warming";
      emit("preparing", {{"phase", "model_warmup"}});
    } else if (type == "disarm" || type == "cancel") {
      disarm();
      emit("released");
    } else if(type=="prepare_review" && j.value("scanId",uint64_t(0))==scan_) {
      try {exportResult(true);}
      catch(const std::exception &e) {emit("review_error",{{"error",e.what()}});}
    } else if(type=="cancel_review" && j.value("scanId",uint64_t(0))==scan_) {
      if(exportReview_)cancelExport();
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
    else if (mock_ && type == "test.memory") {
      mockMemory_ = std::max(0, std::min(2, j.value("state", 0)));
    } else if (mock_ && type == "test.press") {
      if (state_=="ocr_keys") {
        const bool down=j.value("pressed",false);
        if (down!=keyDown_) { keyDown_=down; if(down)++ocrSequence_; emit("ocr_key",{{"down",down},{"sequence",ocrSequence_}}); }
      } else keyEvent(j.value("pressed", false));
    } else if (mock_ && type == "test.frame" && capturing_) {
      std::vector<uint8_t> pixels(size_t(profile_.width) * profile_.height, 210);
      for (int y = 20; y < profile_.height - 20; ++y)
        for (int x = 20; x < profile_.width - 20; ++x)
          if ((x + y) % 11 < 3)
            pixels[size_t(y) * profile_.width + x] = 30;
      frame(pixels.data(), profile_.width,
            j.value("rawSequence", haveSequence_ ? lastSequence_ + 1 : uint64_t(0)),
            j.value("rawTimestamp", clockUs()));
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
    if (!mock_ && afterReleasedFrame(timestamp, sourceCutoffUs_, draining_, profile_.gatesExposure)) {
      ++rejectedAfterRelease_;
      // Outside the released pen's capture window, not a lost in-window frame.
      seal();
      return;
    }

    if (haveSequence_ && uint32_t(sequence) != uint32_t(lastSequence_ + 1)) {
      const auto distance = uint32_t(sequence) - uint32_t(lastSequence_);
      // A backwards/reset sequence is not billions of missing frames.
      if (distance > 1 && distance < 0x80000000u)
        droppedRaw_ += distance - 1;
      // Preserve the actual next image and report every lost source frame.
      // All supported boards continue through up to eight missing frames.
      const auto elapsed = timestamp - lastRawTimestamp_;
      const bool recoverable = recoverableSequenceGap(
          distance, elapsed);
      if (gapDetailCount_ < gapDetails_.size())
        gapDetails_[gapDetailCount_++] = {uint32_t(lastSequence_),
            uint32_t(sequence), elapsed, framesSinceGap_, recoverable};
      if (recoverable) {
        ++recoveredGaps_;
        recoveredFrames_ += distance - 1;
        framesSinceGap_ = 0;
      } else {
        captureFailure_ = distance > 1 && distance < 0x80000000u
                              ? "sensor_sequence_gap"
                              : "sensor_sequence_reset";
        requireRelease_ = true;
        drain();
        seal();
        return;
      }
    }
    haveSequence_ = true;
    lastSequence_ = sequence;
    lastRawTimestamp_ = timestamp;
    framesSinceGap_ = std::min(16u, framesSinceGap_ + 1);
    const bool wasDraining = draining_;
    const auto region=sensorRegion(profile_);
    const auto *paper=pixels+std::size_t(region.top)*stride+region.left;
    const auto pushed = engine_->pushFrame(scan_, paper, config_.width, config_.height, stride, sequence, timestamp);
#ifdef SCANNER_DIAGNOSTIC_TRACE
    if(pushed.accepted)trace_.append(pixels,stride,sequence,timestamp);
#endif
    if (!pushed.accepted || pushed.code == Code::StopCapture) {
      captureFailure_ = *pushed.reason ? pushed.reason : "memory_allocation_failed";
      if (!pushed.accepted) ++discardedBacklog_;
      rememberStop();
      requireRelease_ = true;
      drain(); seal();
      return;
    }
    if (!firstFrame_) firstFrame_ = lastFrame_;
    if (wasDraining && capturing_ && afterStop_ && --afterStop_ == 0)
      seal();
  }
  void pumpFrames() {
    if (!scan_ || finished_ || engineSealed_) return;
    if (!capturing_) {
      engineSealed_ = true;
      engine_->endScan(scan_);
    }
  }
  void tick() {
    pollExport();
    engine_->checkResources();
    if (state_ == "warming") {
      const auto model = engine_->modelPreparation();
      if (model.state == ModelState::Failed)
        throw std::runtime_error(model.error.empty() ? "model_preparation_failed" : model.error);
      if (model.state == ModelState::Ready) {
        engine_->reclaimIdle();
        if (mock_) {
          armed_ = true;
          state_ = "ready";
          emit("ready", {{"model_prepared_us", model.elapsedUs}});
        } else {
          hardware_.start();
          armed_ = true;
          requireRelease_ = false;
          state_ = "ready";
          emit("ready", {{"model_prepared_us", model.elapsedUs}});
        }
      }
    }
    if (finished_ && !reclaimed_) reclaimed_ = engine_->reclaimIdle();
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
    if (state_ == "ocr_keys" && !mock_) {
      bool down = false;
      while (hardware_.keyEvent(down)) {
        if (down) ++ocrSequence_;
        emit("ocr_key", {{"down", down}, {"sequence", ocrSequence_}});
      }
    }
    if (armed_ && !mock_) {
      auto keys = [this] {
        bool down = false; int64_t eventUs = 0;
        while (hardware_.keyEvent(down, &eventUs)) keyEvent(down, eventUs);
      };
      // Keep standby draining, but never let a blocking 16-frame batch defer
      // the release event. The callback checks again after each CVI read.
      bool readAny = false;
      pollCaptureBatch(keys,
          [this](const auto& consume) { return hardware_.readFrame(consume); },
          [this, &readAny](const uint8_t* y, int stride, uint64_t seq, int64_t ts) {
            readAny = true; frame(y, stride, seq, ts);
          },
          [this, &readAny] {
            // After S6 seals, return promptly to endScan/control processing;
            // sixteen blocking standby reads must not delay result/commands.
            return (!profile_.cvi || capturing_ || !readAny) &&
                   (!draining_ || clockUs() < drainDeadline_);
          });
      if (draining_ && clockUs() >= drainDeadline_)
        seal();
      if (capturing_ && clockUs() - lastFrame_ > 5000000)
        throw std::runtime_error("pen camera stalled");
    }
    pumpFrames();
    std::deque<Event> events;
    {
      std::lock_guard<std::mutex> l(eventMutex_);
      events.swap(events_);
    }
    for (auto &e : events) {
      if (e.scan != scan_)
        continue;
      if (e.kind == Event::Kind::StopRequested) {
        if (captureFailure_.empty()) captureFailure_ = e.reason;
        rememberStop();
        requireRelease_ = true;
        drain(); seal();
        emit("stopped", {{"reason", e.reason}});
      }
      if (e.kind == Event::Kind::Finished) {
#ifdef SCANNER_DIAGNOSTIC_TRACE
        try {trace_.finish();}catch(...){std::fprintf(stderr,"SCAN_TRACE_SAVE_START_FAILED\n");trace_.cancel();}
#endif
        result_ = engine_->result(scan_);
        finished_ = true;
        state_ = "result";
        requestPreview(true);
        auto b = result_->bounds();
        Json gaps = Json::array();
        for (size_t i = 0; i < gapDetailCount_; ++i) {
          const auto &gap = gapDetails_[i];
          gaps.push_back({{"before", gap.before}, {"after", gap.after},
              {"elapsed_us", gap.elapsedUs}, {"stable_frames", gap.stableFrames},
              {"recovered", gap.recovered}});
        }
        emit("finished",
             {{"frame_gaps", gaps}, {"memory_at_stop", memoryAtStop_},
              {"status", captureFailure_.empty() ? int(result_->status())
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
              {"maxCaptureLoopUs", maxCaptureLoopUs_},
              {"captureEndedUs", sourceCutoffUs_},
              {"releaseObservedUs", releaseObservedUs_},
              {"lastSourceFrameUs", lastRawTimestamp_}});
        if (retryWhenEmpty_ && keyDown_)
          retryEmpty();
      }
    }
    if (scan_ && !finished_ && clockUs() >= nextPreview_) {
      nextPreview_ = clockUs() + 100000;
      requestPreview();
      emit("progress", {{"counts", counts()}, {"phase", state_}});
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
      engineSealed_ = true;
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
  bool idle() const { return state_ == "disarmed" && deferredControl_.empty(); }
};
} // namespace
int main(int argc, char **argv) {
  configureScannerAllocator();
  int fd = -1, preview = -1, processLock = -1;
  std::string resources, directory;
  bool mock = false;
#ifdef SCANNER_TEST_SOURCE
  std::string mockProfile="orange";
#endif
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
#ifdef SCANNER_TEST_SOURCE
    if (a == "--simulate" || a == "--simulate-y02" || a == "--simulate-melon" || a == "--simulate-y07" || a == "--simulate-s6") {
      mock = true;
      mockProfile=a=="--simulate"?"orange":a.substr(std::string("--simulate-").size());
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
    auto profile = detectedScanProfile();
#ifdef SCANNER_TEST_SOURCE
    if (mock)
      profile = mockProfile=="y02" ? scanProfile("Rockchip RK3562 Y02 LP4 V10 Board", "sc035gs")
              : mockProfile=="melon" ? scanProfile("Rockchip RK3562 MELON LP4 V10 Board", "ov7251")
              : mockProfile=="y07" ? scanProfile("Rockchip RK3562 Y07 LP4 V10 Board", "sc2356")
              : mockProfile=="s6" ? scanProfile("Cvitek Cv1826 for Youdao Plum V0 version", "PIXART_PAG7920_MIPI_240P_100FPS_10BIT")
              : scanProfile("Rockchip RK3562 ORANGE LP4 V10 Board", "sc2356");
#endif
    Worker worker(fd, slot, resources, directory, mock, profile);
    while (running) {
      pollfd p{fd, POLLIN, 0};
      poll(&p, 1, worker.idle() ? -1 : 5);
      if (p.revents & (POLLHUP | POLLERR | POLLNVAL))
        break;
      try {
        worker.drainDeferred();
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
