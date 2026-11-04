#include <cerrno>

// Exported by the scanner executable so the vendor runtime's system import
// resolves here before libc. Never inspect/log the argument or start a process.
extern "C" int system(const char *command) {
  if (!command)
    return 0;
  errno = ENOTSUP;
  return -1;
}
