#pragma once
#include <cstdint>
#include <stdexcept>

namespace penchat_scan {
constexpr std::uint32_t cviBufferEmpty=0xc006800e;
inline bool cviStartupCanReopen(unsigned received,int lastCode) {
  return received==0&&std::uint32_t(lastCode)==cviBufferEmpty;
}
struct CviStartupEmpty:std::runtime_error {using std::runtime_error::runtime_error;};

// A constructor failure must finish capture-only teardown before throwing.
// Keep the hardware lease and healthy model; allow exactly one fresh pipeline.
// Configuration, pool cleanup, malformed frames and discontinuities never retry.
template<class Open,class Notify>
auto openCviWithRecovery(Open open,Notify notify)->decltype(open()) {
  try{return open();}
  catch(const CviStartupEmpty &error){notify(error.what());}
  return open();
}
}
