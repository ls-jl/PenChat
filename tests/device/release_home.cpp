#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

static bool supportsHome(const char *path, dev_t *identity = nullptr) {
  int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) return false;
  unsigned long bits[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
  struct stat status = {};
  const bool ok = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0 && fstat(fd, &status) == 0 &&
    (bits[KEY_HOME / (8 * sizeof(unsigned long))] & (1UL << (KEY_HOME % (8 * sizeof(unsigned long)))));
  if (identity) *identity = status.st_rdev;
  close(fd);
  return ok;
}

static bool send(int fd, int type, int code, int value) {
  input_event e = {};
  e.type = type; e.code = code; e.value = value;
  return write(fd, &e, sizeof(e)) == sizeof(e);
}

int main(int argc, char **argv) {
  if (argc == 2 && !std::strcmp(argv[1], "--discover")) {
    std::vector<std::string> candidates;
    std::set<dev_t> seen;
    for (int i = 0; i < 32; ++i) {
      const auto path = std::string("/dev/input/event") + std::to_string(i);
      dev_t identity = 0;
      if (supportsHome(path.c_str(), &identity) && seen.insert(identity).second) candidates.push_back(path);
    }
    if (candidates.size() != 1) {
      std::printf("{\"supported\":false,\"candidate_count\":%zu}\n", candidates.size());
    } else {
      std::printf("{\"supported\":true,\"path\":\"%s\",\"capability\":\"KEY_HOME\"}\n", candidates[0].c_str());
    }
    return 0;
  }
  if (argc != 3 || std::strcmp(argv[1], "--send") || std::strncmp(argv[2], "/dev/input/event", 16) || !supportsHome(argv[2])) return 2;
  int fd = open(argv[2], O_WRONLY | O_CLOEXEC);
  if (fd < 0) return 3;
  bool ok = send(fd, EV_KEY, KEY_HOME, 1) && send(fd, EV_SYN, SYN_REPORT, 0);
  usleep(100000);
  ok = send(fd, EV_KEY, KEY_HOME, 0) && send(fd, EV_SYN, SYN_REPORT, 0) && ok;
  close(fd);
  if (!ok) return 4;
  std::puts("verified-home-paired");
  return 0;
}
