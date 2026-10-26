#include "pinyin.hpp"
#include "editor.hpp"
#include "pinyin_ime.h"
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace penchat {
namespace {
constexpr size_t UserLimit = 2 * 1024 * 1024, RecordLimit = 10000;
std::string digits(const std::string &pinyin) {
  static const char map[] = "22233344455566677778889999";
  std::string result;
  for (auto c : pinyin) if (c >= 'a' && c <= 'z') result += map[c-'a'];
  return result;
}
bool validWord(const Json &r) {
  if (!r.is_object() || !r.value("pinyin", Json()).is_string() || !r.value("word", Json()).is_string()) return false;
  const auto py=r["pinyin"].get<std::string>(), word=r["word"].get<std::string>();
  if (py.empty() || py.size()>128 || py.front()=='\'' || py.back()=='\'' || py.find("''")!=std::string::npos ||
      py.find_first_not_of("abcdefghijklmnopqrstuvwxyz'")!=std::string::npos || word.empty() || word.size()>128) return false;
  auto chars=editScalars(word);
  if(chars.empty() || chars.size()>32) return false;
  for(auto cp:chars) if(!((cp>=0x3400&&cp<=0x9fff)||(cp>=0x20000&&cp<=0x323af))) return false;
  return true;
}
std::string remainingFixed(std::string fixed, size_t consumed) {
  size_t offset=0, count=0;
  while(offset<fixed.size() && count<consumed) {
    if(fixed[offset]!='\'')++count;
    ++offset;
  }
  if(count!=consumed)return {};
  while(offset<fixed.size()&&fixed[offset]=='\'')++offset;
  return fixed.substr(offset);
}
}
struct PinyinService::Impl {
  Events &events;
  std::string resources, directory;
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<Json> queue;
  std::thread worker;
  bool stopping=false, initialized=false, dirty=false;
  pinyin_ime_t *engine=nullptr;
  Json profile=Json::array(), last=Json::object(), choices=Json::array();
  Impl(Events &e, std::string r, std::string d):events(e),resources(std::move(r)),directory(join(d,"ime")) {worker=std::thread([this]{run();});}
  ~Impl(){stop();if(engine)pinyin_ime_destroy(engine);}
  void stop() {
    {std::lock_guard<std::mutex> guard(mutex);stopping=true;
      queue.erase(std::remove_if(queue.begin(),queue.end(),[](const Json &j){return j.value("operation",std::string())=="query"||j.value("operation",std::string())=="choose";}),queue.end());}
    cv.notify_one();if(worker.joinable())worker.join();
  }
  bool enqueue(Json request) {
    std::lock_guard<std::mutex> guard(mutex);
    if(stopping)return false;
    if(request.value("operation",std::string())=="query")
      queue.erase(std::remove_if(queue.begin(),queue.end(),[](const Json &j){return j.value("operation",std::string())=="query";}),queue.end());
    if(queue.size()>=32)return false;
    queue.push_back(std::move(request));cv.notify_one();return true;
  }
  int privateDirectory() {
    if(mkdir(directory.c_str(),0700) && errno!=EEXIST)throw std::runtime_error("ime directory");
    int fd=open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)throw std::runtime_error("ime directory");
    return fd;
  }
  void notice(const char *text) {events.push("pinyin.notice",{{"message",text}});}
  void load() {
    initialized=true;
    int dir=-1,fd=-1;
    try {
      dir=privateDirectory();fd=openat(dir,"user.json",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
      if(fd<0) {if(errno!=ENOENT)throw std::runtime_error("user dictionary");close(dir);return;}
      struct stat st{};if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size>ssize_t(UserLimit)||st.st_size<=0)throw std::runtime_error("invalid user dictionary");
      std::string text(size_t(st.st_size),'\0');size_t at=0;
      while(at<text.size()) {auto n=read(fd,&text[at],text.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("read");at+=size_t(n);}
      close(fd);fd=-1;
      auto data=Json::parse(text);
      if(data.value("version",0)!=1 || !data.at("records").is_array() || data["records"].size()>RecordLimit)throw std::runtime_error("user format");
      std::set<std::string> keys;
      for(const auto &r:data["records"]) {
        if(!validWord(r)||(!r.value("count",Json()).is_number_unsigned()&&!r.value("count",Json()).is_number_integer())||
           !r.value("last",Json()).is_number_integer()||r["count"].get<int64_t>()<1||r["count"].get<int64_t>()>1000000||
           !keys.insert(r["pinyin"].get<std::string>()+":"+r["word"].get<std::string>()).second)throw std::runtime_error("user record");
      }
      profile=data["records"];close(dir);
    } catch(...) {
      profile=Json::array();if(fd>=0)close(fd);
      if(dir>=0) {const auto name="user.corrupt-"+uid("dictionary")+".json";renameat(dir,"user.json",dir,name.c_str());close(dir);}
      notice("拼音记忆无法读取，已保留原文件并使用基础词典。");
    }
  }
  void openEngine() {
    if(engine)return;
    engine=pinyin_ime_init(join(resources,"pinyin.txt").c_str(),join(resources,"dictionary.data").c_str());
    if(!engine)throw std::runtime_error("dictionary unavailable");
    for(const auto &r:profile)apply(r);
  }
  void apply(const Json &r) {
    if(engine)pinyin_ime_user_word(engine,r["pinyin"].get<std::string>().c_str(),r["word"].get<std::string>().c_str(),
                                 uint32_t(1000000+std::min<int64_t>(1000000,r["count"].get<int64_t>())*1000));
  }
  void save() {
    if(!dirty)return;
    std::sort(profile.begin(),profile.end(),[](const Json &a,const Json &b){return a["last"].get<int64_t>()>b["last"].get<int64_t>();});
    const auto oldSize=profile.size();
    if(profile.size()>RecordLimit)profile.erase(profile.begin()+RecordLimit,profile.end());
    auto dump=[this]{return Json{{"version",1},{"records",profile}}.dump();};
    size_t bytes=Json{{"version",1},{"records",Json::array()}}.dump().size();
    if(!profile.empty())bytes+=profile.size()-1;
    for(const auto &r:profile)bytes+=r.dump().size();
    while(bytes>UserLimit&&!profile.empty()) {
      bytes-=profile.back().dump().size()+(profile.size()>1?1:0);profile.erase(profile.end()-1);
    }
    const auto text=dump();
    if(profile.size()!=oldSize&&engine){pinyin_ime_destroy(engine);engine=nullptr;}
    int dir=-1,fd=-1;
    try {
      dir=privateDirectory();fd=openat(dir,"user.json.part",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
      if(fd<0)throw std::runtime_error("write");
      size_t at=0;while(at<text.size()){auto n=write(fd,text.data()+at,text.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("write");at+=size_t(n);}
      if(fsync(fd))throw std::runtime_error("sync");
      close(fd);fd=-1;
      if(renameat(dir,"user.json.part",dir,"user.json")||fsync(dir))throw std::runtime_error("publish");
      close(dir);dirty=false;
    }catch(...){if(fd>=0)close(fd);if(dir>=0)close(dir);notice("拼音记忆未能保存，文字输入已保留。");}
  }
  void learn(const Json &request) {
    const auto text=request.value("text",std::string());
    std::map<std::string,size_t> indices;
    for(size_t i=0;i<profile.size();++i)indices[profile[i]["pinyin"].get<std::string>()+":"+profile[i]["word"].get<std::string>()]=i;
    for(const auto &r:request.value("records",Json::array())) {
      if(!validWord(r)||text.find(r["word"].get<std::string>())==std::string::npos)continue;
      const auto key=r["pinyin"].get<std::string>()+":"+r["word"].get<std::string>();
      auto found=indices.find(key);
      const int64_t count=found==indices.end()?1:std::min<int64_t>(1000000,profile[found->second]["count"].get<int64_t>()+1);
      Json value={{"pinyin",r["pinyin"]},{"word",r["word"]},{"count",count},{"last",nowMs()}};
      if(found==indices.end()){indices[key]=profile.size();profile.push_back(value);}else profile[found->second]=value;
      apply(value);dirty=true;
    }
    // Recreate after eviction so old words cannot survive only in memory.
    if(profile.size()>RecordLimit&&engine){pinyin_ime_destroy(engine);engine=nullptr;}
    save();
  }
  Json query(const Json &request) {
    const auto raw=request.value("text",std::string()), fixed=request.value("fixed",std::string());
    const bool t9=request.value("layout",std::string())=="zh9";
    Json result=request;result["ok"]=true;result["candidates"]=Json::array();result["exact"]=Json::array();result["candidate_details"]=Json::array();
    result["candidate_version"]=request.value("sequence",uint64_t(0));
    last=request;choices=Json::array();
    if(raw.empty())return result;
    if(raw.size()>128){result["ok"]=false;result["error"]="拼音过长，请分段选词或按回车保留原文。";return result;}
    if(raw.find_first_not_of(t9?"23456789":"abcdefghijklmnopqrstuvwxyz'")!=std::string::npos)throw std::runtime_error("invalid composition");
    std::string input=raw;
    if(t9&&!fixed.empty()) {
      const auto prefix=digits(fixed);
      if(prefix.empty()||raw.compare(0,prefix.size(),prefix)!=0||fixed.find_first_not_of("abcdefghijklmnopqrstuvwxyz'")!=std::string::npos)throw std::runtime_error("invalid filter");
      input=fixed+(prefix.size()<raw.size()?"'"+raw.substr(prefix.size()):"");
    }
    openEngine();
    pinyin_ime_set_correction(engine,!t9&&request.value("correction",true));
    if(pinyin_ime_input(engine,input.c_str())!=PINYIN_IME_OK)return result;
    for(int i=0;i<std::min(48,pinyin_ime_get_candidate_count(engine));++i) {
      const auto *word=pinyin_ime_get_candidate(engine,i), *py=pinyin_ime_candidate_pinyin(engine,i);
      if(!word||!py)continue;
      const auto consumed=pinyin_ime_candidate_consumed(engine,i);
      if(consumed<=0||size_t(consumed)>raw.size())continue;
      choices.push_back({{"word",word},{"pinyin",py},{"consumed",consumed}});
      result["candidates"].push_back(word);
      result["candidate_details"].push_back({{"corrected",bool(pinyin_ime_candidate_corrected(engine,i))},{"pinyin",py},{"consumed",consumed}});
    }
    for(int i=0;i<std::min(64,pinyin_ime_get_k9_exact_count(engine));++i) {
      const char *value=pinyin_ime_get_k9_exact(engine,i);if(value)result["exact"].push_back(value);
    }
    return result;
  }
  Json choose(const Json &request) {
    Json result=request;result["ok"]=false;
    for(const auto *key:{"session","revision","text","fixed","layout","correction"})
      if(request.value(key,Json())!=last.value(key,Json()))return result;
    if(request.value("candidate_version",uint64_t(0))!=last.value("sequence",uint64_t(0)))return result;
    const int index=request.value("candidate",-1);
    if(index<0||size_t(index)>=choices.size())return result;
    auto chosen=choices[index];const auto raw=request.value("text",std::string());
    const int consumed=chosen["consumed"].get<int>();
    result["ok"]=true;result["committed"]=chosen["word"];result["pinyin"]=chosen["pinyin"];
    result["consumed"]=consumed;result["remaining"]=raw.substr(consumed);
    result["fixed"]=remainingFixed(request.value("fixed",std::string()),size_t(consumed));
    last=Json::object();choices=Json::array();return result;
  }
  void run() {
    for(;;) {
      Json request;
      {std::unique_lock<std::mutex> guard(mutex);cv.wait(guard,[this]{return stopping||!queue.empty();});
        if(queue.empty()&&stopping)break;
        request=std::move(queue.front());queue.pop_front();}
      if(!initialized)load();
      auto op=request.value("operation",std::string());
      try {
        if(op=="commit")learn(request);
        else if(op=="clear") {
          profile=Json::array();dirty=true;if(engine){pinyin_ime_destroy(engine);engine=nullptr;}last=Json::object();choices=Json::array();save();
          events.push("pinyin.cleared",{{"ok",!dirty}});
        } else {
          auto result=op=="choose"?choose(request):query(request);
          events.pushLatest("pinyin.result",std::move(result));
        }
      } catch(...) {
        if(op=="query"||op=="choose"){request["ok"]=false;request["error"]="本地拼音词典不可用，可重试或使用官方键盘。";events.pushLatest("pinyin.result",request);}
        else notice("拼音记忆操作失败，文字输入已保留。");
      }
    }
    save();
  }
};
PinyinService::PinyinService(Events &e,const std::string &r,const std::string &d):impl_(new Impl(e,r,d)){}
PinyinService::~PinyinService()=default;
bool PinyinService::request(Json j){return impl_->enqueue(std::move(j));}
void PinyinService::commit(const Json &records,const std::string &text){
  if(!records.empty()&&!impl_->enqueue({{"operation","commit"},{"records",records},{"text",text}}))impl_->notice("拼音记忆队列繁忙，文字输入已保留。");
}
void PinyinService::clear(){if(!impl_->enqueue({{"operation","clear"}}))impl_->notice("拼音记忆暂时无法清除，请稍后重试。");}
void PinyinService::stop(){impl_->stop();}
} // namespace penchat
