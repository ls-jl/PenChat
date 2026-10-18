#pragma once
#include "common.hpp"
#include <algorithm>
namespace penchat {
inline std::string titleExcerpt(const std::string &text, size_t limit) {
  std::string out;
  for (const auto &c : characters(text)) {
    if (!limit--)
      break;
    out += c;
  }
  return out;
}
inline std::string plainTitle(const std::string &raw, size_t limit) {
  std::string source = raw;
  auto parsed = Json::parse(raw, nullptr, false);
  if (parsed.is_object() && parsed.contains("title") &&
      parsed["title"].is_string())
    source = parsed["title"];
  else if (parsed.is_string())
    source = parsed.get<std::string>();
  size_t at = 0;
  while (at <= source.size()) {
    auto end = source.find('\n', at);
    std::string line =
        source.substr(at, end == std::string::npos ? end : end - at);
    auto start = line.find_first_not_of(" \t\r#");
    if (start != std::string::npos)
      line.erase(0, start);
    else
      line.clear();
    if (!line.empty() && line.find("```") != 0) {
      for (const auto &prefix :
           {std::string("标题："), std::string("标题:"), std::string("Title:")})
        if (line.find(prefix) == 0)
          line.erase(0, prefix.size());
      auto parts = characters(line);
      auto trim = [](const std::string &c) {
        return c == " " || c == "\t" || c == "\r" || c == "　" || c == "\"" ||
               c == "'" || c == "`" || c == "“" || c == "”" || c == "「" ||
               c == "」";
      };
      while (!parts.empty() && trim(parts.front()))
        parts.erase(parts.begin());
      while (!parts.empty() && trim(parts.back()))
        parts.pop_back();
      std::string out;
      for (size_t i = 0; i < std::min(limit, parts.size()); ++i)
        out += parts[i];
      if (!out.empty())
        return out;
    }
    if (end == std::string::npos)
      break;
    at = end + 1;
  }
  return {};
}
} // namespace penchat
