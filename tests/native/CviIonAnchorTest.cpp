#include "cvi_ion_anchor.h"
#include <cassert>
#include <cstdint>
#include <dlfcn.h>
#include <iostream>
int main(int argc,char **argv) {
  assert(argc==2);
  void *runtime=dlopen(argv[1],RTLD_NOW|RTLD_NODELETE);assert(runtime);
  auto allocate=reinterpret_cast<int(*)(std::uint64_t *,void **,const char *,std::uint32_t)>(dlsym(runtime,"CVI_SYS_IonAlloc"));
  auto release=reinterpret_cast<int(*)(std::uint64_t,void *)>(dlsym(runtime,"CVI_SYS_IonFree"));
  auto live=reinterpret_cast<unsigned(*)()>(dlsym(runtime,"test_live"));
  auto total=reinterpret_cast<unsigned(*)()>(dlsym(runtime,"test_allocations"));
  assert(allocate&&release&&live&&total);
  {
    camera::cvi::IonArenaAnchor anchor(runtime);
    assert(live()==1);
    for(int i=0;i<20;++i) {
      std::uint64_t address=0;
      assert(allocate(&address,nullptr,"display-recreate",4096)==0);
      assert(live()==2&&release(address,nullptr)==0&&live()==1);
    }
    assert(total()==21);
  }
  assert(live()==0);
  std::cout<<"CVI_ION_ANCHOR_REUSE_AND_FINAL_RELEASE_OK\n";
}
