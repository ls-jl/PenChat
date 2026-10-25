#include "util.h"

#include "camera_logic.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <sstream>

namespace camera {
namespace {

const uint32_t kShaK[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void sha256(const uint8_t* data, size_t len, uint8_t out[32])
{
    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };
    std::vector<uint8_t> msg(data, data + len);
    const uint64_t bitLen = static_cast<uint64_t>(len) * 8;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xff));
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t)msg[off + i * 4] << 24 | (uint32_t)msg[off + i * 4 + 1] << 16 |
                   (uint32_t)msg[off + i * 4 + 2] << 8 | (uint32_t)msg[off + i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = hh + S1 + ch + kShaK[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;
            hh = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (h[i] >> 24) & 0xff;
        out[i * 4 + 1] = (h[i] >> 16) & 0xff;
        out[i * 4 + 2] = (h[i] >> 8) & 0xff;
        out[i * 4 + 3] = h[i] & 0xff;
    }
}

}  // namespace

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
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    close(fd);
    return out;
}

bool writeFile(const std::string& path, const void* data, size_t size)
{
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t left = size;
    while (left) {
        const ssize_t n = write(fd, p, left);
        if (n <= 0) {
            close(fd);
            return false;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
    close(fd);
    return true;
}

bool fileExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
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

long fileSize(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<long>(st.st_size);
}

long fileMtime(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<long>(st.st_mtime);
}

bool removeFile(const std::string& path)
{
    return unlink(path.c_str()) == 0 || errno == ENOENT;
}

std::string timestampName()
{
    time_t now = time(0);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

std::string sha256Hex(const void* data, size_t size)
{
    uint8_t digest[32];
    sha256(static_cast<const uint8_t*>(data), size, digest);
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = hex[(digest[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[digest[i] & 0xf];
    }
    return out;
}

std::string sha256File(const std::string& path)
{
    const std::string data = readFile(path, 8 * 1024 * 1024);
    if (data.empty() && fileSize(path) != 0) return "";
    return sha256Hex(data.data(), data.size());
}

int clampi(int value, int lo, int hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

bool pathHasParentRef(const std::string& path)
{
    return path.find("..") != std::string::npos;
}

std::vector<std::string> listDir(const std::string& path)
{
    std::vector<std::string> out;
    DIR* dir = opendir(path.c_str());
    if (!dir) return out;
    while (dirent* ent = readdir(dir)) {
        if (ent->d_name[0] == '.') continue;
        out.push_back(ent->d_name);
    }
    closedir(dir);
    return out;
}

int64_t fileSize64(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<int64_t>(st.st_size);
}

int64_t fileMtime64(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<int64_t>(st.st_mtime);
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

namespace {

bool mediaComponent(const std::string& root, const std::string& input, std::string* name, int* errOut)
{
    char rootReal[PATH_MAX];
    if (!realpath(root.c_str(), rootReal)) {
        if (errOut) *errOut = errno;
        return false;
    }
    std::string leaf;
    if (input.find('/') == std::string::npos && input.find('\\') == std::string::npos) {
        leaf = input;
    } else {
        char inReal[PATH_MAX];
        if (!realpath(input.c_str(), inReal)) {
            if (errOut) *errOut = errno;
            return false;
        }
        const std::string full(inReal);
        const std::string base(rootReal);
        if (full.size() <= base.size() || full.compare(0, base.size(), base) != 0 || full[base.size()] != '/') {
            if (errOut) *errOut = EPERM;
            return false;
        }
        leaf = full.substr(base.size() + 1);
        if (leaf.find('/') != std::string::npos) {
            if (errOut) *errOut = EPERM;
            return false;
        }
    }
    if (!mediaFileNameSafe(leaf)) {
        if (errOut) *errOut = EINVAL;
        return false;
    }
    if (name) *name = leaf;
    return true;
}

}  // namespace

bool openMediaAt(const std::string& root, const std::string& input, std::string* nameOut, int* fdOut, int* errOut)
{
    if (errOut) *errOut = 0;
    std::string name;
    if (!mediaComponent(root, input, &name, errOut)) return false;
    char rootReal[PATH_MAX];
    if (!realpath(root.c_str(), rootReal)) {
        if (errOut) *errOut = errno;
        return false;
    }
    const int dir = open(rootReal, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) {
        if (errOut) *errOut = errno;
        return false;
    }
    struct stat st;
    if (fstatat(dir, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) {
        if (errOut) *errOut = errno ? errno : EINVAL;
        close(dir);
        return false;
    }
    const int fd = openat(dir, name.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    const int err = errno;
    close(dir);
    if (fd < 0) {
        if (errOut) *errOut = err;
        return false;
    }
    if (nameOut) *nameOut = name;
    if (fdOut) *fdOut = fd;
    else close(fd);
    return true;
}

bool unlinkMediaAt(const std::string& root, const std::string& input, int* errOut)
{
    if (errOut) *errOut = 0;
    std::string name;
    if (!mediaComponent(root, input, &name, errOut)) return false;
    char rootReal[PATH_MAX];
    if (!realpath(root.c_str(), rootReal)) {
        if (errOut) *errOut = errno;
        return false;
    }
    const int dir = open(rootReal, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) {
        if (errOut) *errOut = errno;
        return false;
    }
    struct stat st;
    if (fstatat(dir, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode)) {
        if (errOut) *errOut = errno ? errno : EINVAL;
        close(dir);
        return false;
    }
    const int rc = unlinkat(dir, name.c_str(), 0);
    const int err = errno;
    close(dir);
    if (rc != 0) {
        if (errOut) *errOut = err;
        return false;
    }
    return true;
}

std::string readSys(const std::string& path)
{
    return readFile(path, 4096);
}

}  // namespace camera
