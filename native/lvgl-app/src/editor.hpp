#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace penchat {
// Cursor positions are Unicode scalars, never UTF-8 bytes or UTF-16 units.
inline size_t editByte(const std::string &s, size_t index) {
  size_t i = 0;
  while (i < s.size() && index--) { ++i; while (i < s.size() && (static_cast<unsigned char>(s[i]) & 192) == 128) ++i; }
  return i;
}
inline size_t editLength(const std::string &s) {
  size_t n = 0; for (unsigned char c : s) if ((c & 192) != 128) ++n; return n;
}
inline size_t editUtf16(const std::string &s, size_t cursor) {
  size_t units = 0; for (size_t i=0, end=editByte(s,cursor); i<end; ++i)
    if ((static_cast<unsigned char>(s[i]) & 192) != 128) units += static_cast<unsigned char>(s[i]) >= 240 ? 2 : 1;
  return units;
}
struct EditorState {
  struct Snapshot { std::string text, preedit; size_t cursor; bool all; };
  std::string text, preedit, placeholder, id, notice;
  size_t cursor = 0, limit = 4096;
  bool active = false, chinese = true, multiline = false, secret = false, reveal = false, all = false;
  int mode = 0, shift = 0;
  std::vector<Snapshot> undo;
  void remember() { if (undo.size() == 20) undo.erase(undo.begin()); undo.push_back({text,preedit,cursor,all}); }
  bool insert(const std::string &value, bool record = true) {
    if ((all ? 0 : editLength(text)) + editLength(value) > limit) { notice="已达到长度上限"; return false; }
    if (record) remember();
    if (all) { text.clear(); cursor=0; all=false; }
    text.insert(editByte(text,cursor), value); cursor += editLength(value); return true;
  }
  bool raw() { if (preedit.empty()) return true; if (!insert(preedit)) return false; preedit.clear(); return true; }
  void erase() {
    if (preedit.empty() && text.empty()) return;
    remember();
    if (!preedit.empty()) preedit.pop_back();
    else if (all) { text.clear(); cursor=0; all=false; }
    else if (cursor) { auto a=editByte(text,cursor-1), b=editByte(text,cursor); text.erase(a,b-a); --cursor; }
  }
  void revert() { if (undo.empty()) return; auto s=undo.back(); undo.pop_back(); text=s.text; preedit=s.preedit; cursor=s.cursor; all=s.all; }
};
}
