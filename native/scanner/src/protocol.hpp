#pragma once
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace penchat_scan {
using Json = nlohmann::json;
constexpr unsigned ProtocolVersion = 1, PreviewWidth = 1024,
                   PreviewHeight = 256;
constexpr size_t PreviewPixels = PreviewWidth * PreviewHeight;
struct PreviewSlot {
  std::atomic<uint32_t> sequence{0};
  uint32_t width = 0, height = 0;
  uint64_t generation = 0, scan = 0;
  uint8_t pixels[PreviewPixels]{};
};
inline bool sendPacket(int fd, const Json &packet) {
  const auto bytes = packet.dump();
  if (bytes.size() > 32768)
    throw std::runtime_error("scanner packet too large");
  for (int retry = 0; retry < 5; ++retry) {
    auto n = send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n == ssize_t(bytes.size()))
      return true;
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      pollfd p{fd, POLLOUT, 0};
      poll(&p, 1, 20);
      continue;
    }
    return false;
  }
  return false;
}
// null: no packet; false: EOF. One seqpacket is one complete JSON envelope.
inline Json receivePacket(int fd) {
  char bytes[32769];
  const auto n = recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
  if (n == 0)
    return false;
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return nullptr;
    return false;
  }
  if (n > 32768)
    throw std::runtime_error("oversized scanner command");
  auto p = Json::parse(bytes, bytes + n);
  if (!p.is_object() || p.value("v", 0u) != ProtocolVersion)
    throw std::runtime_error("unsupported scanner protocol");
  return p;
}
} // namespace penchat_scan
