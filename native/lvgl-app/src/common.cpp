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
  if (!cfg.contains("screen") || !cfg.at("screen").is_object())
    throw std::runtime_error("Falcon configuration has no screen section");
  const auto &c = cfg.at("screen");
  auto integer = [&](const char *key, int minimum, int maximum, bool required) {
    if (!c.contains(key)) {
      if (!required) return 0;
      throw std::runtime_error(std::string("Missing Falcon screen.") + key);
    }
    const auto &v = c.at(key);
    if (!v.is_number_integer() || v.get<double>() < minimum || v.get<double>() > maximum)
      throw std::runtime_error(std::string("Invalid Falcon screen.") + key);
    return v.get<int>();
  };
  // Bounds cap allocation/DRM coordinates; they never supply missing geometry.
  s.width = integer("width", 200, 4096, true);
  s.height = integer("height", 100, 4096, true);
  s.rotation = integer("direction", 0, 270, false);
  s.xoffset = integer("xoffset", -4096, 4096, false);
  s.yoffset = integer("yoffset", -4096, 4096, false);
  s.touchRotation = c.contains("tp_direction") ? integer("tp_direction",0,270,false) : s.rotation;
  s.touchXoffset = integer("tp_xoffset",-4096,4096,false);
  s.touchYoffset = integer("tp_yoffset",-4096,4096,false);
  if(s.touchRotation!=0 && s.touchRotation!=90 && s.touchRotation!=180 && s.touchRotation!=270)
    throw std::runtime_error("Invalid touch rotation");
  if (s.rotation != 0 && s.rotation != 90 && s.rotation != 180 &&
      s.rotation != 270)
    throw std::runtime_error("Invalid screen rotation");
  return s;
}
Json Screen::descriptor() const {
  return {{"width",width},{"height",height},{"rotation",rotation},
          {"xoffset",xoffset},{"yoffset",yoffset}};
}
} // namespace penchat
