#pragma once
#include "protocol.hpp"
#include "file_read.hpp"
#include <cerrno>
#include <sstream>
#include <sys/ioctl.h>
namespace penchat_scan { namespace hardware_detail {
inline std::string read(const std::string &p) {
  return readOptionalFile(p);
}
inline std::string startTime(int pid) {
  auto s = read("/proc/" + std::to_string(pid) + "/stat");
  auto end = s.rfind(')');
  if (end == std::string::npos)
    return {};
  std::istringstream in(s.substr(end + 2));
  std::string v;
  for (int i = 3; i <= 22; ++i)
    if (!(in >> v))
      return {};
  return v;
}
inline bool same(const Json &p) {
  return !p.value("start", std::string()).empty() &&
         startTime(p.at("pid").get<int>()) == p.at("start").get<std::string>();
}
inline bool match(int pid, const std::string &comm, const std::string &needle) {
  return read("/proc/" + std::to_string(pid) + "/comm") == comm + "\n" &&
         read("/proc/" + std::to_string(pid) + "/cmdline").find(needle) !=
             std::string::npos;
}
inline int io(int fd, unsigned long request, void *p) {
  int rc;
  do {
    rc = ioctl(fd, request, p);
  } while (rc < 0 && errno == EINTR);
  return rc;
}
inline void require(bool b, const char *s) {
  if (!b)
    throw std::runtime_error(s);
}
}} // namespace penchat_scan::hardware_detail
