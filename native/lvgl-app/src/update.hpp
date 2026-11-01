#pragma once
#include "common.hpp"
#include <array>
namespace penchat {
namespace updates {
constexpr uint64_t PackageLimit=256ull*1024*1024, ExpandedLimit=512ull*1024*1024;
struct Version {
  std::array<uint32_t,3> parts{};
  static bool parse(const std::string &,Version &);
  bool operator<(const Version &v)const{return parts<v.parts;}
};
struct Link {std::string name;bool directory=false;};
std::vector<Link> directoryLinks(const std::string &html,const std::string &base);
std::string checksum(const std::string &text,const std::string &filename);
std::string fileHash(const std::string &,std::atomic<bool> &);
Json validatePackage(const std::string &,const std::string &appId,const std::string &version,
                     const std::string &hash,std::atomic<bool> &);
struct Config {
  std::string root="https://cdn.ls-jl.cn/penchat-rk/",appId=PENCHAT_APP_ID,current=PENCHAT_VERSION;
  std::vector<std::string> nodes;
  bool autoCheck=true;
  static Config build();
};
std::vector<std::string> resolve(const std::string &host,std::atomic<bool> &cancel);
struct Request {std::string url,node;bool head=false;size_t limit=1024*1024;};
struct Response {
  long status=0;int code=0;int64_t ttfb=0,length=-1;
  std::string body,etag,modified,range,encoding,error;
};
class Transport {
  struct Impl;std::unique_ptr<Impl> p_;
public:
  Transport(const Config &,std::atomic<bool> &);~Transport();
  std::vector<Response> fetch(const std::vector<Request> &);
  Json download(const Json &release,const std::string &path,std::function<void(Json)> progress);
};
}
class Updater {
  struct Impl;std::unique_ptr<Impl> p_;
public:
  Updater(Events &,const std::string &data,updates::Config config=updates::Config::build());
  ~Updater();
  void request(const std::string &,Json args=Json::object());
  void cancel(bool decline=false);
  Json state() const;
  Json recovery() const;
  std::string note(size_t index) const;
  void stop();
};
}
