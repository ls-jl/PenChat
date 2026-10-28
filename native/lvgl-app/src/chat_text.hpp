#pragma once
#include <string>

namespace penchat {
// Unicode White_Space only: keep zero-width joiners and other text formatting.
inline size_t chatWhitespace(const std::string &text, size_t at) {
  const auto c = static_cast<unsigned char>(text[at]);
  if (c == ' ' || (c >= '\t' && c <= '\r')) return 1;
  if (text.compare(at, 2, "\xc2\x85") == 0 || text.compare(at, 2, "\xc2\xa0") == 0) return 2;
  if (text.compare(at, 3, "\xe1\x9a\x80") == 0 ||
      text.compare(at, 3, "\xe2\x80\xaf") == 0 || text.compare(at, 3, "\xe2\x81\x9f") == 0 ||
      text.compare(at, 3, "\xe3\x80\x80") == 0) return 3;
  if (at + 2 < text.size() && c == 0xe2 && static_cast<unsigned char>(text[at + 1]) == 0x80) {
    const auto last = static_cast<unsigned char>(text[at + 2]);
    if ((last >= 0x80 && last <= 0x8a) || last == 0xa8 || last == 0xa9) return 3;
  }
  return 0;
}
inline bool hasChatText(const std::string &text) {
  for (size_t i = 0; i < text.size();) {
    const auto space = chatWhitespace(text, i);
    if (!space) return true;
    i += space;
  }
  return false;
}
// Remove empty boundary lines, not indentation, Markdown hard breaks or the
// blank lines separating paragraphs/code inside a message. Drafts stay intact.
inline std::string normalizedChatText(const std::string &text) {
  size_t first = std::string::npos, last = 0, line = 0;
  bool content = false;
  for (size_t i = 0; i <= text.size();) {
    size_t newline = 0;
    if (i < text.size()) {
      if (text[i] == '\n') newline = 1;
      else if (text[i] == '\r') newline = i + 1 < text.size() && text[i + 1] == '\n' ? 2 : 1;
      else if (text.compare(i, 2, "\xc2\x85") == 0) newline = 2;
      else if (text.compare(i, 3, "\xe2\x80\xa8") == 0 || text.compare(i, 3, "\xe2\x80\xa9") == 0) newline = 3;
    }
    if (i == text.size() || newline) {
      if (content) { if (first == std::string::npos) first = line; last = i; }
      if (i == text.size()) break;
      i += newline; line = i; content = false;
    } else {
      const auto space = chatWhitespace(text, i);
      if (!space) content = true;
      i += space ? space : 1;
    }
  }
  return first == std::string::npos ? std::string() : text.substr(first, last - first);
}
} // namespace penchat
