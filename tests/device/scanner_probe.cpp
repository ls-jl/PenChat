// ORANGE-only bench harness. Does not access the app database or MiniApp host.
#include "../../native/scanner/src/hardware.hpp"
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
using namespace penchat_scan;
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  int pair[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair))
    return 2;
  int memory = int(syscall(SYS_memfd_create, "probe-preview", 0));
  if (memory < 0 || ftruncate(memory, sizeof(PreviewSlot)))
    return 2;
  auto slot = static_cast<PreviewSlot *>(mmap(nullptr, sizeof(PreviewSlot),
                                              PROT_READ | PROT_WRITE,
                                              MAP_SHARED, memory, 0));
  new (slot) PreviewSlot;
  std::string root = argv[1], binary = root + "/penchat-scanner";
  int processLock = acquireScanProcessLock();
  pid_t child = fork();
  if (child == 0) {
    dup2(pair[1], 3);
    dup2(memory, 4);
    dup2(processLock, 5);
    std::string libs = root + "/lib";
    setenv("LD_LIBRARY_PATH", libs.c_str(), 1);
    execl(binary.c_str(), binary.c_str(), "--bridge-fd", "3", "--preview-fd",
          "4", "--lease-fd", "5", "--resources", root.c_str(), "--data-dir",
          "/tmp/penchat-scanner-probe-pending", static_cast<char *>(nullptr));
    _exit(127);
  }
  if (child < 0) {
    close(pair[0]);
    close(pair[1]);
    close(memory);
    close(processLock);
    munmap(slot, sizeof(PreviewSlot));
    return 1;
  }
  close(pair[1]);
  close(memory);
  Json lease;
  std::thread touch;
  bool okay = false, finished = false;
  uint64_t scan = 0;
  auto send = [&](const std::string &type) {
    return sendPacket(pair[0], {{"v", 1},
                                {"type", type},
                                {"generation", 1},
                                {"requestId", 1},
                                {"session", "hardware-probe"},
                                {"scanId", scan}});
  };
  send("arm");
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  try {
    while (std::chrono::steady_clock::now() < end) {
      pollfd p{pair[0], POLLIN, 0};
      poll(&p, 1, 20);
      if (p.revents & (POLLHUP | POLLERR))
        break;
      auto j = receivePacket(pair[0]);
      if (j.is_null())
        continue;
      if (j.is_boolean())
        break;
      auto type = j.value("type", std::string());
      std::cout << j.dump() << std::endl;
      if (type == "error" || type == "fatal")
        break;
      if (type == "lease") {
        lease = j["lease"];
        send("lease_ack");
      }
      if (type == "ready" && !touch.joinable()) {
        touch = std::thread([] {
          usleep(100000);
          int fd = open("/dev/input/by-path/led_control", O_WRONLY | O_CLOEXEC);
          if (fd < 0)
            return;
          auto event = [&](int type, int code, int value) {
            input_event e{};
            e.type = type;
            e.code = code;
            e.value = value;
            return write(fd, &e, sizeof(e)) == sizeof(e);
          };
          event(EV_KEY, 134, 1);
          event(EV_SYN, SYN_REPORT, 0);
          usleep(500000);
          event(EV_KEY, 134, 0);
          event(EV_SYN, SYN_REPORT, 0);
          close(fd);
        });
      }
      if (type == "started")
        scan = j["scanId"];
      if (type == "finished") {
        if (j["counts"].value("received", 0) < 5)
          throw std::runtime_error("no sustained capture");
        finished = true;
        send("discard");
      }
      if (type == "ready" && finished)
        send("disarm");
      if (type == "released") {
        okay = finished;
        lease = Json();
        break;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
  }
  if (touch.joinable())
    touch.join();
  close(pair[0]);
  bool exited = false;
  for (int i = 0; i < 400; ++i) {
    int status = 0;
    auto waited = waitpid(child, &status, WNOHANG);
    if (waited == child) {
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        okay = false;
      exited = true;
      break;
    }
    usleep(10000);
  }
  if (!exited) {
    okay = false;
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
  }
  if (!lease.empty())
    restoreScanLease(lease);
  close(processLock);
  munmap(slot, sizeof(PreviewSlot));
  return okay ? 0 : 1;
}
