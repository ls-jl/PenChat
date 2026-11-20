#pragma once
namespace pen { namespace stream {
// C++14 standard-library compatibility for the ARM32 board CRT. Numerically
// identical to std::clamp for ordered inputs, including reference semantics.
template<class T> constexpr const T &bounded(const T &v,const T &lo,const T &hi) {
  return v<lo?lo:hi<v?hi:v;
}
}}
