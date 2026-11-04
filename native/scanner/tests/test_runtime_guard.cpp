#include <cassert>
#include <cerrno>
#include <dlfcn.h>
int main(int argc, char **argv) {
  assert(argc == 2);
  void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  assert(library);
  auto command = reinterpret_cast<int (*)(const char *)>(dlsym(library, "vendorCommand"));
  assert(command);
  assert(command(nullptr) == 0);
  errno = 0;
  assert(command("exit 0") == -1 && errno == ENOTSUP);
  // A real shell would return success; the vendor import must bind our stub.
  dlclose(library);
}
