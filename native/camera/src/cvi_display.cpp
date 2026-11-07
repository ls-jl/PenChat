#include "display_surface.h"
#include "cvi_media_abi.h"
#include "cvi_ion_anchor.h"
#include "cvi_frame_slots.h"
#include "cvi_pixels.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <linux/videodev2.h>
#include <openssl/evp.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

// These symbols belong only to the native display executable. Never link them
// into the Falcon launcher. Limit broad SYS_Init to our display resources.
extern "C" {
int v4l2_dev_open(int,int,int,int){return 0;}
int v4l2_dev_close(){return 0;}
int init_isp_device(){return 0;}
int init_vpss_device(){return 0;}
int init_disp_device(){return 0;}
int init_dwa_device(){return 0;}
int CVI_SYS_SetVIVPSSMode(void *){return 0;}
int CVI_SYS_SetVPSSModeEx(void *){return 0;}
void CVI_SYS_TraceBegin(){}
void CVI_SYS_TraceEnd(){}
}
namespace camera {
namespace {
using namespace cvi;
std::string hash(const char *path) {
  std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("CVI library unavailable");
  auto ctx=EVP_MD_CTX_new();if(!ctx)throw std::bad_alloc();
  std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> owner(ctx,EVP_MD_CTX_free);
  if(!EVP_DigestInit_ex(ctx,EVP_sha256(),nullptr))throw std::runtime_error("CVI hash init failed");
  char buffer[16384];while(f.read(buffer,sizeof(buffer))||f.gcount())
    if(!EVP_DigestUpdate(ctx,buffer,f.gcount()))throw std::runtime_error("CVI hash failed");
  if(!f.eof())throw std::runtime_error("CVI library read failed");
  unsigned char result[32];unsigned n=0;
  if(!EVP_DigestFinal_ex(ctx,result,&n)||n!=32)throw std::runtime_error("CVI hash failed");
  std::string out;for(auto v:result){out+="0123456789abcdef"[v>>4];out+="0123456789abcdef"[v&15];}return out;
}
int driverOrigin(const char *axis,int configured) {
  // CVI's physical video layer bypasses the framebuffer's visible-window crop.
  // Falcon describes that window's size; its physical origin is owned by cvifb.
  std::ifstream file(std::string("/sys/module/cvifb/parameters/start_")+axis);
  long origin=-2;file>>origin;
  if(!file||origin< -1||origin>4096)throw std::runtime_error("CVI framebuffer origin unavailable");
  const long value=std::max(0L,origin)+configured;
  if(value<0||value>4096)throw std::runtime_error("CVI framebuffer origin invalid");
  return int(value);
}
class CviDisplay final : public DisplaySurface {
  void *sys_=nullptr,*vpu_=nullptr;
  DisplayGeometry geometry_;
  int formatFd_=-1;
  U32 sequence_=0;
  int (*pts_)(U64 *)=nullptr;
  bool deviceOpen_=false;
  U32 pool_=~0u;bool layer_=false,channel_=false,shown_=false,haveLayer_=false,haveChannel_=false;
  Layer oldLayer_{};Channel oldChannel_{};
  struct Buffer {U32 block=~0u;U64 address=0;std::uint8_t *map=nullptr;};
  Buffer buffers_[3];FrameSlots<3> slots_;
  U32 stride_=0,size_=0,yBytes_=0,uvBytes_=0;
  std::vector<std::uint8_t> converted_;
  int (*users_)(U32,U32 *)=nullptr;
  int (*layerSet_)(int,Layer *)=nullptr,(*channelSet_)(int,int,Channel *)=nullptr;
  int (*layerDisable_)(int)=nullptr,(*channelDisable_)(int,int)=nullptr;
  int (*show_)(int,int)=nullptr,(*hide_)(int,int)=nullptr,(*clear_)(int,int,std::uint8_t)=nullptr;
  int (*send_)(int,int,FrameInfo *,int)=nullptr;
  U32 (*create_)(Pool *)=nullptr,(*get_)(U32,U32,const char *)=nullptr;
  int (*destroy_)(U32)=nullptr,(*release_)(U32)=nullptr,(*unmap_)(void *,U32)=nullptr;
  U64 (*physical_)(U32)=nullptr;void *(*map_)(U64,U32)=nullptr;
  template<typename F> F load(void *library,const char *name) {
    auto f=reinterpret_cast<F>(dlsym(library,name));if(!f)throw std::runtime_error(std::string("Missing CVI API: ")+name);return f;
  }
  static void check(int rc){if(rc)throw std::runtime_error("CVI display error "+std::to_string(rc));}
  void close() noexcept {
    if(deviceOpen_)std::fprintf(stderr,"S6_MEDIA display.release.begin\n");
    if(shown_&&hide_)hide_(0,0);
    if(channel_&&clear_)clear_(0,0,1);
    if(channel_&&channelDisable_)channelDisable_(0,0);
    if(layer_&&layerDisable_)layerDisable_(0);
    if(haveChannel_&&channelSet_&&oldChannel_.rect.w&&oldChannel_.rect.h)channelSet_(0,0,&oldChannel_);
    if(haveLayer_&&layerSet_&&oldLayer_.w&&oldLayer_.h)layerSet_(0,&oldLayer_);
    for(auto &b:buffers_) {
      if(b.map&&unmap_)unmap_(b.map,size_);
      if(b.block!=~0u&&release_)release_(b.block);
      b=Buffer{};
    }
    if(pool_!=~0u&&destroy_)destroy_(pool_);
    pool_=~0u;shown_=channel_=layer_=haveChannel_=haveLayer_=false;
    if(formatFd_>=0)::close(formatFd_);formatFd_=-1;
    // DisableChn joins the VO event thread before closing our final display fd.
    // Keeping that fd open while the stock camera closes its own display fd
    // leaves this kernel's shared control handler dangling.
    if(deviceOpen_&&sys_) {
      // Suspend also unmaps the SDK's device-backed context, releasing its VMA
      // reference before the last fd is closed. No broad SYS suspend is used.
      auto suspend=reinterpret_cast<int(*)()>(dlsym(vpu_,"CVI_VO_Suspend"));
      if(suspend)suspend();
      auto closeDisplay=reinterpret_cast<int(*)()>(dlsym(sys_,"vo_close"));
      if(closeDisplay)closeDisplay();
      deviceOpen_=false;
      std::fprintf(stderr,"S6_MEDIA display.release.end busy_checks=%llu deferred=%llu\n",
        static_cast<unsigned long long>(slots_.busyChecks),static_cast<unsigned long long>(slots_.deferred));
    }
    if(vpu_)dlclose(vpu_);if(sys_)dlclose(sys_);vpu_=sys_=nullptr;
  }
public:
  explicit CviDisplay(const DisplayGeometry &g):geometry_(g) {
    try {
      if(g.panelWidth<=0||g.panelHeight<=0||g.panelWidth%2||g.panelHeight%2||
         ((g.rotation==90||g.rotation==270)?(g.width!=g.panelHeight||g.height!=g.panelWidth):
           (g.width!=g.panelWidth||g.height!=g.panelHeight))||
         (g.rotation!=0&&g.rotation!=90&&g.rotation!=180&&g.rotation!=270))
        throw std::runtime_error("Unsupported CVI panel geometry");
      // A reviewed legacy context helper is firmware-specific; no timing writes,
      // memory patching, or guessing offsets on unknown firmware.
      if(hash("/usr/lib/libsys.so")!="72a7023c0075d045e1bca08b0e0bbc68a8b5a2aca82f30732ca3aa3796b9a068"||
         hash("/usr/lib/libvpu.so")!="d28a11869dfa5569d7312a65fd5a54f30e849d77d2884d4a480bd4fb8bc65a60")
        throw std::runtime_error("S6 display firmware is not verified");
      // Vendor media DSOs import ARM EABI helpers from the shared board runtime.
      // The app's static libgcc does not export those helpers to dlopen callers.
      static void *support=dlopen("/lib/libgcc_s.so.1",RTLD_NOW|RTLD_GLOBAL);
      if(!support)
        throw std::runtime_error(std::string("CVI support runtime: ")+dlerror());
      sys_=dlopen("/usr/lib/libsys.so",RTLD_LAZY|RTLD_GLOBAL|RTLD_NODELETE);
      if(!sys_)throw std::runtime_error(std::string("Cannot load CVI system library: ")+dlerror());
      vpu_=dlopen("/usr/lib/libvpu.so",RTLD_LAZY|RTLD_GLOBAL|RTLD_NODELETE);
      if(!vpu_)throw std::runtime_error(std::string("Cannot load CVI display library: ")+dlerror());
      Dl_info sysInfo{},vpuInfo{};
      dladdr(dlsym(sys_,"CVI_SYS_Init"),&sysInfo);dladdr(dlsym(vpu_,"CVI_VO_SendFrame"),&vpuInfo);
      std::fprintf(stderr,"S6_MEDIA sdk sys=%p vpu=%p\n",sysInfo.dli_fbase,vpuInfo.dli_fbase);

      static int levels[64]={};for(auto &v:levels)v=2;
      auto log=reinterpret_cast<int **>(dlsym(sys_,"log_levels"));if(log)*log=levels;
      // The legacy SDK owns a VO event thread. Reinitializing its context on
      // every surface recreation races that thread's ioctl and can panic this
      // kernel. Initialize SDK context once, then explicitly close/reopen the
      // owned display fd at capture handoffs after its event thread is joined.
      static const bool initialized=[this] {
        check(load<int(*)()>(sys_,"CVI_SYS_Init")());
        check(load<int(*)()>(sys_,"CVI_VB_Init")());
        return true;
      }();
      (void)initialized;
      retainIonArena(sys_);
      check(load<int(*)(int,int,int,int)>(sys_,"v4l2_dev_open")(0,0,1,0));
      deviceOpen_=true;
      std::fprintf(stderr,"S6_MEDIA display.acquire\n");
      check(load<int(*)()>(sys_,"init_disp_device")());
      auto callback=reinterpret_cast<std::uintptr_t>(dlsym(vpu_,"vo_gdc_callback"));
      if(!callback)throw std::runtime_error("CVI context initializer unavailable");
      const auto helper=callback+0x24;
      U32 prologue=0;std::memcpy(&prologue,reinterpret_cast<void *>(helper&~std::uintptr_t(1)),sizeof(prologue));
      if(prologue!=0x2100b5f0u)throw std::runtime_error("CVI context initializer mismatch");
      check(reinterpret_cast<int(*)()>(helper)());
      layerSet_=load<decltype(layerSet_)>(vpu_,"CVI_VO_SetVideoLayerAttr");
      channelSet_=load<decltype(channelSet_)>(vpu_,"CVI_VO_SetChnAttr");
      layerDisable_=load<decltype(layerDisable_)>(vpu_,"CVI_VO_DisableVideoLayer");
      channelDisable_=load<decltype(channelDisable_)>(vpu_,"CVI_VO_DisableChn");
      show_=load<decltype(show_)>(vpu_,"CVI_VO_ShowChn");hide_=load<decltype(hide_)>(vpu_,"CVI_VO_HideChn");
      clear_=load<decltype(clear_)>(vpu_,"CVI_VO_ClearChnBuf");send_=load<decltype(send_)>(vpu_,"CVI_VO_SendFrame");
      pts_=load<decltype(pts_)>(sys_,"CVI_SYS_GetCurPTS");
      users_=load<decltype(users_)>(sys_,"CVI_VB_InquireUserCnt");
      create_=load<decltype(create_)>(sys_,"CVI_VB_CreatePool");get_=load<decltype(get_)>(sys_,"CVI_VB_GetBlock");
      destroy_=load<decltype(destroy_)>(sys_,"CVI_VB_DestroyPool");release_=load<decltype(release_)>(sys_,"CVI_VB_ReleaseBlock");
      physical_=load<decltype(physical_)>(sys_,"CVI_VB_Handle2PhysAddr");map_=load<decltype(map_)>(sys_,"CVI_SYS_Mmap");unmap_=load<decltype(unmap_)>(sys_,"CVI_SYS_Munmap");
      haveLayer_=load<int(*)(int,Layer *)>(vpu_,"CVI_VO_GetVideoLayerAttr")(0,&oldLayer_)==0;
      haveChannel_=load<int(*)(int,int,Channel *)>(vpu_,"CVI_VO_GetChnAttr")(0,0,&oldChannel_)==0;
      const int originX=driverOrigin("x",g.xoffset),originY=driverOrigin("y",g.yoffset);
      Layer layer{{originX,originY,U32(g.panelWidth),U32(g.panelHeight)},U32(g.panelWidth),U32(g.panelHeight),60,18};
      check(layerSet_(0,&layer));
      Channel channel{0,{0,0,U32(g.panelWidth),U32(g.panelHeight)}};
      check(channelSet_(0,0,&channel));
      // Reuse the media library's already opened file description. Opening and
      // closing a second video109 fd resets this legacy driver's active stream.
      // The exported 36-byte device record is part of the hash-locked ABI.
      auto device=static_cast<const unsigned char *>(dlsym(sys_,"dev_disp"));
      int owned=-1;if(device)std::memcpy(&owned,device+16,sizeof(owned));
      int fd=owned>=0?fcntl(owned,F_DUPFD_CLOEXEC,3):-1;formatFd_=fd;
      if(fd<0)throw std::runtime_error("CVI display format unavailable");
      v4l2_format fmt{};fmt.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
      fmt.fmt.pix_mp.width=g.panelWidth;fmt.fmt.pix_mp.height=g.panelHeight;
      fmt.fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12M;fmt.fmt.pix_mp.num_planes=2;
      // Negotiate the real input on our fd before stream-on. Cached SDK
      // dimensions do not prove that a freshly opened driver is configured.
      // Never reopen the device or re-S_FMT after enabling the channel.
      fmt.fmt.pix_mp.field=V4L2_FIELD_ANY;
      fmt.fmt.pix_mp.colorspace=V4L2_COLORSPACE_SMPTE170M;
      const int rc=ioctl(fd,VIDIOC_S_FMT,&fmt);
      Layer actual{};
      check(load<int(*)(int,Layer *)>(vpu_,"CVI_VO_GetVideoLayerAttr")(0,&actual));
      if(int(actual.w)!=g.panelWidth||int(actual.h)!=g.panelHeight||actual.format!=18||actual.rect.x!=originX||actual.rect.y!=originY)
        throw std::runtime_error("CVI layer geometry mismatch");
      // S_FMT's returned geometry/planes are authoritative; do not accept
      // a zero-dimension G_FMT just because the SDK's shared cache looks valid.
      for(unsigned target:{V4L2_SEL_TGT_CROP,V4L2_SEL_TGT_CROP_BOUNDS,V4L2_SEL_TGT_COMPOSE,V4L2_SEL_TGT_COMPOSE_BOUNDS}) {
        v4l2_selection selection{};selection.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;selection.target=target;
        const int selected=ioctl(fd,VIDIOC_G_SELECTION,&selection);
        std::fprintf(stderr,"S6_DISPLAY selection %u rc=%d rect=%d,%d,%u,%u\n",target,selected,selection.r.left,selection.r.top,selection.r.width,selection.r.height);
      }
      std::fprintf(stderr,"S6_DISPLAY layer rect=%d,%d,%u,%u source=%u,%u\n",actual.rect.x,actual.rect.y,actual.rect.w,actual.rect.h,actual.w,actual.h);
      const auto &f=fmt.fmt.pix_mp;
      if(rc||int(f.width)!=g.panelWidth||int(f.height)!=g.panelHeight||f.pixelformat!=V4L2_PIX_FMT_NV12M||f.num_planes!=2||
         f.plane_fmt[0].bytesperline!=f.plane_fmt[1].bytesperline||f.plane_fmt[0].bytesperline<unsigned(g.panelWidth))
        throw std::runtime_error("CVI negotiated display format mismatch: "+std::to_string(rc)+" "+std::to_string(f.width)+"x"+std::to_string(f.height)+" format="+std::to_string(f.pixelformat)+" planes="+std::to_string(f.num_planes)+" stride="+std::to_string(f.plane_fmt[0].bytesperline));
      stride_=f.plane_fmt[0].bytesperline;yBytes_=f.plane_fmt[0].sizeimage;uvBytes_=f.plane_fmt[1].sizeimage;
      if(stride_>16384||yBytes_<std::uint64_t(stride_)*g.panelHeight||uvBytes_<std::uint64_t(stride_)*g.panelHeight/2||
         std::uint64_t(yBytes_)+uvBytes_>64*1024*1024)throw std::runtime_error("CVI display buffer size invalid");
      check(load<int(*)()>(sys_,"init_disp_device")());
      check(load<int(*)(int)>(vpu_,"CVI_VO_EnableVideoLayer")(0));layer_=true;
      check(load<int(*)(int,int)>(vpu_,"CVI_VO_EnableChn")(0,0));channel_=true;
      size_=yBytes_+uvBytes_;converted_.resize(size_);Pool cfg{size_,3,1,"penchat-display"};pool_=create_(&cfg);
      if(pool_==~0u)throw std::runtime_error("CVI display pool allocation failed");
      for(auto &b:buffers_) {
        b.block=get_(pool_,size_,nullptr);if(b.block==~0u)throw std::bad_alloc();
        b.address=physical_(b.block);if(!b.address)throw std::bad_alloc();
        auto mapped=map_(b.address,size_);if(!mapped||mapped==MAP_FAILED)throw std::bad_alloc();
        b.map=static_cast<std::uint8_t *>(mapped);
      }
    }catch(...){close();throw;}
  }
  ~CviDisplay() override {close();}
  void hide() override {if(shown_){check(hide_(0,0));shown_=false;check(clear_(0,0,1));}}
  bool present(const std::uint8_t *pixels,int w,int h) override {
    if(!pixels||w!=geometry_.width||h!=geometry_.height)throw std::runtime_error("CVI source geometry mismatch");
    const int selected=slots_.writable([this](std::size_t index) {
      U32 count=0;check(users_(buffers_[index].block,&count));return count;
    });
    if(selected<0)return false;
    auto &b=buffers_[selected];
    // Device mappings are uncached. Convert in reusable cached RAM, then use a
    // contiguous copy instead of thousands of individual device-memory writes.
    bgraToNv12(pixels,w,h,geometry_.rotation,converted_.data(),
      geometry_.panelWidth,geometry_.panelHeight,stride_,yBytes_,uvBytes_);
    std::memcpy(b.map,converted_.data(),size_);
    FrameInfo frame{};frame.pool=pool_;frame.frame.w=geometry_.panelWidth;frame.frame.h=geometry_.panelHeight;frame.frame.format=18;
    frame.frame.stride[0]=frame.frame.stride[1]=stride_;frame.frame.physical[0]=b.address;frame.frame.physical[1]=b.address+yBytes_;
    frame.frame.virtualAddress[0]=b.map;frame.frame.virtualAddress[1]=b.map+yBytes_;frame.frame.length[0]=yBytes_;frame.frame.length[1]=uvBytes_;
    // VO consumes timed video frames. A permanently zero PTS/time-reference
    // makes successive LVGL flushes indistinguishable to the SDK scheduler.
    check(pts_(&frame.frame.pts));frame.frame.time=++sequence_;
    if(!shown_){check(show_(0,0));shown_=true;}
    check(send_(0,0,&frame,100));
    return true;
  }
};
}
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry &g){return std::unique_ptr<DisplaySurface>(new CviDisplay(g));}
}
