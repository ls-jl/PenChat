#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
namespace penchat {
// Indices are Unicode scalars. Convert to UTF-16 only at the official IME bridge.
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
inline std::vector<uint32_t> editScalars(const std::string &s) {
  std::vector<uint32_t> out;
  for (size_t i=0;i<s.size();) {
    unsigned char first=s[i++]; uint32_t c=first;
    int remaining=0;
    if(first>=240) { c=first&7; remaining=3; }
    else if(first>=224) { c=first&15; remaining=2; }
    else if(first>=192) { c=first&31; remaining=1; }
    while(remaining-- && i<s.size()) c=(c<<6)|(static_cast<unsigned char>(s[i++])&63);
    out.push_back(c);
  }
  return out;
}
inline bool editCombining(uint32_t c) {
  return (c>=0x300&&c<=0x36f)||(c>=0x1ab0&&c<=0x1aff)||
      (c>=0x1dc0&&c<=0x1dff)||(c>=0x20d0&&c<=0x20ff)||
      (c>=0xfe00&&c<=0xfe0f)||(c>=0xfe20&&c<=0xfe2f)||
      (c>=0x1f3fb&&c<=0x1f3ff)||(c>=0xe0020&&c<=0xe007f)||
      (c>=0xe0100&&c<=0xe01ef);
}
inline std::vector<size_t> editBoundaries(const std::string &s) {
  auto chars=editScalars(s); std::vector<size_t> result{0}; size_t flags=0;
  for(size_t i=0;i<chars.size();++i) {
    bool flag=chars[i]>=0x1f1e6&&chars[i]<=0x1f1ff;
    if(i && !editCombining(chars[i]) && chars[i]!=0x200d && chars[i-1]!=0x200d &&
        !(flag && flags%2)) result.push_back(i);
    flags=flag?flags+1:0;
  }
  if(result.back()!=chars.size()) result.push_back(chars.size());
  return result;
}
inline size_t editBoundary(const std::string &s,size_t index,bool forward=false) {
  auto boundaries=editBoundaries(s); index=std::min(index,editLength(s));
  auto next=std::lower_bound(boundaries.begin(),boundaries.end(),index);
  if(next==boundaries.end()) return boundaries.back();
  return *next==index||forward||next==boundaries.begin()?*next:*std::prev(next);
}
struct EditorState {
  struct Composition { std::string raw, fixed, word, pinyin; int pieces=0; };
  struct Snapshot {
    std::string text, preedit, fixed, word, pinyin;
    std::array<Composition,2> parked;
    size_t cursor, anchor; bool all; int layout, pieces;
  };
  // Main layout and numeric/symbol page are independent state machines.
  int layout=0, preferredChinese=0, chainPieces=0;
  uint64_t lastGlobe=0;
  bool globeChain=false;
  std::string fixed, chainWord, chainPinyin;
  std::array<Composition,2> parked;
  void contentChanged() { globeChain=false; }
  void changeLayout(int next) {
    if(layout!=1)parked[layout==2?1:0]={preedit,fixed,chainWord,chainPinyin,chainPieces};
    preedit.clear();fixed.clear();chainWord.clear();chainPinyin.clear();chainPieces=0;
    layout=next;chinese=next!=1;mode=0;
    if(chinese) {
      auto &stored=parked[layout==2?1:0];
      preedit=stored.raw;fixed=stored.fixed;chainWord=stored.word;chainPinyin=stored.pinyin;chainPieces=stored.pieces;
      stored=Composition{};
    }
  }
  void globe(uint64_t now) {
    const bool rapid=globeChain&&now>=lastGlobe&&now-lastGlobe<2000;
    const int next=rapid?(layout+1)%3:layout==1?preferredChinese:1;
    changeLayout(next);lastGlobe=now;globeChain=true;
  }
  std::string text, preedit, placeholder, id, notice;
  size_t cursor=0, anchor=0, limit=4096;
  bool active=false, chinese=true, multiline=false, secret=false, reveal=false, all=false;
  int mode=0, shift=0;
  std::vector<Snapshot> undo;
  std::pair<size_t,size_t> range() const { if(all)return {0,editLength(text)};return {std::min(cursor,anchor),std::max(cursor,anchor)}; }
  bool selected() const { auto r=range();return r.first!=r.second; }
  void position(size_t index) { cursor=editBoundary(text,index);anchor=cursor;all=false; }
  void select(size_t start,size_t end) { all=false;anchor=editBoundary(text,start);cursor=editBoundary(text,end,true); }
  std::string selection() const { auto r=range();auto a=editByte(text,r.first);return text.substr(a,editByte(text,r.second)-a); }
  size_t retained() const { auto r=range();return editLength(text)-(r.second-r.first); }
  void remember() { if(undo.size()==20) undo.erase(undo.begin());undo.push_back({text,preedit,fixed,chainWord,chainPinyin,parked,cursor,anchor,all,layout,chainPieces}); }
  bool insert(const std::string &value,bool record=true) {
    if(retained()+editLength(value)>limit) { notice="已达到长度上限";return false; }
    if(record) remember();
    auto r=range();auto a=editByte(text,r.first);text.replace(a,editByte(text,r.second)-a,value);
    cursor=r.first+editLength(value);anchor=cursor;all=false;return true;
  }
  bool commit(const std::string &value) { auto combined=preedit+value;if(!insert(combined))return false;preedit.clear();fixed.clear();chainWord.clear();chainPinyin.clear();chainPieces=0;return true; }
  bool raw() { if(preedit.empty()) return true;if(!insert(preedit)) return false;preedit.clear();fixed.clear();chainWord.clear();chainPinyin.clear();chainPieces=0;return true; }
  void erase() {
    if(preedit.empty() && !selected() && !cursor) return;
    remember();
    if(!preedit.empty()) { preedit.pop_back(); fixed.clear(); }
    else if(selected()) insert("",false);
    else { auto end=cursor;auto start=editBoundary(text,cursor-1);text.erase(editByte(text,start),editByte(text,end)-editByte(text,start));position(start); }
  }
  void word(size_t at) {
    auto chars=editScalars(text);if(chars.empty())return;
    at=std::min(at,chars.size()-1);size_t begin=editBoundary(text,at),end=editBoundary(text,at+1,true);
    auto asciiWord=[](uint32_t c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';};
    if(asciiWord(chars[at])) { while(begin && asciiWord(chars[begin-1]))--begin;while(end<chars.size()&&asciiWord(chars[end]))++end; }
    select(begin,end);
  }
  void revert() { if(undo.empty())return;auto s=undo.back();undo.pop_back();text=s.text;preedit=s.preedit;fixed=s.fixed;chainWord=s.word;chainPinyin=s.pinyin;parked=s.parked;layout=s.layout;chinese=layout!=1;chainPieces=s.pieces;cursor=s.cursor;anchor=s.anchor;all=s.all;contentChanged(); }
};
struct EditorClipboard {
  std::deque<std::string> records;
  std::string current;
  bool sensitiveCurrent=false;
  bool copy(const std::string &text,bool sensitive=false) {
    if(text.empty()||text.size()>512*1024) return false;
    current=text;sensitiveCurrent=sensitive;
    if(sensitive) return true;
    records.erase(std::remove(records.begin(),records.end(),text),records.end());records.push_front(text);
    size_t bytes=0;for(auto &s:records)bytes+=s.size();
    while(records.size()>20||bytes>512*1024) {bytes-=records.back().size();records.pop_back();}
    return true;
  }
  void clear() { records.clear();current.clear();sensitiveCurrent=false; }
};
}
