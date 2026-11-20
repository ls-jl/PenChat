#include "update.hpp"
#include "feedback.hpp"
#include <curl/curl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <set>
#include <stdexcept>
namespace penchat {
namespace {
std::string escaped(const std::string &text){auto value=curl_easy_escape(nullptr,text.c_str(),int(text.size()));if(!value)throw std::runtime_error("更新文件名编码失败");std::string out=value;curl_free(value);return out;}
bool amr(std::string name){std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});return name.size()>4&&name.substr(name.size()-4)==".amr";}
bool greater(const std::string &a,const std::string &b){updates::Version left,right;return updates::Version::parse(a,left)&&updates::Version::parse(b,right)&&right<left;}
void ownedDirectory(const std::string &path){struct stat s{};if(!lstat(path.c_str(),&s)&&!S_ISDIR(s.st_mode))throw std::runtime_error("更新目录不可用");ensureDirectory(path);}
}
struct Updater::Impl {
  Events &events;std::string folder;updates::Config config;
  mutable std::mutex mutex;std::condition_variable wake;std::thread worker;
  std::deque<Event> commands;std::atomic<bool> closing{false},cancelled{false},declineCancel{false};
  Json value=Json::object(),published=Json::object(),saved=Json::object(),recovery=Json::object(),notes=Json::array(),release=Json::object();
  bool stopped=false,manual=false;uint64_t generation=0;std::unique_ptr<updates::Transport> transport;
  Impl(Events &e,const std::string &data,updates::Config settings):events(e),folder(join(data,".updates")),config(std::move(settings)) {
    try {
      ownedDirectory(folder);
      saved=feedbackRead(join(folder,"state.json"),128*1024);if(!saved.is_object())saved=Json::object();
      recovery=feedbackRead(join(folder,"restore.json"),256*1024);
      if(!recovery.is_object()||recovery.value("appid",Json())!=config.appId)recovery=Json::object();
      value={{"status","idle"},{"current",config.current},{"generation",0}};
      const auto ticket=feedbackRead(join(folder,"install.json"),65536);
      if(ticket.is_object()&&ticket.value("appid",Json())==config.appId) {
        if(ticket.value("version",std::string())==config.current) {
          value["status"]="installed";value["message"]="更新已完成";
          unlink(join(folder,"package.amr").c_str());unlink(join(folder,"install.json").c_str());
        } else if(ticket.value("state",Json())=="requested" || ticket.value("state",Json())=="accepted" || ticket.value("state",Json())=="prepared") {
          value["status"]="uncertain";value["message"]="上次安装结果尚未确认，可手动检查后重试";
          value["ticket"]=ticket;
        } else if(ticket.value("state",Json())=="failed") {
          value["ticket"]=ticket;value["status"]="install_error";value["message"]="上次安装未成功，可重试已校验的安装包";
        }
      }
      unlink(join(folder,"download.part").c_str());
    }catch(...){value={{"status","error"},{"message","本地更新目录不可用，正常聊天不受影响"},{"current",config.current}};}
    publish();worker=std::thread([this]{run();});
  }
  ~Impl(){stop();}
  void persist(){feedbackWrite(join(folder,"state.json"),saved);}
  void publish() {
    value["generation"]=generation;value["manual"]=manual;value["notes"]=notes;value["release"]=release;
    {std::lock_guard<std::mutex> lock(mutex);published=value;}
    events.pushLatest("update_state",value);
  }
  void status(const std::string &state,const std::string &message=""){value["status"]=state;value["message"]=message;publish();}
  void enqueue(const std::string &op,Json data) {
    std::lock_guard<std::mutex> lock(mutex);if(closing||commands.size()>=16)return;
    commands.push_back({op,std::move(data)});wake.notify_all();
  }
  void fault(const std::string &version,const std::string &kind,int count=0) {
    // Feedback owns durable send-once de-duplication. No report body or hardware
    // state is taken from App::diagnostics or the current chat.
    events.push("update_release_fault",{{"appid",config.appId},{"current",config.current},{"version",version},{"kind",kind},{"count",count}});
  }
  bool ok(const updates::Response &r)const{return r.code==0&&r.status==200;}
  updates::Transport &network(){if(!transport)transport.reset(new updates::Transport(config,cancelled));return *transport;}
  void check(bool explicitCheck) {
#ifdef PENCHAT_CVI_PLATFORM
    (void)explicitCheck;
    status("unsupported","当前 S6 构建未配置对应更新源，请手动安装 ARM32 包");
    return;
#endif
    manual=explicitCheck;release=Json::object();notes=Json::array();++generation;value.erase("note_text");status("checking","正在检查更新…");
    auto addresses=config.nodes;
    if(addresses.empty()) {
      auto begin=config.root.find("://");begin=begin==std::string::npos?0:begin+3;auto end=config.root.find('/',begin);
      auto host=config.root.substr(begin,end-begin);const auto colon=host.find(':');if(colon!=std::string::npos)host=host.substr(0,colon);
      addresses=updates::resolve(host,cancelled);
    }
    if(addresses.empty())addresses.push_back("");
    if(addresses.size()>16)addresses.resize(16);
    std::vector<updates::Request> queries;for(const auto &a:addresses)queries.push_back({config.root,a});
    const auto roots=network().fetch(queries);std::map<std::string,updates::Version> versions;bool rootValid=false;
    for(size_t i=0;i<roots.size();++i)if(ok(roots[i])) {
      try{for(const auto &link:updates::directoryLinks(roots[i].body,config.root)) {
        updates::Version version;if(link.directory&&updates::Version::parse(link.name,version)&&greater(link.name,config.current))versions[link.name]=version;
      }rootValid=true;}catch(...){}
    }
    if(!rootValid)throw std::runtime_error("无法读取更新目录，请检查网络后重试");
    std::vector<std::pair<std::string,updates::Version>> ordered(versions.begin(),versions.end());
    std::sort(ordered.begin(),ordered.end(),[](const auto &a,const auto &b){return b.second<a.second;});
    for(const auto &candidate:ordered) {
      if(cancelled)throw std::runtime_error("cancelled");
      const auto base=config.root+escaped(candidate.first)+"/";
      queries.clear();for(const auto &a:addresses)queries.push_back({base,a});const auto listings=network().fetch(queries);
      std::vector<Json> available;bool publicationFault=false;std::string reason="amr_count";int wrongCount=0;
      for(size_t n=0;n<addresses.size();++n) {
        if(!ok(listings[n]))continue;
        std::vector<updates::Link> links;
        try{links=updates::directoryLinks(listings[n].body,base);}catch(...){continue;}
        std::vector<std::string> files;std::set<std::string> names;
        for(const auto &l:links)if(!l.directory){names.insert(l.name);if(amr(l.name))files.push_back(l.name);}
        if(files.size()!=1){publicationFault=true;wrongCount=int(files.size());continue;}
        const auto filename=files[0],url=base+escaped(filename);std::string hash;
        if(names.count(filename+".sha256")) {
          const auto checks=network().fetch({{url+".sha256",addresses[n],false,4096}})[0];
          if(ok(checks)){try{hash=updates::checksum(checks.body,filename);}catch(...){publicationFault=true;reason="invalid_checksum";continue;}}
          else if(checks.status!=404)continue;
        }
        available.push_back({{"address",addresses[n]},{"url",url},{"filename",filename},{"sha256",hash},{"samples",Json::array()}});
      }
      for(unsigned round=0;round<3&&!available.empty();++round) {
        queries.clear();for(const auto &a:available)queries.push_back({a["url"],a["address"],true});
        const auto heads=network().fetch(queries);
        for(size_t i=0;i<heads.size();++i) {
          const auto &head=heads[i];auto &a=available[i];
          if(!ok(head)||head.length<=0||uint64_t(head.length)>updates::PackageLimit)continue;
          if(a.contains("size")&&(a["size"]!=head.length||a.value("etag",std::string())!=head.etag)){a["unstable"]=true;continue;}
          a["size"]=head.length;a["etag"]=head.etag;a["modified"]=head.modified;a["samples"].push_back(head.ttfb);
        }
      }
      available.erase(std::remove_if(available.begin(),available.end(),[](const Json &a){return a["samples"].size()!=3||a.value("unstable",false);}),available.end());
      if(available.empty()){if(publicationFault)fault(candidate.first,reason,wrongCount);continue;}
      for(auto &a:available){auto samples=a["samples"].get<std::vector<int64_t>>();std::sort(samples.begin(),samples.end());a["latency_us"]=samples[samples.size()/2];a.erase("samples");}
      std::stable_sort(available.begin(),available.end(),[](const Json &a,const Json &b){return a["latency_us"]<b["latency_us"];});
      const auto primary=available.front();Json nodes=Json::array({primary});
      if(!primary.at("sha256").get<std::string>().empty())for(size_t i=1;i<available.size();++i) {
        const auto &a=available[i];if(a["filename"]==primary["filename"]&&a["size"]==primary["size"]&&a["sha256"]==primary["sha256"]){nodes.push_back(a);break;}
      }
      release={{"version",candidate.first},{"filename",primary["filename"]},{"url",primary["url"]},{"size",primary["size"]},
        {"sha256",primary["sha256"]},{"nodes",nodes},{"weak",primary.at("sha256").get<std::string>().empty()}};
      // Missing notes are omitted; every available intermediate version remains
      // indexed. Text lives on disk and only the selected page enters UI state.
      for(auto it=ordered.rbegin();it!=ordered.rend();++it)if(!greater(it->first,candidate.first)) {
        const auto url=config.root+escaped(it->first)+"/release-note.md";
        for(const auto &a:addresses) {
          const auto response=network().fetch({{url,a,false,256*1024}})[0];
          if(response.status==404)continue;
          if(ok(response)) {
            const auto name=join(folder,"note-"+it->first+".md");atomicText(name,response.body);trimNotes(name);
            notes.push_back({{"version",it->first},{"url",url},{"address",a},{"path",name},{"bytes",response.body.size()}});break;
          }
          if(response.status==200){notes.push_back({{"version",it->first},{"url",url},{"address",a},{"error","更新说明超过256KiB，无法展示"}});break;}
        }
      }
      const auto ignored=saved.value("declined",std::string());
      value["suppressed"]=!manual&&!ignored.empty()&&!greater(candidate.first,ignored);
      status("available",release.value("weak",false)?"此版本未提供独立 SHA-256 校验，是否仍然下载并安装？":"发现新版本");
      if(!notes.empty())loadNote(0,0);
      return;
    }
    value["suppressed"]=false;status("current","当前没有可用的新版本");
  }
  void trimNotes(const std::string &keep="") {
    DIR *dir=opendir(folder.c_str());if(!dir)return;
    std::vector<std::pair<int64_t,std::string>> files;uint64_t bytes=0;
    while(auto e=readdir(dir)) {
      const std::string name=e->d_name;
      if(name.compare(0,5,"note-")||name.size()<9||name.substr(name.size()-3)!=".md")continue;
      updates::Version version;if(!updates::Version::parse(name.substr(5,name.size()-8),version))continue;
      const auto path=join(folder,name);struct stat st{};
      if(!lstat(path.c_str(),&st)&&S_ISREG(st.st_mode)){bytes+=st.st_size;files.push_back({st.st_mtime,path});}
    }
    closedir(dir);std::sort(files.begin(),files.end());
    for(const auto &f:files)if(bytes>8*1024*1024&&f.second!=keep) {struct stat st{};if(!lstat(f.second.c_str(),&st)&&!unlink(f.second.c_str()))bytes-=st.st_size;}
  }
  void loadNote(size_t index,size_t page) {
    if(index>=notes.size())return;
    const auto &note=notes[index];value["note_index"]=index;value["note_page"]=page;
    if(note.contains("error")){value["note_text"]=note["error"];value["note_pages"]=1;publish();return;}
    const auto path=note.at("path").get<std::string>();
    struct stat st{};
    if(lstat(path.c_str(),&st)||!S_ISREG(st.st_mode)) {
      const auto response=network().fetch({{note.at("url"),note.at("address"),false,256*1024}})[0];
      if(!ok(response))throw std::runtime_error("此版本更新说明暂时无法读取，请重试");
      atomicText(path,response.body);trimNotes(path);
    }
    const auto text=fileText(path,256*1024);std::vector<std::string> pages;size_t offset=0;
    while(offset<text.size()){size_t end=std::min(offset+8192,text.size());if(end<text.size()){auto paragraph=text.rfind("\n\n",end);if(paragraph!=std::string::npos&&paragraph>offset+4096)end=paragraph+2;while(end>offset&&(static_cast<unsigned char>(text[end])&0xc0)==0x80)--end;}pages.push_back(text.substr(offset,end-offset));offset=end;}
    if(pages.empty())pages.push_back("");
    page=std::min(page,pages.size()-1);value["note_page"]=page;value["note_pages"]=pages.size();value["note_text"]=pages[page];publish();
  }
  void decline() {
    if(!release.empty()){const auto old=saved.value("declined",std::string());const auto target=release.at("version").get<std::string>();if(old.empty()||greater(target,old))saved["declined"]=target;persist();}
    status("deferred","已稍后更新，下一个版本再提醒");
  }
  void download(const Json &args) {
    if(release.empty())throw std::runtime_error("请先检查更新");
    if(release.value("weak",false)&&!args.value("allow_weak",false))throw std::runtime_error("需要先同意缺少校验的更新提示");
    const auto snapshot=args.value("snapshot",Json::object());
    unlink(join(folder,"download.part").c_str());++generation;status("downloading","正在下载更新…");
    const auto part=join(folder,"download.part");
    const auto statistics=network().download(release,part,[this](Json progress){value["progress"]=std::move(progress);publish();});
    status("verifying","下载完成，正在校验安装包…");
    Json verified;
    try{verified=updates::validatePackage(part,config.appId,release.at("version"),release.at("sha256"),cancelled);}
    catch(...){unlink(part.c_str());if(!cancelled)fault(release.at("version"),"package_validation");throw;}
    if(cancelled){unlink(part.c_str());throw std::runtime_error("cancelled");}
    const auto package=join(folder,"package.amr");if(rename(part.c_str(),package.c_str()))throw std::runtime_error("无法保存已校验安装包");
    feedbackWrite(join(folder,"restore.json"),{{"appid",config.appId},{"snapshot",snapshot},{"target",release["version"]}});
    Json ticket={{"operation",feedbackUuid()},{"appid",config.appId},{"version",release["version"]},{"path",package},
      {"sha256",verified["sha256"]},{"state","prepared"},{"time_ms",nowMs()}};
    feedbackWrite(join(folder,"install.json"),ticket);value["ticket"]=ticket;value["statistics"]=statistics;status("ready","校验成功，正在准备安装…");
  }
  void process(const Event &command) {
    const auto &op=command.type;const auto &args=command.data;
    if(op=="check")check(args.value("manual",true));
    else if(op=="note")loadNote(args.value("index",size_t(0)),args.value("page",size_t(0)));
    else if(op=="decline")decline();
    else if(op=="download")download(args);
    else if(op=="restore_ack"){unlink(join(folder,"restore.json").c_str());}
    else if(op=="install_requested") {
      auto ticket=value.at("ticket");ticket["state"]="requested";feedbackWrite(join(folder,"install.json"),ticket);value["ticket"]=ticket;status("installing","正在安装，请稍候…");
    } else if(op=="install_result") {
      if(value.value("ticket",Json::object()).value("operation",std::string())!=args.value("operation",std::string()))return;
      auto ticket=value.at("ticket");ticket["state"]=args.value("success",false)?"installed":"failed";feedbackWrite(join(folder,"install.json"),ticket);
      value["ticket"]=ticket;status(args.value("success",false)?"installed":"install_error",args.value("success",false)?"安装已完成":"安装失败，旧数据已保留，可重试安装");
    } else if(op=="retry_install") {
      auto ticket=value.value("ticket",Json::object());if(ticket.empty())ticket=feedbackRead(join(folder,"install.json"),65536);
      auto verified=updates::validatePackage(ticket.at("path"),config.appId,ticket.at("version"),ticket.at("sha256"),cancelled);
      feedbackWrite(join(folder,"restore.json"),{{"appid",config.appId},{"snapshot",args.value("snapshot",Json::object())},{"target",ticket["version"]}});
      ticket["operation"]=feedbackUuid();ticket["state"]="prepared";feedbackWrite(join(folder,"install.json"),ticket);value["ticket"]=ticket;status("ready","正在准备安装…");
    }
  }
  void run() {
    if(config.autoCheck&&value.value("status",std::string())!="uncertain"&&value.value("status",std::string())!="install_error")enqueue("check",{{"manual",false}});
    for(;;) {
      Event command;
      {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return closing||!commands.empty();});if(closing)break;command=std::move(commands.front());commands.pop_front();cancelled=false;declineCancel=false;}
      try{process(command);}catch(const std::exception &error){
        unlink(join(folder,"download.part").c_str());
        if(closing)break;
        if(cancelled){if(declineCancel)try{decline();}catch(...){status("error","取消已生效，但稍后设置保存失败");}else status("cancelled","更新已取消");}
        else status("error",error.what());
      }
    }
    unlink(join(folder,"download.part").c_str());
    transport.reset();
  }
  void stop(){if(stopped)return;stopped=true;closing=true;cancelled=true;wake.notify_all();if(worker.joinable())worker.join();}
};
Updater::Updater(Events &e,const std::string &data,updates::Config config):p_(new Impl(e,data,std::move(config))){}
Updater::~Updater()=default;
void Updater::request(const std::string &op,Json data){p_->enqueue(op,std::move(data));}
void Updater::cancel(bool decline){
  std::lock_guard<std::mutex> lock(p_->mutex);p_->declineCancel=decline;p_->cancelled=true;
  p_->commands.erase(std::remove_if(p_->commands.begin(),p_->commands.end(),[](const Event &e){return e.type=="check"||e.type=="download"||e.type=="note";}),p_->commands.end());
  if(decline)p_->commands.push_back({"decline",Json::object()});
  p_->wake.notify_all();
}
Json Updater::state()const{std::lock_guard<std::mutex> lock(p_->mutex);return p_->published;}
Json Updater::recovery()const{return p_->recovery;}
std::string Updater::note(size_t)const{return state().value("note_text",std::string());}
void Updater::stop(){p_->stop();}
}
