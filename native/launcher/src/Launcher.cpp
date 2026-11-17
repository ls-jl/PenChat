#include "jquick_config.h"
#include "JQuickContext.h"
#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <set>
#include <dlfcn.h>
#include "update.hpp"
#include "HeadKeyRoute.hpp"
#include "SystemOverlay.hpp"
#include "RuntimeLease.hpp"
#include "ScreenPower.hpp"
#ifdef PENCHAT_CVI_PLATFORM
#include "composite.hpp"
#endif
namespace JQuick { class PackageInfo; }
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace JQUTIL_NS;
namespace {
#ifdef PENCHAT_CVI_PLATFORM
void pinDisplayRelay() {
  static void* pinned = nullptr;
  if(pinned)return;
  Dl_info info{};
  if(!dladdr(reinterpret_cast<void*>(&pinDisplayRelay),&info)||!info.dli_fname)
    throw std::runtime_error("Cannot identify S6 display adapter");
  pinned=dlopen(info.dli_fname,RTLD_NOW|RTLD_NODELETE);
  if(!pinned)throw std::runtime_error("Cannot retain S6 display adapter");
}
#endif
void saveInstallTicket(const std::string &path,const penchat::Json &ticket) {
  penchat::atomicText(path,ticket.dump());
  int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
  if(fd<0)throw std::runtime_error("Cannot sync install ticket");
  const int result=fsync(fd);close(fd);if(result)throw std::runtime_error("Cannot sync install ticket");
  const auto parent=path.substr(0,path.find_last_of('/'));fd=open(parent.c_str(),O_DIRECTORY|O_RDONLY|O_CLOEXEC);
  if(fd<0)throw std::runtime_error("Cannot sync install directory");
  const int synced=fsync(fd);close(fd);if(synced)throw std::runtime_error("Cannot sync install directory");
}
class Launcher : public JQBaseObject {
  std::mutex mutex_;
  penchat::HeadKeyRoute headRoute_{penchat::HeadKeyRoute::realSystem()};
  penchat::SystemOverlay systemOverlay_{penchat::SystemOverlay::realSystem()};
  bool ocrRouting_=false, observingPen_=false, ready_=false;
  Bson screenDescriptor_;
  int fd_ = -1;
  pid_t pid_ = -1;
  std::string ownId_, ownData_, runtimeWorkspace_, runtimeData_;
  std::set<std::string> installations_;
  penchat::RuntimeLease lease_;
  penchat::ScreenPower screenPower_;
  penchat::ScreenPower::State sentPower_;
  int powerRuntime_=0,powerGeneration_=0;
  std::int64_t powerSentAt_=0;
  static Bson powerState(const penchat::ScreenPower::State &state) {
    return Bson::object{{"available",state.available},{"known",state.known},{"awake",state.awake},{"sequence",double(state.sequence)}};
  }
  void publishPower(const penchat::ScreenPower::State &state) {
    std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
    if(!lock.owns_lock())return; // Keep sampling even during start/stop/slow SDK calls.
    if(fd_<0||pid_<=0)return;
    const auto now=penchat::ticks();
    const bool changed=state.available!=sentPower_.available||state.known!=sentPower_.known||state.awake!=sentPower_.awake;
    if(!changed&&powerRuntime_==lease_.runtime()&&powerGeneration_==lease_.generation()&&now-powerSentAt_<500)return;
    const auto packet=penchat::Json{{"v",1},{"type","screen.power"},{"generation",lease_.generation()},
      {"sequence",state.sequence},{"available",state.available},{"known",state.known},{"awake",state.awake}}.dump();
    if(::send(fd_,packet.data(),packet.size(),MSG_NOSIGNAL|MSG_DONTWAIT)!=ssize_t(packet.size()))return;
    sentPower_=state;powerSentAt_=now;powerRuntime_=lease_.runtime();powerGeneration_=lease_.generation();
#ifdef PENCHAT_CVI_PLATFORM
    if(state.available&&(!state.known||!state.awake))penchat_compositor::Composite::instance().focus(false);
#endif
  }
  Bson lastExit_;
  bool exitPending_=false;
  std::string exitReason_;
  bool lastFocused_=false,lastPanel_=false,pageAttached_=false;
  void persistExit() {
    try {if(!runtimeData_.empty())penchat::atomicText(runtimeData_+"/last-exit.json",lastExit_.dump());}catch(...){}
  }
  void noteExitReason(const std::string &reason) {
    static const std::set<std::string> allowed{"normal","explicit_exit","signal","ipc_eof","update","runtime_error"};
    if(!allowed.count(reason))return;
    exitReason_=reason;
    if(pid_<=0&&!lastExit_.is_null()) {
      auto record=lastExit_.object_items();record["reason"]=reason;lastExit_=Bson(record);persistExit();
    }
  }
  void reaped(int status,bool unknown=false) {
    if(pid_<=0)return;
    const int child=int(pid_);pid_=-1;ready_=false;headRoute_.stop();
    const int code=unknown?-1:WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
    lastExit_=Bson::object{{"v",1},{"type","exit"},{"status",code},{"pid",child},
      {"runtimeId",lease_.runtime()},{"ownerGeneration",lease_.generation()},
      {"time_ms",double(penchat::nowMs())},{"window_focused",lastFocused_},{"panel_visible",lastPanel_},{"page_attached",pageAttached_},
      {"screen_power",powerState(screenPower_.state())},
      {"reason",exitReason_.empty()?(unknown?"unknown":WIFSIGNALED(status)?"signal":"process_exit"):exitReason_}};
    exitPending_=true;
    persistExit();
  }
  void reap() {
    if(pid_<=0)return;
    int status=0;const auto result=waitpid(pid_,&status,WNOHANG);
    if(result==pid_||(result<0&&errno==ECHILD))reaped(status,result<0);
  }
#ifdef PENCHAT_CVI_PLATFORM
  std::uint64_t focusSequence_ = 0;
  int generation_ = 0;
#endif
  void stopLocked() {
    headRoute_.stop();ocrRouting_=observingPen_=false;ready_=false;
    if (fd_ >= 0) {
      shutdown(fd_, SHUT_RDWR);
      close(fd_);
      fd_ = -1;
    }
    reap();
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      // Permit bounded HTTP cancellation, scanner lease restoration and DB
      // final-save acknowledgement before falling back to SIGKILL.
      for (int i = 0; i < 500; ++i) {
        int status=0;
        auto result = waitpid(pid_, &status, WNOHANG);
        if (result == pid_ || result < 0) {
          reaped(status,result<0);
          break;
        }
        usleep(10000);
      }
      if (pid_ > 0) {
        kill(pid_, SIGKILL);
        int status=0;const auto result=waitpid(pid_,&status,0);
        reaped(status,result<0);
      }
    }
#ifdef PENCHAT_CVI_PLATFORM
    penchat_compositor::Composite::instance().stop();
#endif
  }

public:
  void OnCtor(JQFunctionInfo &info) override {
    ownId_=JQuick::getAppid(info.GetContext());ownData_=JQuick::getDataDir(info.GetContext());
    screenPower_.start([this](const penchat::ScreenPower::State &state){publishPower(state);});
  }
  void install(JQAsyncInfo &info) {
    try {
      const auto ticket=penchat::Json::parse(info[0].dump());
      const auto operation=ticket.at("operation").get<std::string>();
      const auto version=ticket.at("version").get<std::string>();
      const auto path=penchat::join(penchat::join(ownData_,".updates"),"package.amr");
      penchat::updates::Version parsed;
      if(ownId_.empty()||ticket.at("appid")!=ownId_||ticket.at("path")!=path||
         operation.size()!=36||operation.find_first_not_of("0123456789abcdefABCDEF-")!=std::string::npos||
         !penchat::updates::Version::parse(version,parsed)||ticket.at("sha256").get<std::string>().size()!=64)
        throw std::runtime_error("Invalid self-update identity");
      std::lock_guard<std::mutex> lock(mutex_);
      if(installations_.count(operation)||installations_.size()>=8)throw std::runtime_error("Installation already submitted");
      std::atomic<bool> cancel{false};
      penchat::updates::validatePackage(path,ownId_,version,ticket.at("sha256"),cancel);
      // Use exported Falcon methods, never object offsets, fixed addresses,
      // a forged app identity, system-policy modifications or a child command.
      using Instance=void *(*)();
      using Complete=std::function<void(int,const JQuick::PackageInfo &)>;
      using Install=void (*)(void *,const std::string &,Complete,int);
      auto instance=reinterpret_cast<Instance>(dlsym(RTLD_DEFAULT,"_ZN6JQuick17IotPackageManager8instanceEv"));
      auto submit=reinterpret_cast<Install>(dlsym(RTLD_DEFAULT,"_ZN6JQuick17IotPackageManager14installPackageERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt8functionIFviRKNS_11PackageInfoEEEi"));
      if(!instance||!submit)throw std::runtime_error("Falcon native package API is unavailable");
      auto manager=instance();if(!manager)throw std::runtime_error("Falcon package manager is unavailable");
      auto accepted=ticket;accepted["state"]="accepted";
      const auto record=penchat::join(penchat::join(ownData_,".updates"),"install.json");saveInstallTicket(record,accepted);
      installations_.insert(operation);
      submit(manager,path,[info,accepted,record](int code,const JQuick::PackageInfo &) {
        auto result=accepted;result["state"]=code==0?"installed":"failed";
        try{saveInstallTicket(record,result);}catch(...){}
        info.post(Bson::object{{"res",code}});
      },0);
    }catch(const std::exception &error){info.postError(error.what());}
  }
  ~Launcher() override {
    screenPower_.stop();
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
  }
  void start(JQAsyncInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!screenPower_.running())screenPower_.start([this](const penchat::ScreenPower::State &state){publishPower(state);});
    auto o = info[0];
    auto workspace = o["workspace"].string_value(),
         data = o["dataDir"].string_value();
    if (workspace.empty() || data.empty()) {
      info.postError("Missing application paths");
      return;
    }
    if(!lease_.claim(o["ownerGeneration"].int_value())) {info.postError("Stale runtime owner");return;}
    reap();
    if(pid_>0&&fd_>=0) {
      if(workspace!=runtimeWorkspace_||data!=runtimeData_){info.postError("A different runtime is already active");return;}
      info.post(Bson::object{{"ok",true},{"pid",int(pid_)},{"runtimeId",lease_.runtime()},{"reused",true},{"ready",ready_},{"screen",screenDescriptor_}});return;
    }
    stopLocked();runtimeWorkspace_=workspace;runtimeData_=data;
    lease_.created();exitReason_.clear();exitPending_=false;
    // Preserve the preceding stderr trace before either architecture truncates it.
    rename((data+"/lvgl-runtime.log").c_str(),(data+"/lvgl-runtime.previous.log").c_str());
    auto binary = workspace + "/assets/bin/penchat-lvgl";
    chmod(binary.c_str(), 0755);
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair)) {
      info.postError("socketpair failed");
      return;
    }
#ifdef PENCHAT_CVI_PLATFORM
    try {
      const auto model=penchat::fileText("/proc/device-tree/model",4096);
      if(ownId_!=PENCHAT_APP_ID || model.find("Cv1826")==std::string::npos || model.find("Plum")==std::string::npos)
        throw std::runtime_error("S6 compositor device/application identity mismatch");
      const auto screen=penchat::Screen::fromConfig(penchat::Json::parse(penchat::fileText("/etc/miniapp/resources/cfg.json",65536)),true);
      pinDisplayRelay();
      const auto slot=penchat_compositor::locate("libfalcon.so","_ZN7greenui7post_fbEPvRKNS_4RectEii",
          "11e46687d3c7301d3049d02d84144ee37e5a9994ca5ec1e9f5eaf278db92519b");
      penchat_compositor::Composite::instance().start(slot,screen.width,screen.height,[&](int socket,int shared) {
        pid_=penchat_compositor::spawnApplication(binary,socket,shared,pair[1],data,workspace);
        return pid_;
      });
      close(pair[1]); fd_=pair[0]; focusSequence_=0; generation_=0;
    } catch(const std::exception& error) {
      close(pair[0]);close(pair[1]);stopLocked();info.postError(error.what());return;
    }
#else
    const auto descriptor = std::to_string(pair[1]);
    const auto log = data + "/lvgl-runtime.log";
    rlimit limits{};
    getrlimit(RLIMIT_NOFILE, &limits);
    const int maxFd = std::min<rlim_t>(limits.rlim_cur, 65536);
    auto parent = getpid();
    auto child = fork();
    if (child == 0) {
      close(pair[0]);
      if (getppid() != parent)
        _exit(126);
      fcntl(pair[1], F_SETFD, 0);
      int out = open(log.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
      if (out >= 0) {
        dup2(out, STDERR_FILENO);
        dup2(out, STDOUT_FILENO);
        close(out);
      }
      for (int fd = 3; fd < maxFd; ++fd)
        if (fd != pair[1])
          close(fd);
      execl(binary.c_str(), binary.c_str(), "--bridge-fd", descriptor.c_str(),
            "--data-dir", data.c_str(), "--workspace", workspace.c_str(),
            nullptr);
      _exit(127);
    }
    close(pair[1]);
    if (child < 0) {
      close(pair[0]);
      info.postError("fork failed");
      return;
    }
    pid_ = child;
    fd_ = pair[0];
#endif
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
    info.post(Bson::object{{"ok", true}, {"pid", int(pid_)},{"runtimeId",lease_.runtime()}});
  }
  void reopen(JQFunctionInfo &info) {
    const auto id=JQuick::getAppid(info.GetContext());
    info.GetReturnValue().Set(true);
    JQuick::closeApp(id);
    JQuick::startApp(id,"",{});
  }
  void send(JQFunctionInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto input = info.toParamsHolder();
    auto payload = input[0].dump();
    const auto messageType=input[0]["type"].string_value();
    if(input[0]["generation"].int_value()!=lease_.generation()) {info.GetReturnValue().Set(false);return;}
    if(messageType=="exit")exitReason_="explicit_exit";
    if(messageType=="init")pageAttached_=true;
    if(messageType=="page.detach")pageAttached_=false;
    if(messageType=="window.focus")lastFocused_=input[0]["focused"].bool_value();
    if(messageType=="system.overlay.status")lastPanel_=input[0]["panel_visible"].bool_value();
    if(messageType=="page.detach"||(messageType=="visibility"&&!input[0]["visible"].bool_value())) {
      observingPen_=false;if(!ocrRouting_)headRoute_.stop();
    }
    bool ok = fd_ >= 0 && payload.size() <= 65536 &&
              ::send(fd_, payload.data(), payload.size(),
                     MSG_NOSIGNAL | MSG_DONTWAIT) == ssize_t(payload.size());
#ifdef PENCHAT_CVI_PLATFORM
    if(ok) {
      const auto type=input[0]["type"].string_value();
      if(type=="init") {generation_=input[0]["generation"].int_value();focusSequence_=0;}
      else if(type=="input.focus") {
        const auto message=penchat::Json::parse(payload,nullptr,false);
        if(!message.is_discarded()&&message.value("generation",-1)==generation_) {
          const auto sequence=message.value("sequence",std::uint64_t(0));
          if(sequence>focusSequence_) {
            focusSequence_=sequence;
            // Display lease is separate from native input focus. Falcon's
            // windows are already blended above LVGL, including partial covers.
            const bool inputActive=message.value("active",false);
            const auto power=screenPower_.state();
            penchat_compositor::Composite::instance().focus(message.value("displayActive",inputActive)&&
              (!power.available||(power.known&&power.awake)));
          }
        }
      } else if(type=="exit" || type=="page.detach" || (type=="visibility"&&!input[0]["visible"].bool_value()))
        penchat_compositor::Composite::instance().focus(false);
    }
#endif
    info.GetReturnValue().Set(ok);
  }
  void status(JQFunctionInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    reap();
    Bson::object state{{"runtimeId",lease_.runtime()},{"ownerGeneration",lease_.generation()},{"lastExit",lastExit_},{"alive", pid_ > 0 && fd_ >= 0}, {"pid", int(pid_)},{"ready",ready_},{"screen",screenDescriptor_},{"head_route_error",headRoute_.failure()},{"head_route_clients",int(headRoute_.clients())},{"observer_active",observingPen_}};
#ifdef PENCHAT_CVI_PLATFORM
    const auto compositor=penchat_compositor::Composite::instance().stats();
    state["display_frames"]=int(compositor.outputFrames);state["system_frames"]=int(compositor.systemFrames);
    state["display_focused"]=compositor.focused;state["display_restored"]=compositor.originalRestored;
    state["display_reason"]=compositor.reason;
#endif
    info.GetReturnValue().Set(state);
  }
  void closeOfficialKeyboard(JQFunctionInfo &info) {
    if(JQuick::getTopApp()!=ownId_){info.GetReturnValue().Set(false);return;}
    void *module=dlopen("libfalcon.so",RTLD_LAZY|RTLD_NOLOAD);
    void *scope=module?module:RTLD_DEFAULT;
    auto panel=reinterpret_cast<void **>(dlsym(scope,"_ZN7greenui9_im_panelE"));
    auto hide=reinterpret_cast<void (*)(void *)>(dlsym(scope,"_ZN2im13FalconIMPanel4hideEv"));
    if(panel&&*panel&&hide)hide(*panel);
    if(module)dlclose(module);
    info.GetReturnValue().Set(true);
  }
  void officialKeyboard(JQFunctionInfo &info) {
    // UI-only status. The focused Falcon input creates its own IM panel; no
    // pinyin API, foreign Storage, object layout or fixed address is used.
    void *module=dlopen("libfalcon.so",RTLD_LAZY|RTLD_NOLOAD);
    void *scope=module?module:RTLD_DEFAULT;
    using Height=int (*)(void *);using Showing=bool (*)(const void *);
    auto panel=reinterpret_cast<void **>(dlsym(scope,"_ZN7greenui9_im_panelE"));
    auto height=reinterpret_cast<Height>(dlsym(scope,"_ZN2im13FalconIMPanel14getPanelHeightEv"));
    auto showing=reinterpret_cast<Showing>(dlsym(scope,"_ZNK7greenui7NWindow9isShowingEv"));
    const bool supported=panel&&height&&showing;
    const bool visible=supported&&*panel&&showing(*panel);
    const int pixels=visible?height(*panel):0;
    if(module)dlclose(module);
    info.GetReturnValue().Set(Bson::object{{"supported",supported},{"visible",visible},{"height",pixels},{"foreground",JQuick::getTopApp()==ownId_&&!systemOverlay_.state().visible}});
  }
  void systemOverlay(JQFunctionInfo &info) {
    const auto state=systemOverlay_.state();
    info.GetReturnValue().Set(Bson::object{{"available",systemOverlay_.available()},{"supported",state.supported},{"visible",state.visible},
      {"power",powerState(screenPower_.state())}});
  }
  void updateHeadRoute(JQFunctionInfo &info,bool observing) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto p=info.toParamsHolder();
    if(!p[0].is_bool()){info.GetReturnValue().Set(false);return;}
    const auto power=screenPower_.state();
    const bool dark=power.available&&(!power.known||!power.awake);
    if(observing&&p[0].bool_value()&&!dark&&!systemOverlay_.state().visible&&JQuick::getTopApp()!=ownId_) {
      info.GetReturnValue().Set(false);return;
    }
    bool &flag=observing?observingPen_:ocrRouting_;
    const bool previous=flag;flag=p[0].bool_value();
    bool ok=true;
    if(ocrRouting_||observingPen_)ok=headRoute_.start();else headRoute_.stop();
    if(!ok)flag=previous;
    info.GetReturnValue().Set(ok);
  }
  void ocrRoute(JQFunctionInfo &info) {updateHeadRoute(info,false);}
  void observePen(JQFunctionInfo &info) {updateHeadRoute(info,true);}
  void ocrStroke(JQFunctionInfo &info) {
    const auto parameters = info.toParamsHolder();
    const bool down = parameters[0].bool_value();
    const int sequence = parameters[1].int_value();
    using Instance = void *(*)();
    using Stroke = void (*)(void *, unsigned, bool);
    // Falcon loads vendor business modules locally on some firmware. Resolve
    // within the already loaded module; never load a second OCR engine.
    void *module = dlopen("libyddal_business_core.so", RTLD_LAZY | RTLD_NOLOAD);
    if (!module) module = dlopen("/oem/YoudaoDictPen/output/libs/libyddal_business_core.so", RTLD_LAZY | RTLD_NOLOAD);
    void *scope = module ? module : RTLD_DEFAULT;
    const auto instance = reinterpret_cast<Instance>(dlsym(scope,"_ZN7YDALAPI11YOCRManager8InstanceEv"));
    const auto press = reinterpret_cast<Stroke>(dlsym(scope,"_ZN7YDALAPI11YOCRManager17onHeadButtonPressEjb"));
    const auto release = reinterpret_cast<Stroke>(dlsym(scope,"_ZN7YDALAPI11YOCRManager18onHeadButtonReleseEjb"));
    bool ok = parameters[0].is_bool() && sequence > 0 && instance && press && release;
    if (ok) {
      auto engine = instance(); ok = engine != nullptr;
      if (ok) (down ? press : release)(engine, unsigned(sequence), false);
    }
    if (module) dlclose(module);
    info.GetReturnValue().Set(ok);
  }
  void poll(JQFunctionInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    Bson::array messages;
    auto parameters=info.toParamsHolder();
    auto token=parameters[0];
    if(!lease_.owns(token["runtimeId"].int_value(),token["ownerGeneration"].int_value())) {
      info.GetReturnValue().Set(Bson(messages));return;
    }
    for (int i = 0; i < 64 && fd_ >= 0; ++i) {
      char b[65537];
      ssize_t n = recv(fd_, b, 65536, MSG_DONTWAIT);
      if (n > 0) {
        std::string error;
        auto j = Bson::parse(std::string(b, n), error);
        if (error.empty()) {
          if(j["type"].string_value()=="ready")ready_=true;
          if(j["type"].string_value()=="native.exit")noteExitReason(j["reason"].string_value());
          if(j["type"].string_value()=="fatal")noteExitReason("runtime_error");
          if(j["type"].string_value()=="booted")screenDescriptor_=j["screen"];
          messages.push_back(j);
        }
      } else if (n == 0) {
        headRoute_.stop();
        close(fd_);
        fd_ = -1;
        break;
      } else
        break;
    }
    reap();
    if(exitPending_) {messages.push_back(lastExit_);exitPending_=false;}
    info.GetReturnValue().Set(Bson(messages));
  }
  void stop(JQAsyncInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto o=info[0];
    if(!lease_.owns(o["runtimeId"].int_value(),o["ownerGeneration"].int_value())) {
      info.post(Bson::object{{"ok",false},{"stale",true}});return;
    }
    if(o["preservePublic"].bool_value()&&pid_>0)
      penchat::atomicText(ownData_+"/.rescue-preserve-public",penchat::Json{{"pid",int(pid_)},{"time_ms",penchat::nowMs()}}.dump());
    if(exitReason_.empty())exitReason_="requested_stop";stopLocked();screenPower_.stop();
    info.post(Bson::object{{"ok", true}});
  }
};
JSValue createRuntime(JSContext *ctx) {
  auto env = JQGlobalObjectEnv::Create(ctx,"__penchat_runtime_v1");
  env->setAppid(JQuick::getAppid(ctx));env->setDataDir(JQuick::getDataDir(ctx));
  env->setAppDir(JQuick::getWorkspace(ctx));
  auto tpl = JQFunctionTemplate::New(env.get(), "launcher");
  tpl->InstanceTemplate()->setObjectCreator([]() { return new Launcher; });
  tpl->SetProtoMethodPromise("start", &Launcher::start);
  tpl->SetProtoMethodPromise("install", &Launcher::install);
  tpl->SetProtoMethod("reopen", &Launcher::reopen);
  tpl->SetProtoMethod("send", &Launcher::send);
  tpl->SetProtoMethod("poll", &Launcher::poll);
  tpl->SetProtoMethod("ocrRoute", &Launcher::ocrRoute);
  tpl->SetProtoMethod("observePen", &Launcher::observePen);
  tpl->SetProtoMethod("ocrStroke", &Launcher::ocrStroke);
  tpl->SetProtoMethod("status", &Launcher::status);
  tpl->SetProtoMethod("systemOverlay", &Launcher::systemOverlay);
  tpl->SetProtoMethod("officialKeyboard", &Launcher::officialKeyboard);
  tpl->SetProtoMethod("closeOfficialKeyboard", &Launcher::closeOfficialKeyboard);
  tpl->SetProtoMethodPromise("stop", &Launcher::stop);
  return tpl->CallConstructor();
}
std::vector<std::string> exports = {"launcher"};
int init(JSContext *ctx, JSModuleDef *module) {
  auto env=JQModuleEnv::CreateModule(ctx,module,"penchatLauncher");
  env->setModuleExport("launcher",createRuntime(ctx));
  env->setModuleExportDone(JS_UNDEFINED, exports);
  return 0;
}
DEF_MODULE_LOAD_FUNC_EXPORT(penchatLauncher, init, exports)
} // namespace
extern "C" JQUICK_EXPORT void custom_init_jsapis() {
  registerCModuleLoader("penchatLauncher", &penchatLauncher_module_load);
}
extern "C" JQUICK_EXPORT int jsapi_check_unload() { return 0; }
extern "C" JQUICK_EXPORT JSValue penchat_create_runtime_v1(JSContext *ctx) { return createRuntime(ctx); }
