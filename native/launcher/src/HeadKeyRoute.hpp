#pragma once
#include <functional>
#include <vector>
#include <cstdint>
#include <utility>
namespace penchat {
// Suppress the host's head-key clients only. CaptureFrame is a separate process
// and must continue receiving physical presses to drive the lamp and camera.
class HeadKeyRoute {
public:
  using Mask=std::vector<unsigned char>;
  struct System {
    std::function<std::vector<int>()> clients;
    std::function<int(int)> duplicate;
    std::function<bool(int,Mask &)> read;
    std::function<bool(int,const Mask &)> write;
    std::function<void(int)> close;
  };
  explicit HeadKeyRoute(System system):system_(std::move(system)){}
  ~HeadKeyRoute(){stop();}
  HeadKeyRoute(const HeadKeyRoute &)=delete;
  HeadKeyRoute &operator=(const HeadKeyRoute &)=delete;
  int failure() const {return failure_;}
  std::size_t clients() const {return saved_.size();}
  bool start() {
    failure_=0;
    if(!saved_.empty())return true;
    const auto clients=system_.clients();
    for(int source:clients) {
      int fd=system_.duplicate(source);if(fd<0){failure_=2;stop();return false;}
      Mask original(96,0);
      if(!system_.read(fd,original)||original.size()<=134/8){failure_=3;system_.close(fd);stop();return false;}
      if(!(original[134/8]&(1u<<(134%8)))){system_.close(fd);continue;}
      saved_.push_back({fd,original});auto filtered=original;
      filtered[134/8]&=~(1u<<(134%8));
      if(!system_.write(fd,filtered)){failure_=4;stop();return false;}
    }
    if(saved_.empty())failure_=clients.empty()?1:5;
    return !saved_.empty();
  }
  void stop() {
    for(const auto &s:saved_){system_.write(s.fd,s.mask);system_.close(s.fd);}
    saved_.clear();
  }
  static System realSystem();
private:
  struct Saved {int fd;Mask mask;};
  int failure_=0;
  System system_;
  std::vector<Saved> saved_;
};
}
