#include <cstdint>
namespace {unsigned entries=0,allocations=0;bool descriptor=false,map=false;}
extern "C" int CVI_SYS_IonAlloc(std::uint64_t *address,void **,const char *,std::uint32_t bytes) {
  if(!descriptor){descriptor=true;map=true;}
  // Match the verified legacy SDK failure: fd survives after its empty map.
  if(!map||!address||bytes!=4096)return -1;
  *address=std::uint64_t(++allocations)*4096;++entries;return 0;
}
extern "C" int CVI_SYS_IonFree(std::uint64_t address,void *) {
  if(!address||!entries)return -1;
  if(!--entries)map=false;
  return 0;
}
extern "C" unsigned test_live(){return entries;}
extern "C" unsigned test_allocations(){return allocations;}
