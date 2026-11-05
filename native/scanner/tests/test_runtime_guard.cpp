#include <cassert>
#include <cerrno>
#include <dlfcn.h>
#include "runtime_io.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
int main(int argc, char **argv) {
  assert(argc == 2);
  void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  assert(library);
  auto command = reinterpret_cast<int (*)(const char *)>(dlsym(library, "vendorCommand"));
  assert(command);
  assert(command(nullptr) == 0);
  errno = 0;
  assert(command("exit 0") == -1 && errno == ENOTSUP);
  auto request = reinterpret_cast<int (*)(int, unsigned long, void *)>(dlsym(library, "vendorIoctl"));
  assert(request);
  auto target = reinterpret_cast<void *(*)()>(dlsym(library, "vendorIoctlTarget"));
  assert(target && target() == reinterpret_cast<void *>(&ioctl));
  int fd = open("/dev/null", O_RDONLY);
  assert(fd >= 0);
  penchat_scan::NpuSync sync{1, 0, 1, 0, 62914560};
  for (bool enabled : {false, true}) {
    penchat_scan::RuntimeIoScope scope(enabled);
    errno = 0;
    assert(request(fd, penchat_scan::NpuSyncRequest, &sync) == -1 && errno == ENOTTY);
    assert(penchat_scan::runtimeIoMetrics().operations == 0); // Wrong device: no slicing.
  }
  close(fd);
  // A real shell would return success; the vendor import must bind our stub.
  dlclose(library);
}
