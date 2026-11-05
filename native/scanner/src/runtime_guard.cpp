#include <cerrno>
#include "runtime_io.hpp"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace {
thread_local bool sliceEnabled = false;
thread_local int verifiedFd = -1;
thread_local dev_t verifiedDevice = 0;
thread_local ino_t verifiedInode = 0;
std::atomic<std::uint64_t> operations{0}, slices{0}, maxSliceUs{0};

int rawIoctl(int fd, unsigned long request, const void *arg) noexcept {
  return int(syscall(SYS_ioctl, fd, request, arg));
}
bool supportedNpuFd(int fd) noexcept {
  struct stat st {};
  if (fstat(fd, &st) || !S_ISCHR(st.st_mode)) return false;
  if (verifiedFd == fd && verifiedDevice == st.st_rdev && verifiedInode == st.st_ino)
    return true;
  char path[128], target[256];
  snprintf(path, sizeof(path), "/sys/dev/char/%u:%u/device/driver",
           unsigned(major(st.st_rdev)), unsigned(minor(st.st_rdev)));
  const auto n = readlink(path, target, sizeof(target) - 1);
  if (n <= 0) return false;
  target[n] = 0;
  const auto name = strrchr(target, '/');
  if (!name || strcmp(name + 1, "RKNPU")) return false;
  struct { std::uint32_t flags, value; } version{1, 0};
  // This policy has been validated against the Y02 DRM driver v0.9.1.
  if (rawIoctl(fd, 0xc0086440UL, &version) || version.value != 901) return false;
  verifiedFd = fd;
  verifiedDevice = st.st_rdev;
  verifiedInode = st.st_ino;
  return true;
}
} // namespace

namespace penchat_scan {
RuntimeIoScope::RuntimeIoScope(bool enable) noexcept : previous_(sliceEnabled) {
  sliceEnabled = enable;
  verifiedFd = -1; // fd reuse across backend operations cannot inherit validation.
}
RuntimeIoScope::~RuntimeIoScope() {
  sliceEnabled = previous_;
  verifiedFd = -1;
}
RuntimeIoMetrics runtimeIoMetrics() noexcept {
  return {operations.load(), slices.load(), maxSliceUs.load()};
}
} // namespace penchat_scan

// Match libc's variadic ioctl ABI. This executable-only export also resolves
// the vendor runtime's public ioctl import; never inspect tensor data.
extern "C" int ioctl(int fd, unsigned long request, ...) noexcept {
  va_list args;
  va_start(args, request);
  const void *arg = va_arg(args, void *);
  va_end(args);
  if (!sliceEnabled || request != penchat_scan::NpuSyncRequest || !arg)
    return rawIoctl(fd, request, arg);
  const int before = errno;
  if (!supportedNpuFd(fd)) {
    errno = before;
    return rawIoctl(fd, request, arg);
  }
  errno = before;
  penchat_scan::NpuSync sync;
  memcpy(&sync, arg, sizeof(sync));
  if (!penchat_scan::canSliceNpuSync(request, sync))
    return rawIoctl(fd, request, arg);
  ++operations;
  return penchat_scan::slicedNpuSync(fd, request, sync,
      [](int target, unsigned long command, const void *part) {
        const auto start = std::chrono::steady_clock::now();
        const int rc = rawIoctl(target, command, part);
        const int saved = errno;
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        ++slices;
        auto maximum = maxSliceUs.load();
        while (maximum < std::uint64_t(elapsed) &&
               !maxSliceUs.compare_exchange_weak(maximum, elapsed)) {}
        errno = saved;
        return rc;
      });
}

// Exported by the scanner executable so the vendor runtime's system import
// resolves here before libc. Never inspect/log the argument or start a process.
extern "C" int system(const char *command) {
  if (!command)
    return 0;
  errno = ENOTSUP;
  return -1;
}
