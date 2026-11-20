#include "hardware.hpp"
#include "cvi_capture.hpp"
#include "platform_inference.hpp"
#include "pen_stream/stream.hpp"
#include <array>
#include <dirent.h>
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
    const std::string mode=argc>1?argv[1]:"";
    if(((argc==3||argc==4)&&(mode=="--cycles"||mode=="--direct-cycles")) ||
       ((argc==4||argc==5)&&mode=="--model-cycles")) {
      const bool direct=mode=="--direct-cycles";
      const int cycles=std::max(1,std::min(50,std::stoi(argv[2])));
      const int delayArg=mode=="--model-cycles"?4:3;
      const int delayMs=argc>delayArg?std::max(0,std::min(3000,std::stoi(argv[delayArg]))):1500;
      alarm(unsigned(cycles*10+10));
      Hardware owner;
      auto saveLease=[&](const Json &snapshot) {
        std::ofstream saved(leasePath);saved<<snapshot.dump();
        if(!saved)throw std::runtime_error("lease_write_failed");
      };
      if(direct){const auto snapshot=scanLeaseSnapshot();saveLease(snapshot);owner.reserve(snapshot);}
      std::unique_ptr<pen::stream::Inference> model;
      std::array<std::uint8_t,80*144> gray{};gray.fill(127);
      pen::stream::NetworkBatch batch;batch.validCount=32;batch.gray.fill(gray.data());
      pen::stream::Heads reference;
      for(int cycle=1;cycle<=cycles&&!stop;++cycle) {
        const auto snapshot=[&] {
          if(direct)return Json();
          const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
          for(;;)try {return scanLeaseSnapshot();}catch(const std::exception &e) {
            if(std::string(e.what())!="system capture preparing"||std::chrono::steady_clock::now()>=end)throw;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
          }
        }();
        if(!direct)saveLease(snapshot);
        Hardware capture;std::shared_ptr<CaptureStream> stream;
        if(direct)stream=openCviCapture();else {
          capture.reserve(snapshot);
          if(mode=="--model-cycles"&&!model) {
            model=makePlatformInference(scanProfileById(snapshot.at("profile").get<std::string>()),argv[3]);
            model->open();model->infer(batch,reference);
          } else if(model) {
            pen::stream::Heads actual;model->infer(batch,actual);
            if(actual!=reference)throw std::runtime_error("resident model output changed after capture release");
          }
          capture.start();
        }
        std::uint64_t frames=0,last=0;std::int64_t stamp=0;
        const auto consume=[&](const std::uint8_t *,int stride,std::uint64_t sequence,std::int64_t timestamp) {
          if(stride<320||frames&&(sequence!=last+1||timestamp<=stamp))
            throw std::runtime_error("cycle frame discontinuity");
          ++frames;last=sequence;stamp=timestamp;
        };
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(frames<100&&!stop&&std::chrono::steady_clock::now()<deadline) {
          if(direct)stream->read(consume);else capture.readFrame(consume);
        }
        stream.reset();capture.close();if(!direct)unlink(leasePath.c_str());
        if(frames<100)throw std::runtime_error("cycle camera did not sustain frames");
        unsigned descriptors=0,mappings=0;
        if(auto directory=opendir("/proc/self/fd")) {
          while(auto item=readdir(directory))if(item->d_name[0]!='.')++descriptors;
          closedir(directory);
        }
        {std::ifstream file("/proc/self/maps");std::string line;while(std::getline(file,line))++mappings;}
        std::cout<<"CAPTURE_CYCLE cycle="<<cycle<<" frames="<<frames<<" last="<<last
                 <<" descriptors="<<descriptors<<" mappings="<<mappings<<" resident_model="<<bool(model)<<std::endl;
        // Let the real stock owner rebuild its own VI/VPSS/VB between leases.
        // Direct mode deliberately omits this ownership transition.
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
      }
      owner.close();unlink(leasePath.c_str());return stop?1:0;
    }
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
