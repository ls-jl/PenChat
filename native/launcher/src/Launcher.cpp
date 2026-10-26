#include "jquick_config.h"
#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <signal.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace JQUTIL_NS;
namespace {
class Launcher : public JQBaseObject {
  std::mutex mutex_;
  int fd_ = -1;
  pid_t pid_ = -1;
  void stopLocked() {
    if (fd_ >= 0) {
      shutdown(fd_, SHUT_RDWR);
      close(fd_);
      fd_ = -1;
    }
    if (pid_ > 0) {
      int status = 0;
      auto result = waitpid(pid_, &status, WNOHANG);
      if (result == pid_ || (result < 0 && errno == ECHILD))
        pid_ = -1;
    }
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      // Permit bounded HTTP cancellation, scanner lease restoration and DB
      // final-save acknowledgement before falling back to SIGKILL.
      for (int i = 0; i < 500; ++i) {
        int status;
        auto result = waitpid(pid_, &status, WNOHANG);
        if (result == pid_ || result < 0) {
          pid_ = -1;
          break;
        }
        usleep(10000);
      }
      if (pid_ > 0) {
        kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
        pid_ = -1;
      }
    }
  }

public:
  ~Launcher() override {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
  }
  void start(JQAsyncInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
    auto o = info[0];
    auto workspace = o["workspace"].string_value(),
         data = o["dataDir"].string_value();
    if (workspace.empty() || data.empty()) {
      info.postError("Missing application paths");
      return;
    }
    auto binary = workspace + "/assets/bin/penchat-lvgl";
    chmod(binary.c_str(), 0755);
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair)) {
      info.postError("socketpair failed");
      return;
    }
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
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
    info.post(Bson::object{{"ok", true}, {"pid", int(child)}});
  }
  void send(JQFunctionInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto input = info.toParamsHolder();
    auto payload = input[0].dump();
    bool ok = fd_ >= 0 && payload.size() <= 65536 &&
              ::send(fd_, payload.data(), payload.size(),
                     MSG_NOSIGNAL | MSG_DONTWAIT) == ssize_t(payload.size());
    info.GetReturnValue().Set(ok);
  }
  void status(JQFunctionInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pid_ > 0) {
      int code = 0;
      auto result = waitpid(pid_, &code, WNOHANG);
      if (result == pid_ || (result < 0 && errno == ECHILD)) {
        pid_ = -1;
        if (fd_ >= 0) {
          close(fd_);
          fd_ = -1;
        }
      }
    }
    info.GetReturnValue().Set(
        Bson::object{{"alive", pid_ > 0 && fd_ >= 0}, {"pid", int(pid_)}});
  }
  void poll(JQFunctionInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    Bson::array messages;
    for (int i = 0; i < 64 && fd_ >= 0; ++i) {
      char b[65537];
      ssize_t n = recv(fd_, b, 65536, MSG_DONTWAIT);
      if (n > 0) {
        std::string error;
        auto j = Bson::parse(std::string(b, n), error);
        if (error.empty())
          messages.push_back(j);
      } else if (n == 0) {
        close(fd_);
        fd_ = -1;
        break;
      } else
        break;
    }
    if (pid_ > 0) {
      int status = 0;
      auto result = waitpid(pid_, &status, WNOHANG);
      if (result == pid_ || (result < 0 && errno == ECHILD)) {
        pid_ = -1;
        messages.push_back(Bson::object{
            {"v", 1},
            {"type", "exit"},
            {"status", WIFEXITED(status) ? WEXITSTATUS(status)
                                         : 128 + WTERMSIG(status)}});
      }
    }
    info.GetReturnValue().Set(Bson(messages));
  }
  void stop(JQAsyncInfo &info) {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
    info.post(Bson::object{{"ok", true}});
  }
};
std::vector<std::string> exports = {"launcher"};
int init(JSContext *ctx, JSModuleDef *module) {
  auto env = JQModuleEnv::CreateModule(ctx, module, "penchatLauncher");
  auto tpl = JQFunctionTemplate::New(env.get(), "launcher");
  tpl->InstanceTemplate()->setObjectCreator([]() { return new Launcher; });
  tpl->SetProtoMethodPromise("start", &Launcher::start);
  tpl->SetProtoMethod("send", &Launcher::send);
  tpl->SetProtoMethod("poll", &Launcher::poll);
  tpl->SetProtoMethod("status", &Launcher::status);
  tpl->SetProtoMethodPromise("stop", &Launcher::stop);
  env->setModuleExport("launcher", tpl->CallConstructor());
  env->setModuleExportDone(JS_UNDEFINED, exports);
  return 0;
}
DEF_MODULE_LOAD_FUNC_EXPORT(penchatLauncher, init, exports)
} // namespace
extern "C" JQUICK_EXPORT void custom_init_jsapis() {
  registerCModuleLoader("penchatLauncher", &penchatLauncher_module_load);
}
extern "C" JQUICK_EXPORT int jsapi_check_unload() { return 0; }
