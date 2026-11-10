// PenChat bundled public trust store. Apache-2.0.
#pragma once
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace penchat {
inline std::string webTrustPath(const std::string &executable) {
  const auto slash = executable.rfind('/');
  if (executable.empty() || executable.front() != '/' || slash == std::string::npos)
    throw std::runtime_error("Cannot locate bundled HTTPS CA certificates");
  return executable.substr(0, slash) + "/../web-search/cacert.pem";
}
inline void validateWebTrust(const std::string &path) {
  struct stat info {};
  if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_size <= 0 || info.st_size > 512 * 1024)
    throw std::runtime_error("Bundled HTTPS CA certificates missing or invalid; reinstall PenChat");
  std::ifstream input(path, std::ios::binary);
  std::string pem(static_cast<size_t>(info.st_size), '\0');
  if (!input.read(&pem[0], pem.size()) ||
      pem.find("-----BEGIN CERTIFICATE-----") == std::string::npos ||
      pem.find("-----END CERTIFICATE-----") == std::string::npos)
    throw std::runtime_error("Bundled HTTPS CA certificates unreadable or invalid; reinstall PenChat");
}
inline std::string bundledWebTrust() {
  char executable[4096];
  const auto length = readlink("/proc/self/exe", executable, sizeof(executable));
  if (length <= 0 || size_t(length) == sizeof(executable))
    throw std::runtime_error("Cannot locate bundled HTTPS CA certificates");
  const auto path = webTrustPath(std::string(executable, size_t(length)));
  validateWebTrust(path);
  return path;
}
} // namespace penchat
