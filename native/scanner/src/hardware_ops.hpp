#pragma once
#include "protocol.hpp"
#include "file_read.hpp"
#include "capture_owner.hpp"
#include <dirent.h>
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
// Identity was verified before signalling. Do not consult cmdline here: exit_mm
// clears it before exit_files and exit_task_work close the video's final refs.
inline bool captureReleased(const Json &owner) {
  const auto base="/proc/"+std::to_string(owner.at("pid").get<int>());
  const auto text=read(base+"/stat");
  const bool gone=text.empty()&&access(base.c_str(),F_OK)<0&&errno==ENOENT;
  const auto leader=captureTaskState(text);
  const auto expected=owner.at("start").get<std::string>();
  if(gone||(leader.valid&&leader.start!=expected))return true;
  if(!captureTaskExited(leader))return false;
  auto directory=opendir((base+"/task").c_str());
  if(!directory)return access(base.c_str(),F_OK)<0&&errno==ENOENT;
  std::vector<CaptureTaskState> threads;
  while(auto entry=readdir(directory)) {
    const std::string name=entry->d_name;
    if(name.empty()||name.find_first_not_of("0123456789")!=std::string::npos)continue;
    const auto task=base+"/task/"+name;
    const auto stat=read(task+"/stat");
    if(stat.empty()&&access(task.c_str(),F_OK)<0&&errno==ENOENT)continue;
    threads.push_back(captureTaskState(stat)); // Unreadable is not released.
  }
  closedir(directory);
  if(access(base.c_str(),F_OK)<0&&errno==ENOENT)return true;
  return captureOwnerExited(expected,leader,threads);
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
