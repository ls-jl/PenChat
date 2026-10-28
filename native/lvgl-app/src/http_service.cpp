#include "network.hpp"
#include <curl/curl.h>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <map>
#include <set>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace penchat {
namespace {
constexpr size_t ResponseLimit = 32 * 1024 * 1024, TailLimit = 64 * 1024;
constexpr size_t ActiveLimit = 4, QueueLimit = 8, PoolLimit = 8, SocketLimit = 8;
class HttpService {
  struct Pool;
  struct Job {
    std::string url, method, provider, key;
    Json headers;
    ApiOptions options;
    const std::string *sourceBody = nullptr; // Caller waits until published.
    std::string body;
    size_t upload = 0, bytes = 0, tailBytes = 0;
    std::atomic<bool> *cancel = nullptr;
    std::function<void(const char *, size_t)> chunks;
    std::function<bool()> complete;
    std::promise<HttpResult> promise;
    HttpResult result;
    CURL *easy = nullptr;
    curl_slist *headerList = nullptr;
    Pool *pool = nullptr;
    int attempts = 0;
    bool published = false, logical = false, retryDenied = false;
    int64_t submitted = ticks(), started = 0, tailStarted = 0;
  };
  struct Pool {
    HttpService *owner;
    std::string key, provider;
    CURLSH *share = nullptr;
    size_t active = 0, sockets = 0;
    bool retired = false;
    int64_t lastUsed = ticks();
    ~Pool() { if (share) curl_share_cleanup(share); }
  };
  std::mutex mutex_;
  std::thread thread_;
  std::deque<std::shared_ptr<Job>> queued_;
  std::set<std::string> invalidations_;
  bool invalidateAll_ = false, stopping_ = false;
  CURLM *wake_ = nullptr, *multi_ = nullptr;
  std::atomic<int> idle_{90};
  std::vector<std::unique_ptr<Pool>> pools_;
  std::map<CURL *, std::shared_ptr<Job>> active_;
  std::map<curl_socket_t, Pool *> sockets_;
  std::mutex diagnosticsMutex_;
  Json diagnostics_ = {{"active",0},{"draining",0},{"queued",0},{"pools",0},{"sockets",0},{"running",false}};
  Json last_ = Json::object();
  bool initialized_ = false;

  void wakeLocked() { if (wake_) curl_multi_wakeup(wake_); }
  static std::string origin(const std::string &url) {
    const auto protocol = url.find("://");
    return url.substr(0, url.find('/', protocol == std::string::npos ? 0 : protocol + 3));
  }
  static bool cancelled(const Job &j) { return j.cancel && j.cancel->load(); }
  static size_t header(char *p, size_t a, size_t b, void *data) {
    auto &j = *static_cast<Job *>(data); const auto n = a * b;
    try {
      if (j.published) return n;
      std::string line(p, n);
      if (line.compare(0, 5, "HTTP/") == 0) {
        const auto at = line.find(' ');
        if (at != std::string::npos) j.result.status = std::strtol(line.c_str() + at + 1, nullptr, 10);
        j.result.contentType.clear(); j.result.requestId.clear();
      } else {
        const auto at = line.find(':');
        if (at != std::string::npos) {
          auto key = line.substr(0, at), value = line.substr(at + 1);
          for (auto &c : key) c = std::tolower(static_cast<unsigned char>(c));
          while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
          auto first = value.find_first_not_of(" \t"); value = first == std::string::npos ? "" : value.substr(first);
          if (key == "content-type") j.result.contentType = value.substr(0, 256);
          if (key == "x-request-id" || key == "request-id")
            j.result.requestId = Json::parse(Json(value.substr(0,256)).dump(-1,' ',false,Json::error_handler_t::replace)).get<std::string>();
        }
      }
      return n;
    } catch (...) { return 0; }
  }
  static bool protocolDone(Job &j) {
    if (j.logical) return true;
    if (j.complete && j.complete()) {
      j.logical = true; j.tailStarted = ticks(); return true;
    }
    return false;
  }
  static size_t receive(char *p, size_t a, size_t b, void *data) {
    auto &j = *static_cast<Job *>(data); const auto n = a * b;
    if (cancelled(j)) return 0;
    try {
      if (j.bytes + n > ResponseLimit) throw std::runtime_error("Response size limit exceeded");
      j.bytes += n;
      if (j.chunks && j.result.status >= 200 && j.result.status < 300) {
        size_t used = 0;
        // Split only at line boundaries. The protocol callback can finish at an
        // SSE delimiter without passing any later tail bytes to its stack state.
        while (used < n && !protocolDone(j)) {
          const auto lf = static_cast<const char *>(memchr(p + used, '\n', n - used));
          const auto cr = static_cast<const char *>(memchr(p + used, '\r', n - used));
          const auto end = lf && cr ? std::min(lf, cr) : lf ? lf : cr;
          const auto size = end ? size_t(end - p - used + 1) : n - used;
          j.chunks(p + used, size); used += size;
        }
        if (protocolDone(j)) {
          j.tailBytes += n - used;
          if (j.tailBytes > TailLimit) return 0;
        }
      } else if (j.logical) {
        j.tailBytes += n;
        if (j.tailBytes > TailLimit) return 0;
      } else if (j.result.status >= 200 && j.result.status < 300) j.result.body.append(p,n);
      else if (j.result.body.size() < 65536) j.result.body.append(p,std::min(n,size_t(65536)-j.result.body.size()));
      return n;
    } catch (const std::exception &e) { j.result.error = e.what(); return 0; }
  }
  static size_t upload(char *p,size_t a,size_t b,void *data) {
    auto &j=*static_cast<Job *>(data);
    if(cancelled(j)) return CURL_READFUNC_ABORT;
    const auto n=std::min(a*b,j.body.size()-j.upload);
    if(n)memcpy(p,j.body.data()+j.upload,n);
    j.upload+=n;return n;
  }
  static int seek(void *data,curl_off_t offset,int whence) {
    auto &j=*static_cast<Job *>(data);
    if(whence==SEEK_SET&&offset==0&&j.upload==0)return CURL_SEEKFUNC_OK;
    j.retryDenied=true;return CURL_SEEKFUNC_FAIL;
  }
  static int prerequisite(void *data,char *,char *,int,int) {
    auto &j=*static_cast<Job *>(data);
    if(++j.attempts==1)return CURL_PREREQFUNC_OK;
    j.retryDenied=true;return CURL_PREREQFUNC_ABORT;
  }
  static int progress(void *data,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    return cancelled(*static_cast<Job *>(data))?1:0;
  }
  static curl_socket_t openSocket(void *data,curlsocktype,struct curl_sockaddr *address) {
    auto &pool=*static_cast<Pool *>(data);auto &s=*pool.owner;
    if(s.sockets_.size()>=SocketLimit)return CURL_SOCKET_BAD;
    const int fd=::socket(address->family,address->socktype|SOCK_CLOEXEC,address->protocol);
    if(fd<0)return CURL_SOCKET_BAD;
    try {s.sockets_.emplace(fd,&pool);++pool.sockets;return fd;}
    catch (...) {::close(fd);return CURL_SOCKET_BAD;}
  }
  static int closeSocket(void *data,curl_socket_t fd) {
    auto &s=*static_cast<HttpService *>(data);auto it=s.sockets_.find(fd);
    if(it!=s.sockets_.end()){--it->second->sockets;s.sockets_.erase(it);}
    return ::close(fd);
  }
  Pool *pool(Job &j) {
    for(auto &p:pools_)if(!p->retired&&p->key==j.key)return p.get();
    while(pools_.size()>=PoolLimit)if(!evict())return nullptr;
    auto p=std::unique_ptr<Pool>(new Pool{this,j.key,j.provider});
    p->share=curl_share_init();if(!p->share)throw std::runtime_error("Cannot allocate HTTP pool");
    for(auto type:{CURL_LOCK_DATA_CONNECT,CURL_LOCK_DATA_SSL_SESSION,CURL_LOCK_DATA_DNS})
      if(curl_share_setopt(p->share,CURLSHOPT_SHARE,type)!=CURLSHE_OK)throw std::runtime_error("Cannot configure HTTP pool");
    auto raw=p.get();pools_.push_back(std::move(p));return raw;
  }
  bool evict(Pool *exclude=nullptr) {
    auto oldest=pools_.end();
    for(auto it=pools_.begin();it!=pools_.end();++it)
      if(!(*it)->active&&it->get()!=exclude&&(oldest==pools_.end()||(*it)->lastUsed<(*oldest)->lastUsed))oldest=it;
    if(oldest==pools_.end())return false;
    pools_.erase(oldest);return true;
  }
  void measure(Job &j) {
    double dns=0,connect=0,tls=0,first=0;long created=0;
    curl_easy_getinfo(j.easy,CURLINFO_RESPONSE_CODE,&j.result.status);
    curl_easy_getinfo(j.easy,CURLINFO_NAMELOOKUP_TIME,&dns);
    curl_easy_getinfo(j.easy,CURLINFO_CONNECT_TIME,&connect);
    curl_easy_getinfo(j.easy,CURLINFO_APPCONNECT_TIME,&tls);
    curl_easy_getinfo(j.easy,CURLINFO_STARTTRANSFER_TIME,&first);
    curl_easy_getinfo(j.easy,CURLINFO_NUM_CONNECTS,&created);
    j.result.elapsedMs=ticks()-j.submitted;
    j.result.timings={{"dns_ms",dns*1000},{"tcp_ms",std::max(0.,connect-dns)*1000},
      {"tls_ms",tls>0?std::max(0.,tls-connect)*1000:0.}, {"first_byte_ms",first*1000},
      {"elapsed_ms",j.result.elapsedMs},{"queue_ms",j.started-j.submitted},
      {"new_connections",created},{"connection_reused",created==0&&j.attempts>0},{"attempts",std::min(1,j.attempts)}};
  }
  void publish(Job &j) {
    if(j.published)return;
    if(j.easy)measure(j);
    if(cancelled(j))j.result.error="cancelled";
    // Release every callback and caller pointer before waking the waiting task.
    j.chunks={};j.complete={};j.cancel=nullptr;j.sourceBody=nullptr;j.published=true;
    last_=j.result.timings;last_["status"]=j.result.status;
    j.promise.set_value(std::move(j.result));
  }
  void start(const std::shared_ptr<Job> &j,Pool &p) {
    j->pool=&p;++p.active;j->started=ticks();
    j->easy=curl_easy_init();
    if(!j->easy)throw std::runtime_error("Cannot initialize HTTP");
    auto h=j->easy;
    for(auto i=j->headers.begin();i!=j->headers.end();++i) {
      if(!i->is_string()||i.key().find_first_of("\r\n:")!=std::string::npos||i->get<std::string>().find_first_of("\r\n")!=std::string::npos)
        throw std::runtime_error("Invalid HTTP header");
      auto list=curl_slist_append(j->headerList,(i.key()+": "+i->get<std::string>()).c_str());
      if(!list)throw std::runtime_error("Cannot allocate HTTP headers");
      j->headerList=list;
    }
#define SET(option,value) do { if(curl_easy_setopt(h,option,value)!=CURLE_OK)throw std::runtime_error("Cannot configure HTTP transfer"); } while(0)
    SET(CURLOPT_URL,j->url.c_str());SET(CURLOPT_HTTPHEADER,j->headerList);SET(CURLOPT_SHARE,p.share);
    SET(CURLOPT_NOSIGNAL,1L);SET(CURLOPT_CONNECTTIMEOUT_MS,15000L);SET(CURLOPT_TIMEOUT_MS,long(j->options.timeout)*1000);
    SET(CURLOPT_SSL_VERIFYPEER,1L);SET(CURLOPT_SSL_VERIFYHOST,2L);SET(CURLOPT_FOLLOWLOCATION,0L);
    SET(CURLOPT_MAXAGE_CONN,long(idle_.load()));SET(CURLOPT_SSL_SESSIONID_CACHE,1L);
    if(j->options.http1)SET(CURLOPT_HTTP_VERSION,CURL_HTTP_VERSION_1_1);
#if !defined(__aarch64__) || defined(PENCHAT_HTTP_TESTING)
    if(const char *ca=getenv("PENCHAT_TEST_CA"))SET(CURLOPT_CAINFO,ca);
#endif
    SET(CURLOPT_WRITEFUNCTION,receive);SET(CURLOPT_WRITEDATA,j.get());SET(CURLOPT_HEADERFUNCTION,header);SET(CURLOPT_HEADERDATA,j.get());
    SET(CURLOPT_NOPROGRESS,0L);SET(CURLOPT_XFERINFOFUNCTION,progress);SET(CURLOPT_XFERINFODATA,j.get());
    SET(CURLOPT_PREREQFUNCTION,prerequisite);SET(CURLOPT_PREREQDATA,j.get());
    SET(CURLOPT_OPENSOCKETFUNCTION,openSocket);SET(CURLOPT_OPENSOCKETDATA,&p);
    SET(CURLOPT_CLOSESOCKETFUNCTION,closeSocket);SET(CURLOPT_CLOSESOCKETDATA,this);
    if(j->method!="GET") {
      j->body=*j->sourceBody;j->sourceBody=nullptr;
      SET(CURLOPT_POST,1L);SET(CURLOPT_POSTFIELDSIZE_LARGE,curl_off_t(j->body.size()));
      SET(CURLOPT_READFUNCTION,upload);SET(CURLOPT_READDATA,j.get());SET(CURLOPT_SEEKFUNCTION,seek);SET(CURLOPT_SEEKDATA,j.get());
      SET(CURLOPT_CUSTOMREQUEST,j->method.c_str());
    }
#undef SET
    const auto status=curl_multi_add_handle(multi_,h);
    if(status!=CURLM_OK)throw std::runtime_error("Cannot schedule HTTP transfer");
    active_[h]=j;
  }
  void finish(const std::shared_ptr<Job> &j,CURLcode code,bool forceClose=false) {
    if(j->easy) {
      if(forceClose||code!=CURLE_OK)curl_easy_setopt(j->easy,CURLOPT_FORBID_REUSE,1L);
      if(!j->published) {
        if(j->retryDenied)j->result.error="连接已失效，请手动重试";
        if(code!=CURLE_OK&&!j->logical&&j->result.error.empty())j->result.error=code==CURLE_OPERATION_TIMEDOUT?
          "请求超时，请检查连接或增加请求超时后手动重试":curl_easy_strerror(code);
        publish(*j);
      }
      curl_multi_remove_handle(multi_,j->easy);active_.erase(j->easy);curl_easy_cleanup(j->easy);j->easy=nullptr;
    } else publish(*j);
    curl_slist_free_all(j->headerList);j->headerList=nullptr;
    if(j->pool){--j->pool->active;j->pool->lastUsed=ticks();j->pool=nullptr;}
  }
  void snapshot() {
    size_t queued=0;{std::lock_guard<std::mutex> lock(mutex_);queued=queued_.size();}
    size_t draining=0;for(const auto &j:active_)draining+=j.second->published;
    std::lock_guard<std::mutex> lock(diagnosticsMutex_);
    const auto version=curl_version_info(CURLVERSION_NOW);
    diagnostics_={{"idle_seconds",idle_.load()},{"active",active_.size()-draining},{"draining",draining},
      {"queued",queued},{"pools",pools_.size()},{"sockets",sockets_.size()},{"last",last_},{"running",multi_!=nullptr},
      {"curl_version",version->version},{"tls_backend",version->ssl_version?version->ssl_version:""}};
  }
  void run() {
    try {
      multi_=curl_multi_init();if(!multi_)throw std::runtime_error("Cannot initialize HTTP service");
      curl_multi_setopt(multi_,CURLMOPT_MAX_TOTAL_CONNECTIONS,long(SocketLimit));
      curl_multi_setopt(multi_,CURLMOPT_MAX_HOST_CONNECTIONS,2L);
      curl_multi_setopt(multi_,CURLMOPT_MAXCONNECTS,long(SocketLimit));
      {std::lock_guard<std::mutex> lock(mutex_);wake_=multi_;}
      for(;;) {
        bool stop;std::set<std::string> invalidations;bool all;
        {std::lock_guard<std::mutex> lock(mutex_);stop=stopping_;invalidations.swap(invalidations_);all=invalidateAll_;invalidateAll_=false;}
        for(auto &p:pools_)if(all||invalidations.count(p->provider))p->retired=true;
        const auto now=ticks();
        for(auto i=pools_.begin();i!=pools_.end();) {
          if(!(*i)->active&&((*i)->retired||now-(*i)->lastUsed>=idle_.load()*1000LL))i=pools_.erase(i);else ++i;
        }
        if(stop)break;
        std::vector<std::shared_ptr<Job>> cancelledJobs;
        {std::lock_guard<std::mutex> lock(mutex_);
          for(auto it=queued_.begin();it!=queued_.end();) {
            if(cancelled(**it)){cancelledJobs.push_back(*it);it=queued_.erase(it);}else ++it;
          }
        }
        for(auto &j:cancelledJobs)publish(*j);
        for(;;) {
          size_t running=0;for(const auto &a:active_)running+=!a.second->published;
          if(running>=ActiveLimit||active_.size()>=SocketLimit)break;
          std::shared_ptr<Job> j;
          {std::lock_guard<std::mutex> lock(mutex_);if(!queued_.empty())j=queued_.front();}
          if(!j)break;
          if(cancelled(*j)){std::lock_guard<std::mutex> lock(mutex_);queued_.pop_front();publish(*j);continue;}
          auto p=pool(*j);if(!p)break;
          // A busy partition can need a second socket even when it already
          // owns one. Reclaim unrelated idle caches before curl's open callback,
          // which cannot safely destroy another share from inside libcurl.
          if(p->active||!p->sockets) {
            while(sockets_.size()>=SocketLimit)if(!evict(p))break;
            if(sockets_.size()>=SocketLimit&&p->sockets<=p->active)break;
          }
          {std::lock_guard<std::mutex> lock(mutex_);queued_.pop_front();}
          try {start(j,*p);}catch(const std::exception &e){j->result.error=e.what();finish(j,CURLE_FAILED_INIT,true);}
        }
        int running=0;const auto status=curl_multi_perform(multi_,&running);
        if(status!=CURLM_OK)throw std::runtime_error("HTTP service transfer failed");
        int left=0;
        while(auto m=curl_multi_info_read(multi_,&left))if(m->msg==CURLMSG_DONE) {
          auto found=active_.find(m->easy_handle);
          if(found!=active_.end()){auto j=found->second;finish(j,m->data.result);}
        }
        std::vector<std::shared_ptr<Job>> jobs;for(auto &j:active_)jobs.push_back(j.second);
        for(auto &j:jobs) {
          if(cancelled(*j)){j->result.error="cancelled";finish(j,CURLE_ABORTED_BY_CALLBACK,true);continue;}
          try { if(!j->published&&protocolDone(*j))publish(*j); }
          catch(const std::exception &e){j->result.error=e.what();finish(j,CURLE_WRITE_ERROR,true);continue;}
          if(j->published&&(j->tailBytes>TailLimit||ticks()-j->tailStarted>=1000))finish(j,CURLE_ABORTED_BY_CALLBACK,true);
        }
        snapshot();int descriptors=0;
        curl_multi_poll(multi_,nullptr,0,active_.empty()?1000:50,&descriptors);
      }
    } catch(const std::exception &) {
      // Messages below are deliberately generic; never include URL/header data.
    }
    std::deque<std::shared_ptr<Job>> waiting;
    {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;wake_=nullptr;waiting.swap(queued_);}
    for(auto &j:waiting){j->result.error="HTTP 服务已关闭";publish(*j);}
    while(!active_.empty()){auto j=active_.begin()->second;j->result.error="cancelled";finish(j,CURLE_ABORTED_BY_CALLBACK,true);}
    pools_.clear();if(multi_){curl_multi_cleanup(multi_);multi_=nullptr;}snapshot();
  }
public:
  ~HttpService(){shutdown();if(initialized_)curl_global_cleanup();}
  HttpResult request(const std::string &url,const std::string &method,const Json &headers,const std::string &body,
                     std::atomic<bool> &cancel,std::function<void(const char *,size_t)> chunks,
                     const ApiOptions &options,std::function<bool()> complete,const HttpIdentity &identity) {
    if(url.compare(0,8,"https://")&&url.compare(0,7,"http://")){HttpResult r;r.error="Only HTTP(S) URLs are supported";return r;}
    auto j=std::make_shared<Job>();j->url=url;j->method=method;j->headers=headers;j->sourceBody=&body;
    j->cancel=&cancel;j->chunks=std::move(chunks);j->complete=std::move(complete);j->options=options;j->provider=identity.provider;
    // Private in-memory key only. It is never included in diagnostics or logs.
    j->key=Json({identity.provider,identity.endpoint.empty()?origin(url):identity.endpoint,identity.revision,options.http1,
                identity.headerProfile.empty()?headers:Json(identity.headerProfile)}).dump();
    auto future=j->promise.get_future();
    {std::lock_guard<std::mutex> lock(mutex_);
      if(stopping_){HttpResult r;r.error="HTTP 服务已关闭";return r;}
      if(queued_.size()>=QueueLimit){HttpResult r;r.error="网络请求队列已满，请稍后手动重试";return r;}
      if(!thread_.joinable()) {
        if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){HttpResult r;r.error="Cannot initialize HTTP";return r;}
        initialized_=true;thread_=std::thread([this]{run();});
      }
      queued_.push_back(j);wakeLocked();
    }
    return future.get();
  }
  void configure(int seconds){idle_=seconds>=15&&seconds<=600?seconds:90;std::lock_guard<std::mutex> lock(mutex_);wakeLocked();}
  int idle() const{return idle_.load();}
  void invalidate(const std::string &provider){std::lock_guard<std::mutex> lock(mutex_);if(!thread_.joinable())return;
    if(invalidations_.size()<128)invalidations_.insert(provider);else invalidateAll_=true;wakeLocked();}
  Json diagnostics(){std::lock_guard<std::mutex> lock(diagnosticsMutex_);auto result=diagnostics_;result["idle_seconds"]=idle_.load();return result;}
  void shutdown(){ {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;wakeLocked();}if(thread_.joinable())thread_.join();}
};
HttpService &service(){static HttpService instance;return instance;}
}
void setHttpIdleSeconds(int seconds){service().configure(seconds);}
int httpIdleSeconds(){return service().idle();}
void invalidateHttpProvider(const std::string &provider){service().invalidate(provider);}
void shutdownHttp(){service().shutdown();}
Json httpDiagnostics(){return service().diagnostics();}
HttpResult http(const std::string &url,const std::string &method,const Json &headers,const std::string &body,
                std::atomic<bool> &cancel,std::function<void(const char *,size_t)> chunks,
                const ApiOptions &options,std::function<bool()> complete,const HttpIdentity &identity) {
  return service().request(url,method,headers,body,cancel,std::move(chunks),options,std::move(complete),identity);
}
} // namespace penchat
