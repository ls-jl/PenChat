#include "jquick_config.h"
#include "JQuickContext.h"
#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "Rescue.hpp"
#include "update.hpp"
#include "feedback_private.hpp"
#include "../launcher/src/SystemOverlay.hpp"
#include "../launcher/src/ScreenPower.hpp"
#include <dlfcn.h>
#include <set>
namespace JQuick {class PackageInfo;}
using namespace JQUTIL_NS;
namespace {
Bson bson(const penchat::Json &value){std::string error;return Bson::parse(value.dump(),error);}
void pinBridge() {
  static void *pinned=nullptr;if(pinned)return;Dl_info info{};
  if(!dladdr(reinterpret_cast<void*>(&pinBridge),&info)||!info.dli_fname)throw std::runtime_error("无法识别降级组件");
  pinned=dlopen(info.dli_fname,RTLD_NOW|RTLD_NODELETE);if(!pinned)throw std::runtime_error("无法保留降级安装回调");
}
class Rescue : public JQBaseObject {
  std::unique_ptr<penchat::rescue::Service> service_;
  std::string workspace_,data_,id_;
  void *runtime_=nullptr;
  JSValue launcher_=JS_UNDEFINED;
  JSContext *context_=nullptr;
public:
  void OnCtor(JQFunctionInfo &info) override {
    pinBridge();
    context_=info.GetContext();workspace_=JQuick::getWorkspace(context_);data_=JQuick::getDataDir(context_);id_=JQuick::getAppid(context_);
    if(id_!=PENCHAT_APP_ID)throw std::runtime_error("Unexpected rescue application identity");
    penchat::rescue::Config config;config.endpoint=penchat::FeedbackBuild_endpoint;config.token=penchat::FeedbackBuild_token;
    config.nodes=penchat::Json::parse(penchat::FeedbackBuild_nodes).get<std::vector<std::string>>();
    service_.reset(new penchat::rescue::Service(workspace_,data_,"/userdisk/Favorite/PenChat",std::move(config)));
  }
  void OnGCMark(JSRuntime *rt,JSValueConst,JS_MarkFunc *mark) override {JS_MarkValue(rt,launcher_,mark);}
  void OnGCCollect() override {JS_FreeValue(context_,launcher_);launcher_=JS_UNDEFINED;}
  ~Rescue() override {service_.reset();}
  void bootstrap(JQFunctionInfo &info){
    auto state=service_->bootstrap();state["foreground"]=JQuick::getTopApp()==id_;
    const auto panel=penchat::SystemOverlay::realSystem().state();state["panel_visible"]=panel.supported&&panel.visible;
    const auto power=penchat::ScreenPower::read("/sys/class/backlight");
    state["screen_interactive"]=!power.available||(power.known&&power.awake);
    info.GetReturnValue().Set(bson(state));
  }
  void loadRuntime(JQFunctionInfo &info) {
    try {
      if(!JS_IsUndefined(launcher_)){info.GetReturnValue().Set(JS_DupValue(context_,launcher_));return;}
      const auto env=service_->bootstrap();if(!env.value("compatible",false))throw std::runtime_error("当前安装包与设备不匹配，或原生主程序不可用，请使用降级");
      const auto payload=env.at("payload");const auto name=payload.value("runtime",std::string());
      if(name.compare(0,15,"assets/runtime/")!=0||name.find("..")!=std::string::npos||name.find('\\')!=std::string::npos)throw std::runtime_error("Invalid runtime path");
      const auto path=penchat::join(workspace_,name);std::atomic<bool> cancel{false};
      if(penchat::updates::fileHash(path,cancel)!=payload.value("runtime_sha256",std::string()))throw std::runtime_error("原生运行时校验失败");
      penchat::rescue::verifyElf(path,env.at("platform"));
      runtime_=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);
      if(!runtime_)throw std::runtime_error("原生运行时依赖无法加载");
      auto create=reinterpret_cast<JSValue(*)(JSContext*)>(dlsym(runtime_,"penchat_create_runtime_v1"));
      if(!create)throw std::runtime_error("原生运行时工厂接口不匹配");
      launcher_=create(context_);if(JS_IsException(launcher_)){launcher_=JS_UNDEFINED;JS_FreeValue(context_,JS_GetException(context_));throw std::runtime_error("原生运行时创建失败");}
      info.GetReturnValue().Set(JS_DupValue(context_,launcher_));
    }catch(const std::exception &e){info.GetReturnValue().ThrowTypeError(e.what());}
  }
  void state(JQFunctionInfo &info){info.GetReturnValue().Set(bson(service_->state()));}
  void request(JQFunctionInfo &info) {
    try {const auto p=info.toParamsHolder();const auto args=penchat::Json::parse(p[1].dump(),nullptr,false);info.GetReturnValue().Set(bson(service_->request(p[0].string_value(),args.is_object()?args:penchat::Json::object())));}
    catch(const std::exception &e){info.GetReturnValue().ThrowTypeError(e.what());}
  }
  void cancel(JQFunctionInfo &info){service_->cancel();info.GetReturnValue().Set(true);}
  void report(JQFunctionInfo &info) {
    const auto p=info.toParamsHolder();auto args=penchat::Json::parse(p[0].dump(),nullptr,false);if(!args.is_object())args=penchat::Json::object();
    service_->report(args,args.value("retry",false));info.GetReturnValue().Set(true);
  }
  void verifyInstalled(JQAsyncInfo &info) {
    try{info.post(bson(service_->verifyInstalled(penchat::Json::parse(info[0].dump()))));}
    catch(const std::exception &e){info.postError(e.what());}
  }
  void install(JQAsyncInfo &info) {
    std::string operation;bool submitted=false,authorized=false;
    try {
      const auto input=penchat::Json::parse(info[0].dump());operation=input.value("operation",std::string());
      using Instance=void*(*)();using Complete=std::function<void(int,const JQuick::PackageInfo&)>;
      using Install=void(*)(void*,const std::string&,Complete,int);
      auto instance=reinterpret_cast<Instance>(dlsym(RTLD_DEFAULT,"_ZN6JQuick17IotPackageManager8instanceEv"));
      auto install=reinterpret_cast<Install>(dlsym(RTLD_DEFAULT,"_ZN6JQuick17IotPackageManager14installPackageERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt8functionIFviRKNS_11PackageInfoEEEi"));
      if(!instance||!install||!instance())throw std::runtime_error("当前固件安装接口不可用");
      const auto ticket=service_->authorize(input);
      authorized=true;
      std::string firmware;
      penchat::updates::Version target,current;
      penchat::updates::Version::parse(ticket.at("version"),target);penchat::updates::Version::parse(PENCHAT_VERSION,current);
      if(target<current){
        Dl_info library{};if(!dladdr(reinterpret_cast<void*>(install),&library)||!library.dli_fname)throw std::runtime_error("无法核验 Falcon 安装接口");
        std::atomic<bool> cancelled{false};firmware=penchat::updates::fileHash(library.dli_fname,cancelled);
      }
      const int flags=penchat::rescue::installationFlags(ticket.at("version"),firmware);
      const auto data=data_;submitted=true;
      install(instance(),ticket.at("path"),[info,data,operation](int code,const JQuick::PackageInfo&) {
        // The package manager can destroy this module during self-install. Never
        // capture Service/Rescue pointers; only own the durable operation record.
        try {
          penchat::rescue::recordInstallResult(data,operation,code==0?"unknown":"failed",code);
        }catch(...){}
        info.post(Bson::object{{"res",code},{"operation",operation}});
      },flags);
    }catch(const std::exception &e){
      if(authorized)try{service_->installResult(operation,submitted?"unknown":"failed",-1);}catch(...){}
      info.postError(e.what());
    }
  }
  void reopen(JQFunctionInfo &info){info.GetReturnValue().Set(true);JQuick::closeApp(id_);JQuick::startApp(id_,"",{});}
};
std::vector<std::string> exports={"rescue"};
int init(JSContext *ctx,JSModuleDef *module) {
  auto env=JQModuleEnv::CreateModule(ctx,module,"penchatRescue");auto tpl=JQFunctionTemplate::New(env.get(),"rescue");
  tpl->InstanceTemplate()->setObjectCreator([]{return new Rescue;});
  tpl->SetProtoMethod("bootstrap",&Rescue::bootstrap);tpl->SetProtoMethod("loadRuntime",&Rescue::loadRuntime);
  tpl->SetProtoMethod("state",&Rescue::state);tpl->SetProtoMethod("request",&Rescue::request);
  tpl->SetProtoMethod("cancel",&Rescue::cancel);tpl->SetProtoMethod("report",&Rescue::report);
  tpl->SetProtoMethodPromise("install",&Rescue::install);tpl->SetProtoMethod("reopen",&Rescue::reopen);
  tpl->SetProtoMethodPromise("verifyInstalled",&Rescue::verifyInstalled);
  env->setModuleExport("rescue",tpl->CallConstructor());env->setModuleExportDone(JS_UNDEFINED,exports);return 0;
}
DEF_MODULE_LOAD_FUNC_EXPORT(penchatRescue,init,exports)
}
extern "C" JQUICK_EXPORT void custom_init_jsapis(){registerCModuleLoader("penchatRescue",&penchatRescue_module_load);}
extern "C" JQUICK_EXPORT int jsapi_check_unload(){return 0;}
