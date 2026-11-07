#include "hardware.hpp"
#include <csignal>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
namespace {volatile sig_atomic_t stop=0;void stopped(int){stop=1;}}
int main(int argc,char **argv) {
  using namespace penchat_scan;
  try {
    const std::string leasePath="/tmp/penchat-s6-capture-lease.json";
    if(argc==2&&std::string(argv[1])=="--restore") {
      std::ifstream saved(leasePath);Json j;saved>>j;restoreScanLease(j);return 0;
    }
    signal(SIGTERM,stopped);signal(SIGINT,stopped);signal(SIGALRM,stopped);alarm(25);
    const auto lease=scanLeaseSnapshot();
    {std::ofstream saved(leasePath);saved<<lease.dump();if(!saved)throw std::runtime_error("lease_write_failed");}
    Hardware hardware;
    try {
      hardware.open(lease);hardware.light(true);std::uint64_t frames=0,last=0;bool savedFrame=false;
      const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
      while(!stop&&std::chrono::steady_clock::now()<end) {
        hardware.readFrame([&](const std::uint8_t *pixels,int stride,std::uint64_t seq,std::int64_t timestamp){
          unsigned low=255,high=0;std::uint64_t sum=0;
          for(int y=0;y<240;++y)for(int x=0;x<320;++x){unsigned v=pixels[y*stride+x];low=std::min(low,v);high=std::max(high,v);sum+=v;}
          if(!savedFrame&&frames>8&&high-low>8){std::ofstream image("/tmp/penchat-s6-raw.pgm",std::ios::binary);image<<"P5\n320 240\n255\n";for(int y=0;y<240;++y)image.write(reinterpret_cast<const char *>(pixels)+y*stride,320);savedFrame=true;}
          if(frames%100==0)std::cerr<<"PIXELS frame="<<frames<<" low="<<low<<" high="<<high<<" mean="<<sum/76800<<'\n';
          ++frames;last=seq;
          if(frames==1)std::cerr<<"RAW_FRAME stride="<<stride<<" sequence="<<seq<<" timestamp="<<timestamp<<'\n';
        });
      }
      hardware.close();std::cout<<"CVI_CAPTURE frames="<<frames<<" last="<<last<<'\n';return frames?0:1;
    }catch(...){hardware.close();throw;}
  }catch(const std::exception &e){std::cerr<<"S6_CAPTURE_ERROR "<<e.what()<<'\n';return 1;}
}
