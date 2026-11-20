#pragma once
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace penchat {
// Recheck before every spawn: a retained factory can outlive package replacement,
// including a 32-bit executable on a kernel that supports compatibility mode.
inline bool runtimeExecutableMatches(const char *path,unsigned bits,unsigned machine) {
  struct stat st{};
  if(lstat(path,&st)||!S_ISREG(st.st_mode)||st.st_size<52)return false;
  const int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
  if(fd<0)return false;
  unsigned char bytes[64]{};size_t used=0;
  bool regular=fstat(fd,&st)==0&&S_ISREG(st.st_mode);
  while(regular&&used<sizeof(bytes)) {
    const auto n=read(fd,bytes+used,sizeof(bytes)-used);
    if(n<0&&errno==EINTR)continue;
    if(n<=0)break;used+=size_t(n);
  }
  close(fd);
  return regular&&used>=(bits==64?64u:52u)&&bytes[0]==127&&bytes[1]=='E'&&bytes[2]=='L'&&bytes[3]=='F'&&
    bytes[4]==(bits==64?2:1)&&bytes[5]==1&&bytes[6]==1&&(bytes[16]==2||bytes[16]==3)&&bytes[17]==0&&
    (unsigned(bytes[18])|(unsigned(bytes[19])<<8))==machine&&(bits!=32||(bytes[37]&4));
}
}
