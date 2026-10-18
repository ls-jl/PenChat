#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/ioctl.h>
#include <algorithm>

static bool event(int fd, unsigned short type, unsigned short code, int value)
{
    input_event data = {};
    gettimeofday(&data.time, 0);
    data.type = type;
    data.code = code;
    data.value = value;
    return write(fd, &data, sizeof(data)) == sizeof(data);
}

int main(int argc, char** argv)
{
    const bool hold = argc == 5 && !strcmp(argv[1], "--hold");
    const bool pinch = argc == 10 && !strcmp(argv[1], "--pinch");
    if (argc != 3 && argc != 5 && argc != 6 && !pinch) return 2;
    const int x = std::atoi(argv[(pinch || hold) ? 2 : 1]), y = std::atoi(argv[(pinch || hold) ? 3 : 2]);
    const int endX = hold ? x : pinch ? std::atoi(argv[6]) : (argc == 5 || argc == 6) ? std::atoi(argv[3]) : x;
    const int endY = hold ? y : pinch ? std::atoi(argv[7]) : (argc == 5 || argc == 6) ? std::atoi(argv[4]) : y;
    int fd = -1;
    input_absinfo ax = {}, ay = {};
    for (int i = 0; i < 32; ++i) {
        char path[64], name[128] = {};
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        const int candidate = open(path, O_RDWR | O_CLOEXEC);
        if (candidate < 0) continue;
        if (ioctl(candidate, EVIOCGNAME(sizeof(name)), name) >= 0 &&
            (strstr(name, "hyn_ts") || strstr(name, "Hyn_TS")) &&
            ioctl(candidate, EVIOCGABS(ABS_MT_POSITION_X), &ax) == 0 &&
            ioctl(candidate, EVIOCGABS(ABS_MT_POSITION_Y), &ay) == 0) {
            fd = candidate;
            fprintf(stderr, "touch %s %s ranges=%d..%d,%d..%d\n", path, name,
                ax.minimum, ax.maximum, ay.minimum, ay.maximum);
            break;
        }
        close(candidate);
    }
    if (fd < 0) { perror("touch device"); return 1; }
    if (pinch) {
        input_absinfo slots = {};
        const int x2 = std::atoi(argv[4]), y2 = std::atoi(argv[5]);
        const int ex2 = std::atoi(argv[8]), ey2 = std::atoi(argv[9]);
        if (ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slots) || slots.maximum < 1 ||
            std::min(x, endX) < ax.minimum || std::max(x, endX) > ax.maximum ||
            std::min(y, endY) < ay.minimum || std::max(y, endY) > ay.maximum ||
            std::min(x2, ex2) < ax.minimum || std::max(x2, ex2) > ax.maximum ||
            std::min(y2, ey2) < ay.minimum || std::max(y2, ey2) > ay.maximum) { close(fd); return 2; }
        bool ok = true;
        for (int slot = 0; slot < 2; ++slot) {
            ok = event(fd, EV_ABS, ABS_MT_SLOT, slot) &&
                event(fd, EV_ABS, ABS_MT_TRACKING_ID, getpid() % 60000 + slot + 1) &&
                event(fd, EV_ABS, ABS_MT_POSITION_X, slot ? x2 : x) &&
                event(fd, EV_ABS, ABS_MT_POSITION_Y, slot ? y2 : y) &&
                event(fd, EV_ABS, ABS_MT_TOUCH_MAJOR, 10) && event(fd, EV_ABS, ABS_MT_WIDTH_MAJOR, 10) && ok;
        }
        ok = event(fd, EV_KEY, BTN_TOOL_FINGER, 1) && event(fd, EV_KEY, BTN_TOUCH, 1) && event(fd, EV_SYN, SYN_REPORT, 0) && ok;
        usleep(150000);
        for (int step = 1; step <= 20; ++step) {
            for (int slot = 0; slot < 2; ++slot) {
                ok = event(fd, EV_ABS, ABS_MT_SLOT, slot) &&
                    event(fd, EV_ABS, ABS_MT_POSITION_X, (slot ? x2 : x) + ((slot ? ex2 : endX) - (slot ? x2 : x)) * step / 20) &&
                    event(fd, EV_ABS, ABS_MT_POSITION_Y, (slot ? y2 : y) + ((slot ? ey2 : endY) - (slot ? y2 : y)) * step / 20) && ok;
            }
            ok = event(fd, EV_SYN, SYN_REPORT, 0) && ok; usleep(25000);
        }
        for (int slot = 0; slot < 2; ++slot) ok = event(fd, EV_ABS, ABS_MT_SLOT, slot) && event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1) && ok;
        ok = event(fd, EV_KEY, BTN_TOUCH, 0) && event(fd, EV_KEY, BTN_TOOL_FINGER, 0) && event(fd, EV_SYN, SYN_REPORT, 0) && ok;
        close(fd); return ok ? 0 : 1;
    }
    if (x < ax.minimum || x > ax.maximum || y < ay.minimum || y > ay.maximum ||
        endX < ax.minimum || endX > ax.maximum || endY < ay.minimum || endY > ay.maximum) {
        close(fd); return 2;
    }
    bool ok = event(fd, EV_ABS, ABS_MT_SLOT, 0) &&
        event(fd, EV_ABS, ABS_MT_TRACKING_ID, getpid() % 65535 + 1) &&
        event(fd, EV_ABS, ABS_MT_POSITION_X, x) &&
        event(fd, EV_ABS, ABS_MT_POSITION_Y, y) &&
        event(fd, EV_ABS, ABS_MT_TOUCH_MAJOR, 10) &&
        event(fd, EV_ABS, ABS_MT_WIDTH_MAJOR, 10) &&
        event(fd, EV_KEY, BTN_TOOL_FINGER, 1) &&
        event(fd, EV_KEY, BTN_TOUCH, 1) && event(fd, EV_SYN, SYN_REPORT, 0);
    usleep(hold ? std::max(400, std::min(2500, std::atoi(argv[4]))) * 1000 : 150000);
    if (!hold && (argc == 5 || argc == 6)) {
        const int stepDelay = argc == 6 ? std::max(1, std::min(200, std::atoi(argv[5]))) : 20;
        for (int step = 1; step <= 12; ++step) {
            ok = event(fd, EV_ABS, ABS_MT_POSITION_X, x + (endX - x) * step / 12) &&
                event(fd, EV_ABS, ABS_MT_POSITION_Y, y + (endY - y) * step / 12) &&
                event(fd, EV_SYN, SYN_REPORT, 0) && ok;
            usleep(stepDelay * 1000);
        }
    }
    const bool released = event(fd, EV_ABS, ABS_MT_SLOT, 0) &&
        event(fd, EV_ABS, ABS_MT_TRACKING_ID, -1) &&
        event(fd, EV_KEY, BTN_TOUCH, 0) &&
        event(fd, EV_KEY, BTN_TOOL_FINGER, 0) && event(fd, EV_SYN, SYN_REPORT, 0);
    close(fd);
    return ok && released ? 0 : 1;
}
