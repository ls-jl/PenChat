// Opt-in S6 test plugin. Not referenced by the application or production package.
#include "jquick_config.h"
#include "JQuickContext.h"
#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "probe.hpp"
#include "composite.hpp"
#include <nlohmann/json.hpp>
#include <dlfcn.h>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
using namespace JQUTIL_NS;
namespace {
using Json = nlohmann::json;
constexpr char kSymbol[] = "_ZN7greenui7post_fbEPvRKNS_4RectEii";
constexpr char kHash[] = "11e46687d3c7301d3049d02d84144ee37e5a9994ca5ec1e9f5eaf278db92519b";
std::string readBounded(const char* path, std::size_t max) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot read probe platform configuration");
  std::string text(max + 1, '\0');
  stream.read(&text[0], text.size());
  text.resize(stream.gcount());
  if (text.size() > max) throw std::runtime_error("Probe platform configuration too large");
  return text;
}
Json screen() {
  const auto model = readBounded("/proc/device-tree/model", 4096);
  if (model.find("Cv1826") == std::string::npos || model.find("Plum") == std::string::npos)
    throw std::runtime_error("This probe only supports the verified S6 board");
  const auto cfg = Json::parse(readBounded("/etc/miniapp/resources/cfg.json", 65536));
  const auto& s = cfg.at("screen");
  for (const auto* key : {"width", "height", "direction"})
    if (!s.at(key).is_number_integer()) throw std::runtime_error("Invalid Falcon screen field");
  int width = s.at("width").get<int>(), height = s.at("height").get<int>();
  const int rotation = s.at("direction").get<int>();
  if (width < 100 || height < 100 || width > 4096 || height > 4096 ||
      std::uint64_t(width) * height > 4 * 1024 * 1024 || rotation % 90 || rotation < 0 || rotation > 270)
    throw std::runtime_error("Invalid bounded Falcon geometry");
  if (rotation == 90 || rotation == 270) std::swap(width, height);
  return {{"width", width}, {"height", height}, {"rotation", rotation}};
}
Bson bson(const Json& json) {
  std::string error;
  auto value = Bson::parse(json.dump(), error);
  if (!error.empty()) throw std::runtime_error("Cannot encode probe metrics");
  return value;
}
Json statsJson() {
  const auto s = penchat_compositor::Probe::instance().stats();
  return {{"installed", s.installed}, {"calls", s.calls}, {"samples", s.samples},
    {"invalid", s.invalid}, {"busy", s.busy}, {"width", s.width}, {"height", s.height},
    {"bytes", s.bytes}, {"copy_median_ms", s.copyMedianMs}, {"copy_p95_ms", s.copyP95Ms},
    {"copy_max_ms", s.copyMaxMs}, {"original_restored", s.originalRestored}, {"error", s.error},
    {"mode", "observe-and-original-pass-through"}, {"pixels_written_by_probe", false}};
}
Json compositeJson() {
  const auto s = penchat_compositor::Composite::instance().stats();
  return {{"running", s.running}, {"original_restored", s.originalRestored}, {"focused", s.focused},
    {"child_pid", s.child}, {"system_frames", s.systemFrames}, {"output_frames", s.outputFrames},
    {"native_frames", s.nativeFrames}, {"native_taps", s.taps}, {"epoch", s.epoch},
    {"coalesced", s.coalesced}, {"rejected", s.rejected}, {"elapsed_ms", s.elapsedMs},
    {"shared_bytes", s.sharedBytes}, {"copy_median_ms", s.copyMedianMs},
    {"blend_median_ms", s.blendMedianMs}, {"present_median_ms", s.presentMedianMs},
    {"reason", s.reason}, {"mode", "isolated-shared-memory-lvgl-sample"}};
}
void pinPlugin() {
  static void* pinned = nullptr;
  if (pinned) return;
  Dl_info info{};
  if (!dladdr(reinterpret_cast<void*>(&pinPlugin), &info) || !info.dli_fname)
    throw std::runtime_error("Cannot identify probe plugin lifetime");
  pinned = dlopen(info.dli_fname, RTLD_NOW | RTLD_NODELETE);
  if (!pinned) throw std::runtime_error("Cannot pin probe relay lifetime");
}
class Module : public JQBaseObject {
  std::mutex action_;
  std::string data_, workspace_, phase_;
  bool compositeRecorded_ = true;
  bool recorded_ = true, authorized_ = false;
  void recordComposite() {
    if (compositeRecorded_ || data_.empty()) return;
    const auto s = compositeJson();
    if (s.at("running").get<bool>() || !s.at("original_restored").get<bool>() || s.at("child_pid").get<int>() >= 0) return;
    const auto path = data_ + "/composite-probe.json";
    std::ofstream file(path, std::ios::trunc);
    if (file) { file << s.dump(2) << '\n'; file.close(); chmod(path.c_str(), 0600); compositeRecorded_ = true; }
  }
  void record() {
    if (recorded_ || data_.empty() || phase_.empty()) return;
    const auto s = statsJson();
    if (s.at("installed").get<bool>()) return;
    const auto path = data_ + "/submit-probe-" + phase_ + ".json";
    std::ofstream file(path, std::ios::trunc);
    if (file) { file << s.dump(2) << '\n'; file.close(); chmod(path.c_str(), 0600); recorded_ = true; }
  }
public:
  void OnCtor(JQFunctionInfo& info) override {
    authorized_ = JQuick::getAppid(info.GetContext()) == PENCHAT_SUBMIT_PROBE_APP_ID;
    if (authorized_) {
      data_ = JQuick::getDataDir(info.GetContext());
      workspace_ = JQuick::getWorkspace(info.GetContext());
    }
  }
  ~Module() override {
    if (authorized_) try {
      std::lock_guard<std::mutex> lock(action_);
      penchat_compositor::Composite::instance().stop(); recordComposite();
      penchat_compositor::Probe::instance().stop(); record();
    } catch (...) {}
  }
  void geometry(JQFunctionInfo& info) {
    try {
      if (!authorized_) throw std::runtime_error("Probe application identity mismatch");
      info.GetReturnValue().Set(bson(screen()));
    }
    catch (const std::exception& e) { info.GetReturnValue().Set(Bson::object{{"error", e.what()}}); }
  }
  void start(JQAsyncInfo& info) {
    std::lock_guard<std::mutex> lock(action_);
    try {
      if (!authorized_) throw std::runtime_error("Probe application identity mismatch");
      penchat_compositor::Composite::instance().stop(); recordComposite();
      penchat_compositor::Probe::instance().stop(); record();
      const auto cfg = screen();
      const bool copy = info[0]["copy"].bool_value();
      // A stale relay can already have been fetched by the display thread when
      // the GOT is restored. Pin the small plugin so it can always call original.
      pinPlugin();
      auto slot = penchat_compositor::locate("libfalcon.so", kSymbol, kHash);
      penchat_compositor::Probe::instance().start(slot, cfg.at("width").get<int>(), cfg.at("height").get<int>(), copy, 15, 30);
      phase_ = copy ? "copy" : "pass"; recorded_ = false;
      info.post(bson(statsJson()));
    } catch (const std::exception& e) { info.postError(e.what()); }
  }
  void startComposite(JQAsyncInfo& info) {
    std::lock_guard<std::mutex> lock(action_);
    try {
      if (!authorized_) throw std::runtime_error("Probe application identity mismatch");
      penchat_compositor::Probe::instance().stop(); record();
      penchat_compositor::Composite::instance().stop(); recordComposite();
      const auto cfg = screen();
      const auto executable = workspace_ + "/assets/bin/penchat-composite-client";
      chmod(executable.c_str(), 0755);
      pinPlugin();
      auto slot = penchat_compositor::locate("libfalcon.so", kSymbol, kHash);
      penchat_compositor::Composite::instance().start(slot, cfg.at("width").get<int>(), cfg.at("height").get<int>(), executable);
      compositeRecorded_ = false;
      info.post(bson(compositeJson()));
    } catch (const std::exception& e) {
      penchat_compositor::Composite::instance().stop(); recordComposite();
      info.postError(e.what());
    }
  }
  void compositeStatus(JQFunctionInfo& info) {
    std::lock_guard<std::mutex> lock(action_);
    if (!authorized_) { info.GetReturnValue().Set(false); return; }
    recordComposite(); info.GetReturnValue().Set(bson(compositeJson()));
  }
  void focusComposite(JQFunctionInfo& info) {
    std::lock_guard<std::mutex> lock(action_);
    if (authorized_) penchat_compositor::Composite::instance().focus(info.toParamsHolder()[0].bool_value());
    info.GetReturnValue().Set(authorized_);
  }
  void stopComposite(JQFunctionInfo& info) {
    std::lock_guard<std::mutex> lock(action_);
    if (!authorized_) { info.GetReturnValue().Set(false); return; }
    penchat_compositor::Composite::instance().stop(); recordComposite();
    info.GetReturnValue().Set(bson(compositeJson()));
  }
  void disconnectComposite(JQFunctionInfo& info) {
    std::lock_guard<std::mutex> lock(action_);
    if (authorized_) penchat_compositor::Composite::instance().disconnect();
    info.GetReturnValue().Set(authorized_);
  }
  void status(JQFunctionInfo& info) {
    std::lock_guard<std::mutex> lock(action_);
    if (!authorized_) { info.GetReturnValue().Set(false); return; }
    record(); info.GetReturnValue().Set(bson(statsJson()));
  }
  void stop(JQFunctionInfo& info) {
    std::lock_guard<std::mutex> lock(action_);
    if (!authorized_) { info.GetReturnValue().Set(false); return; }
    penchat_compositor::Probe::instance().stop(); record();
    info.GetReturnValue().Set(bson(statsJson()));
  }
};
std::vector<std::string> exports = {"probe"};
int init(JSContext* ctx, JSModuleDef* module) {
  auto env = JQModuleEnv::CreateModule(ctx, module, "penchatSubmitProbe");
  auto tpl = JQFunctionTemplate::New(env.get(), "probe");
  tpl->InstanceTemplate()->setObjectCreator([]() { return new Module; });
  tpl->SetProtoMethod("geometry", &Module::geometry);
  tpl->SetProtoMethodPromise("start", &Module::start);
  tpl->SetProtoMethod("status", &Module::status);
  tpl->SetProtoMethodPromise("startComposite", &Module::startComposite);
  tpl->SetProtoMethod("compositeStatus", &Module::compositeStatus);
  tpl->SetProtoMethod("focusComposite", &Module::focusComposite);
  tpl->SetProtoMethod("stopComposite", &Module::stopComposite);
  tpl->SetProtoMethod("disconnectComposite", &Module::disconnectComposite);
  tpl->SetProtoMethod("stop", &Module::stop);
  env->setModuleExport("probe", tpl->CallConstructor());
  env->setModuleExportDone(JS_UNDEFINED, exports);
  return 0;
}
DEF_MODULE_LOAD_FUNC_EXPORT(penchatSubmitProbe, init, exports)
}
extern "C" JQUICK_EXPORT void custom_init_jsapis() {
  registerCModuleLoader("penchatSubmitProbe", &penchatSubmitProbe_module_load);
}
extern "C" JQUICK_EXPORT int jsapi_check_unload() { return 0; }
