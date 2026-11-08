#include "../../camera/src/cvi_ion_anchor.h"
#include "cvi_capture.hpp"
#include "../../camera/src/cvi_media_abi.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <fstream>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <openssl/sha.h>

// Scanner-executable-only scoped initialization guards. Subsequent SDK resume
// operations must reach ISP/VPSS, but this process must never open the UI's VO.
namespace {void *captureSystem=nullptr;bool captureBooting=true;
template<class F>F captureApi(const char *name){return reinterpret_cast<F>(dlsym(captureSystem,name));}}
extern "C" {
int v4l2_dev_open(int isp,int vpss,int,int){return captureBooting?0:captureApi<int(*)(int,int,int,int)>("v4l2_dev_open")(isp,vpss,0,0);}
int v4l2_dev_close(){return captureBooting?0:captureApi<int(*)()>("v4l2_dev_close")();}
int init_isp_device(){return captureBooting?0:captureApi<int(*)()>("init_isp_device")();}
int init_vpss_device(){return captureBooting?0:captureApi<int(*)()>("init_vpss_device")();}
int init_disp_device(){return 0;}
int init_dwa_device(){return 0;}
int CVI_SYS_SetVIVPSSMode(void *p){return captureBooting?0:captureApi<int(*)(void *)>("CVI_SYS_SetVIVPSSMode")(p);}
int CVI_SYS_SetVPSSModeEx(void *p){return captureBooting?0:captureApi<int(*)(void *)>("CVI_SYS_SetVPSSModeEx")(p);}
void CVI_SYS_TraceBegin(){}
void CVI_SYS_TraceEnd(){}
}
namespace penchat_scan {
void verifyCviCaptureRuntime() {
  static void *group=[] {
    dlopen("/lib/libgcc_s.so.1",RTLD_NOW|RTLD_GLOBAL);
    auto handle=dlopen("libpenchat_cvi_media.so",RTLD_LAZY|RTLD_GLOBAL);
    if(!handle)throw std::runtime_error(std::string("CVI media dependency group: ")+dlerror());
    return handle;
  }();
  (void)group;
}
namespace {
using namespace camera::cvi;
struct Rates {int source=-1,destination=-1;};
struct Aspect {int mode=0;bool background=false;U32 color=0;Rect rect{};};
struct Normalize {bool enabled=false;float factor[3]{},mean[3]{};int rounding=0;};
struct VpssChannel {U32 w=320,h=240;int video=0,format=18;Rates rate;bool mirror=false,flip=false;U32 depth=3;Aspect aspect;Normalize normalize;};
struct VpssGroup {U32 w=320,h=240;int format=15;Rates rate;std::uint8_t device=1;};
struct Pipe {int bypass=0;bool skip=false,isp=false;U32 w=320,h=240;int format=8,compress=0,bits=1;bool nr=true,sharpen=false;Rates rate;bool discard=false,yuv=false;};
struct Crop {bool enabled=false;int coordinates=1;Rect rect{0,0,320,240};};
static_assert(sizeof(VpssChannel)==92,"S6 VPSS channel ABI");
static_assert(sizeof(VpssGroup)==24,"S6 VPSS group ABI");
static_assert(sizeof(Pipe)==44,"S6 VI pipe ABI");
class CviCapture final:public CaptureStream {
  void *sys_=nullptr,*vpu_=nullptr,*sample_=nullptr,*dependencies_=nullptr;
  alignas(8) std::array<unsigned char,280> vi_{};
  bool descriptors_=false,sensor_=false,isp_=false,group_=false,groupStarted_=false,channel_=false,bound_=false;
  int (*get_)(int,int,FrameInfo *,int)=nullptr,(*release_)(int,int,FrameInfo *)=nullptr;
  void *(*map_)(U64,U32)=nullptr;int (*unmap_)(void *,U32)=nullptr;
  template<class F>F fn(void *lib,const char *name){auto f=reinterpret_cast<F>(dlsym(lib,name));if(!f)throw std::runtime_error(std::string("Missing CVI capture API: ")+name);return f;}
  static void check(int rc,const char *stage){std::fprintf(stderr,"CVI_STAGE %s %d\n",stage,rc);std::fflush(stderr);if(rc)throw std::runtime_error(std::string("CVI capture ")+stage+": "+std::to_string(rc));}
  void close() noexcept {
    auto call=[&](const char *name,int a){auto f=reinterpret_cast<int(*)(int)>(dlsym(vpu_,name));if(f)f(a);};
    if(vpu_) {
      if(bound_&&sample_){auto f=reinterpret_cast<int(*)(int,int,int)>(dlsym(sample_,"SAMPLE_COMM_VI_UnBind_VPSS"));if(f)f(0,0,0);}
      if(channel_){auto f=reinterpret_cast<int(*)(int,int)>(dlsym(vpu_,"CVI_VPSS_DisableChn"));if(f)f(0,0);}
      if(groupStarted_)call("CVI_VPSS_StopGrp",0);
      if(group_)call("CVI_VPSS_DestroyGrp",0);
      if(isp_&&sample_){auto f=reinterpret_cast<int(*)(void *)>(dlsym(sample_,"SAMPLE_COMM_VI_DestroyIsp"));if(f)f(vi_.data());}
      if(sensor_&&sample_){auto f=reinterpret_cast<int(*)(void *)>(dlsym(sample_,"SAMPLE_COMM_VI_DestroyVi"));if(f)f(vi_.data());}
    }
    if(descriptors_&&sys_) {
      for(const char *name:{"vpss_close","v4l2_isp_close"}) {
        auto f=reinterpret_cast<int(*)()>(dlsym(sys_,name));if(f)f();
      }
      descriptors_=false;
    }
    if(sample_)dlclose(sample_);if(vpu_)dlclose(vpu_);if(sys_)dlclose(sys_);if(dependencies_)dlclose(dependencies_);dependencies_=nullptr;
    sys_=vpu_=sample_=nullptr;
  }
public:
  CviCapture(){
    try {
      verifyCviCaptureRuntime();
      // Group loading resolves SDK data-symbol cycles before relocations.
      // Our own tiny shim names only public media and sensor DSOs.
      dependencies_=dlopen("libpenchat_cvi_media.so",RTLD_LAZY|RTLD_GLOBAL);
      if(!dependencies_)throw std::runtime_error(std::string("CVI media dependency group: ")+dlerror());
      sys_=dlopen("/usr/lib/libsys.so",RTLD_LAZY|RTLD_GLOBAL);
      vpu_=dlopen("/usr/lib/libvpu.so",RTLD_LAZY|RTLD_GLOBAL);
      sample_=dlopen("/usr/lib/libsample.so",RTLD_LAZY|RTLD_GLOBAL);
      if(!sys_||!vpu_||!sample_)throw std::runtime_error("CVI media SDK unavailable");
      {
        std::ifstream file("/usr/lib/libsample.so",std::ios::binary);SHA256_CTX ctx;SHA256_Init(&ctx);
        char bytes[16384];while(file.read(bytes,sizeof(bytes))||file.gcount())SHA256_Update(&ctx,bytes,file.gcount());
        if(!file.eof())throw std::runtime_error("CVI sensor ABI read failed");
        unsigned char digest[32];SHA256_Final(digest,&ctx);std::string hex;
        for(auto v:digest){hex+="0123456789abcdef"[v>>4];hex+="0123456789abcdef"[v&15];}
        if(hex!="73d0c2dd024882befcf8a0e0c2f04c992b99a9cf594ab77690350cf71c0adeae")
          throw std::runtime_error("CVI sensor configuration ABI is not verified");
      }
      static int levels[64];for(auto &v:levels)v=2;auto log=reinterpret_cast<int **>(dlsym(sys_,"log_levels"));if(log)*log=levels;
      // As with VO, SDK descriptors are process-scoped, not per scan. Stop
      // owned VI/VPSS resources below, but never reset live SDK worker contexts.
      static const bool initialized=[this] {
      captureSystem=sys_;captureBooting=true;
      check(fn<int(*)()>(sys_,"CVI_SYS_Init")(),"system_init");
      captureBooting=false;
      struct VbConfig {U32 pools;Pool entries[16];} config{};
      static_assert(sizeof(VbConfig)==708,"S6 VB configuration ABI");
      config.pools=1;config.entries[0].bytes=(320*240*2+4095)&~4095u;config.entries[0].count=10;
      check(fn<int(*)(VbConfig *)>(sys_,"CVI_VB_SetConfig")(&config),"vb_configuration");
      check(fn<int(*)()>(sys_,"CVI_VB_Init")(),"vb_init");
      return true;
      }();
      (void)initialized;
      camera::cvi::retainIonArena(sys_);
      check(fn<int(*)(int,int,int,int)>(sys_,"v4l2_dev_open")(1,1,0,0),"open_capture_devices");
      descriptors_=true;
      check(fn<int(*)()>(sys_,"init_isp_device")(),"isp_device");
      check(fn<int(*)()>(sys_,"init_vpss_device")(),"vpss_device");
      fn<void(*)(int)>(vpu_,"CVI_VI_Set_SensorPower")(1);
      // Match the installed S6 sensor pipeline, bypassing only our SYS_Init
      // guards once all owned capture descriptors have been opened.
      std::array<int,16> viMode{};viMode[0]=3;
      check(fn<int(*)(void *)>(sys_,"CVI_SYS_SetVIVPSSMode")(viMode.data()),"vi_vpss_mode");
      std::array<int,5> vpssMode{{1,0,1,0,0}};
      check(fn<int(*)(void *)>(sys_,"CVI_SYS_SetVPSSModeEx")(vpssMode.data()),"vpss_input_mode");
      // Platform sensor configuration is produced by the installed public SDK
      // parser. No opaque camera/stitch wrapper, shell command or private model.
      alignas(8) std::array<unsigned char,88> ini{};ini[4]=1;
      const int parsed=fn<int(*)(void *)>(sample_,"SAMPLE_COMM_VI_ParseIni")(ini.data());
      if(parsed<0||parsed>1||ini[4]!=1)throw std::runtime_error("CVI sensor configuration invalid");
      check(fn<int(*)(void *,void *)>(sample_,"SAMPLE_COMM_VI_IniToViCfg")(ini.data(),vi_.data()),"sensor_profile");
      int sensorType=0,picture=0;std::memcpy(&sensorType,vi_.data(),sizeof(sensorType));
      check(fn<int(*)(int,int *)>(sample_,"SAMPLE_COMM_VI_GetSizeBySensor")(sensorType,&picture),"sensor_size");
      struct Size {U32 w,h;} size{};
      check(fn<int(*)(int,Size *)>(sample_,"SAMPLE_COMM_SYS_GetPicSize")(picture,&size),"picture_size");
      if(size.w!=320||size.h!=240)throw std::runtime_error("S6 sensor native dimensions changed");
      fn<int(*)(int)>(vpu_,"CVI_VI_SetDevNum")(1);
      sensor_=true;
      check(fn<int(*)(void *)>(sample_,"SAMPLE_COMM_VI_StartSensor")(vi_.data()),"sensor_start");
      check(fn<int(*)(void *)>(sample_,"SAMPLE_COMM_VI_StartDev")(vi_.data()),"device_start");
      // StartDev only enables the input device; it does not bind its pipe.
      // A stale binding left by the stock process made this appear to work on
      // some runs. Establish ownership explicitly on every capture startup.
      check(fn<int(*)(void *)>(sample_,"SAMPLE_COMM_VI_BindPipeDev")(vi_.data()),"bind_sensor_pipe");
      check(fn<int(*)(void *)>(sample_,"SAMPLE_COMM_VI_StartMIPI")(vi_.data()),"mipi_start");
      Pipe pipe;check(fn<int(*)(int,Pipe *)>(vpu_,"CVI_VI_CreatePipe")(0,&pipe),"pipe_create");
      check(fn<int(*)(int)>(vpu_,"CVI_VI_StartPipe")(0),"pipe_start");
      auto bin=reinterpret_cast<int(*)(int,const char *)>(dlsym(RTLD_DEFAULT,"CVI_BIN_SetBinName"));
      if(!bin)throw std::runtime_error("CVI ISP tuning API unavailable");
      check(bin(0,"/usr/configs/cvi_sdr_bin"),"isp_tuning");
      isp_=true;check(fn<int(*)(void *)>(sample_,"SAMPLE_COMM_VI_CreateIsp")(vi_.data()),"isp_start");
      VpssGroup group;check(fn<int(*)(int,VpssGroup *)>(vpu_,"CVI_VPSS_CreateGrp")(0,&group),"vpss_group");group_=true;
      check(fn<int(*)(int)>(vpu_,"CVI_VPSS_ResetGrp")(0),"group_reset");
      VpssChannel channel;
      check(fn<int(*)(int,int,VpssChannel *)>(vpu_,"CVI_VPSS_SetChnAttr")(0,0,&channel),"vpss_channel");
      Crop crop;check(fn<int(*)(int,int,Crop *)>(vpu_,"CVI_VPSS_SetChnCrop")(0,0,&crop),"full_field");
      check(fn<int(*)(int,int)>(vpu_,"CVI_VPSS_EnableChn")(0,0),"channel_enable");channel_=true;
      check(fn<int(*)(int)>(vpu_,"CVI_VPSS_StartGrp")(0),"group_start");groupStarted_=true;
      check(fn<int(*)(int,int,int)>(sample_,"SAMPLE_COMM_VI_Bind_VPSS")(0,0,0),"bind");bound_=true;
      // Arm the ISP output only after its on-the-fly scaler consumer is ready.
      check(fn<int(*)(void *)>(sample_,"SAMPLE_COMM_VI_StartViChn")(vi_.data()),"vi_channel");
      get_=fn<decltype(get_)>(vpu_,"CVI_VPSS_GetChnFrame");release_=fn<decltype(release_)>(vpu_,"CVI_VPSS_ReleaseChnFrame");
      map_=fn<decltype(map_)>(sys_,"CVI_SYS_Mmap");unmap_=fn<decltype(unmap_)>(sys_,"CVI_SYS_Munmap");
      // Configuration success alone is not camera readiness. Drain three real
      // full-field frames before allowing VO resume or reporting scan ready.
      unsigned frames=0;std::uint32_t previous=0;std::int64_t previousTime=0;
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
      while(frames<3&&std::chrono::steady_clock::now()<deadline)
        read([&](const std::uint8_t *,int,std::uint64_t sequence,std::int64_t timestamp){
          frames=frames&&std::uint32_t(sequence)==previous+1&&timestamp>previousTime?frames+1:1;
          previous=std::uint32_t(sequence);previousTime=timestamp;
        });
      if(frames<3)throw std::runtime_error("CVI sensor startup produced no continuous frames");
      std::fprintf(stderr,"CVI_STAGE sensor_frames_ready %u\n",frames);
    }catch(...){close();throw;}
  }
  ~CviCapture()override{close();}
  bool read(const std::function<void(const std::uint8_t *,int,std::uint64_t,std::int64_t)> &consume)override {
    FrameInfo info{};if(get_(0,0,&info,20))return false;
    void *mapped=nullptr;
    try {
      const auto &f=info.frame;
      if(f.w!=320||f.h!=240||f.format!=18||f.stride[0]<f.w||f.stride[0]>16384||
         f.length[0]<std::uint64_t(f.stride[0])*f.h||f.length[0]>8*1024*1024||!f.physical[0])
        throw std::runtime_error("CVI full-field scan frame mismatch");
      mapped=map_(f.physical[0],f.length[0]);if(!mapped||mapped==MAP_FAILED)throw std::runtime_error("CVI scan frame mapping failed");
      consume(static_cast<const std::uint8_t *>(mapped),int(f.stride[0]),f.time,std::int64_t(f.pts));
      unmap_(mapped,f.length[0]);mapped=nullptr;
    }catch(...){if(mapped&&mapped!=MAP_FAILED)unmap_(mapped,info.frame.length[0]);release_(0,0,&info);throw;}
    const int released=release_(0,0,&info);
    if(released)check(released,"frame_release");
    return true;
  }
};
}
std::shared_ptr<CaptureStream> openCviCapture(){return std::make_shared<CviCapture>();}
}
