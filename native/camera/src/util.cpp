#include "util.h"
#include "camera_logic.h"
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
namespace camera {
std::string joinPath(const std::string& base, const std::string& name)
{
    if (base.empty()) return name;
    if (base[base.size() - 1] == '/') return base + name;
    return base + "/" + name;
}

std::string parentDir(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

std::string baseName(const std::string& path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

std::string extensionOf(const std::string& path)
{
    const std::string name = baseName(path);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return "";
    std::string ext = name.substr(dot + 1);
    for (size_t i = 0; i < ext.size(); ++i) {
        if (ext[i] >= 'A' && ext[i] <= 'Z') ext[i] = static_cast<char>(ext[i] - 'A' + 'a');
    }
    return ext;
}

std::string readFile(const std::string& path, size_t maxBytes)
{
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return "";
    std::string out;
    char buf[4096];
    while (out.size() < maxBytes) {
        const ssize_t n = read(fd, buf, std::min(sizeof(buf), maxBytes - out.size()));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return out;
}

bool dirExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool ensureDir(const std::string& path)
{
    if (path.empty() || path == "/") return true;
    if (dirExists(path)) return true;
    if (!ensureDir(parentDir(path))) return false;
    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

bool reserveMediaPaths(const std::string& dir, const char* prefix, const char* ext,
    std::string* finalPath, std::string* partPath, int* errOut)
{
    if (errOut) *errOut = 0;
    if (!ensureDir(dir)) {
        if (errOut) *errOut = errno;
        return false;
    }
    static int counter = 0;
    for (int attempt = 0; attempt < 64; ++attempt) {
        timeval tv;
        gettimeofday(&tv, 0);
        const int n = __sync_add_and_fetch(&counter, 1);
        const std::string name = formatUniqueMediaName(prefix, ext, static_cast<int64_t>(tv.tv_sec),
            static_cast<int>(tv.tv_usec / 1000), n);
        const std::string finalPathLocal = joinPath(dir, name);
        const int fd = open(finalPathLocal.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            if (errOut) *errOut = errno;
            return false;
        }
        close(fd);
        const std::string part = finalPathLocal + ".part";
        const int partFd = open(part.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0644);
        if (partFd < 0) {
            const int err = errno;
            unlink(finalPathLocal.c_str());
            if (err == EEXIST) continue;
            if (errOut) *errOut = err;
            return false;
        }
        close(partFd);
        if (finalPath) *finalPath = finalPathLocal;
        if (partPath) *partPath = part;
        return true;
    }
    if (errOut) *errOut = EEXIST;
    return false;
}
} // namespace camera
