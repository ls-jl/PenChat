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
  bool start() {
    if(!saved_.empty())return true;
    for(int source:system_.clients()) {
      int fd=system_.duplicate(source);if(fd<0){stop();return false;}
      Mask original(96,0);
      if(!system_.read(fd,original)||original.size()<=134/8){system_.close(fd);stop();return false;}
      if(!(original[134/8]&(1u<<(134%8)))){system_.close(fd);continue;}
      saved_.push_back({fd,original});auto filtered=original;
      filtered[134/8]&=~(1u<<(134%8));
      if(!system_.write(fd,filtered)){stop();return false;}
    }
    return !saved_.empty();
  }
  void stop() {
    for(const auto &s:saved_){system_.write(s.fd,s.mask);system_.close(s.fd);}
    saved_.clear();
  }
  static System realSystem();
private:
  struct Saved {int fd;Mask mask;};
  System system_;
  std::vector<Saved> saved_;
};
}
