#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "composite_wire.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
// The S6 glibc 2.23 fcntl headers predate these Linux UAPI seal constants.
#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#define F_SEAL_SEAL 0x0001
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW 0x0004
#endif
extern char** environ;
namespace penchat_compositor {
Geometry Geometry::make(unsigned w, unsigned h, std::size_t page) {
  if (!w || !h || w > 4096 || h > 4096 || std::uint64_t(w) * h > 4 * 1024 * 1024 ||
      !page || (page & (page - 1)) || page > 1024 * 1024)
    throw std::runtime_error("Invalid shared geometry");
  Geometry g;
  g.width = w; g.height = h; g.bytes = std::size_t(w) * h * 4;
  g.slotBytes = (g.bytes + page - 1) & ~(page - 1);
  g.total = g.slotBytes * 4; // two system slots, two output slots
  return g;
}
std::uint32_t monotonicMs() {
  return std::uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());
}
bool sendPacket(int fd, const Packet& packet) {
  return send(fd, &packet, sizeof(packet), MSG_NOSIGNAL | MSG_DONTWAIT) == sizeof(packet);
}
int receivePacket(int fd, Packet& packet) {
  Packet input{};
  const auto n = recv(fd, &input, sizeof(input), MSG_DONTWAIT | MSG_TRUNC);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
  if (n != sizeof(input) || input.magic != kCompositeMagic || input.version != 1 ||
      input.type < Wire::Hello || input.type > Wire::Visibility) return -1;
  packet = input;
  return 1;
}
int createShared(const Geometry& g) {
  const int fd = syscall(SYS_memfd_create, "penchat-composite", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) throw std::runtime_error("Cannot allocate shared framebuffer");
  if (ftruncate(fd, g.total) || fcntl(fd, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL)) {
    close(fd); throw std::runtime_error("Cannot size/seal shared framebuffer");
  }
  return fd;
}
namespace {
pid_t spawn(const std::string& executable, int socket, int shared, int bridge,
            const std::string& data, const std::string& workspace) {
  const bool app = bridge >= 0;
  // All source descriptors are above the fixed child targets, including 0/1/2.
  int descriptors[3] = {fcntl(socket, F_DUPFD_CLOEXEC, 10), fcntl(shared, F_DUPFD_CLOEXEC, 10), -1};
  if (app) descriptors[2] = fcntl(bridge, F_DUPFD_CLOEXEC, 10);
  posix_spawn_file_actions_t actions;
  bool initialized = false;
  try {
    if (descriptors[0] < 0 || descriptors[1] < 0 || (app && descriptors[2] < 0))
      throw std::runtime_error("Cannot isolate compositor descriptors");
    if (posix_spawn_file_actions_init(&actions)) throw std::runtime_error("Cannot create child actions");
    initialized = true;
    int result = posix_spawn_file_actions_adddup2(&actions, descriptors[0], 3);
    if (!result) result = posix_spawn_file_actions_adddup2(&actions, descriptors[1], 4);
    if (app && !result) result = posix_spawn_file_actions_adddup2(&actions, descriptors[2], 5);
    if (app && !result) result = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    const auto log = data + "/lvgl-runtime.log";
    if (app && !result) result = posix_spawn_file_actions_addopen(&actions, 1, log.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (app && !result) result = posix_spawn_file_actions_adddup2(&actions, 1, 2);
    DIR* directory = opendir("/proc/self/fd");
    if (!directory) result = EIO;
    if (directory) {
      const int own = dirfd(directory);
      for (dirent* item; (item = readdir(directory));) {
        char* end = nullptr;
        const long fd = strtol(item->d_name, &end, 10);
        if (*end || fd < (app ? 6 : 5) || fd == own) continue;
        if (!result) result = posix_spawn_file_actions_addclose(&actions, int(fd));
      }
      closedir(directory);
    }
    std::vector<std::string> args{executable};
    if (app) args.insert(args.end(), {"--bridge-fd", "5", "--compositor-fd", "3", "--compositor-memory", "4",
                                     "--data-dir", data, "--workspace", workspace});
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(&arg[0]);
    argv.push_back(nullptr);
    pid_t pid = -1;
    if (!result) result = posix_spawn(&pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
    if (result) throw std::runtime_error("Cannot start native compositor application");
    posix_spawn_file_actions_destroy(&actions);
    for (int fd : descriptors) if (fd >= 0) close(fd);
    return pid;
  } catch (...) {
    if (initialized) posix_spawn_file_actions_destroy(&actions);
    for (int fd : descriptors) if (fd >= 0) close(fd);
    throw;
  }
}
}
pid_t spawnClient(const std::string& executable, int socket, int shared) {
  return spawn(executable, socket, shared, -1, {}, {});
}
pid_t spawnApplication(const std::string& executable, int socket, int shared, int bridge,
                       const std::string& data, const std::string& workspace) {
  return spawn(executable, socket, shared, bridge, data, workspace);
}
void reapClient(pid_t& pid) {
  if (pid <= 0) return;
  int status = 0;
  for (int i = 0; i < 50; ++i) {
    const auto result = waitpid(pid, &status, WNOHANG);
    if (result == pid || (result < 0 && errno == ECHILD)) { pid = -1; return; }
    usleep(10000);
  }
  kill(pid, SIGTERM);
  for (int i = 0; i < 50; ++i) {
    const auto result = waitpid(pid, &status, WNOHANG);
    if (result == pid || (result < 0 && errno == ECHILD)) { pid = -1; return; }
    usleep(10000);
  }
  kill(pid, SIGKILL);
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
  pid = -1;
}
void blendOver(const std::uint8_t* top, const std::uint8_t* bottom, std::uint8_t* output, std::size_t pixels) {
  std::size_t p = 0;
#if defined(__ARM_NEON)
  for (; p + 8 <= pixels; p += 8) {
    auto a = vld4_u8(top + p * 4);
    const auto alpha = vget_lane_u64(vreinterpret_u64_u8(a.val[3]), 0);
    if (alpha == UINT64_MAX) { vst4_u8(output + p * 4, a); continue; }
    auto b = vld4_u8(bottom + p * 4);
    if (!alpha) { vst4_u8(output + p * 4, b); continue; }
    const auto inv = vmvn_u8(a.val[3]);
    for (unsigned c = 0; c < 4; ++c) {
      auto value = vmull_u8(b.val[c], inv);
      value = vaddq_u16(value, vdupq_n_u16(128));
      value = vaddq_u16(value, vshrq_n_u16(value, 8));
      a.val[c] = vqadd_u8(a.val[c], vshrn_n_u16(value, 8));
    }
    vst4_u8(output + p * 4, a);
  }
#endif
  for (; p < pixels; ++p) {
    const unsigned inv = 255 - top[p * 4 + 3];
    for (unsigned c = 0; c < 4; ++c)
      output[p * 4 + c] = std::min(255u, unsigned(top[p * 4 + c]) + (bottom[p * 4 + c] * inv + 127) / 255);
  }
}
}
