#pragma once
#include <algorithm>
#include <cstdint>
namespace penchat_scan {
// evdev defaults to realtime on older kernels. Prefer EVIOCSCLOCKID, otherwise
// translate its occurrence time, never replace a delayed event with dequeue time.
inline std::int64_t scanKeyTimestamp(std::int64_t seconds, std::int64_t micros,
                                    bool monotonic, std::int64_t now, std::int64_t realtime) {
  if (seconds < 0 || seconds > INT64_MAX / 1000000 - 1 || micros < 0 || micros >= 1000000)
    return now;
  const auto value = seconds * 1000000 + micros;
  const auto age = (monotonic ? now : realtime) - value;
  if (value == 0 || age < 0 || age > now) return now;
  return now - age;
}
inline bool afterReleasedFrame(std::int64_t frame, std::int64_t cutoff, bool draining, bool gatesExposure) {
  return draining && !gatesExposure && frame > cutoff;
}
// A capture read can block. Re-read keys inside its callback BEFORE admitting
// that returned frame, as well as between reads; a batch is not an input lease.
template<class Keys, class Read, class Consume, class Continue>
void pollCaptureBatch(Keys keys, Read read, Consume consume, Continue keepReading) {
  for (int n = 0; n < 16; ++n) {
    keys();
    if (!keepReading()) break;
    if (!read([&](const auto &...frame) {
          keys(); consume(frame...);
        })) break;
  }
}
}
