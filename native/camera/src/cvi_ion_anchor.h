#pragma once
#include <cstdint>
#include <dlfcn.h>
#include <stdexcept>

namespace camera { namespace cvi {
// The hash-locked S6 libsys IonFree frees its hashmap when the last entry is
// removed, but leaves its ION fd open. IonAlloc only recreates the map on fd
// open, so the next allocation dereferences null (libsys + 0x590e). Keep one
// page through SDK reuse, using public APIs only. Release it after all surfaces
// and capture objects at normal process exit; no vendor globals are patched.
class IonArenaAnchor {
  std::uint64_t address_=0;
  int (*free_)(std::uint64_t,void *)=nullptr;
public:
  explicit IonArenaAnchor(void *system) {
    auto allocate=reinterpret_cast<int(*)(std::uint64_t *,void **,const char *,std::uint32_t)>(dlsym(system,"CVI_SYS_IonAlloc"));
    free_=reinterpret_cast<decltype(free_)>(dlsym(system,"CVI_SYS_IonFree"));
    if(!allocate||!free_||allocate(&address_,nullptr,"penchat-sdk-state",4096)||!address_)
      throw std::runtime_error("CVI SDK state allocation failed");
  }
  ~IonArenaAnchor(){if(address_&&free_)free_(address_,nullptr);}
  IonArenaAnchor(const IonArenaAnchor &)=delete;
  IonArenaAnchor &operator=(const IonArenaAnchor &)=delete;
};
inline void retainIonArena(void *system) {
  static IonArenaAnchor anchor(system);
  (void)anchor;
}
}}
