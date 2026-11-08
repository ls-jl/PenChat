#include "composite.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
using namespace submit_probe;
template<class F> void waitFor(F predicate, unsigned timeout = 3000) {
  const auto start = monotonicMs();
  while (!predicate()) { assert(std::uint32_t(monotonicMs() - start) < timeout); usleep(5000); }
}
int main(int argc, char** argv) {
  assert(argc == 3);
  const auto g = Geometry::make(13, 7, 4096);
  assert(g.bytes == 364 && g.slotBytes == 4096 && g.total == 16384);
  bool rejected = false;
  try { Geometry::make(4097, 7, 4096); } catch (...) { rejected = true; }
  assert(rejected);
  int shared = createShared(g);
  assert(ftruncate(shared, g.total - 1) < 0 && ftruncate(shared, g.total + 1) < 0); close(shared);
  int sockets[2]; assert(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets));
  Packet packet; assert(sendPacket(sockets[0], packet) && receivePacket(sockets[1], packet) == 1);
  char oversized[sizeof(Packet) + 1]{}; assert(send(sockets[0], oversized, sizeof(oversized), 0) == sizeof(oversized));
  assert(receivePacket(sockets[1], packet) == -1); close(sockets[0]); close(sockets[1]);
  std::vector<std::uint8_t> a(g.bytes), b(g.bytes), output(g.bytes);
  for (unsigned seed = 0; seed < 4; ++seed) {
    for (std::size_t p = 0; p < g.bytes / 4; ++p) {
      const unsigned alpha = seed == 0 ? 0 : seed == 1 ? 255 : unsigned((p * 53) % 256);
      a[p * 4 + 3] = alpha; b[p * 4 + 3] = (p * 71) % 256;
      for (unsigned c = 0; c < 3; ++c) { a[p * 4 + c] = (p * 13 + c * 19) % (alpha + 1); b[p * 4 + c] = (p * 17 + c * 7) % (unsigned(b[p * 4 + 3]) + 1); }
    }
    blendOver(a.data(), b.data(), output.data(), g.bytes / 4);
    for (std::size_t p = 0; p < g.bytes / 4; ++p) for (unsigned c = 0; c < 4; ++c)
      assert(output[p * 4 + c] == a[p * 4 + c] + (b[p * 4 + c] * (255 - a[p * 4 + 3]) + 127) / 255);
  }
  auto* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL); assert(library);
  auto submit = reinterpret_cast<Present>(dlsym(library, "fixture_submit"));
  auto pixel = reinterpret_cast<unsigned (*)()>(dlsym(library, "fixture_pixel"));
  assert(submit && pixel);
  const auto* base = strrchr(argv[1], '/');
  auto slot = locate(base ? base + 1 : argv[1], "fixture_present");
  auto& composite = Composite::instance();
  std::fill(a.begin(), a.end(), 0);
  const Rect all{0, 0, 13, 7};
  composite.start(slot, 13, 7, argv[2], 3);
  composite.focus(true); submit(a.data(), all, 13, 7);
  waitFor([&] { composite.focus(true); return composite.stats().outputFrames >= 5; });
  assert(pixel() == 0xff123456 && composite.stats().systemFrames == 1);
  for (auto byte : a) assert(byte == 0);
  // A suspended bridge must not keep displaying/inputting a native layer.
  usleep(420000);
  waitFor([&] { return !composite.stats().focused && pixel() == 0; });
  const auto resumedFrom = composite.stats().outputFrames;
  composite.focus(true); submit(a.data(), all, 13, 7);
  waitFor([&] { composite.focus(true); return composite.stats().outputFrames > resumedFrom + 2; });
  composite.focus(false);
  const unsigned opaque = 0xffaabbcc;
  for (std::size_t i = 0; i < a.size(); i += 4) std::memcpy(a.data() + i, &opaque, 4);
  submit(a.data(), all, 13, 7); assert(pixel() == opaque);
  auto before = composite.stats().outputFrames; usleep(50000);
  assert(composite.stats().outputFrames == before);
  kill(composite.stats().child, SIGKILL);
  waitFor([&] { return !composite.stats().running; });
  composite.stop(); assert(composite.stats().originalRestored && composite.stats().child == -1 && !composite.stats().focused);
  assert(__atomic_load_n(slot.address, __ATOMIC_SEQ_CST) == slot.original);
  // Invalid output index must never address beyond the shared mapping.
  setenv("PENCHAT_COMPOSITE_FIXTURE_INVALID", "1", 1);
  composite.start(slot, 13, 7, argv[2], 2); unsetenv("PENCHAT_COMPOSITE_FIXTURE_INVALID");
  composite.focus(true); submit(a.data(), all, 13, 7);
  waitFor([&] { return !composite.stats().running; });
  composite.stop(); assert(composite.stats().rejected && composite.stats().originalRestored);
  composite.start(slot, 13, 7, argv[2], 1);
  composite.focus(true); submit(a.data(), all, 13, 7);
  waitFor([&] { return !composite.stats().running; });
  composite.stop(); assert(composite.stats().reason == "deadline" && composite.stats().originalRestored);
  dlclose(library);
  std::cout << "PASS: sealed shared slots, packet limits, premultiplied blend, independent output, focus, disconnect, malformed output, deadline and restoration\n";
}
