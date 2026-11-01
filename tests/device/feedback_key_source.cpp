// Device bench fixture: emit via our own disposable uinput device, never write
// through an evdev reader while the production collector owns its grab.
#include <linux/uinput.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
int main(int argc,char **argv){
 if(argc!=3)return 2;
 const std::string mode=argv[1],path=argv[2];
 if(mode=="--physical") {
  if(path.find("/dev/input/")!=0)return 2;int fd=open(path.c_str(),O_WRONLY|O_CLOEXEC);if(fd<0)return 1;
  bool ok=true;for(int value:{1,0}){input_event e[2]{};e[0].type=EV_KEY;e[0].code=102;e[0].value=value;e[1].type=EV_SYN;e[1].code=SYN_REPORT;ok=ok&&write(fd,e,sizeof(e))==sizeof(e);usleep(100000);}close(fd);puts(ok?"sent":"failed");return ok?0:1;
 }
 if(mode=="--probe"){
  if(path.find("/dev/input/")!=0)return 2;int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)return 1;
  int result=ioctl(fd,EVIOCGRAB,1),error=errno;if(!result)ioctl(fd,EVIOCGRAB,0);close(fd);
  puts(result==0?"free":error==EBUSY?"busy":"error");return result==0||error==EBUSY?0:1;
 }
 if(path.find("/tmp/penchat-feedback-source-")!=0||path.size()>=100)return 2;
 sockaddr_un addr{};addr.sun_family=AF_UNIX;strncpy(addr.sun_path,path.c_str(),sizeof(addr.sun_path)-1);
 int socket=::socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(socket<0)return 1;
 if(mode!="--server"){
  const char *text=mode=="--scan"?"scan":mode=="--home"?"home":mode=="--quit"?"quit":nullptr;if(!text)return 2;
  if(connect(socket,reinterpret_cast<sockaddr *>(&addr),sizeof(addr)))return 1;
  write(socket,text,strlen(text));pollfd p{socket,POLLIN,0};char reply[32]{};
  bool ok=poll(&p,1,2000)>0&&read(socket,reply,31)>0&&!strcmp(reply,"ok");close(socket);return ok?0:1;
 }
 int input=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);if(input<0)return 1;
 ioctl(input,UI_SET_EVBIT,EV_KEY);ioctl(input,UI_SET_KEYBIT,102);ioctl(input,UI_SET_KEYBIT,134);
 uinput_user_dev dev{};snprintf(dev.name,sizeof(dev.name),"PenChat-feedback-source-%d",getpid());dev.id.bustype=BUS_USB;dev.id.vendor=0xdead;dev.id.product=2;
 if(write(input,&dev,sizeof(dev))!=sizeof(dev)||ioctl(input,UI_DEV_CREATE)||bind(socket,reinterpret_cast<sockaddr *>(&addr),sizeof(addr))||listen(socket,2))return 1;
 printf("{\"pid\":%d,\"ready\":true}\n",getpid());fflush(stdout);pollfd p{socket,POLLIN,0};
 while(poll(&p,1,90000)>0){
  int client=accept(socket,nullptr,nullptr);if(client<0)continue;char command[16]{};read(client,command,15);
  if(!strcmp(command,"quit")){write(client,"ok",3);close(client);break;}
  int code=!strcmp(command,"home")?102:!strcmp(command,"scan")?134:0;
  if(code){for(int value:{1,0}){input_event events[2]{};events[0].type=EV_KEY;events[0].code=code;events[0].value=value;events[1].type=EV_SYN;events[1].code=SYN_REPORT;write(input,events,sizeof(events));usleep(100000);}write(client,"ok",3);}close(client);
 }
 close(socket);ioctl(input,UI_DEV_DESTROY);close(input);unlink(path.c_str());return 0;
}
