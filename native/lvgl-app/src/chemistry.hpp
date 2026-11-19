#pragma once
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>
namespace penchat {
// Native, bounded mhchem-style input adapter. MicroTeX supplies the glyphs and
// math layout; this layer supplies chemical upright atoms, scripts and arrows.
// It deliberately keeps ordinary TeX outside \ce unchanged.
namespace chemistry {
inline bool group(const std::string &s, size_t &at, char open, char close, std::string &out) {
  if (at >= s.size() || s[at] != open) return false;
  const auto begin = ++at;
  unsigned depth = 1;
  for (; at < s.size(); ++at) {
    if (s[at] == '\\' && at + 1 < s.size()) { ++at; continue; }
    if (s[at] == open && ++depth > 32) throw std::runtime_error("Chemical nesting too deep");
    if (s[at] == close && !--depth) { out = s.substr(begin, at++ - begin); return true; }
  }
  throw std::runtime_error("Unclosed chemical group");
}
inline std::string equation(const std::string &s, unsigned depth = 0) {
  if (depth > 32 || s.size() > 8192) throw std::runtime_error("Chemical formula too large");
  std::string out;
  bool atom = false;
  auto append = [&](const std::string &value) {
    if (out.size() + value.size() > 65536) throw std::runtime_error("Chemical expansion too large");
    out += value;
  };
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (std::isspace(c)) { append("\\,"); ++i; atom = false; continue; }
    std::string arrow;
    size_t length = 0;
    for (const auto &p : {std::make_pair("<=>", "\\rightleftharpoons"),
                          std::make_pair("<-->", "\\longleftrightarrow"),
                          std::make_pair("<->", "\\leftrightarrow"),
                          std::make_pair("->", "\\longrightarrow"),
                          std::make_pair("<-", "\\longleftarrow")})
      if (s.compare(i, std::char_traits<char>::length(p.first), p.first) == 0) {
        arrow = p.second; length = std::char_traits<char>::length(p.first); break;
      }
    if (length) {
      i += length;
      std::string above, below;
      group(s, i, '[', ']', above); group(s, i, '[', ']', below);
      if (!above.empty() || !below.empty()) {
        arrow = "\\mathop{" + arrow + "}\\limits";
        if (!above.empty()) arrow += "^{" + equation(above, depth + 1) + "}";
        if (!below.empty()) arrow += "_{" + equation(below, depth + 1) + "}";
      }
      append("\\;" + arrow + "\\;"); atom = false; continue;
    }
    if (std::isalpha(c)) {
      const auto begin = i++;
      while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i])) &&
             std::islower(static_cast<unsigned char>(s[i]))) ++i;
      append("\\mathrm{" + s.substr(begin, i - begin) + "}"); atom = true; continue;
    }
    if (std::isdigit(c)) {
      const auto begin = i++;
      while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
      auto number = s.substr(begin, i - begin);
      if (atom) append("_{" + number + "}");
      else { append(number); append("\\,"); }
      continue;
    }
    if (c == '^' || c == '_') {
      const char script = s[i++]; std::string value;
      if (!group(s, i, '{', '}', value)) {
        const auto begin = i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        if (begin == i && i < s.size()) ++i;
        value = s.substr(begin, i - begin);
      }
      if (value.empty()) throw std::runtime_error("Missing chemical script");
      append(std::string(1, script) + "{" + value + "}");
      atom = true; continue;
    }
    if (c == '{') {
      std::string text; group(s, i, '{', '}', text);
      // Braced text explicitly escapes chemical-number interpretation.
      append("\\text{" + text + "}"); atom = true; continue;
    }
    if (c == '\\') {
      const auto begin = i++;
      while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
      if (i == begin + 1 && i < s.size()) ++i;
      const auto command = s.substr(begin, i - begin);
      if (command == "\\ce") {
        std::string value;
        if (!group(s, i, '{', '}', value)) throw std::runtime_error("Missing chemical argument");
        append("{" + equation(value, depth + 1) + "}");
      } else append(command + " ");
      atom = true; continue;
    }
    if (c == '$') {
      const auto end = s.find('$', i + 1);
      if (end == std::string::npos) throw std::runtime_error("Unclosed chemical math");
      append("{" + s.substr(i + 1, end - i - 1) + "}"); i = end + 1; atom = true; continue;
    }
    if (c == '+' || c == '-') {
      const bool charge = atom && (i + 1 == s.size() || std::isspace(static_cast<unsigned char>(s[i + 1])) || s[i + 1] == ')');
      append(charge ? std::string("^{") + char(c) + "}" : c == '-' ? "\\! - \\!" : " + ");
      ++i; if (!charge) atom = false; continue;
    }
    if (s.compare(i, 2, "·") == 0) { append("\\cdot "); i += 2; atom = false; continue; }
    if (c == '*') { append("\\cdot "); ++i; atom = false; continue; }
    if (c == '#') { append("\\equiv "); ++i; atom = false; continue; }
    if (c == '~') { append("\\ "); ++i; atom = false; continue; }
    if (c == '%') { const auto end = s.find('\n', i); i = end == std::string::npos ? s.size() : end; continue; }
    append(s.substr(i++, 1));
    if (c == ')' || c == ']') atom = true;
    else if (c == '(' || c == '[' || c == '=' || c == '/') atom = false;
  }
  return out;
}
} // namespace chemistry
inline std::string chemistryTex(const std::string &source) {
  if (source.size() > 8192) throw std::runtime_error("Formula too large");
  std::string out;
  for (size_t i = 0; i < source.size();) {
    if (source.compare(i, 3, "\\ce") == 0 &&
        (i + 3 == source.size() || !std::isalpha(static_cast<unsigned char>(source[i + 3])))) {
      size_t at = i + 3; while (at < source.size() && std::isspace(static_cast<unsigned char>(source[at]))) ++at;
      std::string value;
      if (!chemistry::group(source, at, '{', '}', value)) throw std::runtime_error("Missing chemical argument");
      out += "{" + chemistry::equation(value) + "}"; i = at;
    } else if (source[i] == '\\' && i + 1 < source.size()) {
      out += source.substr(i, 2); i += 2;
    } else out += source[i++];
    if (out.size() > 65536) throw std::runtime_error("Chemical expansion too large");
  }
  return out;
}
} // namespace penchat
