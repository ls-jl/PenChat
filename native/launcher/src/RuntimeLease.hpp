#pragma once
#include <sys/wait.h>
namespace penchat {
inline int runtimeExitCode(int status,bool unknown) {
  if(unknown)return -1;
  if(WIFEXITED(status))return WEXITSTATUS(status);
  if(WIFSIGNALED(status))return 128+WTERMSIG(status);
  return -1;
}

// App-context lifetime identity. Old page callbacks cannot reclaim a newer mount.
class RuntimeLease {
  int runtime_ = 0, generation_ = 0;
public:
  int runtime() const { return runtime_; }
  int generation() const { return generation_; }
  bool claim(int generation) {
    if(generation<=0||generation<generation_)return false;
    generation_=generation;return true;
  }
  void created() { ++runtime_; }
  bool owns(int runtime,int generation) const {
    return runtime>0&&runtime==runtime_&&generation==generation_;
  }
};
}
