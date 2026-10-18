#include "file_read.hpp"
#include <stdexcept>
#include <sys/wait.h>
int main() {
  using penchat_scan::readOptionalFile;
  if (readOptionalFile("/proc/self/comm").empty())
    throw std::runtime_error("live process must be readable");
  // Opening succeeds but reading offset zero fails with EIO on Linux. The
  // former stream-iterator implementation threw from this kind of read.
  if (!readOptionalFile("/proc/self/mem").empty())
    throw std::runtime_error("failed reads must not return partial content");
  for (int n = 0; n < 100; ++n) {
    const pid_t child = fork();
    if (child < 0)
      throw std::runtime_error("fork failed");
    if (!child)
      _exit(0);
    const auto path = "/proc/" + std::to_string(child) + "/cmdline";
    readOptionalFile(path);
    waitpid(child, nullptr, 0);
    if (!readOptionalFile(path).empty())
      throw std::runtime_error("exited process must be unavailable");
  }
}
