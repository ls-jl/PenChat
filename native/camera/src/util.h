#pragma once
#include <cstddef>
#include <string>
namespace camera {
std::string joinPath(const std::string& base, const std::string& name);
std::string parentDir(const std::string& path);
std::string baseName(const std::string& path);
std::string extensionOf(const std::string& path);
std::string readFile(const std::string& path, size_t maxBytes = 1 << 20);
bool dirExists(const std::string& path);
bool ensureDir(const std::string& path);
bool reserveMediaPaths(const std::string& dir, const char* prefix, const char* ext,
    std::string* finalPath, std::string* partPath, int* errOut);
} // namespace camera
