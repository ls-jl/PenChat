#include "image_input.hpp"
#include <fcntl.h>
#include <unistd.h>
#ifdef __aarch64__
#include <linux/input.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#endif
namespace penchat {
void DirectImageInput::close(){if(fd_>=0)::close(fd_);fd_=-1;slots_.clear();lastCount_=0;slot_=0;waitClear_=false;}
void DirectImageInput::resync(){
#ifdef __aarch64__
  if(fd_<0)return;
  std::vector<int> ids(slots_.size()+1,-1);ids[0]=ABS_MT_TRACKING_ID;
  if(ioctl(fd_,EVIOCGMTSLOTS(ids.size()*sizeof(int)),ids.data())<0){waitClear_=true;return;}
  waitClear_=false;
  for(size_t i=0;i<slots_.size();++i){slots_[i].id=ids[i+1];waitClear_|=slots_[i].id>=0;}
  for(int code:{ABS_MT_POSITION_X,ABS_MT_POSITION_Y}){
    ids[0]=code;
    if(ioctl(fd_,EVIOCGMTSLOTS(ids.size()*sizeof(int)),ids.data())>=0)
      for(size_t i=0;i<slots_.size();++i){if(code==ABS_MT_POSITION_X)slots_[i].x=ids[i+1];else slots_[i].y=ids[i+1];}
  }
  input_absinfo selected{};if(ioctl(fd_,EVIOCGABS(ABS_MT_SLOT),&selected)>=0)slot_=selected.value;
#endif
}
void DirectImageInput::open(){
#ifdef __aarch64__
  for(int i=0;i<32;++i){
    char path[64];snprintf(path,sizeof(path),"/dev/input/event%d",i);
    const int candidate=::open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(candidate<0)continue;
    unsigned long props[2]={};input_absinfo slot{},x{},y{},id{};
    const bool direct=ioctl(candidate,EVIOCGPROP(sizeof(props)),props)>=0 && (props[0]&(1UL<<INPUT_PROP_DIRECT));
    if(direct && ioctl(candidate,EVIOCGABS(ABS_MT_SLOT),&slot)>=0 && slot.minimum==0 && slot.maximum>=1 && slot.maximum<32 &&
       ioctl(candidate,EVIOCGABS(ABS_MT_POSITION_X),&x)>=0 && ioctl(candidate,EVIOCGABS(ABS_MT_POSITION_Y),&y)>=0 &&
       ioctl(candidate,EVIOCGABS(ABS_MT_TRACKING_ID),&id)>=0){
      fd_=candidate;slots_.resize(slot.maximum+1);resync();return;
    }
    ::close(candidate);
  }
#endif
}
std::vector<Json> DirectImageInput::poll(bool enabled){
  std::vector<Json> out;
  if(!enabled){close();return out;}
  if(fd_<0)open();
#ifdef __aarch64__
  if(fd_<0)return out;
  input_event events[64];
  for(int batch=0;batch<8;++batch){
    const auto n=::read(fd_,events,sizeof(events));
    if(n<0){if(errno==EINTR)continue;if(errno!=EAGAIN&&errno!=EWOULDBLOCK){out.push_back({{"type","touch"},{"source","direct-image"},{"cancelled",true},{"pressed",false}});close();}break;}
    if(n==0){close();break;}
    for(size_t i=0;i<size_t(n)/sizeof(input_event);++i){
      const auto &e=events[i];
      if(e.type==EV_ABS){
        if(e.code==ABS_MT_SLOT)slot_=e.value;
        else if(slot_>=0 && size_t(slot_)<slots_.size()){
          auto &s=slots_[slot_];
          if(e.code==ABS_MT_TRACKING_ID)s.id=e.value;
          else if(e.code==ABS_MT_POSITION_X)s.x=e.value;
          else if(e.code==ABS_MT_POSITION_Y)s.y=e.value;
        }
      } else if(e.type==EV_SYN && e.code==SYN_DROPPED){
        out.push_back({{"type","touch"},{"source","direct-image"},{"cancelled",true},{"pressed",false}});resync();waitClear_=true;
      } else if(e.type==EV_SYN && e.code==SYN_REPORT){
        Json points=Json::array();
        for(const auto &s:slots_)if(s.id>=0){
          const auto logical=logicalImageTouch(screen_,s.x,s.y);
          points.push_back({{"id",s.id},{"x",logical.first},{"y",logical.second}});
        }
        if(waitClear_){if(points.empty()){waitClear_=false;lastCount_=0;}continue;}
        if(!lastCount_ && !points.empty())++serial_;
        const int count=int(points.size());
        if(count || lastCount_)out.push_back({{"type","touch"},{"source","direct-image"},{"gesture",serial_},{"cancelled",false},
          {"pressed",count>0},{"x",count?points[0]["x"]:Json(0)},{"y",count?points[0]["y"]:Json(0)},{"points",points}});
        lastCount_=count;
      }
    }
  }
#endif
  return out;
}
} // namespace penchat
