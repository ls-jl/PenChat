#pragma once
#include "profile.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
namespace penchat_scan {
// Borrowed until the consumer returns; bytes bounds the accessible Y plane.
struct FrameView {
  const std::uint8_t *y = nullptr;
  std::size_t bytes = 0;
  int width = 0, height = 0, stride = 0;
  std::uint64_t sequence = 0;
  std::int64_t timestamp = 0; // Source CLOCK_MONOTONIC microseconds.
};
inline void validateFrame(const FrameView &f) {
  if (!f.y || f.width<2 || f.height<2 || f.stride<f.width || f.timestamp<=0 ||
      std::uint64_t(f.stride)*std::uint64_t(f.height-1)+unsigned(f.width)>f.bytes)
    throw std::runtime_error("invalid scan frame view");
}
inline FrameView paperFrame(const FrameView &f, const CaptureProfile &p) {
  validateFrame(f);
  if (f.width!=p.width || f.height!=p.height)
    throw std::runtime_error("scan native frame dimensions changed");
  const auto r=sensorRegion(p);
  const auto offset=std::size_t(r.top)*std::size_t(f.stride)+std::size_t(r.left);
  FrameView paper{f.y+offset,f.bytes-offset,r.width,r.height,f.stride,f.sequence,f.timestamp};
  validateFrame(paper);
  return paper;
}
using FrameConsumer = std::function<void(const FrameView &)>;
using LegacyFrameConsumer = std::function<void(const std::uint8_t *,int,std::uint64_t,std::int64_t)>;
class CaptureStream {
public:
  virtual ~CaptureStream()=default;
  virtual bool read(const FrameConsumer &)=0;
  virtual void exposure(bool) {}
  // Compatibility for device probes; the production path uses FrameView.
  bool read(const LegacyFrameConsumer &consume) {
    return read(FrameConsumer([&](const FrameView &f){consume(f.y,f.stride,f.sequence,f.timestamp);}));
  }
};
}
