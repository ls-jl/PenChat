#include "../../native/compositor/elf_slot.hpp"
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <stdexcept>
#include <string>
#include <iostream>
int main(int argc,char **argv){
  assert(argc==2);void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(module);
  const auto slash=std::strrchr(argv[1],'/');auto slot=penchat_compositor::locate(slash?slash+1:argv[1],"fixture_present");
  const auto original=__atomic_load_n(slot.address,__ATOMIC_SEQ_CST);bool refused=false;
  try{penchat_compositor::locateCviDisplay();}catch(const std::runtime_error &error){refused=std::string(error.what()).find("CVI Falcon ABI is not verified")!=std::string::npos;}
  assert(refused&&original==__atomic_load_n(slot.address,__ATOMIC_SEQ_CST));dlclose(module);
  std::cout<<"CVI_UNKNOWN_FIRMWARE_REJECTED_WITHOUT_HOOK_OR_CHILD_OK\n";
}
