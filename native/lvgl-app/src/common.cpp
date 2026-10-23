#include "common.hpp"
#include <cerrno>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace penchat {
std::string uid(const std::string &prefix) {
  static std::atomic<unsigned> seed{0};
  return prefix + "_" + std::to_string(nowMs()) + "_" + std::to_string(++seed);
}
std::string join(const std::string &root, const std::string &leaf) {
  return root + (root.empty() || root.back() == '/' ? "" : "/") + leaf;
}
void ensureDirectory(const std::string &path) {
  if (path.empty())
    return;
  for (size_t i = 1; i <= path.size(); ++i)
    if (i == path.size() || path[i] == '/') {
      auto p = path.substr(0, i);
      if (mkdir(p.c_str(), 0700) && errno != EEXIST)
        throw std::runtime_error("Cannot create data directory");
    }
}
std::string fileText(const std::string &path, size_t limit) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return {};
  std::string out;
  char b[8192];
  while (f.read(b, sizeof(b)) || f.gcount()) {
    if (out.size() + f.gcount() > limit)
      throw std::runtime_error("File size limit exceeded");
    out.append(b, f.gcount());
  }
  return out;
}
void atomicText(const std::string &path, const std::string &content) {
  auto tmp = path + ".part";
  {
    std::ofstream f(tmp, std::ios::binary);
    f << content;
    if (!f)
      throw std::runtime_error("Cannot write file");
  }
  if (rename(tmp.c_str(), path.c_str()))
    throw std::runtime_error("Cannot commit file");
}
std::string utf8(uint32_t c) {
  std::string s;
  if (c < 0x80)
    s += char(c);
  else if (c < 0x800) {
    s += char(0xc0 | (c >> 6));
    s += char(0x80 | (c & 63));
  } else if (c < 0x10000) {
    s += char(0xe0 | (c >> 12));
    s += char(0x80 | ((c >> 6) & 63));
    s += char(0x80 | (c & 63));
  } else {
    s += char(0xf0 | (c >> 18));
    s += char(0x80 | ((c >> 12) & 63));
    s += char(0x80 | ((c >> 6) & 63));
    s += char(0x80 | (c & 63));
  }
  return s;
}
std::vector<std::string> characters(const std::string &v) {
  std::vector<std::string> result;
  for (size_t i = 0; i < v.size();) {
    unsigned char c = v[i];
    size_t n = c < 128 ? 1 : (c < 224 ? 2 : (c < 240 ? 3 : 4));
    n = std::min(n, v.size() - i);
    result.push_back(v.substr(i, n));
    i += n;
  }
  return result;
}
Screen Screen::fromConfig(const Json &cfg) {
  Screen s;
  const auto c = cfg.value("screen", Json::object());
  s.width = c.value("width", 960);
  s.height = c.value("height", 266);
  s.rotation = c.value("direction", 270);
  s.xoffset = c.value("xoffset", 0);
  s.yoffset = c.value("yoffset", 107);
  if (s.width < 200 || s.height < 100 || s.width > 4096 || s.height > 4096)
    throw std::runtime_error("Invalid screen size");
  if (s.rotation != 0 && s.rotation != 90 && s.rotation != 180 &&
      s.rotation != 270)
    throw std::runtime_error("Invalid screen rotation");
  return s;
}
} // namespace penchat
