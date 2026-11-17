#include "RuntimeLease.hpp"
#include <cassert>
int main() {
  penchat::RuntimeLease lease;
  assert(!lease.owns(0,0)&&!lease.claim(0));
  assert(lease.claim(1));lease.created();const int first=lease.runtime();
  assert(lease.owns(first,1));assert(lease.claim(2));
  assert(!lease.claim(1)&&!lease.owns(first,1)&&lease.owns(first,2));
  lease.created();assert(!lease.owns(first,2)&&lease.owns(lease.runtime(),2));
}
