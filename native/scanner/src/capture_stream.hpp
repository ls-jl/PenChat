#pragma once
#include <cstdint>
#include <functional>
namespace penchat_scan {
class CaptureStream {
public:
  virtual ~CaptureStream()=default;
  virtual bool read(const std::function<void(const std::uint8_t *,int,std::uint64_t,std::int64_t)> &)=0;
};
}
