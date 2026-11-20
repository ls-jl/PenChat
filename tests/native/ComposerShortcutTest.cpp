#include "composer_shortcut.hpp"
#include <cassert>
#include <iostream>
using penchat::ComposerShortcut;
int main() {
  ComposerShortcut s;
  auto tap = [&](int64_t now) {
    assert(s.touch(35, 230, true, false, true, now, 266));
    assert(s.touch(35, 230, false, false, true, now + 40, 266));
  };
  assert(!s.touch(200, 230, true, false, false, 1, 266));
  tap(100); s.tick(200); assert(!s.shown());
  tap(250); assert(s.shown() && s.limit() == 399);
  s.tick(800); assert(s.shown()); // No first-tap dismissal after a double tap.
  s.scroll(200); s.scroll(-199); assert(s.shown() && s.distance() == 399);
  s.scroll(1); assert(!s.shown());
  tap(1000); tap(1150); assert(s.shown());
  s.scroll(250);
  tap(1300); tap(1450); assert(s.shown() && s.distance() == 0);
  tap(1800); s.tick(2190); assert(s.shown());
  s.tick(2191); assert(!s.shown());
  tap(2300); tap(2450); assert(s.shown());
  // An out-and-back drag and cancelled touch must not dismiss.
  assert(s.touch(35, 230, true, false, true, 2700, 266));
  s.touch(60, 230, true, false, true, 2720, 266);
  s.touch(35, 230, false, false, true, 2740, 266);
  s.tick(3200); assert(s.shown());
  s.touch(35, 230, true, false, true, 3300, 266);
  s.touch(35, 230, false, true, true, 3340, 266);
  s.tick(3800); assert(s.shown());
  tap(3900); s.pause(); s.tick(4400); assert(s.shown());
  s.configure("manual"); tap(4500); tap(4650);
  s.scroll(100000); assert(s.shown());
  tap(4800); s.tick(5200); assert(!s.shown());
  s.configure("off"); assert(!s.enabled());
  assert(!s.touch(35, 230, true, false, true, 6000, 266));
  s.configure("unknown"); assert(s.enabled() && !s.shown());
  tap(6500); tap(6650); s.reset(); assert(!s.shown() && !s.tracking());
  // Geometry is frozen at reveal and based on logical height, not a model ID.
  s.touch(35, 230, true, false, true, 7000, 280);
  s.touch(35, 230, false, false, true, 7040, 280);
  s.touch(35, 230, true, false, true, 7150, 280);
  s.touch(35, 230, false, false, true, 7190, 280);
  assert(s.limit() == 420);
  std::cout << "Composer shortcut: taps, cancellation, pause, modes and logical-distance boundary passed.\n";
}
