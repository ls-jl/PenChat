#include <cstdlib>
#include <sys/ioctl.h>
extern "C" int vendorCommand(const char *text) { return std::system(text); }
extern "C" int vendorIoctl(int fd, unsigned long command, void *p) {
  return ioctl(fd, command, p);
}
extern "C" void *vendorIoctlTarget() {
  return reinterpret_cast<void *>(&ioctl);
}
