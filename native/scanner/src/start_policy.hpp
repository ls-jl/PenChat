#pragma once
#include "profile.hpp"
#include <stdexcept>
namespace penchat_scan {
enum class StartupFrames { None, Unstable, Stable };
enum class StreamControl { QuickOnly, StreamOn, QuickStart };
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
// Buffers are ready before this runs. Failures unwind the owning backend.
template<class Control, class Sleep, class Wait>
void startRkCapture(StartupPolicy policy, Control control, Sleep sleep, Wait wait) {
  if (policy==StartupPolicy::CviPipeline) throw std::runtime_error("invalid RK startup policy");
  if (policy!=StartupPolicy::StandardFirst) control(StreamControl::QuickOnly);
  control(StreamControl::StreamOn);
  if (policy==StartupPolicy::StandardFirst) {
    startOv7251(wait,[&]{control(StreamControl::QuickStart);});
  } else if (policy==StartupPolicy::DoubleQuick) {
    sleep(200); control(StreamControl::QuickStart);
    sleep(100); control(StreamControl::QuickStart);
  } else {
    sleep(50); control(StreamControl::QuickStart);
    if (wait(2000)!=StartupFrames::Stable)
      throw std::runtime_error("pen sensor did not deliver sustained frames");
  }
}
}
