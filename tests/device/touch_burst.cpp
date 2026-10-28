// Explicit-device, bounded evdev injector for alternating-slot touch regression.
#include <linux/input.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <vector>
static bool emit(int fd,int type,int code,int value) {
  input_event e={};gettimeofday(&e.time,nullptr);e.type=type;e.code=code;e.value=value;
  return write(fd,&e,sizeof(e))==sizeof(e);
}
int main(int argc,char **argv) {
  if(argc!=10)return 2;
  const int x[]={atoi(argv[2]),atoi(argv[4])},y[]={atoi(argv[3]),atoi(argv[5])};
  const int down=atoi(argv[6]),gap=atoi(argv[7]),count=atoi(argv[8]),slots=atoi(argv[9]);
  if(down<0||down>1000||gap<0||gap>1000||count<1||count>1000||slots<1||slots>4)return 2;
  int fd=open(argv[1],O_RDWR|O_CLOEXEC);if(fd<0)return 3;
  input_absinfo ax={},ay={},as={};
  if(ioctl(fd,EVIOCGABS(ABS_MT_SLOT),&as)||as.minimum!=0||as.maximum<slots-1||as.maximum>31||
     ioctl(fd,EVIOCGABS(ABS_MT_POSITION_X),&ax)||ioctl(fd,EVIOCGABS(ABS_MT_POSITION_Y),&ay))return 3;
  std::vector<int> ids(as.maximum+2,-1);ids[0]=ABS_MT_TRACKING_ID;
  if(ioctl(fd,EVIOCGMTSLOTS(ids.size()*sizeof(int)),ids.data()))return 3;
  for(size_t i=1;i<ids.size();++i)if(ids[i]>=0)return 4; // Never interrupt a held finger.
  for(int i=0;i<2;++i)if(x[i]<ax.minimum||x[i]>ax.maximum||y[i]<ay.minimum||y[i]>ay.maximum)return 2;
  bool ok=true;
  for(int i=0;i<count&&ok;++i) {
    const int slot=i%slots;
    ok=emit(fd,EV_ABS,ABS_MT_SLOT,slot)&&emit(fd,EV_ABS,ABS_MT_TRACKING_ID,1000+i)&&
       emit(fd,EV_ABS,ABS_MT_POSITION_X,x[i%2])&&emit(fd,EV_ABS,ABS_MT_POSITION_Y,y[i%2])&&
       emit(fd,EV_ABS,ABS_MT_TOUCH_MAJOR,10)&&emit(fd,EV_ABS,ABS_MT_WIDTH_MAJOR,10)&&
       emit(fd,EV_KEY,BTN_TOUCH,1)&&emit(fd,EV_SYN,SYN_REPORT,0);
    usleep(down*1000);
    // Attempt every release even if an earlier event failed.
    const bool a=emit(fd,EV_ABS,ABS_MT_SLOT,slot),b=emit(fd,EV_ABS,ABS_MT_TRACKING_ID,-1),
      c=emit(fd,EV_ABS,ABS_MT_TOUCH_MAJOR,0),d=emit(fd,EV_ABS,ABS_MT_WIDTH_MAJOR,0),
      e=emit(fd,EV_KEY,BTN_TOUCH,0),f=emit(fd,EV_SYN,SYN_REPORT,0);
    ok=ok&&a&&b&&c&&d&&e&&f;usleep(gap*1000);
  }
  close(fd);return ok?0:3;
}
