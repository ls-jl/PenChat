#include "feedback.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <set>
#include <sys/ioctl.h>
#include <unistd.h>

namespace penchat {
struct FeedbackKeys::Impl {
  std::function<void(Json)> complete;
  std::atomic<bool> cancel{false};
  std::thread worker;
  explicit Impl(std::function<void(Json)> callback) : complete(std::move(callback)) {}
  void run(const std::string &step, uint64_t generation) {
    Json result = {{"step",step},{"generation",generation},{"status","timeout"},
                   {"events",Json::array()},{"devices",Json::array()}};
    std::vector<pollfd> inputs;
    auto closeAll = [&] { for (auto &fd : inputs) close(fd.fd); inputs.clear(); };
    try {
      std::vector<std::string> paths;
      if (auto dir = opendir("/dev/input")) {
        while (auto item = readdir(dir)) {
          const std::string name = item->d_name;
          if (name.find("event") == 0 && name.size() > 5 &&
              name.find_first_not_of("0123456789",5) == std::string::npos && paths.size() < 128)
            paths.push_back("/dev/input/" + name);
        }
        closedir(dir);
      }
      std::sort(paths.begin(),paths.end());
      for (const auto &path : paths) {
        const int fd = open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd < 0) { result["devices"].push_back({{"path",path},{"errno",errno},{"status","unavailable"}}); continue; }
        unsigned char keys[(KEY_MAX+8)/8]{};
        unsigned char properties[16]{};
        ioctl(fd,EVIOCGPROP(sizeof(properties)),properties);
        // Touchscreen contact buttons and virtual/full keyboards are not the
        // physical-button test. Never listen to text entry or touch coordinates.
        const bool touch = properties[INPUT_PROP_DIRECT/8] & (1u << (INPUT_PROP_DIRECT%8));
        const bool readable = ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(keys)),keys) >= 0;
        unsigned count = 0;
        for (auto value : keys) for (unsigned bit=0;bit<8;++bit) count += bool(value & (1u<<bit));
        if (touch || !readable || !count || count > 64) {
          result["devices"].push_back({{"path",path},{"status",touch?"excluded_touch":count>64?"excluded_keyboard":"no_key_capability"}});
          close(fd);continue;
        }
        char name[128]{}; ioctl(fd,EVIOCGNAME(sizeof(name)),name);
        std::string title;
        for (unsigned i=0;i<sizeof(name)&&name[i];++i) if (name[i]>=32&&name[i]<127) title += name[i];
        Json supported=Json::array();
        for(unsigned code=0;code<=KEY_MAX;++code)if(keys[code/8]&(1u<<(code%8)))supported.push_back(code);
        result["devices"].push_back({{"path",path},{"name",title},{"key_count",count},{"key_codes",supported},{"status","observing"}});
        // Discard events queued before the user began this step.
        input_event discard[32];
        for (unsigned n=0;n<8 && read(fd,discard,sizeof(discard))>0;++n) {}
        inputs.push_back({fd,POLLIN,0});
      }
      if(!inputs.empty()) {
        auto armed=result;armed["status"]="listening";
        try{complete(std::move(armed));}catch(...){}
      }
      const auto start = ticks();
      int64_t completedAt = 0;
      std::set<std::pair<int,int>> downs, observed;
      bool lost = false;
      if (inputs.empty()) result["status"] = "unavailable";
      while (!inputs.empty() && !cancel && ticks()-start < 20000) {
        if (poll(inputs.data(),inputs.size(),50) < 0) { if (errno==EINTR) continue; break; }
        for (auto &input : inputs) {
          if (input.revents & (POLLHUP|POLLERR|POLLNVAL)) { lost=true; continue; }
          if (!(input.revents & POLLIN)) continue;
          input_event packet[32]; auto bytes=read(input.fd,packet,sizeof(packet));
          if (bytes<=0) continue;
          for (size_t i=0;i<size_t(bytes)/sizeof(input_event);++i) {
            const auto &event=packet[i];
            if (event.type==EV_SYN && event.code==SYN_DROPPED) { lost=true; continue; }
            if (event.type!=EV_KEY || event.code==BTN_TOUCH || event.code==BTN_TOOL_PEN ||
                event.value<0 || event.value>2) continue;
            char link[64],path[256]{}; snprintf(link,sizeof(link),"/proc/self/fd/%d",input.fd);
            auto length=readlink(link,path,sizeof(path)-1);
            if (length<0) continue;
            const auto identity=std::make_pair(input.fd,int(event.code));
            if (event.value==1) { downs.insert(identity); observed.insert(identity); }
            else if (event.value==0) downs.erase(identity);
            if (result["events"].size()>=128) { lost=true; break; }
            result["events"].push_back({{"device",std::string(path,size_t(length))},
              {"type",event.type},{"code",event.code},{"value",event.value},{"relative_ms",ticks()-start}});
            if (event.value==1) {
              auto partial=result; partial["status"]="pressed";
              try{complete(std::move(partial));}catch(...){} // Persist Home down before a possible unload.
            }
            if (!observed.empty()&&downs.empty()) completedAt=ticks();
          }
        }
        if (lost || (completedAt && ticks()-completedAt>=120)) break;
      }
      if(!downs.empty())result["release_missing"]=true;
      if (cancel) result["status"]="cancelled";
      else if (lost) result["status"]="events_lost";
      else if (observed.size()>1) result["status"]="ambiguous";
      else if (observed.size()==1) result["status"]=downs.empty()?"complete":"release_missing";
    } catch (...) { result["status"]="unavailable"; }
    closeAll();
    try{complete(std::move(result));}catch(...){}
  }
};
FeedbackKeys::FeedbackKeys(std::function<void(Json)> callback) : p_(new Impl(std::move(callback))) {}
FeedbackKeys::~FeedbackKeys() { cancel(); join(); }
void FeedbackKeys::cancel() { p_->cancel=true; }
void FeedbackKeys::join() { if (p_->worker.joinable()) p_->worker.join(); }
void FeedbackKeys::start(const std::string &step,uint64_t generation) {
  cancel(); join(); p_->cancel=false;
  p_->worker=std::thread([this,step,generation] { p_->run(step,generation); });
}
} // namespace penchat
