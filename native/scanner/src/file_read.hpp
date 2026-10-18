#pragma once
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <unistd.h>
namespace penchat_scan {
// /proc entries can disappear between enumeration, open and read. Treat an
// incomplete observation as unavailable, never as a valid owner identity.
inline std::string readOptionalFile(const std::string &path) {
  int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return {};
  std::string result;
  char bytes[4096];
  for (;;) {
    const auto n = ::read(fd, bytes, sizeof(bytes));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      if (n < 0)
        result.clear();
      ::close(fd);
      return result;
    }
    result.append(bytes, static_cast<size_t>(n));
  }
}
} // namespace penchat_scan
