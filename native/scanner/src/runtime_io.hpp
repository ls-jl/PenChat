#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace penchat_scan {
// Public Rockchip DRM ABI, not an RKNN private symbol. Keep the layout exact.
struct NpuSync {
  std::uint32_t flags, reserved;
  std::uint64_t object, offset, size;
};
constexpr std::size_t NpuSyncSlice = 2 * 1024 * 1024;
constexpr unsigned long NpuSyncRequest = 0xc0206445UL;
static_assert(sizeof(NpuSync) == 32, "Rockchip DMA synchronization ABI");
inline bool canSliceNpuSync(unsigned long request, const NpuSync &sync) {
  return request == NpuSyncRequest && (sync.flags == 1 || sync.flags == 2) &&
      !sync.reserved && sync.size > NpuSyncSlice && sync.size <= 64 * 1024 * 1024 &&
      sync.offset <= std::numeric_limits<std::uint64_t>::max() - sync.size;
}

// Preserve direction, object, reserved field, order and the exact byte range.
// No sleep, heartbeat, cancellation or retry is inserted in a vendor operation.
template <class Call>
int slicedNpuSync(int fd, unsigned long request, const NpuSync &original, Call &&call) {
  if (!canSliceNpuSync(request, original))
    return call(fd, request, &original);
  NpuSync part = original;
  for (std::uint64_t done = 0; done < original.size; done += part.size) {
    part.offset = original.offset + done;
    part.size = std::min<std::uint64_t>(NpuSyncSlice, original.size - done);
    const int rc = call(fd, request, &part);
    if (rc != 0) return rc;
  }
  return 0;
}

// Only the backend owner thread opts in, and only on the verified Y02 profile.
// Other boards, driver versions, fd types and unrelated ioctls pass through.
class RuntimeIoScope {
  bool previous_;
public:
  explicit RuntimeIoScope(bool enable) noexcept;
  ~RuntimeIoScope();
  RuntimeIoScope(const RuntimeIoScope &) = delete;
  RuntimeIoScope &operator=(const RuntimeIoScope &) = delete;
};
struct RuntimeIoMetrics {
  std::uint64_t operations = 0, slices = 0, maxSliceUs = 0;
};
RuntimeIoMetrics runtimeIoMetrics() noexcept;
} // namespace penchat_scan
