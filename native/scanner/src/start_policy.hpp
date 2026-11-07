#pragma once
#include <stdexcept>
namespace penchat_scan {
enum class StartupFrames { None, Unstable, Stable };
// OV7251 gets one optional quick-stream attempt, only when standard STREAMON
// delivered no frames at all. Invalid frames are errors in the reader itself.
template<class Wait, class Quick> void startOv7251(Wait wait, Quick quick) {
  auto state = wait(2000);
  if (state == StartupFrames::Stable) return;
  if (state != StartupFrames::None)
    throw std::runtime_error("pen sensor did not deliver sustained frames");
  quick();
  if (wait(2000) != StartupFrames::Stable)
    throw std::runtime_error("pen sensor did not deliver sustained frames");
}
}
