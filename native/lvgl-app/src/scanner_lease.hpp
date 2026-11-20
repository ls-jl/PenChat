#pragma once
#include "common.hpp"
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace penchat {
namespace scan_recovery {
constexpr size_t maxLeaseBytes=65536;
struct File {
  int fd;
  explicit File(int value):fd(value){}
  ~File(){if(fd>=0)::close(fd);}
};
inline void require(bool okay,const char *message) {
  if(!okay)throw std::runtime_error(message);
}
inline void syncDirectory(const std::string &path) {
  const auto slash=path.rfind('/');
  const auto directory=slash==std::string::npos?std::string("."):path.substr(0,slash);
  File fd(::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW));
  require(fd.fd>=0 && ::fsync(fd.fd)==0,"Cannot sync capture recovery directory");
}
inline void persist(const std::string &path,const Json &lease) {
  struct stat existing{};
  require(::lstat(path.c_str(),&existing)==0?S_ISREG(existing.st_mode):errno==ENOENT,
          "Invalid capture recovery destination");
  const auto bytes=lease.dump();
  require(lease.is_object() && !lease.empty() && bytes.size()<=maxLeaseBytes,"Invalid capture recovery snapshot");
  const auto temporary=path+".part-"+uid("lease");
  File fd(::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600));
  require(fd.fd>=0,"Cannot create capture recovery snapshot");
  try {
    size_t written=0;
    while(written<bytes.size()) {
      const auto count=::write(fd.fd,bytes.data()+written,bytes.size()-written);
      if(count<0&&errno==EINTR)continue;
      require(count>0,"Cannot write capture recovery snapshot");
      written+=size_t(count);
    }
    require(::fsync(fd.fd)==0,"Cannot sync capture recovery snapshot");
    const int result=::close(fd.fd);fd.fd=-1;
    require(result==0,"Cannot close capture recovery snapshot");
    require(::rename(temporary.c_str(),path.c_str())==0,"Cannot publish capture recovery snapshot");
    syncDirectory(path);
  }catch(...){::unlink(temporary.c_str());throw;}
}
inline bool ownersValid(const Json &lease) {
  for(const char *name:{"guardians","captures"}) {
    const auto found=lease.find(name);
    if(found==lease.end()||!found->is_array()||found->empty())return false;
    for(const auto &owner:*found) {
      if(!owner.is_object()||!owner.contains("pid")||!owner["pid"].is_number_integer()||owner["pid"]<=1||owner["pid"]>INT_MAX||
         !owner.contains("start")||!owner["start"].is_string()||owner["start"].get_ref<const std::string &>().empty()||
         !owner.contains("paused")||!owner["paused"].is_boolean())return false;
    }
  }
  return true;
}
// A corrupt snapshot never authorizes PID signalling or camera changes. Only
// a fresh, identity-checked, unpaused stock owner permits quarantining it.
template<class Restore,class Idle>
std::string recover(const std::string &path,const std::string &profile,Restore restore,Idle idle) {
  File fd(::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
  if(fd.fd<0&&errno==ENOENT)return {};
  require(fd.fd>=0,"Cannot open capture recovery snapshot");
  struct stat st{};
  require(::fstat(fd.fd,&st)==0&&S_ISREG(st.st_mode)&&st.st_size>=0&&size_t(st.st_size)<=maxLeaseBytes,
          "Invalid capture recovery file");
  std::string bytes;char buffer[4096];
  for(;;) {
    const auto count=::read(fd.fd,buffer,sizeof(buffer));
    if(count<0&&errno==EINTR)continue;
    require(count>=0,"Cannot read capture recovery snapshot");
    if(!count)break;
    require(bytes.size()+size_t(count)<=maxLeaseBytes,"Capture recovery snapshot too large");
    bytes.append(buffer,size_t(count));
  }
  const auto lease=Json::parse(bytes,nullptr,false);
  if(lease.is_object()&&lease.contains("profile")&&lease["profile"].is_string())
    require(lease["profile"]==profile,"Unexpected capture recovery profile");
  const bool valid=lease.is_object()&&lease.contains("profile")&&lease["profile"]==profile&&ownersValid(lease);
  if(valid) {
    restore(lease);
    require(::unlink(path.c_str())==0,"Cannot retire capture recovery snapshot");
    syncDirectory(path);
    return {};
  }
  // Validate first. If a process is still paused, leave the evidence untouched
  // and fail explicitly rather than guessing the previous owner or its state.
  idle();
  const auto quarantine=path+".invalid-"+uid("recovery");
  require(::rename(path.c_str(),quarantine.c_str())==0,"Cannot preserve corrupt capture recovery snapshot");
  syncDirectory(path);
  return quarantine;
}
} // namespace scan_recovery
} // namespace penchat
