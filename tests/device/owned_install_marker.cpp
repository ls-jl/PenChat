// Test-only synchronous lifecycle marker for a newly generated owned AppID.
// Never packaged in PenChat. No network, DB, input or background callbacks.
#include "jquick_config.h"
#include "JQuickContext.h"
#include "jsmodules/JSCModuleExtension.h"
#include <cerrno>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#ifndef PENCHAT_SELFTEST_APPID
#error Owned AppID required
#endif
#ifndef PENCHAT_MAIN_APPID
#error Main application identity required
#endif
#ifndef PENCHAT_SELFTEST_OPERATION
#error Owned operation required
#endif
#ifndef PENCHAT_SELFTEST_MODULE
#error Owned module name required
#endif
namespace {
struct Fd {
  int value;
  explicit Fd(int fd) : value(fd) { if(fd<0)throw std::runtime_error("marker directory unavailable"); }
  ~Fd() { close(value); }
};
int child(int parent,const std::string &name) {
  if(mkdirat(parent,name.c_str(),0700)&&errno!=EEXIST)
    throw std::runtime_error("marker mkdir failed");
  fsync(parent);
  return openat(parent,name.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
}
JSValue mark(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv) {
  try {
    if(argc!=2||JQuick::getAppid(ctx)!=PENCHAT_SELFTEST_APPID||
       std::string(PENCHAT_SELFTEST_APPID)==PENCHAT_MAIN_APPID)
      throw std::runtime_error("marker owner mismatch");
    const char *v=JS_ToCString(ctx,argv[0]);
    if(!v)return JS_EXCEPTION;
    const std::string version(v);JS_FreeCString(ctx,v);
    const char *e=JS_ToCString(ctx,argv[1]);
    if(!e)return JS_EXCEPTION;
    const std::string event(e);JS_FreeCString(ctx,e);
    if((version!="0.0.1"&&version!="0.0.2"&&version!="0.0.3")||
       (event!="mounted"&&event!="unloaded"&&event!="finish-requested"))
      throw std::runtime_error("invalid marker event");
    const std::string directory=JQuick::getDataDir(ctx);
    Fd root(open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
    Fd operation(child(root.value,std::string("selftest-")+PENCHAT_SELFTEST_OPERATION));
    Fd release(child(operation.value,version));Fd leaf(child(release.value,event));
    fsync(leaf.value);return JS_NewBool(ctx,true);
  } catch(const std::exception &error) {return JS_ThrowInternalError(ctx,"%s",error.what());}
}
JSValue runtime(JSContext *ctx,JSValueConst,int,JSValueConst *) {
  if(JQuick::getAppid(ctx)!=PENCHAT_SELFTEST_APPID||
     std::string(PENCHAT_SELFTEST_APPID)==PENCHAT_MAIN_APPID)
    return JS_ThrowTypeError(ctx,"runtime marker owner mismatch");
  const auto path=JQuick::getWorkspace(ctx)+"/assets/runtime/libpenchat_selftest_runtime.so";
  auto library=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);
  if(!library)return JS_ThrowTypeError(ctx,"owned runtime dependency load failed");
  auto create=reinterpret_cast<JSValue(*)(JSContext*)>(dlsym(library,"penchat_create_runtime_v1"));
  if(!create)return JS_ThrowTypeError(ctx,"owned runtime factory missing");
  return create(ctx);
}
int init(JSContext *ctx,JSModuleDef *module) {
  if(JS_SetModuleExport(ctx,module,"mark",JS_NewCFunction(ctx,mark,"mark",2))<0)return -1;
  return JS_SetModuleExport(ctx,module,"loadRuntime",JS_NewCFunction(ctx,runtime,"loadRuntime",0));
}
JSModuleDef *load(JSContext *ctx,const char *name) {
  auto module=JS_NewCModule(ctx,name,init);
  if(module){JS_AddModuleExport(ctx,module,"mark");JS_AddModuleExport(ctx,module,"loadRuntime");}
  return module;
}
}
extern "C" JQUICK_EXPORT void custom_init_jsapis() {registerCModuleLoader(PENCHAT_SELFTEST_MODULE,load);}
extern "C" JQUICK_EXPORT int jsapi_check_unload() {return 0;}
