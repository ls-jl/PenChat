#include "hardware.hpp"
#include <csignal>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
namespace {volatile sig_atomic_t stop=0;void stopped(int){stop=1;}}
int main(int argc,char **argv) {
  using namespace penchat_scan;
  try {
    umask(0077);
    const int lock=acquireScanProcessLock(2000);
    struct Lock {int fd;~Lock(){::close(fd);}} held{lock};
    const std::string leasePath="/tmp/penchat-s6-capture-lease.json";
    if(argc==2&&std::string(argv[1])=="--restore") {
      std::ifstream saved(leasePath);Json j;saved>>j;restoreScanLease(j);return 0;
    }
    signal(SIGTERM,stopped);signal(SIGINT,stopped);signal(SIGALRM,stopped);alarm(25);
    const auto lease=scanLeaseSnapshot();
    {std::ofstream saved(leasePath);saved<<lease.dump();if(!saved)throw std::runtime_error("lease_write_failed");}
    Hardware hardware;
    try {
      if(argc==2&&std::string(argv[1])=="--record") {
        alarm(70);
        // Explicit paper diagnostic only, never linked into the application.
        // Hold one bounded RAM recording; no disk writes in the capture loop.
        constexpr std::size_t frameBytes=320*240, maximum=512;
        std::vector<std::uint8_t> pixels;pixels.reserve(frameBytes*maximum);
        Json events=Json::array();bool pressed=false,finished=false;
        hardware.open(lease);
        std::cerr<<"PAPER_RECORD_READY press tip, scan text, release within 60 seconds\n";
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(!stop&&!finished&&std::chrono::steady_clock::now()<end) {
          bool down=false;
          if(hardware.keyEvent(down)) {
            if(down&&!pressed){pressed=true;hardware.light(true);}
            else if(!down&&pressed){hardware.light(false);finished=true;}
          }
          if(finished)break;
          hardware.readFrame([&](const std::uint8_t *frame,int stride,std::uint64_t seq,std::int64_t timestamp){
            if(!pressed)return;
            if(events.size()==maximum){finished=true;return;}
            const auto offset=pixels.size();pixels.resize(offset+frameBytes);
            for(int y=0;y<240;++y)std::memcpy(pixels.data()+offset+y*320,frame+y*stride,320);
            events.push_back({{"sequence",seq},{"timestamp",timestamp}});
          });
        }
        hardware.close();unlink(leasePath.c_str());
        if(!pressed||!finished||pixels.empty())throw std::runtime_error("paper_record_missing_or_timeout");
        std::ofstream data("/tmp/penchat-s6-paper.ps32",std::ios::binary);
        const std::uint32_t header[]={1,1,320,240,0,std::uint32_t(events.size()),0,0};
        std::vector<float> gain(160*288,1.f);
        data.write("PS32",4);data.write(reinterpret_cast<const char *>(header),sizeof(header));
        data.write(reinterpret_cast<const char *>(gain.data()),gain.size()*sizeof(float));
        data.write(reinterpret_cast<const char *>(pixels.data()),pixels.size());
        data.close();if(!data)throw std::runtime_error("paper_record_write_failed");
        std::ofstream metadata("/tmp/penchat-s6-paper.json");metadata<<events.dump();
        if(!metadata)throw std::runtime_error("paper_metadata_write_failed");
        std::cout<<"PAPER_RECORD_COMPLETE frames="<<events.size()<<" capped="<<(events.size()==maximum)<<'\n';return 0;
      }
      hardware.open(lease);hardware.light(true);std::uint64_t frames=0,last=0;bool savedFrame=false;
      const int seconds=(argc==3&&std::string(argv[1])=="--seconds")?std::max(1,std::min(15,std::stoi(argv[2]))):3;
      auto report=std::chrono::steady_clock::now();
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
      while(!stop&&std::chrono::steady_clock::now()<end) {
        if(std::chrono::steady_clock::now()-report>=std::chrono::seconds(1)){std::cerr<<"CAPTURE_PROGRESS frames="<<frames<<"\n";report=std::chrono::steady_clock::now();}
        hardware.readFrame([&](const std::uint8_t *pixels,int stride,std::uint64_t seq,std::int64_t timestamp){
          unsigned low=255,high=0;std::uint64_t sum=0;
          for(int y=0;y<240;++y)for(int x=0;x<320;++x){unsigned v=pixels[y*stride+x];low=std::min(low,v);high=std::max(high,v);sum+=v;}
          if(!savedFrame&&frames>8&&high-low>8){std::ofstream image("/tmp/penchat-s6-raw.pgm",std::ios::binary);image<<"P5\n320 240\n255\n";for(int y=0;y<240;++y)image.write(reinterpret_cast<const char *>(pixels)+y*stride,320);savedFrame=true;}
          if(frames%100==0)std::cerr<<"PIXELS frame="<<frames<<" low="<<low<<" high="<<high<<" mean="<<sum/76800<<'\n';
          ++frames;last=seq;
          if(frames==1)std::cerr<<"RAW_FRAME stride="<<stride<<" sequence="<<seq<<" timestamp="<<timestamp<<'\n';
        });
      }
      hardware.close();unlink(leasePath.c_str());std::cout<<"CVI_CAPTURE frames="<<frames<<" last="<<last<<'\n';return frames?0:1;
    }catch(...){hardware.close();throw;}
  }catch(const std::exception &e){std::cerr<<"S6_CAPTURE_ERROR "<<e.what()<<'\n';return 1;}
}
