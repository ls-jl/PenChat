// Opt-in native device probe. No database, network, input grabs or saved photos.
#include "common.hpp"
#include "overlay.h"
#include "camera_logic.h"
#include "capture.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm.h>
#include <drm_mode.h>
#include <memory>
#include <iostream>
#include <stdexcept>
using penchat::Json;
static void need(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
static uint32_t planeFb(int fd, uint32_t id) {
  drm_mode_get_plane p = {}; p.plane_id = id;
  need(ioctl(fd, DRM_IOCTL_MODE_GETPLANE, &p) == 0, "GETPLANE failed"); return p.fb_id;
}
static size_t checkPixels(int fd, uint32_t plane, const std::vector<uint8_t> &expected, int w, int h) {
  drm_mode_fb_cmd fb = {}; fb.fb_id = planeFb(fd, plane);
  need(fb.fb_id && ioctl(fd, DRM_IOCTL_MODE_GETFB, &fb) == 0, "GETFB failed");
  need(fb.bpp == 32 && int(fb.width) >= w && int(fb.height) == h && fb.handle, "unexpected framebuffer format");
  drm_mode_map_dumb map = {}; map.handle = fb.handle;
  need(ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) == 0, "MAP_DUMB failed");
  const size_t size = size_t(fb.pitch) * fb.height;
  auto p = static_cast<uint8_t *>(mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, map.offset));
  need(p != MAP_FAILED, "cannot read own framebuffer");
  size_t different = 0;
  for (int y = 0; y < h; ++y) for (int x = 0; x < w*4; ++x)
    different += p[size_t(y)*fb.pitch+x] != expected[size_t(y)*w*4+x];
  munmap(p,size); drm_gem_close release = {}; release.handle = fb.handle;
  ioctl(fd, DRM_IOCTL_GEM_CLOSE, &release);
  return different;
}
struct VideoState {
  std::string path; v4l2_format format = {}; v4l2_selection crop = {};
  bool valid = false, hasCrop = false, safe = false, restored = false;
  explicit VideoState(std::string p) : path(std::move(p)) {
    int fd = open(path.c_str(), O_RDWR|O_CLOEXEC); if (fd < 0) return;
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd,VIDIOC_G_FMT,&format)) {format={};format.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;}
    valid = ioctl(fd,VIDIOC_G_FMT,&format)==0;
    crop.type=format.type; crop.target=V4L2_SEL_TGT_CROP;
    hasCrop=ioctl(fd,VIDIOC_G_SELECTION,&crop)==0;
    auto bounds=crop;bounds.target=V4L2_SEL_TGT_CROP_BOUNDS;
    safe=valid&&(hasCrop||ioctl(fd,VIDIOC_G_SELECTION,&bounds)!=0);close(fd);
  }
  static Json dimensions(const v4l2_format &f) {
    if(f.type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
      return {f.fmt.pix_mp.width,f.fmt.pix_mp.height,f.fmt.pix_mp.pixelformat};
    return {f.fmt.pix.width,f.fmt.pix.height,f.fmt.pix.pixelformat};
  }
  bool restore() {
    if(restored||!valid)return true;
    int fd=open(path.c_str(),O_RDWR|O_CLOEXEC);if(fd<0)return false;
    auto current=format;bool ok=ioctl(fd,VIDIOC_G_FMT,&current)==0;
    if(ok&&dimensions(current)!=dimensions(format)){auto original=format;ok=ioctl(fd,VIDIOC_S_FMT,&original)==0;}
    if(hasCrop){auto original=crop;ok=(ioctl(fd,VIDIOC_S_SELECTION,&original)==0)&&ok;}
    current=format;ok=(ioctl(fd,VIDIOC_G_FMT,&current)==0&&dimensions(current)==dimensions(format))&&ok;
    if(hasCrop){auto actual=crop;ok=(ioctl(fd,VIDIOC_G_SELECTION,&actual)==0&&actual.r.left==crop.r.left&&
      actual.r.top==crop.r.top&&actual.r.width==crop.r.width&&actual.r.height==crop.r.height)&&ok;}
    close(fd);restored=ok;return ok;
  }
  ~VideoState(){if(!restored)restore();}
};
int main() {
  try {
    const auto screen=penchat::Screen::fromConfig(Json::parse(penchat::fileText("/etc/miniapp/resources/cfg.json")));
    auto first=std::unique_ptr<camera::Overlay>(new camera::Overlay);
    auto hole=camera::HoleRect(0,0,screen.width,screen.height,screen.width,screen.height,
                              screen.rotation,0,0,screen.xoffset,screen.yoffset);
    first->setHole(hole);need(first->open(),"overlay unavailable");
    int fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);need(fd>=0,"cannot inspect DRM");
    const auto plane=first->planeId();
    if(planeFb(fd,plane)){close(fd);std::cout<<"SKIP: selected overlay is already occupied\n";return 77;}
    std::vector<uint8_t> bgra(size_t(screen.width)*screen.height*4),rgba(bgra.size());
    for(size_t i=0;i<bgra.size();i+=4){bgra[i]=uint8_t(i*7);bgra[i+1]=uint8_t(i*11+1);bgra[i+2]=uint8_t(i*13+2);bgra[i+3]=255;}
    for(int tries=0;!first->presentBgra(bgra.data(),screen.width,screen.height);++tries){need(tries<20,"present failed");usleep(20000);}
    usleep(100000);int x,y,w,h;first->mapToCrtc(&x,&y,&w,&h);
    const auto fit=camera::fitPhysicalViewport({x,y,w,h,true},screen.width,screen.height,screen.rotation,false);
    std::vector<uint8_t> expected(size_t(w)*h*4);camera::rgbaToBgra(bgra.data(),rgba.data(),bgra.size()/4);
    camera::resampleRgbaViewport(rgba.data(),screen.width,screen.height,expected.data(),w,h,fit.left-x,fit.top-y,fit.width,fit.height,screen.rotation);
    camera::rgbaToBgra(expected.data(),expected.data(),size_t(w)*h);
    const auto different=checkPixels(fd,plane,expected,w,h);need(!different,"DRM pixels differ from previous upload path");
    auto second=std::unique_ptr<camera::Overlay>(new camera::Overlay);second->setHole(hole);need(second->open(),"second overlay open");
    const auto firstFb=planeFb(fd,plane);second.reset();
    need(planeFb(fd,plane)==firstFb,"unpresented overlay destructor cleared another owner");
    second.reset(new camera::Overlay);second->setHole(hole);need(second->open(),"replacement overlay open");
    for(int tries=0;!second->presentBgra(bgra.data(),screen.width,screen.height);++tries){need(tries<20,"replacement present failed");usleep(20000);}
    usleep(100000);const auto secondFb=planeFb(fd,plane);need(secondFb!=firstFb,"replacement did not switch framebuffer");
    first.reset();need(planeFb(fd,plane)==secondFb,"old owner cleared replacement framebuffer");
    second.reset();usleep(100000);need(!planeFb(fd,plane),"own overlay not released");close(fd);
    VideoState preview("/dev/video30"),photo("/dev/video29");
    Json cameraResult={{"tested",false}};
    if(preview.safe&&photo.safe){
      camera::Capture capture;need(!capture.open("/dev/penchat-no-camera",960,720),"explicit missing node fell back to another device");
      need(capture.open("/dev/video30",960,720),"preview capture failed");
      camera::CaptureFrame frame;bool got=false;
      for(int n=0;n<15&&!got;++n)got=capture.grab(&frame,200);
      need(got,"preview produced no frame");cameraResult={{"tested",true},{"preview",{frame.width,frame.height}}};
      need(capture.release(frame.index),"preview buffer release failed");capture.close();
      if(penchat::fileText("/proc/device-tree/model",4096).find("RK3562 Y07 ")!=std::string::npos){
        need(capture.open("/dev/video29",3264,2448),"HD capture failed");got=false;
        for(int n=0;n<15&&!got;++n)got=capture.grab(&frame,200);
        need(got&&frame.width==3264&&frame.height==2448,"HD frame dimensions wrong");
        cameraResult["photo"]={frame.width,frame.height};capture.release(frame.index);capture.close();
      }
    }else cameraResult["reason"]="cannot snapshot camera format/crop safely";
    need(preview.restore()&&photo.restore(),"camera state restoration failed");
    std::cout<<Json{{"screen",screen.descriptor()},{"drm_pixel_differences",different},
      {"ownership_checks",true},{"camera",cameraResult},{"restored",true}}.dump()<<'\n';return 0;
  }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
