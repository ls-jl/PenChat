// Uses only disposable uinput nodes, never existing workstation/pen buttons.
#include "feedback.hpp"
#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace penchat;
namespace {
void require(bool ok,const char *text){if(!ok)throw std::runtime_error(text);}
struct VirtualKey {
  int writer=-1,reader=-1;std::string path,name;
  explicit VirtualKey(int n,bool pointer=false,bool keyboard=false) {
    writer=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
    require(writer>=0,"uinput unavailable");
    name="PenChat-feedback-test-"+std::to_string(getpid())+"-"+std::to_string(n);
    try {
      require(!ioctl(writer,UI_SET_EVBIT,EV_KEY),"EV_KEY failed");
      if(keyboard){for(unsigned key:{unsigned(KEY_A),unsigned(KEY_Z),unsigned(KEY_SPACE)})require(!ioctl(writer,UI_SET_KEYBIT,key),"key bit failed");}
      else require(!ioctl(writer,UI_SET_KEYBIT,BTN_TRIGGER_HAPPY40),"key bit failed");
      if(pointer){require(!ioctl(writer,UI_SET_EVBIT,EV_ABS),"ABS failed");require(!ioctl(writer,UI_SET_ABSBIT,ABS_X),"axis failed");}
      uinput_user_dev device{};strncpy(device.name,name.c_str(),sizeof(device.name)-1);device.id.bustype=BUS_USB;device.id.vendor=0xdead;device.id.product=1;device.id.version=1;device.absmax[ABS_X]=1024;
      require(write(writer,&device,sizeof(device))==sizeof(device)&&!ioctl(writer,UI_DEV_CREATE),"uinput create failed");
      const auto end=ticks()+3000;
      while(ticks()<end&&reader<0) {
        auto dir=opendir("/sys/class/input");require(dir,"sysfs input missing");
        while(auto entry=readdir(dir)) {
          const std::string key=entry->d_name;if(key.find("event")!=0)continue;
          auto title=fileText("/sys/class/input/"+key+"/device/name",256);if(!title.empty()&&title.back()=='\n')title.pop_back();
          if(title==name){path="/dev/input/"+key;reader=open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);break;}
        }
        closedir(dir);if(reader<0)usleep(10000);
      }
      require(reader>=0,"private uinput node needs read permission");
    } catch(...) {closeAll();throw;}
  }
  void closeAll(){if(reader>=0){close(reader);reader=-1;}if(writer>=0){ioctl(writer,UI_DEV_DESTROY);close(writer);writer=-1;}}
  ~VirtualKey(){closeAll();}
  void emit(int value){input_event e[2]{};e[0].type=EV_KEY;e[0].code=BTN_TRIGGER_HAPPY40;e[0].value=value;e[1].type=EV_SYN;e[1].code=SYN_REPORT;require(write(writer,e,sizeof(e))==sizeof(e),"emit failed");usleep(10000);}
  void free(){require(!ioctl(reader,EVIOCGRAB,1),"key grab was not released");require(!ioctl(reader,EVIOCGRAB,0),"test release failed");}
  int count(){input_event events[64];int total=0;ssize_t bytes;while((bytes=read(reader,events,sizeof(events)))>0)for(size_t i=0;i<size_t(bytes)/sizeof(input_event);++i)total+=events[i].type==EV_KEY;return total;}
};
struct Capture {
  std::mutex mutex;Json latest=Json::object();uint64_t generation=0;FeedbackKeys keys;
  explicit Capture():keys([this](Json s){std::lock_guard<std::mutex> lock(mutex);latest=std::move(s);}){}
  Json state(){std::lock_guard<std::mutex> lock(mutex);return latest;}
  void start(){++generation;keys.start("home",generation);}
  Json wait(const char *status){const auto end=ticks()+4000;while(ticks()<end){auto s=state();if(s.value("generation",uint64_t(0))==generation&&s.value("status",std::string())==status)return s;usleep(5000);}throw std::runtime_error("capture status timeout: "+state().dump());}
};
}
int main() {
  // Unprivileged host CI skips kernel-device coverage; both rooted pen targets
  // run this exact executable. No chmod, sudo or real-device ACL changes.
  char folder[]="/tmp/penchat-key-test-XXXXXX";require(mkdtemp(folder),"mkdtemp failed");
  const auto config=std::string(folder)+"/config.json";setenv("PENCHAT_TEST_FEEDBACK_KEYS",config.c_str(),1);
  try {
    std::unique_ptr<VirtualKey> a;
    try{a.reset(new VirtualKey(1));}catch(const std::exception &e){std::cerr<<e.what()<<'\n';unlink(config.c_str());rmdir(folder);return 77;}
    VirtualKey b(2),pointer(3,true),keyboard(4,false,true);
    auto configure=[&](Json paths,int limit=1000){atomicText(config,Json{{"paths",paths},{"timeout_ms",limit}}.dump());};
    configure({a->path,b.path});
    {
      Capture c;c.start();auto s=c.wait("listening");require(s["blocking"]==true&&s["limit_ms"]==30000,"wrong capture limit");
      require(ioctl(a->reader,EVIOCGRAB,1)<0&&errno==EBUSY,"normal reader can grab during capture");
      a->emit(1);a->emit(0);s=c.wait("complete");require(!s["blocking"].get<bool>()&&s["elapsed_ms"].get<int>()<1000,"not released early");require(s["events"].size()==2&&a->count()==0,"events escaped grab");a->free();b.free();
      a->emit(1);a->emit(0);require(a->count()==2,"normal events did not resume");
      c.start();c.wait("listening");c.keys.cancel();c.keys.join();require(c.state()["status"]=="cancelled","cancel status");a->free();b.free();
    }
    configure({a->path},180);
    {Capture c;c.start();c.wait("listening");auto s=c.wait("timeout");require(!s["blocking"].get<bool>(),"timeout grab leak");a->free();c.start();c.wait("listening");a->emit(1);s=c.wait("timeout");require(s["release_missing"]==true,"held timeout not recorded");a->free();a->emit(0);a->count();}
    configure({a->path,b.path});require(!ioctl(b.reader,EVIOCGRAB,1),"busy fixture");
    {Capture c;c.start();c.wait("grab_failed");a->free();}ioctl(b.reader,EVIOCGRAB,0);
    configure({a->path,"/dev/input/penchat-no-such-node"});{Capture c;c.start();c.wait("unavailable");a->free();}
    configure({a->path,b.path});
    a->emit(1);{Capture c;c.start();c.wait("key_held");a->free();}a->emit(0);a->count();
    configure({a->path,pointer.path,keyboard.path});
    {Capture c;c.start();auto s=c.wait("listening");require(s["devices"][1]["status"]=="excluded_pointer"&&s["devices"][2]["status"]=="excluded_keyboard","unsafe capture scope");pointer.free();keyboard.free();}
    a->free();configure({a->path,b.path});int signal[2];require(!pipe(signal),"pipe failed");const auto child=fork();require(child>=0,"fork failed");
    if(!child){close(signal[0]);FeedbackKeys keys([&](Json s){if(s["status"]=="listening")write(signal[1],"R",1);});keys.start("home",1);for(;;)pause();}
    close(signal[1]);char ready=0;pollfd waiting{signal[0],POLLIN,0};const bool started=poll(&waiting,1,4000)>0&&read(signal[0],&ready,1)==1&&ready=='R';close(signal[0]);kill(child,SIGKILL);waitpid(child,nullptr,0);require(started,"child did not capture");a->free();b.free();
    unlink(config.c_str());rmdir(folder);std::cout<<"Kernel key leases: suppression, early release, timeout/held, cancellation, busy rollback, held start, pointer/keyboard exclusion and process-death release passed.\n";return 0;
  }catch(const std::exception &e){unlink(config.c_str());rmdir(folder);std::cerr<<e.what()<<'\n';return 1;}
}
