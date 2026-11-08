#include "composite_wire.hpp"
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
using namespace submit_probe;
int main() {
  Packet hello; pollfd p{3, POLLIN, 0};
  if (poll(&p, 1, 3000) <= 0 || receivePacket(3, hello) != 1) return 2;
  const auto g = Geometry::make(hello.value, hello.aux, sysconf(_SC_PAGESIZE));
  const auto* system = static_cast<const std::uint8_t*>(mmap(nullptr, 2 * g.slotBytes, PROT_READ, MAP_SHARED, 4, 0));
  auto* output = static_cast<std::uint8_t*>(mmap(nullptr, 2 * g.slotBytes, PROT_READ | PROT_WRITE, MAP_SHARED, 4, 2 * g.slotBytes));
  if (system == MAP_FAILED || output == MAP_FAILED || !sendPacket(3, hello)) return 3;
  std::vector<std::uint8_t> native(g.bytes);
  for (std::size_t i = 0; i < g.bytes; i += 4) { native[i] = 0x56; native[i + 1] = 0x34; native[i + 2] = 0x12; native[i + 3] = 255; }
  bool focused = false, free[2] = {true, true};
  int held = -1; unsigned systemSequence = 0, epoch = 0, sequence = 0;
  while (true) {
    poll(&p, 1, 5);
    if (p.revents & (POLLHUP | POLLERR)) break;
    Packet packet;
    for (int batch = 0; batch < 16; ++batch) {
      auto result = receivePacket(3, packet);
      if (result < 0 || (result == 1 && packet.type == Wire::Stop)) return 0;
      if (!result) break;
      if (packet.type == Wire::Focus) { focused = packet.value; epoch = packet.epoch; }
      if (packet.type == Wire::System) {
        if (held >= 0) { Packet release; release.type = Wire::ReleaseSystem; release.index = held; release.sequence = systemSequence; sendPacket(3, release); }
        held = packet.index; systemSequence = packet.sequence;
      }
      if (packet.type == Wire::ReleaseOutput && packet.index < 2) free[packet.index] = true;
    }
    const int slot = free[0] ? 0 : free[1] ? 1 : -1;
    if (slot < 0 || held < 0 || !focused) continue;
    blendOver(system + held * g.slotBytes, native.data(), output + slot * g.slotBytes, g.bytes / 4);
    Packet frame; frame.type = Wire::Output; frame.index = slot; frame.sequence = ++sequence;
    frame.epoch = epoch; frame.value2 = sequence;
    if (std::getenv("PENCHAT_COMPOSITE_FIXTURE_INVALID")) frame.index = 2;
    if (!sendPacket(3, frame)) return 0;
    free[slot] = false;
  }
  return 0;
}
