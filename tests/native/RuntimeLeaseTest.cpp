#include "RuntimeLease.hpp"
#include <cassert>
int main() {
  assert(penchat::runtimeExitCode(0,true)==-1);
  assert(penchat::runtimeExitCode(0,false)==0);
  assert(penchat::runtimeExitCode(1<<8,false)==1);
  assert(penchat::runtimeExitCode(SIGTERM,false)==128+SIGTERM);
  assert(penchat::runtimeExitCode((SIGSTOP<<8)|0x7f,false)==-1);
  penchat::RuntimeLease lease;
  assert(!lease.owns(0,0)&&!lease.claim(0));
  assert(lease.claim(1));lease.created();const int first=lease.runtime();
  assert(lease.owns(first,1));assert(lease.claim(2));
  assert(!lease.claim(1)&&!lease.owns(first,1)&&lease.owns(first,2));
  lease.created();assert(!lease.owns(first,2)&&lease.owns(lease.runtime(),2));
}
