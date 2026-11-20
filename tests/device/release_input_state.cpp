#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>

static bool sendEvent(int fd, unsigned short type, unsigned short code, int value) {
  input_event event = {};
  gettimeofday(&event.time, nullptr);
  event.type = type;
  event.code = code;
  event.value = value;
  return write(fd, &event, sizeof(event)) == sizeof(event);
}

static int failure(const char *operation,int code) {
  std::printf("{\"error\":\"%s\",\"errno\":%d}\n",operation,errno);
  return code;
}
int main(int argc, char **argv) {
  if (argc != 2 && !(argc == 3 && !std::strcmp(argv[2], "--release"))) return 2;
  const bool release = argc == 3;
  int fd = open(argv[1], (release ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) return failure("open",1);
  input_absinfo slot = {};
  if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slot) || slot.minimum != 0 || slot.maximum < 0 || slot.maximum >= 16) {
    const int code=failure("slot_query",3);close(fd);return code;
  }
  if (release) {
    bool ok = true;
    for (int i = 0; i <= slot.maximum; ++i) {
      ok = sendEvent(fd, EV_ABS, ABS_MT_SLOT, i) && ok;
      ok = sendEvent(fd, EV_ABS, ABS_MT_TRACKING_ID, -1) && ok;
    }
    ok = sendEvent(fd, EV_ABS, ABS_MT_SLOT, 0) && ok;
    ok = sendEvent(fd, EV_KEY, BTN_TOUCH, 0) && ok;
    ok = sendEvent(fd, EV_KEY, BTN_TOOL_FINGER, 0) && ok;
    ok = sendEvent(fd, EV_SYN, SYN_REPORT, 0) && ok;
    if (!ok) {const int code=failure("release_write",4);close(fd);return code;}
  }
  int ids[17] = {};
  ids[0] = ABS_MT_TRACKING_ID;
  unsigned long keys[(KEY_MAX + 8 * sizeof(unsigned long)) / (8 * sizeof(unsigned long))] = {};
  if (ioctl(fd, EVIOCGMTSLOTS((slot.maximum + 2) * sizeof(int)), ids) < 0 || ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) < 0) {
    const int code=failure("state_query",5);close(fd);return code;
  }
  const bool touching = keys[BTN_TOUCH / (8 * sizeof(unsigned long))] & (1UL << (BTN_TOUCH % (8 * sizeof(unsigned long))));
  std::printf("{\"released\":%s,\"btn_touch\":%s,\"active_slots\":[", release ? "true" : "false", touching ? "true" : "false");
  bool comma = false;
  for (int i = 0; i <= slot.maximum; ++i) if (ids[i + 1] >= 0) {
    std::printf("%s{\"slot\":%d,\"tracking_id\":%d}", comma ? "," : "", i, ids[i + 1]);
    comma = true;
  }
  std::puts("]}");
  close(fd);
  return 0;
}
