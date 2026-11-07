#include "SystemOverlay.hpp"
#include <cassert>
#include <iostream>
namespace {
int calls=0;
bool showing(const void *window) {++calls;return *static_cast<const bool *>(window);}
}
int main() {
  using penchat::SystemOverlay;
  void *panel=nullptr;bool visible=false;
  assert(!SystemOverlay(nullptr,showing).state().supported);
  assert(!SystemOverlay(&panel,showing).state().supported);
  panel=&visible;
  assert(!SystemOverlay(&panel,nullptr).state().supported&&calls==0);
  SystemOverlay observer(&panel,showing);
  auto s=observer.state();assert(s.supported&&!s.visible&&!visible);
  visible=true;s=observer.state();assert(s.supported&&s.visible&&visible);
  panel=nullptr;const int before=calls;s=observer.state();assert(!s.supported&&!s.visible&&calls==before);
  panel=&visible;assert(observer.state().visible);
  std::cout<<"System overlay: guarded exported pointers, visible/hidden, late initialization and no writes passed\n";
}
