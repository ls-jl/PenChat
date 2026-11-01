#include "feedback.hpp"
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace penchat {
namespace {
constexpr size_t ReportLimit=1024*1024, QueueLimit=10*1024*1024;
bool validId(const std::string &id) {
  if(id.size()!=36)return false;
  for(size_t i=0;i<id.size();++i)
    if(i==8||i==13||i==18||i==23){if(id[i]!='-')return false;}
    else if(!std::isxdigit(static_cast<unsigned char>(id[i])))return false;
  return true;
}
void cleanupParts(const std::string &path,bool reports) {
  std::unique_ptr<DIR,decltype(&closedir)> directory(opendir(path.c_str()),closedir);
  if(!directory)return;
  while(auto entry=readdir(directory.get())) {
    const std::string name=entry->d_name;
    if(name.size()<43 || name.substr(name.size()-5)!=".part")continue;
    const auto split=name.size()-42;
    if(name[split]!='.'||!validId(name.substr(split+1,36)))continue;
    const auto base=name.substr(0,split);
    const bool owned=reports?(base.size()==41&&base.substr(36)==".json"&&validId(base.substr(0,36))):
      base=="draft.json"||base=="receipts.json"||base=="route.json"||base=="diagnostic-current.json"||base=="diagnostic-previous.json";
    struct stat st{};const auto file=join(path,name);
    if(owned&&!lstat(file.c_str(),&st)&&S_ISREG(st.st_mode))unlink(file.c_str());
  }
}
void privateDirectory(const std::string &path) {
  struct stat st{};
  if(lstat(path.c_str(),&st)==0&&!S_ISDIR(st.st_mode))throw std::runtime_error("反馈目录不是普通目录");
  ensureDirectory(path);
}
Json runtimeFields(const Json &input) {
  Json out=Json::object();
  if(!input.is_object())return out;
  for(auto key:{"generating","compressing","camera_ready","camera_angle","camera_auto","scanner_available","storage_unsaved","storage_failed"}) {
    auto v=input.find(key);if(v!=input.end()&&(v->is_boolean()||v->is_number_integer()))out[key]=*v;
  }
  return out;
}
std::string failureName(long status,int curl,bool confirmed) {
  if(status==201&&!confirmed)return "服务器确认异常，可能已经收到反馈，请稍后手动核对或重试";
  if(status==401)return "反馈服务鉴权失败，请联系开发者";
  if(status==404)return "反馈服务地址不可用";
  if(status==413)return "服务器拒绝了报告大小";
  if(status==429)return "反馈服务繁忙，等待后重试";
  if(status==507)return "反馈节点存储不足";
  if(status>=500)return "反馈服务暂时不可用";
  if(!status) {
    if(curl==6)return "反馈服务域名解析失败";
    if(curl==7)return "无法连接反馈服务";
    if(curl==60)return "反馈服务证书校验失败";
    return curl==28?"反馈发送超时，服务器可能已经收到":"反馈连接失败或网络繁忙";
  }
  return "反馈未被服务器确认（HTTP "+std::to_string(status)+"）";
}
}
struct Feedback::Impl {
  struct Upload {std::string id,body;unsigned route=0;uint64_t serial=0;};
  Events &events;
  std::string root,folder;
  Json screen;
  bool native;
  FeedbackConfig config;
  mutable std::mutex mutex;
  std::condition_variable cv,uploadCv;
  std::deque<Event> commands;
  std::thread worker,uploader;
  std::atomic<bool> closing{false},httpCancel{false};
  bool stopped=false,storageReady=false;
  Json published=Json::object(),draft=Json::object(),records=Json::array(),receipts=Json::array();
  Json report=Json::object();
  std::vector<std::string> pages;
  std::vector<std::string> publishedPages;
  std::shared_ptr<Upload> active,uploadSlot;
  std::string error,notice,keyStep,lastAction;
  uint64_t actionSequence=0;
  uint64_t keyGeneration=0,revision=0,preparedRevision=0,serial=0;
  unsigned preferredRoute=0;
  bool collecting=false,dispatchPaused=false;
  int64_t lastFlush=0, nextDispatch=0;
  FeedbackKeys keys;
  Impl(Events &e,std::string data,Json geometry,bool physical,FeedbackConfig settings)
      :events(e),root(join(data,".feedback")),folder(join(root,"reports")),screen(std::move(geometry)),
       native(physical),config(std::move(settings)),keys([this](Json result){enqueue("keys.result",std::move(result));}) {
    try {uploader=std::thread([this]{uploadLoop();});worker=std::thread([this]{run();});}
    catch(...){closing=true;uploadCv.notify_all();if(uploader.joinable())uploader.join();throw;}
  }
  ~Impl(){stop();}
  void enqueue(const std::string &op,Json args=Json::object()) {
    std::lock_guard<std::mutex> lock(mutex);
    if(closing&&op!="keys.result"&&op!="upload.result")return;
    if(commands.size()>=64) {
      if(op!="keys.result"&&op!="upload.result")return;
      auto expendable=std::find_if(commands.begin(),commands.end(),[](const Event &e){return e.type!="upload.result";});
      if(expendable!=commands.end())commands.erase(expendable);else return;
    }
    commands.push_back({op,std::move(args)});cv.notify_all();
  }
  void publish(const std::string &action="") {
    if(!action.empty()){lastAction=action;++actionSequence;}
    Json list=Json::array();
    for(const auto &record:records) {
      Json row=Json::object();
      for(auto key:{"id","time_ms","state","attempts","due_ms","reason","bytes","auto"})if(record.count(key))row[key]=record[key];
      if(dispatchPaused) {
        row["auto"]=false;row["reason"]="本地保存失败，自动发送已暂停，请重试";
        if(row.value("state",std::string())=="sending"&&!active)row["state"]="manual";
      }
      list.push_back(row);
    }
    Json state={{"ready",storageReady},{"enabled",config.enabled()},{"description",draft.value("description",std::string())},
      {"adapt",draft.value("adapt",false)},{"steps",draft.value("steps",Json::object())},
      {"collecting",collecting},{"key_step",keyStep},{"key_generation",keyGeneration},{"error",error},{"notice",notice},
      {"records",list},{"receipts",receipts},{"pending",records.size()},
      {"preview_ready",!report.empty()&&preparedRevision==revision},
      {"preview_id",report.value("event_id",std::string())},
      {"preview_pages",pages.size()},{"report_bytes",report.empty()?0:report.dump().size()},
      {"revision",revision},{"action",lastAction},{"action_sequence",actionSequence}};
    {std::lock_guard<std::mutex> lock(mutex);published=state;publishedPages=pages;}
    events.pushLatest("feedback_state",state);
  }
  void saveDraft(){feedbackWrite(join(root,"draft.json"),draft);}
  void clearPreview(){++revision;report=Json::object();pages.clear();}
  void save(const Json &record){feedbackWrite(join(folder,record.at("id").get<std::string>()+".json"),record);}
  Json *find(const std::string &id){for(auto &r:records)if(r.value("id",std::string())==id)return &r;return nullptr;}
  void load() {
    privateDirectory(root);privateDirectory(folder);cleanupParts(root,false);cleanupParts(folder,true);storageReady=true;
    draft=feedbackRead(join(root,"draft.json"),128*1024);
    if(!draft.is_object())draft=Json::object();
    auto description=draft.value("description",Json());
    if(!description.is_string()||characters(description.get<std::string>()).size()>2000)draft["description"]="";
    if(!draft.value("adapt",Json()).is_boolean())draft["adapt"]=false;
    if(!draft.value("steps",Json()).is_object())draft["steps"]=Json::object();
    for(auto &step:draft["steps"].items()) {
      if(!step.value().is_object()){step.value()=Json::object();continue;}
      if(step.value().value("status",Json())=="listening") {
        step.value()["status"]="cancelled";step.value()["interrupted"]=true;
      }
      if(step.value().value("status",Json())=="pressed") {
        step.value()["status"]="release_missing";step.value()["interrupted"]=true;
      }
    }
    const auto preference=feedbackRead(join(root,"route.json"),1024);
    if(preference.is_object()&&preference.value("route",Json()).is_number_unsigned())
      preferredRoute=preference["route"].get<unsigned>()%(config.nodes.size()+1);
    receipts=feedbackRead(join(root,"receipts.json"),16384);
    if(!receipts.is_array()||receipts.size()>10)receipts=Json::array();
    size_t total=0;
    std::unique_ptr<DIR,decltype(&closedir)> directory(opendir(folder.c_str()),closedir);
    if(auto dir=directory.get()) {
      while(auto item=readdir(dir)) {
        const std::string name=item->d_name;
        if(name.size()!=41||name.substr(36)!=".json"||!validId(name.substr(0,36)))continue;
        auto r=feedbackRead(join(folder,name),ReportLimit+8192);
        if(r.is_object()&&r.value("state",Json())=="sent_cleanup") {
          if(unlink(join(folder,name).c_str())&&errno!=ENOENT)error="已发送反馈的本地记录仍未清理";
          continue;
        }
        if(!r.is_object()||r.value("id",Json())!=name.substr(0,36)||
           !r.value("payload",Json()).is_object()||r["payload"].value("event_id",Json())!=r["id"]||r["payload"].dump().size()>ReportLimit||
           !r.value("auto",Json()).is_boolean()||!r.value("state",Json()).is_string()||!r.value("due_ms",Json()).is_number_integer()||
           (!r.value("route",Json()).is_number_integer()||r["route"].get<int64_t>()<0||r["route"].get<uint64_t>()>4)||
           !r.value("attempts",Json()).is_number_integer()||r["attempts"].get<int>()<0||r["attempts"].get<int>()>4) {
          error="有损坏的本地反馈已保留，未上传";
          if(records.size()<10)records.push_back({{"id",name.substr(0,36)},{"time_ms",0},{"state","invalid"},
            {"attempts",0},{"due_ms",0},{"auto",false},{"route",0},{"bytes",0},{"reason","本地报告损坏，仅可删除"}});
          continue;
        }
        const auto bytes=r.dump().size();
        if(records.size()>=10||bytes>ReportLimit+8192||total+bytes>QueueLimit){error="反馈存储超出上限，额外文件已保留但不自动发送";continue;}
        total+=bytes;
        if(!r.value("reason",Json()).is_string())r["reason"]="上次保留的反馈";
        r["bytes"]=r["payload"].dump().size();
        if(!r.value("time_ms",Json()).is_number_integer())r["time_ms"]=0;
        if(r.value("state",std::string())=="sending") {
          r["state"]=r.value("auto",false)&&r["attempts"].get<int>()<4?"queued":"manual";
          r["reason"]="上次发送中断，服务器可能已经收到";r["due_ms"]=nowMs();save(r);
        }
        records.push_back(std::move(r));
      }
    }
    publish();
  }
  void prepare(const Json &runtime) {
    if(collecting)throw std::runtime_error("请先完成或停止按键采集");
    report={{"format_version",1},{"event_id",feedbackUuid()},{"time_ms",nowMs()},
      {"app",{{"name",PENCHAT_NAME},{"version",PENCHAT_VERSION},{"platform","linux-native"}}},
      {"description",draft.value("description",std::string())},{"screen",screen},
      {"hardware",feedbackHardware(native)},{"runtime",runtimeFields(runtime)},
      {"diagnostics",diagnosticRecords()}};
    if(draft.value("adapt",false))report["adaptation"]=draft.value("steps",Json::object());
    size_t removed=0;
    for(auto name:{"previous_run","current_run"})
      while(report.dump().size()>ReportLimit&&!report["diagnostics"][name].empty()) {
        report["diagnostics"][name].erase(report["diagnostics"][name].begin());++removed;
      }
    if(removed)report["omitted"]={{"diagnostic_events",removed}};
    if(report.dump().size()>ReportLimit) {report=Json::object();throw std::runtime_error("报告超过1MiB，请取消适配信息后重试");}
    preparedRevision=revision;
    const auto pretty=report.dump(2,' ',false,Json::error_handler_t::replace);
    pages.clear();size_t offset=0;
    while(offset<pretty.size()) {
      size_t end=std::min(offset+4096,pretty.size());
      while(end<pretty.size()&&(static_cast<unsigned char>(pretty[end])&0xc0)==0x80)--end;
      pages.push_back(pretty.substr(offset,end-offset));offset=end;
    }
    publish("prepared");
  }
  std::pair<size_t,size_t> spoolUsage() {
    size_t count=0,bytes=0;
    if(auto dir=opendir(folder.c_str())) {
      while(auto entry=readdir(dir)) {
        const std::string name=entry->d_name;
        if(name=="."||name=="..")continue;
        struct stat st{};
        if(lstat(join(folder,name).c_str(),&st)==0) {
          ++count;
          bytes+=size_t(std::max<off_t>(0,std::min<off_t>(st.st_size,QueueLimit+1)));
          if(bytes>QueueLimit)break;
        }
      }
      closedir(dir);
    }
    return {count,bytes};
  }
  void submit(uint64_t expected) {
    if(!config.enabled())throw std::runtime_error("此构建未配置反馈上传服务");
    if(report.empty()||preparedRevision!=revision||expected!=revision)throw std::runtime_error("内容已变化，请重新预览");
    const auto usage=spoolUsage();
    if(usage.first>=10)throw std::runtime_error("待发送反馈已满，请先删除或发送已有记录");
    Json record={{"id",report["event_id"]},{"time_ms",nowMs()},{"payload",report},{"state","queued"},
      {"attempts",0},{"due_ms",nowMs()},{"auto",true},{"route",preferredRoute},
      {"reason","等待发送"},{"bytes",report.dump().size()}};
    size_t bytes=record.dump().size()+usage.second;
    if(bytes>QueueLimit)throw std::runtime_error("待发送反馈已占满10MiB，请先处理已有记录");
    save(record);records.push_back(std::move(record));clearPreview();
    draft={{"description",""},{"adapt",false},{"steps",Json::object()}};saveDraft();
    notice="反馈已加入发送队列";publish("submitted");
  }
  void schedule() {
    if(active||dispatchPaused||!storageReady||!config.enabled()||closing||ticks()<nextDispatch)return;
    for(auto &r:records) {
      if(!r.value("auto",false)||r.value("state",std::string())!="queued"||r.value("due_ms",int64_t(0))>nowMs())continue;
      auto candidate=r;const auto attempt=candidate.value("attempts",0)+1;
      if(attempt>4){r["auto"]=false;r["state"]="manual";save(r);continue;}
      candidate["state"]="sending";candidate["attempts"]=attempt;candidate["reason"]="正在发送";save(candidate);r=std::move(candidate);
      auto task=std::make_shared<Upload>();task->id=r["id"];task->body=r["payload"].dump();
      task->route=r.value("route",0u)%(config.nodes.size()+1);task->serial=++serial;
      {std::lock_guard<std::mutex> lock(mutex);if(closing)return;httpCancel=false;active=task;uploadSlot=task;uploadCv.notify_all();}
      nextDispatch=ticks()+10*config.retryUnitMs;
      publish();break;
    }
  }
  void uploadLoop() {
    for(;;) {
      std::shared_ptr<Upload> task;
      {std::unique_lock<std::mutex> lock(mutex);uploadCv.wait(lock,[&]{return closing||uploadSlot;});if(closing)break;task=std::move(uploadSlot);}
      Json result={{"id",task->id},{"serial",task->serial},{"status",0},{"curl",0},{"retry_after",0},{"confirmed",false}};
      try {
        ApiOptions options;options.timeout=30;
        HttpRoute route;
        if(task->route) {
          const auto begin=config.endpoint.find("://")+3,end=config.endpoint.find('/',begin);
          const auto authority=config.endpoint.substr(begin,end-begin);const auto colon=authority.find(':');
          route.host=authority.substr(0,colon);route.port=colon==std::string::npos?443:unsigned(std::stoi(authority.substr(colon+1)));
          route.target=config.nodes.at(task->route-1);
        }
        std::string confirmation;
        auto receive=[&confirmation](const char *data,size_t size) {
          if(confirmation.size()+size>65536)throw std::runtime_error("Feedback receipt too large");
          confirmation.append(data,size);
        };
        const auto response=http(config.endpoint,"POST",{{"Authorization","Bearer "+config.token},
          {"Content-Type","application/json; charset=utf-8"}},task->body,httpCancel,receive,options,{},
          {"penchat-feedback",config.endpoint,"",""},route);
        const auto json=Json::parse(confirmation,nullptr,false);
        const bool confirmed=response.status==201&&response.curlCode==0&&response.error.empty()&&
          json.is_object()&&json.value("ok",Json()).is_boolean()&&json["ok"].get<bool>();
        result["status"]=response.status;result["curl"]=response.curlCode;
        result["retry_after"]=response.retryAfterSeconds;result["confirmed"]=confirmed;
      } catch(...) { /* Generic failure; do not retain headers, URLs or bodies. */ }
      enqueue("upload.result",std::move(result));
    }
  }
  void uploaded(const Json &reply) {
    if(!active||active->serial!=reply.value("serial",uint64_t(0)))return;
    const auto route=active->route;const auto id=active->id;active.reset();
    auto r=find(id);if(!r)return;
    const long status=reply.value("status",0L);const int curl=reply.value("curl",0);
    if(reply.value("confirmed",false)) {
      preferredRoute=route;
      try{feedbackWrite(join(root,"route.json"),{{"route",route}});}catch(...){}
      if(unlink(join(folder,id+".json").c_str())&&errno!=ENOENT) {
        r->erase("payload");(*r)["state"]="sent_cleanup";(*r)["auto"]=false;
        (*r)["reason"]="已发送，本地记录清理失败";save(*r);
      } else records.erase(std::find_if(records.begin(),records.end(),[&](const Json &v){return v["id"]==id;}));
      receipts.push_back({{"id",id},{"time_ms",nowMs()}});while(receipts.size()>10)receipts.erase(receipts.begin());
      feedbackWrite(join(root,"receipts.json"),receipts);notice="反馈已发送，编号 "+id.substr(0,8);
    } else {
      const auto reason=failureName(status,curl,false);(*r)["reason"]=reason;
      const bool temporary=!status||status==429||status==507||(status>=500&&status<=599);
      const auto attempts=r->value("attempts",4);
      if(temporary&&r->value("auto",false)&&attempts<4) {
        const int delays[]={10,30,120};
        const int jitter=int(id.back()%3); // Stable small per-report jitter.
        auto delay=int64_t(delays[attempts-1]+jitter)*config.retryUnitMs;
        const auto retry=std::max<int64_t>(0,reply.value("retry_after",int64_t(0)));
        const auto time=std::max<int64_t>(0,nowMs());
        const auto maximum=std::numeric_limits<int64_t>::max()-time;
        delay=std::max(delay,retry>maximum/1000?maximum:retry*1000);
        (*r)["due_ms"]=time+delay;(*r)["state"]="queued";
        if(!status||status==507||status>=500)(*r)["route"]=(route+1)%(config.nodes.size()+1);
        notice=reason+"，将后台重试（"+std::to_string(attempts)+"/3）";
      } else {
        (*r)["state"]="manual";(*r)["auto"]=false;
        notice=reason+"；已保留，可稍后重试或删除";
      }
      save(*r);
    }
    diagnosticRecord("feedback","upload_result",{{"status",status},{"curl_code",curl}});publish("upload_result");
  }
  void process(const Event &command) {
    const auto &op=command.type;const auto &args=command.data;
    if(op=="upload.result"){uploaded(args);return;}
    if(op=="keys.result") {
      if(args.value("generation",uint64_t(0))!=keyGeneration||!draft.value("adapt",false))return;
      auto value=args;value.erase("generation");draft["steps"][args.at("step").get<std::string>()]=value;
      const auto status=args.value("status",std::string());
      collecting=status=="pressed"||status=="listening";if(!collecting)keyStep.clear();
      clearPreview();saveDraft();publish("keys");return;
    }
    error.clear();notice.clear();
    if(!storageReady)throw std::runtime_error("本地反馈目录不可用，正常聊天不受影响");
    if(op=="draft") {
      if(args.contains("description")) {
        const auto text=args.at("description").get<std::string>();
        if(characters(text).size()>2000)throw std::runtime_error("问题描述不能超过2000字");
        draft["description"]=text;
      }
      if(args.contains("adapt")) {
        draft["adapt"]=args.at("adapt").get<bool>();
        if(!draft["adapt"].get<bool>()){++keyGeneration;keys.cancel();collecting=false;keyStep.clear();draft["steps"]=Json::object();}
      }
      clearPreview();saveDraft();publish();
    } else if(op=="prepare") {prepare(args);}
    else if(op=="submit")submit(args.value("revision",uint64_t(0)));
    else if(op=="keys.start") {
      const auto step=args.value("step",std::string());
      if(!draft.value("adapt",false)||(step!="scan"&&step!="home"))throw std::runtime_error("请先开启机型适配信息采集");
      ++keyGeneration;collecting=true;keyStep=step;draft["steps"].erase(step);clearPreview();saveDraft();
      if(native)keys.start(step,keyGeneration);
      else {
#ifdef __aarch64__
        throw std::runtime_error("当前环境不能采集按键");
#endif
      }
      publish("keys_start");
    } else if(op=="keys.cancel"||op=="background") {
      if(op=="background"&&collecting&&keyStep=="home")return;
      keys.cancel();
      if(!native){collecting=false;keyStep.clear();}
      publish();
    } else if(op=="keys.skip") {
      keys.cancel();++keyGeneration;collecting=false;keyStep.clear();
      const auto step=args.value("step",std::string());if(step!="scan"&&step!="home")return;
      draft["steps"][step]={{"step",step},{"status","skipped"},{"events",Json::array()}};clearPreview();saveDraft();publish("keys");
    } else if(op=="retry"||op=="stop_auto"||op=="delete") {
      const auto id=args.value("id",std::string());auto r=find(id);if(!r)throw std::runtime_error("反馈记录不存在");
      if(op=="delete") {
        if(active&&active->id==id)httpCancel=true;
        if(unlink(join(folder,id+".json").c_str())&&errno!=ENOENT)throw std::runtime_error("本地反馈删除失败");
        records.erase(std::find_if(records.begin(),records.end(),[&](const Json &v){return v["id"]==id;}));
      } else if(op=="stop_auto") {
        if(active&&active->id==id)httpCancel=true;
        (*r)["auto"]=false;(*r)["state"]="manual";(*r)["reason"]="已停止自动重试";save(*r);
      } else {
        if(!r->value("payload",Json()).is_object())throw std::runtime_error("本地报告损坏，仅可删除");
        if(active&&active->id==id)throw std::runtime_error("正在结束上一次请求，请稍候");
        (*r)["attempts"]=0;(*r)["auto"]=true;(*r)["state"]="queued";(*r)["due_ms"]=nowMs();(*r)["route"]=preferredRoute;
        (*r)["reason"]="等待重新发送";save(*r);dispatchPaused=false;
      }
      publish();
    }
  }
  void run() {
    try{load();}catch(...){storageReady=false;error="本地反馈目录无法读取或保存，正常聊天不受影响";publish();}
    for(;;) {
      std::deque<Event> pending;
      int64_t delay=1000;
      if(!active && !dispatchPaused && storageReady && config.enabled())for(const auto &r:records)
        if(r.value("auto",false)&&r.value("state",std::string())=="queued")
          delay=std::min(delay,std::max<int64_t>(1,std::max(r.value("due_ms",int64_t(0))-nowMs(),nextDispatch-ticks())));
      {std::unique_lock<std::mutex> lock(mutex);cv.wait_for(lock,std::chrono::milliseconds(delay),[&]{return closing||!commands.empty();});pending.swap(commands);}
      if(closing) {
        keys.cancel();keys.join();httpCancel=true;uploadCv.notify_all();if(uploader.joinable())uploader.join();
        {std::lock_guard<std::mutex> lock(mutex);while(!commands.empty()){pending.push_back(std::move(commands.front()));commands.pop_front();}}
        for(const auto &item:pending)if(item.type=="keys.result"||item.type=="upload.result")try{process(item);}catch(...){}
        flushDiagnostics();break;
      }
      for(const auto &item:pending) {
        try{process(item);}catch(const std::exception &e){
          if(item.type=="upload.result")dispatchPaused=true;
          if(item.type=="keys.start"){++keyGeneration;keys.cancel();collecting=false;keyStep.clear();}
          error=e.what();publish("error");
        }
      }
      try{schedule();}catch(...){dispatchPaused=true;error="反馈记录保存失败，已停止本次发送";publish("error");}
      if(ticks()-lastFlush>=1000){flushDiagnostics();lastFlush=ticks();}
    }
  }
  void stop() {
    {std::lock_guard<std::mutex> lock(mutex);if(stopped)return;stopped=true;closing=true;httpCancel=true;cv.notify_all();uploadCv.notify_all();}
    if(worker.joinable())worker.join();
    if(uploader.joinable())uploader.join();
  }
};
Feedback::Feedback(Events &e,const std::string &data,const Json &screen,bool native,FeedbackConfig config)
    :p_(new Impl(e,data,screen,native,std::move(config))){}
Feedback::~Feedback()=default;
void Feedback::request(const std::string &op,Json args){p_->enqueue(op,std::move(args));}
Json Feedback::state() const {std::lock_guard<std::mutex> lock(p_->mutex);return p_->published;}
std::string Feedback::previewPage(size_t page)const{std::lock_guard<std::mutex> lock(p_->mutex);return page<p_->publishedPages.size()?p_->publishedPages[page]:std::string();}
void Feedback::background(){request("background");}
void Feedback::stop(){p_->stop();}
} // namespace penchat
