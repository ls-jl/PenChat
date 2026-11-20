#include "feedback.hpp"
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <set>
#include <sys/ioctl.h>
#include <unistd.h>

namespace penchat {
namespace {
constexpr int64_t CaptureLimitMs = 30000;
struct KeyLease {
  int fd;
  bool grabbed = false;
  explicit KeyLease(int descriptor) : fd(descriptor) {}
  ~KeyLease() {
    if (grabbed) ioctl(fd, EVIOCGRAB, 0);
    close(fd); // Closing also releases a grab if the explicit ioctl failed.
  }
};
bool bit(const unsigned char *bits, unsigned value) {
  return bits[value / 8] & (1u << (value % 8));
}
std::vector<std::string> inputPaths() {
#if !defined(PENCHAT_NATIVE_DEVICE) || defined(PENCHAT_KEY_CAPTURE_TEST)
  // Tests use only explicitly created private uinput nodes; never host buttons.
  if (const auto file = getenv("PENCHAT_TEST_FEEDBACK_KEYS")) {
    const auto config = Json::parse(fileText(file, 8192));
    return config.at("paths").get<std::vector<std::string>>();
  }
#endif
  std::vector<std::string> paths;
  if (auto dir = opendir("/dev/input")) {
    while (auto item = readdir(dir)) {
      const std::string name = item->d_name;
      if (name.find("event") == 0 && name.size() > 5 &&
          name.find_first_not_of("0123456789", 5) == std::string::npos && paths.size() < 128)
        paths.push_back("/dev/input/" + name);
    }
    closedir(dir);
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}
int64_t captureLimit() {
#if !defined(PENCHAT_NATIVE_DEVICE) || defined(PENCHAT_KEY_CAPTURE_TEST)
  if (const auto file = getenv("PENCHAT_TEST_FEEDBACK_KEYS")) {
    const auto config = Json::parse(fileText(file, 8192));
    return std::max(50, std::min(30000, config.value("timeout_ms", 30000)));
  }
#endif
  return CaptureLimitMs;
}
}
struct FeedbackKeys::Impl {
  std::function<void(Json)> complete;
  std::atomic<bool> cancel{false};
  std::thread worker;
  explicit Impl(std::function<void(Json)> callback) : complete(std::move(callback)) {}
  void emit(Json value) { try { complete(std::move(value)); } catch (...) {} }
  void run(const std::string &step, uint64_t generation) {
    Json result = {{"step",step},{"generation",generation},{"status","unavailable"},
                   {"events",Json::array()},{"devices",Json::array()},
                   {"blocking",false},{"limit_ms",CaptureLimitMs}};
    std::vector<std::unique_ptr<KeyLease>> leases;
    std::vector<pollfd> inputs;
    std::vector<std::string> paths;
    const auto start = ticks();
    try {
      const auto limit = captureLimit();
      bool failed = false;
      for (const auto &path : inputPaths()) {
        if (cancel || ticks()-start >= limit) break;
        const int fd = open(path.c_str(), O_RDONLY|O_NONBLOCK|O_CLOEXEC);
        if (fd < 0) {
          result["devices"].push_back({{"path",path},{"errno",errno},{"status","unavailable"}});
          failed = true; break; // Cannot guarantee suppression of an unreadable button.
        }
        std::unique_ptr<KeyLease> lease(new KeyLease(fd));
        unsigned char keys[(KEY_MAX+8)/8]{}, types[(EV_MAX+8)/8]{}, properties[16]{};
        ioctl(fd, EVIOCGPROP(sizeof(properties)), properties);
        const bool readable = ioctl(fd, EVIOCGBIT(EV_KEY,sizeof(keys)), keys) >= 0 &&
                              ioctl(fd, EVIOCGBIT(0,sizeof(types)), types) >= 0;
        const bool touch = bit(properties, INPUT_PROP_DIRECT) || bit(keys, BTN_TOUCH) ||
                           bit(keys, BTN_TOOL_PEN) || bit(keys, BTN_TOOL_FINGER);
        const bool pointer = bit(types, EV_ABS) || bit(types, EV_REL);
        unsigned count = 0;
        for (auto value : keys) for (unsigned n=0;n<8;++n) count += bool(value & (1u<<n));
        const bool keyboard=count>64||(bit(keys,KEY_A)&&bit(keys,KEY_Z)&&bit(keys,KEY_SPACE));
        if (touch || pointer || !readable || !count || keyboard) {
          result["devices"].push_back({{"path",path},{"status",touch?"excluded_touch":pointer?"excluded_pointer":keyboard?"excluded_keyboard":"no_key_capability"}});
          if (!readable) { failed=true; break; }
          continue;
        }
        unsigned char held[(KEY_MAX+8)/8]{};
        if (ioctl(fd, EVIOCGKEY(sizeof(held)), held) < 0) { failed=true; break; }
        bool down=false;for(auto value:held)down=down||value;
        if (down) {
          result["devices"].push_back({{"path",path},{"status","key_held"}});
          result["status"]="key_held";failed=true;break;
        }
        // Only discard unprotected setup events. Never drain after taking the
        // grab: a quick valid press during the rest of discovery must survive.
        input_event discard[32];for(unsigned n=0;n<8&&read(fd,discard,sizeof(discard))>0;++n){}
        if (ioctl(fd, EVIOCGRAB, 1) < 0) {
          result["devices"].push_back({{"path",path},{"errno",errno},{"status","grab_failed"}});
          result["status"]="grab_failed";failed=true;break;
        }
        lease->grabbed=true;
        memset(held,0,sizeof(held));
        if(ioctl(fd,EVIOCGKEY(sizeof(held)),held)<0){failed=true;break;}
        down=false;for(auto value:held)down=down||value;
        if(down){result["devices"].push_back({{"path",path},{"status","key_held"}});result["status"]="key_held";failed=true;break;}
        char name[128]{};ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        std::string title;
        for(unsigned i=0;i<sizeof(name)&&name[i];++i)if(name[i]>=32&&name[i]<127)title+=name[i];
        Json supported=Json::array();for(unsigned code=0;code<=KEY_MAX;++code)if(bit(keys,code))supported.push_back(code);
        result["devices"].push_back({{"path",path},{"name",title},{"key_count",count},{"key_codes",supported},{"status","capturing"}});
        inputs.push_back({fd,POLLIN,0});paths.push_back(path);leases.push_back(std::move(lease));
      }
      std::set<std::pair<int,int>> downs, observed;
      bool lost=false;int64_t completedAt=0;
      if (!failed && !inputs.empty() && !cancel && ticks()-start < limit) {
        result["status"]="listening";result["blocking"]=true;emit(result);
        while(!cancel && ticks()-start < limit) {
          const auto remaining=limit-(ticks()-start);
          if(remaining<=0)break; // Never pass a negative (infinite) timeout to poll.
          if(poll(inputs.data(),inputs.size(),int(std::min<int64_t>(50,remaining)))<0){if(errno==EINTR)continue;lost=true;break;}
          for(size_t n=0;n<inputs.size();++n) {
            auto &input=inputs[n];
            if(input.revents&(POLLHUP|POLLERR|POLLNVAL)){lost=true;continue;}
            if(!(input.revents&POLLIN))continue;
            input_event packet[32];const auto bytes=read(input.fd,packet,sizeof(packet));
            if(bytes<0&&(errno==EAGAIN||errno==EINTR))continue;
            if(bytes<=0||size_t(bytes)%sizeof(input_event)){lost=true;break;}
            for(size_t i=0;i<size_t(bytes)/sizeof(input_event);++i) {
              const auto &event=packet[i];
              if(event.type==EV_SYN&&event.code==SYN_DROPPED){lost=true;continue;}
              if(event.type!=EV_KEY||event.value<0||event.value>2)continue;
              const auto identity=std::make_pair(input.fd,int(event.code));
              if(event.value==1){downs.insert(identity);observed.insert(identity);completedAt=0;}
              else if(event.value==0)downs.erase(identity);
              if(result["events"].size()>=128){lost=true;break;}
              result["events"].push_back({{"device",paths[n]},{"type",event.type},{"code",event.code},
                  {"value",event.value},{"relative_ms",ticks()-start}});
              if(event.value==1){auto partial=result;partial["status"]="pressed";emit(std::move(partial));}
              if(!observed.empty()&&downs.empty())completedAt=ticks();
            }
          }
          // Briefly drain duplicate reports from the same physical press before
          // declaring it complete; then release every grab before the callback.
          if(lost||(completedAt&&ticks()-completedAt>=120))break;
        }
        if(!downs.empty())result["release_missing"]=true;
        if(cancel)result["status"]="cancelled";
        else if(lost)result["status"]="events_lost";
        else if(ticks()-start>=limit)result["status"]="timeout";
        else if(observed.size()>1)result["status"]="ambiguous";
        else if(observed.size()==1&&downs.empty())result["status"]="complete";
      } else if(cancel)result["status"]="cancelled";
      else if(!failed&&ticks()-start>=limit)result["status"]="timeout";
    }catch(...){result["status"]="unavailable";}
    leases.clear(); // EVIOCGRAB(0) + close, including partial setup and errors.
    for(auto &device:result["devices"])if(device.value("status",std::string())=="capturing")device["status"]="released";
    result["blocking"]=false;result["elapsed_ms"]=ticks()-start;
    emit(std::move(result));
  }
};
FeedbackKeys::FeedbackKeys(std::function<void(Json)> callback):p_(new Impl(std::move(callback))){}
FeedbackKeys::~FeedbackKeys(){cancel();join();}
void FeedbackKeys::cancel(){p_->cancel=true;}
void FeedbackKeys::join(){if(p_->worker.joinable())p_->worker.join();}
void FeedbackKeys::start(const std::string &step,uint64_t generation){
  cancel();join();p_->cancel=false;
  p_->worker=std::thread([this,step,generation]{p_->run(step,generation);});
}
} // namespace penchat
