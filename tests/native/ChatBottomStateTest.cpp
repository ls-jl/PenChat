#include "chat_bottom_state.hpp"
#include <cassert>
#include <iostream>
using penchat::ChatBottomState;
int main() {
  ChatBottomState s;
  assert(s.update(true, 0, 10, 24, 0) && s.atBottom());
  // Neither side of the original 10px threshold can toggle the visual state.
  for (int i = 1; i < 100; ++i) {
    assert(!s.update(true, i % 2 ? 9 : 11, 10, 24, i * 20));
    assert(s.atBottom());
  }
  assert(!s.update(true, 25, 10, 24, 2000));
  assert(!s.update(true, 25, 10, 24, 2119) && s.atBottom());
  assert(s.update(true, 25, 10, 24, 2120) && !s.atBottom());
  // Reversing direction without reaching the real bottom cannot reveal it.
  assert(!s.update(true, 15, 10, 24, 2400) && !s.atBottom());
  assert(!s.update(true, 10, 10, 24, 2500));
  assert(!s.update(true, 11, 10, 24, 2600));
  assert(!s.update(true, 10, 10, 24, 2700));
  assert(!s.update(true, 10, 10, 24, 2819));
  assert(s.update(true, 10, 10, 24, 2820) && s.atBottom());
  // A scroll/pointer cancellation or suspended page cannot age a candidate.
  assert(!s.update(true, 80, 10, 24, 3000));
  s.pause();
  assert(!s.update(true, 80, 10, 24, 4000) && s.atBottom());
  assert(s.update(true, 80, 10, 24, 4120) && !s.atBottom());
  // Jump-to-bottom initializes immediately; older pages are never the latest.
  s.reset(true);
  assert(s.atBottom());
  assert(!s.update(false, 0, 10, 24, 5000));
  assert(s.update(false, 0, 10, 24, 5120) && !s.atBottom());
  // dp-scaled distances do not change the 120ms temporal confirmation.
  s.reset(true);
  assert(!s.update(true, 37, 15, 36, 6000));
  assert(s.update(true, 37, 15, 36, 6120) && !s.atBottom());
  assert(!s.update(true, 15, 15, 36, 6200));
  assert(s.update(true, 15, 15, 36, 6320) && s.atBottom());
  std::cout << "Chat bottom state: hysteresis, confirmation, pause and scaled geometry passed.\n";
}
