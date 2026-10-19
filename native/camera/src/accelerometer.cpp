#include "accelerometer.h"
#include "util.h"

#include <cerrno>
#include <cstdio>
#include <climits>
#include <sstream>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace camera {
namespace {
pthread_mutex_t sensorMutex = PTHREAD_MUTEX_INITIALIZER;
int controlFd = -1;
pid_t guardPid = -1;
int generation = 0;

bool reapGuard()
{
    if (guardPid <= 0) return true;
    const pid_t rc = waitpid(guardPid, 0, WNOHANG);
    if (rc == guardPid || (rc < 0 && errno == ECHILD)) { guardPid = -1; return true; }
    return false;
}

bool stopLocked()
{
    if (controlFd >= 0) { close(controlFd); controlFd = -1; }
    for (int i = 0; i < 60; ++i) {
        if (reapGuard()) return true;
        usleep(10000);
    }
    // Do not kill a helper in the middle of restoring a system setting. Reap on the next call.
    return false;
}
}  // namespace

bool parseAcceleration(const std::string& text, Acceleration* result)
{
    if (!result || text.size() > 256) return false;
    result->ok = false;
    long x = 0, y = 0, z = 0;
    char kx, ky, kz, ex, ey, ez, s1, s2;
    std::istringstream input(text);
    if (!(input >> kx >> ex >> x >> s1 >> ky >> ey >> y >> s2 >> kz >> ez >> z) ||
        kx != 'x' || ky != 'y' || kz != 'z' || ex != '=' || ey != '=' || ez != '=' || s1 != ';' || s2 != ';' ||
        x < -32768 || x > 32767 || y < -32768 || y > 32767 || z < -32768 || z > 32767) return false;
    input >> std::ws;
    if (!input.eof()) return false;
    result->x = x; result->y = y; result->z = z;
    result->ok = true;
    return true;
}

Acceleration startAccelerometer(const std::string& workspace)
{
    pthread_mutex_lock(&sensorMutex);
    Acceleration result;
    if (!stopLocked()) {
        result.error = "previous sensor lease is still being released";
        pthread_mutex_unlock(&sensorMutex);
        return result;
    }
    const std::string helper = joinPath(workspace, "assets/bin/penchat-sensor-guard");
    // Some Falcon package extractors drop executable bits from assets. Repair only this owned file.
    if (!workspace.empty() && access(helper.c_str(), X_OK) != 0) {
        const int fd = open(helper.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        struct stat st;
        if (fd >= 0) {
            if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid()) fchmod(fd, 0755);
            close(fd);
        }
    }
    if (workspace.empty() || access(helper.c_str(), X_OK) != 0 ||
        access("/sys/kernel/gsensor/axis_data", R_OK) != 0) {
        result.error = "accelerometer interface or sampling helper unavailable";
        pthread_mutex_unlock(&sensorMutex);
        return result;
    }
    int control[2] = {-1, -1}, ready[2] = {-1, -1};
    if (pipe2(control, O_CLOEXEC) != 0 || socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ready) != 0) {
        if (control[0] >= 0) { close(control[0]); close(control[1]); }
        result.error = "sensor lease pipe unavailable";
        pthread_mutex_unlock(&sensorMutex);
        return result;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, control[0], 0);
    posix_spawn_file_actions_adddup2(&actions, ready[1], 1);
    posix_spawn_file_actions_addclose(&actions, control[1]);
    posix_spawn_file_actions_addclose(&actions, ready[0]);
    if (control[0] > 1) posix_spawn_file_actions_addclose(&actions, control[0]);
    if (ready[1] > 1) posix_spawn_file_actions_addclose(&actions, ready[1]);
    char* args[] = {const_cast<char*>(helper.c_str()), 0};
    pid_t child = -1;
    const int spawned = posix_spawn(&child, helper.c_str(), &actions, 0, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(control[0]); close(ready[1]);
    if (spawned == 0) {
        guardPid = child;
        controlFd = control[1];
        pollfd p = {ready[0], POLLIN, 0};
        char message[16] = {};
        const int rc = poll(&p, 1, 1500);
        const ssize_t n = rc > 0 ? read(ready[0], message, sizeof(message) - 1) : -1;
        result.ok = n >= 6 && std::string(message, 6) == "READY\n";
        if (result.ok) {
            generation = generation == INT_MAX ? 1 : generation + 1;
            result.session = generation;
        } else {
            result.error = "accelerometer sampling could not be enabled";
            stopLocked();
        }
    } else {
        close(control[1]);
        result.error = "sensor sampling helper could not start";
    }
    close(ready[0]);
    pthread_mutex_unlock(&sensorMutex);
    return result;
}

Acceleration readAccelerometer(int session)
{
    pthread_mutex_lock(&sensorMutex);
    Acceleration result;
    result.session = session;
    if (session <= 0 || session != generation || controlFd < 0 || reapGuard()) {
        result.error = "sensor session expired";
    } else if (!parseAcceleration(readFile("/sys/kernel/gsensor/axis_data", 256), &result)) {
        result.error = "invalid accelerometer data";
    }
    pthread_mutex_unlock(&sensorMutex);
    return result;
}

bool stopAccelerometer(int session)
{
    pthread_mutex_lock(&sensorMutex);
    const bool stopped = session > 0 && session != generation ? true : stopLocked();
    pthread_mutex_unlock(&sensorMutex);
    return stopped;
}

bool accelerometerRunning()
{
    pthread_mutex_lock(&sensorMutex);
    const bool running = !reapGuard();
    pthread_mutex_unlock(&sensorMutex);
    return running;
}
}  // namespace camera
