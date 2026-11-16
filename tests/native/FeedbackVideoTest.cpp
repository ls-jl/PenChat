#include "feedback.hpp"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace penchat;
int main() {
  char pattern[]="/tmp/penchat-feedback-video-XXXXXX";
  const char *root=mkdtemp(pattern);assert(root);
  const std::string base=root,sys=base+"/sys",dev=base+"/dev";
  assert(!mkdir(sys.c_str(),0700));assert(!mkdir(dev.c_str(),0700));
  assert(!mkdir((sys+"/video0").c_str(),0700));
  {std::ofstream f(sys+"/video0/name");f<<"CVI ISP fixture";}
  {std::ofstream f(dev+"/video0");f<<"not a real device";}
  const int watch=inotify_init1(IN_NONBLOCK|IN_CLOEXEC);assert(watch>=0);
  assert(inotify_add_watch(watch,(dev+"/video0").c_str(),IN_OPEN)>=0);
  const auto metadata=feedbackVideoDevices(false,sys,dev);
  assert(metadata.size()==1&&metadata[0]["name"]=="CVI ISP fixture");
  assert(metadata[0]["status"]=="metadata_only"&&metadata[0]["probe_status"]=="not_opened");
  char events[4096];assert(read(watch,events,sizeof(events))<0&&errno==EAGAIN);
  // Positive control: prove the watch detects the probing path. No real video
  // hardware is opened by this test; ioctl on the fixture fails normally.
  const auto probed=feedbackVideoDevices(true,sys,dev);assert(probed.size()==1);
  assert(read(watch,events,sizeof(events))>0);
  close(watch);unlink((dev+"/video0").c_str());unlink((sys+"/video0/name").c_str());
  rmdir((sys+"/video0").c_str());rmdir(sys.c_str());rmdir(dev.c_str());rmdir(base.c_str());
}
