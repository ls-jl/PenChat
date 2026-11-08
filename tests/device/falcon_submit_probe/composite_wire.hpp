#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>
namespace submit_probe {
constexpr std::uint32_t kCompositeMagic = 0x50435332;
enum class Wire : std::uint32_t { Hello = 1, System = 2, ReleaseSystem = 3, Output = 4, ReleaseOutput = 5, Focus = 6, Stop = 7 };
struct Packet {
  std::uint32_t magic = kCompositeMagic, version = 1;
  Wire type = Wire::Hello;
  std::uint32_t index = 0, sequence = 0, epoch = 0;
  std::uint32_t value = 0, aux = 0, value2 = 0, value3 = 0;
};
static_assert(sizeof(Packet) == 40, "Stable local wire ABI");
struct Geometry {
  unsigned width = 0, height = 0;
  std::size_t bytes = 0, slotBytes = 0, total = 0;
  static Geometry make(unsigned width, unsigned height, std::size_t pageSize);
};
bool sendPacket(int fd, const Packet& packet);
// 1 packet, 0 EAGAIN, -1 disconnected/malformed. Never accepts partial/oversize.
int receivePacket(int fd, Packet& packet);
std::uint32_t monotonicMs();
int createShared(const Geometry& geometry);
pid_t spawnClient(const std::string& executable, int socket, int shared);
void reapClient(pid_t& pid);
// Both inputs and output use packed BGRA, premultiplied alpha. Alias-free.
void blendOver(const std::uint8_t* system, const std::uint8_t* native,
               std::uint8_t* output, std::size_t pixels);
}
