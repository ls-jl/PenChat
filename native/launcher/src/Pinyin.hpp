#pragma once
#include <dlfcn.h>
#include <codecvt>
#include <locale>
#include <algorithm>
#include <cstdint>
#include <string>
// The ORANGE Falcon exports the Android Pinyin decoder C ABI. Resolve symbols,
// never code addresses. Falcon's `im` module owns initialization and dictionary
// lifetime; this adapter must never open/close the shared decoder or copy a dict.
class Pinyin {
  using Search = size_t (*)(const char *, size_t);
  using Candidate = const uint16_t *(*)(size_t,uint16_t *,size_t);
  using Split = size_t (*)(const uint16_t **);
  Search search_ = nullptr;
  Candidate candidate_ = nullptr;
  Split split_ = nullptr;
  size_t (*choose_)(size_t) = nullptr;
  size_t (*fixed_)() = nullptr;
  const char *(*raw_)(size_t *) = nullptr;
  void (*reset_)() = nullptr;
  std::string session_, text_;
  int revision_ = -1;
  size_t count_ = 0;
  std::string candidate(size_t n, size_t prefix = 512) {
    uint16_t buffer[513] = {};
    const auto result=candidate_(n,buffer,512);
    if(!result) return {};
    size_t length=0; while(length<512 && buffer[length]) ++length;
    std::u16string value(reinterpret_cast<char16_t *>(buffer),std::min(length,prefix));
    return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>,char16_t>().to_bytes(value);
  }
public:
  void reset(const std::string &session = "") {
    if(!session.empty() && session!=session_) return;
    if(reset_ && !session_.empty()) reset_();
    session_.clear(); text_.clear(); revision_=-1; count_=0;
  }
  JQUTIL_NS::Bson run(const JQUTIL_NS::Bson &input) {
    using JQUTIL_NS::Bson;
    auto session=input["session"].string_value();
    auto text=input["text"].string_value();
    int revision=input["revision"].int_value();
    auto fail=Bson::object{{"ok",false}};
    if(!search_) {
      search_=reinterpret_cast<Search>(dlsym(RTLD_DEFAULT,"im_search"));
      candidate_=reinterpret_cast<Candidate>(dlsym(RTLD_DEFAULT,"im_get_candidate"));
      split_=reinterpret_cast<Split>(dlsym(RTLD_DEFAULT,"im_get_spl_start_pos"));
      choose_=reinterpret_cast<size_t(*)(size_t)>(dlsym(RTLD_DEFAULT,"im_choose"));
      fixed_=reinterpret_cast<size_t(*)()>(dlsym(RTLD_DEFAULT,"im_get_fixed_len"));
      raw_=reinterpret_cast<const char *(*)(size_t *)>(dlsym(RTLD_DEFAULT,"im_get_sps_str"));
      reset_=reinterpret_cast<void(*)()>(dlsym(RTLD_DEFAULT,"im_reset_search"));
    }
    size_t parsed=0;
    if(!search_ || !candidate_ || !split_ || !choose_ || !fixed_ || !raw_ || !reset_ || !raw_(&parsed)) return fail;
    // The decoder has bounded internal composition buffers. Never silently cut
    // a user's composition; unsupported input remains in the native editor.
    if(session.empty() || text.size()>128 || text.find_first_not_of("abcdefghijklmnopqrstuvwxyz'")!=std::string::npos) return fail;
    if(input["operation"].string_value()=="choose") {
      int index=input["candidate"].int_value();
      if(session!=session_ || revision!=revision_ || text!=text_ || index<0 || size_t(index)>=count_) return fail;
      choose_(index);
      const uint16_t *starts=nullptr; size_t syllables=split_(&starts), fixed=fixed_();
      if(!starts || !fixed || fixed>syllables || syllables>128 || starts[fixed]>text.size()) { reset(); return fail; }
      const size_t consumed=starts[fixed];
      const auto committed=candidate(0,fixed);
      if(!consumed || committed.empty()) { reset(); return fail; }
      const auto remaining=text.substr(consumed);
      reset();
      return Bson::object{{"ok",true},{"committed",committed},{"remaining",remaining}};
    }
    reset();
    // A previous official editor may have left fixed segments in the shared
    // decoder. A new native query takes ownership of a fresh composition.
    reset_();
    session_=session; text_=text; revision_=revision;
    count_=search_(text.c_str(),text.size());
    Bson::array candidates;
    for(size_t i=0;i<std::min(count_,size_t(48));++i) { auto c=candidate(i); if(c.empty()) break; candidates.push_back(c); }
    count_=candidates.size();
    return Bson::object{{"ok",true},{"candidates",candidates}};
  }
};
