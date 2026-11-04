#pragma once
#include <cstdlib>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
namespace penchat_scan {
// Scanner-process policy, not a byte quota. Large vendor CPU temporaries must
// not raise glibc's adaptive thresholds and pin a non-main arena's top chunk.
inline void configureScannerAllocator() {
#if defined(__GLIBC__)
  mallopt(M_MMAP_THRESHOLD, 128 * 1024);
  mallopt(M_TRIM_THRESHOLD, 128 * 1024);
#endif
}
} // namespace penchat_scan
