#pragma once
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

namespace penchat {
// Linux backlight power is FB_BLANK_* (0 = unblank, 1..4 = blank).
// Brightness is deliberately not a power signal: S6 is interactive at 5.
class ScreenPower {
public:
  struct State {
    bool available=true, known=false, awake=false;
    std::uint64_t sequence=1;
  };
  static State read(const std::string &root, bool previouslyAvailable=false) {
    State result;result.available=previouslyAvailable;
    DIR *directory=opendir(root.c_str());
    if(!directory)return result;
    unsigned count=0;bool valid=true,awake=true;
    while(auto entry=readdir(directory)) {
      if(entry->d_name[0]=='.')continue;
      const auto path=root+"/"+entry->d_name+"/bl_power";
      if(++count>16){valid=false;break;}
      const int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC);
      if(fd<0){if(errno!=ENOENT)result.available=true;valid=false;continue;}
      result.available=true;
      char data[32]{};const auto size=::read(fd,data,sizeof(data)-1);close(fd);
      if(size<=0||size==ssize_t(sizeof(data)-1)){valid=false;break;}
      char *end=nullptr;const auto value=std::strtol(data,&end,10);
      if(end==data||value<0||value>4){valid=false;break;}
      while(*end==' '||*end=='\n'||*end=='\r'||*end=='\t')++end;
      if(end!=data+size){valid=false;break;}
      awake=awake&&value==0;
    }
    closedir(directory);
    result.known=result.available&&count>0&&valid;result.awake=result.known&&awake;
    return result;
  }
  using Listener=std::function<void(const State &)>;
  void start(Listener listener,const std::string &root="/sys/class/backlight") {
    stop();
    {
      std::lock_guard<std::mutex> lock(mutex_);stopping_=false;observed_=false;state_=State{};
    }
    worker_=std::thread([this,listener,root] {
      for(;;) {
        State previous;
        {std::lock_guard<std::mutex> lock(mutex_);if(stopping_)break;previous=state_;}
        State next;
        try {next=read(root,observed_);}catch(...) {next.known=false;next.awake=false;}
        // Once a source was observed, disappearance/read failure fails closed.
        next.available=next.available||observed_;
        observed_=observed_||next.available;next.sequence=previous.sequence+1;
        {std::lock_guard<std::mutex> lock(mutex_);state_=next;}
        try {listener(next);}catch(...) {} // Never unwind through the Falcon host.
        std::unique_lock<std::mutex> lock(mutex_);
        if(wake_.wait_for(lock,std::chrono::milliseconds(100),[this]{return stopping_;}))break;
      }
    });
  }
  State state() const {std::lock_guard<std::mutex> lock(mutex_);return state_;}
  bool running() const {return worker_.joinable();}
  void stop() {
    {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}
    wake_.notify_all();if(worker_.joinable())worker_.join();
  }
  ~ScreenPower(){stop();}
private:
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::thread worker_;
  State state_;
  bool stopping_=true,observed_=false;
};
} // namespace penchat
