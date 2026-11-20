#pragma once
#include "common.hpp"
#include <algorithm>
#include <cctype>
namespace penchat {
inline std::string titleExcerpt(const std::string &text, size_t limit) {
  size_t end = 0;
  // Only inspect the requested prefix; do not allocate one string per scalar
  // for an entire long answer just to obtain its short naming excerpt.
  while (end < text.size() && limit--) {
    const auto c = static_cast<unsigned char>(text[end]);
    const size_t n = c < 128 ? 1 : c < 224 ? 2 : c < 240 ? 3 : 4;
    if (n > text.size() - end) break;
    end += n;
  }
  return text.substr(0, end);
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
inline bool genericImageTitle(std::string title) {
  for (auto &c : title) c = std::tolower(static_cast<unsigned char>(c));
  return title.empty() || title == "图片" || title == "[图片]" || title == "图片提问" ||
         title == "图像" || title == "照片" || title == "image" || title == "photo";
}
// A failed auxiliary request must not leave an image-only chat named after its
// attachment marker. Reuse public answer text, without another model request.
inline std::string imageAnswerTitle(const std::string &answer, size_t limit) {
  auto title = plainTitle(titleExcerpt(answer, 4096), limit);
  const auto punctuation = title.find_first_of("\n\r");
  if (punctuation != std::string::npos) title.resize(punctuation);
  for (const auto &end : {std::string("。"), std::string("！"), std::string("？")}) {
    const auto at = title.find(end);
    if (at != std::string::npos && at) title.resize(at);
  }
  title.erase(std::remove_if(title.begin(), title.end(), [](char c) { return c == '*' || c == '`'; }), title.end());
  return genericImageTitle(title) ? plainTitle("图像内容讨论", limit) : title;
}
} // namespace penchat
