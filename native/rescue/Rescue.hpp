#pragma once
#include "common.hpp"
namespace penchat { namespace rescue {
struct Config {
  std::string root="https://cdn.ls-jl.cn/app/downgrade/";
  std::string endpoint,token;
  std::vector<std::string> nodes;
#ifndef PENCHAT_NATIVE_DEVICE
  Json fixture;
  int retryUnitMs=1000;
#endif
};
Json environment(const std::string &workspace,const std::string &modelPath="/proc/device-tree/model",
                 const std::string &screenPath="/etc/miniapp/resources/cfg.json");
void verifyElf(const std::string &,const std::string &platform);
Json validate(const std::string &,const Json &release,const std::string &platform,std::atomic<bool> &);
int schema(const std::string &data);
bool nativeAlive(const std::string &data);
int installationFlags(const std::string &version,const std::string &falconSha);
// May be called by the pinned package-manager callback after the page is gone.
void recordInstallResult(const std::string &data,const std::string &operation,const std::string &status,int code);
class Service {
  struct Impl;std::unique_ptr<Impl> p_;
public:
  Service(std::string workspace,std::string data,std::string publicRoot,Config);
  ~Service();
  Json bootstrap() const;
  Json request(const std::string &,const Json &args=Json::object());
  Json state() const;
  Json authorize(const Json &ticket);
  void installResult(const std::string &operation,const std::string &status,int code);
  Json verifyInstalled(const Json &packageInfo);
  void report(const Json &failure,bool retry=false);
  void cancel();
};
} }
