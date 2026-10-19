// Standalone process: the parent pipe's EOF releases continuous reporting even on parent SIGKILL.
// Never writes wake_enable, calibration, thresholds or sampling frequency.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* reportPath = "/sys/kernel/gsensor/report_active";

static int reportState()
{
    const int fd = open(reportPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char buf[16] = {};
    const ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0 || (buf[0] != '0' && buf[0] != '1')) return -1;
    return buf[0] - '0';
}

static bool setReporting(int enabled)
{
    const int fd = open(reportPath, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const char value = enabled ? '1' : '0';
    const bool ok = write(fd, &value, 1) == 1;
    close(fd);
    return ok;
}

static bool parentGone(int timeout)
{
    pollfd p = {STDIN_FILENO, POLLIN | POLLHUP, 0};
    const int rc = poll(&p, 1, timeout);
    if (rc < 0) return errno != EINTR;
    if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) return true;
    if (p.revents & POLLIN) { char ignored[16]; return read(0, ignored, sizeof(ignored)) <= 0; }
    return false;
}

int main(int argc, char** argv)
{
    (void)argc; (void)argv;
#ifdef CAMERA_SENSOR_GUARD_TESTING
    // Only the host-test build accepts fixture paths. The packaged helper has no path override.
    if (argc == 3 && std::strcmp(argv[1], "--test-report") == 0) reportPath = argv[2];
#endif
    // Some host descriptors lack CLOEXEC. Do not inherit camera, DRM or MPP ownership.
    DIR* fds = opendir("/proc/self/fd");
    if (!fds) return 1;
    while (dirent* entry = readdir(fds)) {
        const int fd = std::atoi(entry->d_name);
        if (fd > 2 && fd != dirfd(fds)) close(fd);
    }
    closedir(fds);
    const int lock = open("/tmp/miniapp-camera-sensor.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat st;
    if (lock < 0 || fstat(lock, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1) return 1;
    bool locked = false;
    for (int i = 0; i < 40; ++i) {
        if (flock(lock, LOCK_EX | LOCK_NB) == 0) { locked = true; break; }
        if (errno != EWOULDBLOCK || parentGone(25)) break;
    }
    if (!locked || parentGone(0)) { close(lock); return 1; }
    const int original = reportState();
    if (original < 0) { close(lock); return 1; }
    const bool borrowed = original == 0;
    const bool enabled = !borrowed || setReporting(1);
    const bool ready = enabled && reportState() == 1 && !parentGone(0);
    const char* message = ready ? "READY\n" : "ERROR\n";
    // STDOUT is a socketpair. MSG_NOSIGNAL avoids changing the host's SIGPIPE disposition.
    const bool delivered = send(1, message, std::strlen(message), MSG_NOSIGNAL) == static_cast<ssize_t>(std::strlen(message));
    close(1);
    if (ready && delivered) while (!parentGone(-1)) {}
    bool restored = true;
    if (borrowed && reportState() == 1) restored = setReporting(0);
    close(lock);
    return ready && delivered && restored ? 0 : 1;
}
