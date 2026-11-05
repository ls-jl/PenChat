#include "runtime_io.hpp"
#include <cassert>
#include <cerrno>
#include <limits>
#include <vector>
using namespace penchat_scan;
int main() {
  for (auto direction : {1u, 2u}) {
    for (std::uint64_t size : {0ULL, 1ULL, 2097152ULL, 2097153ULL,
                               5898240ULL, 62914560ULL, 67108864ULL}) {
      NpuSync input{direction, 0, 0xabc, 128, size};
      std::vector<NpuSync> parts;
      assert(slicedNpuSync(7, NpuSyncRequest, input,
          [&](int fd, unsigned long command, const void *p) {
            assert(fd == 7 && command == NpuSyncRequest);
            parts.push_back(*static_cast<const NpuSync *>(p));
            return 0;
          }) == 0);
      std::uint64_t covered = 0;
      for (const auto &p : parts) {
        assert(p.object == input.object && p.flags == direction && !p.reserved);
        assert(p.offset == input.offset + covered && p.size <= NpuSyncSlice);
        covered += p.size;
      }
      assert(covered == size && !parts.empty());
      assert(parts.size() == std::max<std::uint64_t>(1, (size + NpuSyncSlice - 1) / NpuSyncSlice));
    }
  }
  for (auto input : {NpuSync{3, 0, 5, 0, 62914560}, NpuSync{1, 1, 5, 0, 62914560},
                    NpuSync{1, 0, 5, 0, 67108865},
                    NpuSync{1, 0, 5, std::numeric_limits<std::uint64_t>::max() - 1, 62914560}}) {
    unsigned calls = 0;
    assert(slicedNpuSync(2, NpuSyncRequest, input, [&](int, unsigned long, const void *p) {
      assert(p == &input); ++calls; return 0;
    }) == 0 && calls == 1);
  }
  NpuSync input{1, 0, 0xabc, 0, 62914560};
  unsigned calls = 0;
  errno = 0;
  const int rc = slicedNpuSync(2, NpuSyncRequest, input, [&](int, unsigned long, const void *) {
    if (++calls == 3) { errno = EIO; return -1; }
    return 0;
  });
  assert(rc == -1 && errno == EIO && calls == 3); // No retry or later partial call.
  assert(input.offset == 0 && input.size == 62914560); // Caller snapshot is unchanged.
}
