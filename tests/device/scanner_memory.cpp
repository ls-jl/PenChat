// Explicit standalone synthetic memory probe; never opens capture or business data.
#include <pen_stream/stream.hpp>
#include "../../native/scanner/src/memory_policy.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <malloc.h>
#include <thread>
using namespace pen::stream;
static size_t kb(const char *key) {
  std::ifstream f("/proc/self/status"); std::string line;
  while(std::getline(f,line))if(line.compare(0,std::char_traits<char>::length(key),key)==0)
    return std::strtoull(line.c_str()+std::char_traits<char>::length(key),nullptr,10)*1024;
  return 0;
}
int main(int argc,char**argv){
  if(argc!=5)return 2;
  try {
#ifdef PENCHAT_NEW_MEMORY
    penchat_scan::configureScannerAllocator();
#endif
    Limits limits;limits.idleModelMs=0;limits.maxSessions=1;
    Engine engine(makeRknn32(argv[1],argv[2]),limits);
    Config c;c.width=std::atoi(argv[3]);c.height=std::atoi(argv[4]);c.diagnosticAcceptAll=true;
    auto field=std::make_shared<Calibration>();field->version="synthetic-memory-only";field->sensorWidth=c.width;field->sensorHeight=c.height;field->rotation=c.rotation;field->gain.fill(1);c.calibration=field;
    std::vector<uint8_t> pixels(size_t(c.width)*c.height);
    for(unsigned cycle=0;cycle<20;++cycle){
      const auto start=std::chrono::steady_clock::now();
      auto begin=engine.beginScan(c);if(begin.code!=Code::Ok){printf("begin_failed cycle=%u code=%d\n",cycle,int(begin.code));return 3;}
      // This sample measures ingress RSS; worker VmHWM covers full real scans.
      unsigned pushed=0;size_t peak=0;
      for(unsigned n=0;n<80;++n){
        for(int y=0;y<c.height;++y)for(int x=0;x<c.width;++x){unsigned xx=x+n*3,z=(xx/5)*1664525u+(y/7)*1013904223u;pixels[y*c.width+x]=(y%48>8&&y%48<34&&xx%30<20&&z%7<4)?35:210;}
        const auto r=engine.pushFrame(begin.scan,pixels.data(),c.width,c.height,c.width,n,n*8400);pushed+=r.accepted;
        if(!r.accepted||r.code==Code::StopCapture)break;
        peak=std::max(peak,kb("VmRSS:"));std::this_thread::sleep_for(std::chrono::microseconds(8400));
      }
      engine.endScan(begin.scan);auto result=engine.wait(begin.scan);const auto count=result->counts();const auto status=int(result->status());
      engine.releaseResult(result);
#ifdef PENCHAT_NEW_MEMORY
      for(unsigned i=0;i<500&&!engine.reclaimIdle();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
      for(unsigned i=0;i<500&&(engine.metrics().frames||engine.metrics().sessions);++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      const auto m=engine.metrics();auto heap=mallinfo();
      const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
      if(cycle==0){std::ifstream f("/proc/self/maps");std::string line;while(std::getline(f,line)){unsigned long a=0,b=0;if(line.find("/dev/dri/card1")!=std::string::npos&&std::sscanf(line.c_str(),"%lx-%lx",&a,&b)==2&&b-a>=1024*1024)printf("model_map bytes=%lu\n",b-a);}}
      printf("cycle=%u accepted=%u committed=%llu unlocated=%llu status=%d rss=%zu swap=%zu peak=%zu heap_used=%d heap_free=%d frames=%u canvas=%zu sessions=%u model_loaded=%d elapsed_us=%lld\n",cycle,pushed,(unsigned long long)count.committed,(unsigned long long)count.unlocated,status,kb("VmRSS:"),kb("VmSwap:"),peak,heap.uordblks,heap.fordblks,m.frames,m.canvasBytes,m.sessions,m.modelLoaded,(long long)elapsed);fflush(stdout);
      if(m.frames||m.canvasBytes||m.sessions)return 4;
      if(pushed!=80 || count.committed!=80 || count.unlocated ||
          status!=int(Status::Complete) || !m.modelLoaded)return 5;
    }
    engine.shutdown();return 0;
  }catch(const std::exception&e){fprintf(stderr,"memory probe failed: %s\n",e.what());return 1;}
}
