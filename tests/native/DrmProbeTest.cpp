#include <cstdint>
#include <pthread.h>
#include <string>
#include <vector>
#define private public
#include "overlay.h"
#undef private
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <cstdarg>
#include <cerrno>
#include <iostream>
#include <cstdlib>
#include <chrono>
namespace {
struct Fixture {
  bool inactive=false,rejectAtomic=false,argb=true,atomic=true,query=true,vblank=true,order=true,occupied=false,xrgbOnly=false,deliver=true,foreign=false;
  int atomicTestError=0,delay=0,races=0,queries=0,properties=0,legacy=0,atomics=0,hostWrites=0;
  uint32_t attached=0,nextFb=100,nextHandle=0,pending=0;uint64_t pendingNative=0,pendingHost=3;uint64_t nativeZ=0,hostZ=3;
  void *context=nullptr;bool isVblank=false;
} f;
uint32_t planeIds[]={2,1},crtcIds[]={42},connectorIds[]={9},formats[]={DRM_FORMAT_ARGB8888,DRM_FORMAT_XRGB8888};
uint32_t propertyIds[]={10,11,20,21,22,23,24,25,26,27,28,29};
const char *names[]={"type","zpos","FB_ID","CRTC_ID","SRC_X","SRC_Y","SRC_W","SRC_H","CRTC_X","CRTC_Y","CRTC_W","CRTC_H"};
using Open=void *(*)(const char *,int);
}
extern "C" void *dlopen(const char *name,int flags) noexcept {
  auto real=reinterpret_cast<Open>(dlsym(RTLD_NEXT,"dlopen"));
  return real(name&&std::strstr(name,"libdrm")?nullptr:name,flags);
}
extern "C" int open(const char *path,int flags,...) {
  if(std::strcmp(path,"/dev/input/event0")==0&&std::getenv("PENCHAT_FIXTURE_UPPER")) {
    static int writer=-1;if(writer>=0)::close(writer);
    int pair[2];if(pipe2(pair,O_NONBLOCK|O_CLOEXEC))return -1;writer=pair[1];return pair[0];
  }
  if(std::strcmp(path,"/dev/dri/card0")==0)return int(syscall(SYS_memfd_create,"fake-drm",1));
  va_list args;va_start(args,flags);int mode=(flags&O_CREAT)?va_arg(args,int):0;va_end(args);
  return int(syscall(SYS_openat,AT_FDCWD,path,flags,mode));
}
extern "C" int drmSetClientCap(int,uint64_t cap,uint64_t) {static bool once=false;if(!once){once=true;if(std::getenv("PENCHAT_FIXTURE_UPPER"))f.argb=false;}return cap==DRM_CLIENT_CAP_ATOMIC&&!f.atomic?-1:0;}
extern "C" drmModeResPtr drmModeGetResources(int) {static drmModeRes r{};r.count_connectors=1;r.connectors=connectorIds;r.count_crtcs=1;r.crtcs=crtcIds;return &r;}
extern "C" void drmModeFreeResources(drmModeResPtr) {}
extern "C" drmModeConnectorPtr drmModeGetConnector(int,uint32_t) {static drmModeConnector c{};static drmModeModeInfo mode{};mode.hdisplay=100;mode.vdisplay=80;c.count_modes=1;c.modes=&mode;c.connection=DRM_MODE_CONNECTED;c.encoder_id=8;return &c;}
extern "C" void drmModeFreeConnector(drmModeConnectorPtr) {}
extern "C" drmModeEncoderPtr drmModeGetEncoder(int,uint32_t) {static drmModeEncoder e{};e.crtc_id=42;return &e;}
extern "C" void drmModeFreeEncoder(drmModeEncoderPtr) {}
extern "C" drmModeCrtcPtr drmModeGetCrtc(int,uint32_t) {static drmModeCrtc c{};c.mode_valid=1;c.mode.hdisplay=100;c.mode.vdisplay=80;return &c;}
extern "C" void drmModeFreeCrtc(drmModeCrtcPtr) {}
extern "C" drmModePlaneResPtr drmModeGetPlaneResources(int) {static drmModePlaneRes r{};r.count_planes=2;r.planes=planeIds;return &r;}
extern "C" void drmModeFreePlaneResources(drmModePlaneResPtr) {}
extern "C" drmModePlanePtr drmModeGetPlane(int,uint32_t id) {
  static drmModePlane p{};p.plane_id=id;p.possible_crtcs=1;p.crtc_id=42;
  p.fb_id=id==2?(f.delay-->0?0:500):(f.occupied?77:f.attached);
  p.count_formats=f.xrgbOnly?1:2;p.formats=f.xrgbOnly?formats+1:formats;return &p;
}
extern "C" void drmModeFreePlane(drmModePlanePtr) {}
extern "C" drmModeObjectPropertiesPtr drmModeObjectGetProperties(int,uint32_t object,uint32_t) {
  static drmModeObjectProperties p{};static uint64_t values[12]{};static uint32_t crtcProps[]={60,61};
  if(object==42){p.count_props=f.atomic?2:1;p.props=crtcProps;values[0]=f.inactive?0:1;values[1]=0;}
  else {p.count_props=12;p.props=propertyIds;for(auto &v:values)v=0;values[0]=object==2?DRM_PLANE_TYPE_PRIMARY:DRM_PLANE_TYPE_OVERLAY;values[1]=object==2?f.hostZ:f.nativeZ;}
  p.prop_values=values;return &p;
}
extern "C" void drmModeFreeObjectProperties(drmModeObjectPropertiesPtr) {}
extern "C" drmModePropertyPtr drmModeGetProperty(int,uint32_t id) {
  static drmModePropertyRes p{};static uint64_t range[]={0,5};p={};p.prop_id=id;
  const char *name=id==60?"ACTIVE":id==61?"OUT_FENCE_PTR":id==10?"type":id==11?"zpos":names[id-18];
  std::strcpy(p.name,name);if(id==11&&f.order){p.flags=DRM_MODE_PROP_RANGE;p.count_values=2;p.values=range;}
  return &p;
}
extern "C" void drmModeFreeProperty(drmModePropertyPtr) {}
extern "C" int ioctl(int fd,unsigned long request,...) noexcept {
  va_list args;va_start(args,request);auto data=va_arg(args,void *);va_end(args);
  if(_IOC_TYPE(request)=='E'&&std::getenv("PENCHAT_FIXTURE_UPPER")) {
    const auto nr=_IOC_NR(request);const auto size=_IOC_SIZE(request);
    if(nr==0x09){std::memset(data,0,size);static_cast<unsigned long *>(data)[0]=1UL<<INPUT_PROP_DIRECT;return 0;}
    if(nr==0x06){std::strncpy(static_cast<char *>(data),"fixture touchscreen",size);return 0;}
    if(nr>=0x40&&nr<0x80) {
      auto axis=static_cast<input_absinfo *>(data);*axis={};axis->maximum=nr==0x40+ABS_MT_SLOT?0:4095;return 0;
    }
    if(nr==0x0a){auto values=static_cast<int *>(data);for(size_t i=1;i<size/sizeof(int);++i)values[i]=values[0]==ABS_MT_TRACKING_ID?-1:0;return 0;}
    std::memset(data,0,size);return 0;
  }
  if(request==DRM_IOCTL_MODE_GETFB2){++f.queries;auto b=static_cast<drm_mode_fb_cmd2 *>(data);if(!f.query){errno=ENOTTY;return -1;}b->pixel_format=f.argb?DRM_FORMAT_ARGB8888:DRM_FORMAT_XRGB8888;return 0;}
  if(request==DRM_IOCTL_MODE_GETPLANE){auto p=static_cast<drm_mode_get_plane *>(data);p->crtc_id=42;p->fb_id=p->plane_id==2?(f.races-->0?501:500):f.foreign?77:f.attached;return 0;}
  if(request==DRM_IOCTL_WAIT_VBLANK){if(!f.vblank){errno=EINVAL;return -1;}auto b=static_cast<drm_wait_vblank *>(data);f.context=reinterpret_cast<void *>(b->request.signal);f.isVblank=true;return 0;}
  if(request==DRM_IOCTL_MODE_CREATE_DUMB){auto b=static_cast<drm_mode_create_dumb *>(data);b->handle=++f.nextHandle;b->pitch=b->width*4;b->size=uint64_t(b->pitch)*b->height;return ftruncate(fd,1024*1024*int(b->handle+1));}
  if(request==DRM_IOCTL_MODE_MAP_DUMB){auto b=static_cast<drm_mode_map_dumb *>(data);b->offset=b->handle*1024*1024;return 0;}
  if(request==DRM_IOCTL_MODE_DESTROY_DUMB||request==DRM_IOCTL_GEM_CLOSE)return 0;
  return int(syscall(SYS_ioctl,fd,request,data));
}
extern "C" drmModeAtomicReqPtr drmModeAtomicAlloc(){f.pending=f.attached;f.pendingNative=f.nativeZ;f.pendingHost=f.hostZ;return reinterpret_cast<drmModeAtomicReqPtr>(1);}
extern "C" void drmModeAtomicFree(drmModeAtomicReqPtr) {}
extern "C" int drmModeAtomicAddProperty(drmModeAtomicReqPtr,uint32_t object,uint32_t property,uint64_t value){
  if(object==2)f.pendingHost=value;
  else if(property==20)f.pending=uint32_t(value);else if(property==11)f.pendingNative=value;
  return 0;
}
extern "C" int drmModeAtomicCommit(int,drmModeAtomicReqPtr,uint32_t flags,void *context){if(flags&DRM_MODE_ATOMIC_TEST_ONLY){if(f.rejectAtomic||f.atomicTestError){errno=f.atomicTestError?f.atomicTestError:EINVAL;return -1;}return 0;}
 if(f.hostZ!=f.pendingHost)++f.hostWrites;f.hostZ=f.pendingHost;f.nativeZ=f.pendingNative;f.attached=f.pending;++f.atomics;if(flags&DRM_MODE_PAGE_FLIP_EVENT){f.context=context;f.isVblank=false;}return 0;}
extern "C" int drmModeObjectSetProperty(int,uint32_t object,uint32_t,uint32_t,uint64_t value){if(object==2){++f.hostWrites;f.hostZ=value;}else f.nativeZ=value;return 0;}
extern "C" int drmModeSetPlane(int,uint32_t id,uint32_t,uint32_t fb,uint32_t,int32_t,int32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t){assert(id==1&&!f.foreign);++f.legacy;f.attached=fb;return 0;}
extern "C" int drmModeAddFB2(int,uint32_t,uint32_t,uint32_t format,const uint32_t *,const uint32_t *,const uint32_t *,uint32_t *fb,uint32_t){assert(format==(f.xrgbOnly?DRM_FORMAT_XRGB8888:DRM_FORMAT_ARGB8888));*fb=++f.nextFb;return 0;}
extern "C" int drmModeRmFB(int,uint32_t fb){assert(fb!=500&&fb!=77);return 0;}
extern "C" int drmHandleEvent(int fd,drmEventContextPtr context){if(f.deliver&&f.context){auto p=f.context;f.context=nullptr;if(f.isVblank)context->vblank_handler(fd,1,0,0,p);else context->page_flip_handler(fd,1,0,0,p);}return 0;}
#ifndef DRM_PROBE_LIBRARY
int main() {
  auto mode=[](camera::Overlay &o){return o.information().mode;};
  {camera::Overlay o;if(!o.open()){std::cerr<<o.lastError()<<"\n";std::abort();}assert(mode(o)==camera::CompositionMode::Underlay&&f.queries==1);}
  f={};f.inactive=true;{camera::Overlay o;assert(o.open()&&mode(o)==camera::CompositionMode::Underlay);std::vector<uint8_t> image(40*20*4);assert(!o.presentBgra(image.data(),40,20)&&o.temporarilyUnavailable()&&f.atomics==0);f.inactive=false;assert(o.presentBgra(image.data(),40,20));}
  for(auto error:{EBUSY,EAGAIN,EACCES,EPERM}) {f={};f.atomicTestError=error;camera::Overlay o;assert(o.open()&&mode(o)==camera::CompositionMode::Underlay&&o.information().atomic&&f.legacy==0);}
  f={};f.delay=2;{camera::Overlay o;assert(o.open()&&f.queries==1&&o.information().attempts>=3);}
  f={};f.races=1;{camera::Overlay o;assert(o.open()&&f.queries==2);}
  f={};f.argb=false;{camera::Overlay o;assert(o.open()&&mode(o)==camera::CompositionMode::UpperOverlay);assert(o.information().format==DRM_FORMAT_XRGB8888);
    o.setHole({0,0,40,20,100,80});std::vector<uint8_t> image(40*20*4,5);
    assert(o.presentBgra(image.data(),40,20)&&f.atomics==1&&f.hostWrites==0);
    o.disable();assert(f.attached==0&&f.hostWrites==0);
  }
  f={};f.query=false;{camera::Overlay o;assert(o.open()&&mode(o)==camera::CompositionMode::UpperOverlay);assert(o.information().queryError==ENOTTY);}
  f={};f.occupied=true;{camera::Overlay o;assert(!o.open()&&o.information().stage=="no_free_rgb_plane");}
  f={};f.order=false;{camera::Overlay o;assert(!o.open()&&o.information().stage=="host_order_unknown");}
  f={};f.atomic=false;f.xrgbOnly=true;
  {camera::Overlay o;o.setHole({0,0,40,20,100,80});assert(o.open()&&mode(o)==camera::CompositionMode::UpperOverlay);assert(!o.information().atomic);
    std::vector<uint8_t> image(40*20*4,91);assert(o.presentBgra(image.data(),40,20));
    assert(f.legacy==1&&f.hostWrites==0&&o.flipPending_);
    f.deliver=false;auto cur=o.cur_;assert(!o.presentBgra(image.data(),40,20)&&cur==o.cur_&&f.legacy==1);
    o.disable();assert(o.flipPending_&&f.legacy==1);
    f.deliver=true;assert(o.presentBgra(image.data(),40,20)&&f.legacy==2);
    o.disable();assert(!o.presented_&&!o.flipPending_&&f.attached==0&&f.hostWrites==0);
  }
  f={};f.rejectAtomic=true;{camera::Overlay o;assert(o.open()&&mode(o)==camera::CompositionMode::UpperOverlay&&!o.information().atomic&&f.atomics==0);}
  f={};f.atomic=false;f.vblank=false;{camera::Overlay o;assert(!o.open()&&f.legacy==0&&o.information().stage=="completion_unavailable");}
  f={};f.atomic=false;{camera::Overlay o;o.setHole({0,0,40,20,100,80});assert(o.open());std::vector<uint8_t> image(40*20*4,0);assert(o.presentBgra(image.data(),40,20));assert(o.finishPendingFlip(true));f.foreign=true;const int submits=f.legacy;assert(!o.presentBgra(image.data(),40,20));o.disable();assert(f.legacy==submits);}
  f={};f.delay=100000;{
    camera::Overlay o;const auto start=std::chrono::steady_clock::now();assert(!o.open());
    assert(o.information().stage=="primary_not_ready");
    assert(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(1700));
  }
  f={};f.races=100000;{camera::Overlay o;assert(!o.open()&&o.information().stage=="primary_changed");}
  std::cout<<"DRM_REAL_INITIALIZATION_COMPAT_MODES_AND_BUFFER_RETIREMENT_OK\n";
}

#endif
