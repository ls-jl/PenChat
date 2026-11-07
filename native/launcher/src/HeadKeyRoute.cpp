#include "HeadKeyRoute.hpp"
#include <dirent.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#ifndef EVIOCGMASK
struct input_mask {uint32_t type,codes_size;uint64_t codes_ptr;};
#define EVIOCGMASK _IOR('E',0x92,struct input_mask)
#define EVIOCSMASK _IOW('E',0x93,struct input_mask)
#endif
namespace penchat {
namespace {
bool maskIoctl(int fd,unsigned long command,input_mask &mask) {
  int result;do {result=ioctl(fd,command,&mask);}while(result<0&&errno==EINTR);
  return result==0;
}
}
HeadKeyRoute::System HeadKeyRoute::realSystem() {
  return {
    [] {
      std::vector<int> result;
      std::ifstream model("/proc/device-tree/model");std::string identity;
      std::getline(model,identity,'\0');
      if(identity.find("RK3562 Y07")==std::string::npos)return result;
      auto directory=opendir("/proc/self/fd");if(!directory)return result;
      while(auto entry=readdir(directory)) {
        char *end=nullptr;const long fd=strtol(entry->d_name,&end,10);
        if(!end||*end||fd<0||fd>65535)continue;
        char name[128]{};unsigned char bits[96]{};
        if(ioctl(int(fd),EVIOCGNAME(sizeof(name)),name)>=0&&std::string(name)=="led_control"&&
            ioctl(int(fd),EVIOCGBIT(EV_KEY,sizeof(bits)),bits)>=0&&(bits[134/8]&(1u<<(134%8))))result.push_back(int(fd));
      }
      closedir(directory);return result;
    },
    [](int fd){return fcntl(fd,F_DUPFD_CLOEXEC,0);},
    [](int fd,Mask &mask){input_mask m{EV_KEY,uint32_t(mask.size()),uint64_t(reinterpret_cast<uintptr_t>(mask.data()))};return maskIoctl(fd,EVIOCGMASK,m);},
    [](int fd,const Mask &mask){input_mask m{EV_KEY,uint32_t(mask.size()),uint64_t(reinterpret_cast<uintptr_t>(mask.data()))};return maskIoctl(fd,EVIOCSMASK,m);},
    [](int fd){::close(fd);}
  };
}
}
