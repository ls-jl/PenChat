#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace camera {

std::string joinPath(const std::string& base, const std::string& name);
std::string parentDir(const std::string& path);
std::string baseName(const std::string& path);
std::string extensionOf(const std::string& path);
std::string readFile(const std::string& path, size_t maxBytes = 1 << 20);
bool writeFile(const std::string& path, const void* data, size_t size);
bool fileExists(const std::string& path);
bool dirExists(const std::string& path);
bool ensureDir(const std::string& path);
long fileSize(const std::string& path);
long fileMtime(const std::string& path);
int64_t fileSize64(const std::string& path);
int64_t fileMtime64(const std::string& path);
bool removeFile(const std::string& path);
std::string timestampName();
bool reserveMediaPaths(const std::string& dir, const char* prefix, const char* ext,
    std::string* finalPath, std::string* partPath, int* errOut);
bool openMediaAt(const std::string& root, const std::string& input, std::string* nameOut, int* fdOut, int* errOut);
bool unlinkMediaAt(const std::string& root, const std::string& input, int* errOut);
std::string sha256Hex(const void* data, size_t size);
std::string sha256File(const std::string& path);
int clampi(int value, int lo, int hi);
bool pathHasParentRef(const std::string& path);
std::vector<std::string> listDir(const std::string& path);
std::string readSys(const std::string& path);

}  // namespace camera
