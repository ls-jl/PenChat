#include "probe.hpp"
#include <cassert>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <thread>
#include <unistd.h>
int main(int argc, char** argv) {
  assert(argc == 2);
  void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  assert(library);
  auto submit = reinterpret_cast<submit_probe::Present>(dlsym(library, "fixture_submit"));
  auto count = reinterpret_cast<unsigned (*)()>(dlsym(library, "fixture_count"));
  auto arguments = reinterpret_cast<bool (*)(void*, const submit_probe::Rect&, int, int)>(dlsym(library, "fixture_arguments"));
  assert(submit && count && arguments);
  char* base = strrchr(argv[1], '/');
  auto slot = submit_probe::locate(base ? base + 1 : argv[1], "fixture_present");
  auto& probe = submit_probe::Probe::instance();
  unsigned char pixels[13 * 7 * 4]; memset(pixels, 0x4b, sizeof(pixels));
  const submit_probe::Rect dirty{1, 2, 9, 5};
  bool rejected = false;
  try { submit_probe::locate(base ? base + 1 : argv[1], "fixture_present", "wrong"); }
  catch (...) { rejected = true; }
  assert(rejected && __atomic_load_n(slot.address, __ATOMIC_SEQ_CST) == slot.original);
  probe.start(slot, 13, 7, true, 1, 8);
  for (int i = 0; i < 20; ++i) submit(pixels, dirty, 13, 7);
  assert(count() == 20 && arguments(pixels, dirty, 13, 7));
  auto state = probe.stats(); assert(state.calls == 20 && state.samples == 8 && !state.invalid);
  for (auto b : pixels) assert(b == 0x4b);
  // Invalid diagnostics must still pass the original submission through once.
  submit(nullptr, dirty, 0, 0);
  assert(count() == 21 && probe.stats().invalid == 1);
  usleep(1150000);
  state = probe.stats(); assert(!state.installed && state.originalRestored);
  submit(pixels, dirty, 13, 7); assert(count() == 22 && probe.stats().calls == 21);
  probe.stop();
  probe.start(slot, 13, 7, false, 2, 8);
  submit(pixels, dirty, 13, 7);
  assert(probe.stats().samples == 0 && probe.stats().calls == 1);
  probe.stop(); probe.stop();
  assert(probe.stats().originalRestored);
  const auto restoredSlot = submit_probe::locate(base ? base + 1 : argv[1], "fixture_present");
  assert(restoredSlot.protection == slot.protection && restoredSlot.original == slot.original);
  std::string error;
  assert(!slot.replace(slot.original + 1, slot.original, error));
  assert(__atomic_load_n(slot.address, __ATOMIC_SEQ_CST) == slot.original);
  // Change the slot while callers are active, then prove no lost/double calls.
  const auto before = count();
  probe.start(slot, 13, 7, true, 2, 8);
  std::atomic<bool> run{false};
  std::vector<std::thread> workers;
  for (int i = 0; i < 4; ++i) workers.emplace_back([&] {
    while (!run.load()) {}
    for (int n = 0; n < 3000; ++n) submit(pixels, dirty, 13, 7);
  });
  run = true;
  usleep(100);
  probe.stop();
  for (auto& worker : workers) worker.join();
  assert(count() - before == 12000 && probe.stats().originalRestored);
  for (auto b : pixels) assert(b == 0x4b);
  dlclose(library);
  std::cout << "PASS: identity, exact arguments, source unchanged, bounded capture, pass-through, deadline, repeated stop, foreign-owner refusal, concurrent detach\n";
}
