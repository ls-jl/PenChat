#include "update.hpp"
#include <curl/curl.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
namespace penchat { namespace updates {
Config Config::build() {
  Config value;
#ifndef PENCHAT_NATIVE_DEVICE
  value.autoCheck=false;
  if(const auto path=getenv("PENCHAT_TEST_UPDATE_CONFIG")) {
    const auto json=Json::parse(fileText(path,16384));
    value.root=json.value("root",value.root);value.nodes=json.value("nodes",value.nodes);
    value.autoCheck=json.value("auto",true);
    value.current=json.value("current",value.current);value.appId=json.value("appid",value.appId);
  }
#endif
  return value;
}
namespace {
struct Socket {int fd=-1;~Socket(){if(fd>=0)close(fd);}};
uint16_t u16(const unsigned char *p){return uint16_t(p[0])*256+p[1];}
bool skipName(const std::vector<unsigned char> &b,size_t &at) {
  for(unsigned n=0;n<128&&at<b.size();++n) {
    unsigned c=b[at++];if(!c)return true;
    if((c&0xc0)==0xc0){if(at>=b.size())return false;++at;return true;}
    if(c>63||at+c>b.size())return false;
    at+=c;
  }return false;
}
std::string trim(std::string s){while(!s.empty()&&(s.back()=='\r'||s.back()=='\n'||s.back()==' '||s.back()=='\t'))s.pop_back();const auto at=s.find_first_not_of(" \t");return at==std::string::npos?"":s.substr(at);}
}
std::vector<std::string> resolve(const std::string &host,std::atomic<bool> &cancel) {
  // Bounded native DNS queries, instead of an uninterruptible libc resolver on
  // the UI/exit path. A proxy can still resolve the origin when local DNS fails.
  unsigned char literal[16];if(inet_pton(AF_INET,host.c_str(),literal)==1||inet_pton(AF_INET6,host.c_str(),literal)==1)return {host};
  std::ifstream resolv("/etc/resolv.conf");std::string line;std::vector<std::string> servers;
  while(std::getline(resolv,line)&&servers.size()<3){std::istringstream fields(line);std::string key,value;fields>>key>>value;if(key=="nameserver")servers.push_back(value);}
  std::set<std::string> found;
  for(const auto &server:servers) {
    if(cancel)break;
    const bool v6=server.find(':')!=std::string::npos;Socket socket;socket.fd=::socket(v6?AF_INET6:AF_INET,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(socket.fd<0)continue;
    sockaddr_storage addr{};socklen_t length;
    if(v6){auto p=reinterpret_cast<sockaddr_in6 *>(&addr);p->sin6_family=AF_INET6;p->sin6_port=htons(53);if(inet_pton(AF_INET6,server.c_str(),&p->sin6_addr)!=1)continue;length=sizeof(*p);}
    else{auto p=reinterpret_cast<sockaddr_in *>(&addr);p->sin_family=AF_INET;p->sin_port=htons(53);if(inet_pton(AF_INET,server.c_str(),&p->sin_addr)!=1)continue;length=sizeof(*p);}
    if(connect(socket.fd,reinterpret_cast<sockaddr *>(&addr),length))continue;
    const uint16_t id=uint16_t(ticks()^getpid());
    for(unsigned type:{1u,28u}) {
      std::vector<unsigned char> q(12,0);q[0]=id>>8;q[1]=id;q[2]=1;q[5]=1;size_t at=0;
      while(at<host.size()){auto end=host.find('.',at);if(end==std::string::npos)end=host.size();if(end-at>63)return {};q.push_back(unsigned(end-at));q.insert(q.end(),host.begin()+at,host.begin()+end);at=end+1;}
      q.insert(q.end(),{0,0,static_cast<unsigned char>(type),0,1});send(socket.fd,q.data(),q.size(),0);
    }
    const auto until=ticks()+1500;unsigned replies=0;
    while(!cancel&&ticks()<until&&replies<2) {
      pollfd p{socket.fd,POLLIN,0};if(poll(&p,1,40)<=0)continue;
      std::vector<unsigned char> b(4096);auto n=recv(socket.fd,b.data(),b.size(),0);if(n<12)continue;b.resize(size_t(n));
      if(u16(b.data())!=id||!(b[2]&0x80)||(b[3]&15))continue;
      ++replies;
      size_t at=12;const unsigned questions=u16(b.data()+4),answers=u16(b.data()+6);
      if(questions>4||answers>128)continue;
      bool valid=true;for(unsigned i=0;i<questions;++i){if(!skipName(b,at)||at+4>b.size()){valid=false;break;}at+=4;}
      if(!valid)continue;
      for(unsigned i=0;i<answers&&found.size()<16;++i) {
        if(!skipName(b,at)||at+10>b.size())break;
        const unsigned type=u16(b.data()+at),klass=u16(b.data()+at+2),bytes=u16(b.data()+at+8);at+=10;
        if(at+bytes>b.size())break;
        char ip[INET6_ADDRSTRLEN]{};
        if(klass==1&&((type==1&&bytes==4)||(type==28&&bytes==16))&&inet_ntop(type==1?AF_INET:AF_INET6,b.data()+at,ip,sizeof(ip)))found.insert(ip);
        at+=bytes;
      }
    }
    if(!found.empty())break;
  }
  return {found.begin(),found.end()};
}
struct Transport::Impl {
  Config config;std::atomic<bool> &cancel;CURLM *multi=nullptr;
  std::string host;unsigned port=443;
  struct Transfer {
    CURL *easy=nullptr;curl_slist *route=nullptr,*headers=nullptr;Response response;
    Request request;Impl *owner=nullptr;int fd=-1;uint64_t start=0,end=0,total=0,received=0;
    size_t segment=0,node=0;bool headerDone=false,valid=false,whole=false;std::string problem;
    std::function<void(size_t,uint64_t)> meter;
    ~Transfer(){if(easy)curl_easy_cleanup(easy);curl_slist_free_all(route);curl_slist_free_all(headers);}
  };
  explicit Impl(Config c,std::atomic<bool> &flag):config(std::move(c)),cancel(flag) {
    CURLU *url=curl_url();if(!url)throw std::runtime_error("无法解析更新地址");
    char *name=nullptr,*number=nullptr;
    const auto result=curl_url_set(url,CURLUPART_URL,config.root.c_str(),0);
    if(!result&&curl_url_get(url,CURLUPART_HOST,&name,0)==CURLUE_OK)host=name;
    if(curl_url_get(url,CURLUPART_PORT,&number,0)==CURLUE_OK)port=unsigned(std::stoi(number));else port=config.root.compare(0,6,"https:")==0?443:80;
    curl_free(name);curl_free(number);curl_url_cleanup(url);
    if(result||host.empty())throw std::runtime_error("更新源地址无效");
    if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)throw std::runtime_error("无法初始化更新网络");
    multi=curl_multi_init();if(!multi){curl_global_cleanup();throw std::runtime_error("无法初始化更新下载");}
    curl_multi_setopt(multi,CURLMOPT_MAX_TOTAL_CONNECTIONS,8L);curl_multi_setopt(multi,CURLMOPT_MAX_HOST_CONNECTIONS,8L);curl_multi_setopt(multi,CURLMOPT_MAXCONNECTS,8L);
  }
  ~Impl(){if(multi)curl_multi_cleanup(multi);curl_global_cleanup();}
  static size_t header(char *data,size_t a,size_t b,void *opaque) {
    auto &t=*static_cast<Transfer *>(opaque);const size_t n=a*b;
    try {
      if(n>8192)return 0;
      std::string line(data,n);
      if(line.compare(0,5,"HTTP/")==0) {const auto at=line.find(' ');t.response.status=at==std::string::npos?0:std::strtol(line.c_str()+at+1,nullptr,10);t.headerDone=false;t.valid=false;t.whole=false;t.response.length=-1;t.response.range.clear();t.response.encoding.clear();return n;}
      if(line=="\r\n"||line=="\n") {
        t.headerDone=true;
        if(t.fd>=0) {
          if(t.response.status==206) {
            unsigned long long start=0,end=0,total=0;char tail=0;
            t.valid=sscanf(t.response.range.c_str(),"bytes %llu-%llu/%llu%c",&start,&end,&total,&tail)==3&&start==t.start&&end==t.end&&total==t.total;
          } else if(t.response.status==200&&t.segment==0&&t.start==0) {t.whole=true;t.end=t.total-1;t.valid=t.response.length<0||uint64_t(t.response.length)==t.total;}
          if(!t.response.encoding.empty()&&t.response.encoding!="identity")t.valid=false;
          if(!t.valid&&t.response.status>=200&&t.response.status<300)t.problem="更新源返回了无效分段或不同文件";
        }
        return n;
      }
      const auto colon=line.find(':');if(colon==std::string::npos)return n;
      auto key=line.substr(0,colon);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});
      const auto value=trim(line.substr(colon+1));
      if(key=="content-length"){char *end=nullptr;const auto length=strtoll(value.c_str(),&end,10);if(end&&!*end&&length>=0)t.response.length=length;}
      else if(key=="etag")t.response.etag=value;
      else if(key=="last-modified")t.response.modified=value;
      else if(key=="content-range")t.response.range=value;
      else if(key=="content-encoding")t.response.encoding=value;
      return n;
    }catch(...){return 0;}
  }
  static size_t receive(char *data,size_t a,size_t b,void *opaque) {
    auto &t=*static_cast<Transfer *>(opaque);const size_t n=a*b;if(t.owner->cancel)return 0;
    try {
      if(t.fd<0){if(t.response.body.size()+n>t.request.limit){t.problem="更新源响应超过大小限制";return 0;}t.response.body.append(data,n);return n;}
      if(t.response.status<200||t.response.status>=300)return 0;
      if(!t.headerDone||!t.valid||t.received+n>t.end-t.start+1){if(t.problem.empty())t.problem="更新包长度或分段响应异常";return 0;}
      size_t done=0;while(done<n){const auto wrote=pwrite(t.fd,data+done,n-done,off_t(t.start+t.received+done));if(wrote<0&&errno==EINTR)continue;if(wrote<=0){t.problem="更新包写入失败，请检查剩余空间";return 0;}done+=size_t(wrote);}
      t.received+=n;if(t.meter)t.meter(t.node,n);return n;
    }catch(...){t.problem="更新接收失败";return 0;}
  }
  static int progress(void *opaque,curl_off_t,curl_off_t,curl_off_t,curl_off_t){return static_cast<Transfer *>(opaque)->owner->cancel?1:0;}
  std::unique_ptr<Transfer> create(const Request &request) {
    std::unique_ptr<Transfer> t(new Transfer);t->owner=this;t->request=request;t->easy=curl_easy_init();if(!t->easy)throw std::runtime_error("无法创建更新连接");
#define SET(k,v) if(curl_easy_setopt(t->easy,k,v)!=CURLE_OK)throw std::runtime_error("无法设置更新连接")
    SET(CURLOPT_URL,request.url.c_str());SET(CURLOPT_SUPPRESS_CONNECT_HEADERS,1L);SET(CURLOPT_NOSIGNAL,1L);SET(CURLOPT_FOLLOWLOCATION,0L);
    SET(CURLOPT_SSL_VERIFYPEER,1L);SET(CURLOPT_SSL_VERIFYHOST,2L);SET(CURLOPT_CONNECTTIMEOUT_MS,10000L);SET(CURLOPT_TIMEOUT_MS,30000L);
    SET(CURLOPT_WRITEFUNCTION,&receive);SET(CURLOPT_WRITEDATA,t.get());SET(CURLOPT_HEADERFUNCTION,&header);SET(CURLOPT_HEADERDATA,t.get());
    SET(CURLOPT_NOPROGRESS,0L);SET(CURLOPT_XFERINFOFUNCTION,&progress);SET(CURLOPT_XFERINFODATA,t.get());SET(CURLOPT_ACCEPT_ENCODING,"identity");
    if(request.head)SET(CURLOPT_NOBODY,1L);
    if(!request.node.empty()) {
      const auto target=request.node.find(':')==std::string::npos?request.node:"["+request.node+"]";
      const auto route=host+":"+std::to_string(port)+":"+target+":"+std::to_string(port);
      t->route=curl_slist_append(nullptr,route.c_str());if(!t->route)throw std::runtime_error("无法创建更新路由");SET(CURLOPT_CONNECT_TO,t->route);
    }
#ifndef PENCHAT_NATIVE_DEVICE
    if(const auto ca=getenv("PENCHAT_TEST_CA"))SET(CURLOPT_CAINFO,ca);
#endif
#undef SET
    return t;
  }
  std::vector<Response> fetch(const std::vector<Request> &requests) {
    if(requests.size()>32)throw std::runtime_error("更新请求数量超限");
    std::vector<std::unique_ptr<Transfer>> active;std::vector<Response> results;
    struct Remove {Impl *self;std::vector<std::unique_ptr<Transfer>> *active;~Remove(){for(auto &t:*active)curl_multi_remove_handle(self->multi,t->easy);}} remove{this,&active};
    for(const auto &r:requests){if(cancel)throw std::runtime_error("cancelled");auto t=create(r);if(curl_multi_add_handle(multi,t->easy)!=CURLM_OK)throw std::runtime_error("更新请求启动失败");active.push_back(std::move(t));}
    int running=0;
    do {if(cancel)throw std::runtime_error("cancelled");if(curl_multi_perform(multi,&running)!=CURLM_OK)throw std::runtime_error("更新连接失败");if(running)curl_multi_poll(multi,nullptr,0,40,nullptr);}while(running);
    int left=0;while(auto m=curl_multi_info_read(multi,&left))if(m->msg==CURLMSG_DONE)for(auto &t:active)if(t->easy==m->easy_handle)t->response.code=int(m->data.result);
    for(auto &t:active) {
      double time=0;curl_easy_getinfo(t->easy,CURLINFO_STARTTRANSFER_TIME,&time);t->response.ttfb=int64_t(time*1000000);
      t->response.error=t->problem;results.push_back(std::move(t->response));
    }
    return results;
  }
  Json download(const Json &release,const std::string &path,std::function<void(Json)> notify);
};
Json Transport::Impl::download(const Json &release,const std::string &path,std::function<void(Json)> notify) {
  const uint64_t total=release.at("size").get<uint64_t>();if(!total||total>PackageLimit)throw std::runtime_error("更新包大小超限");
  const auto nodes=release.at("nodes");if(!nodes.is_array()||nodes.empty()||nodes.size()>2)throw std::runtime_error("没有可用更新节点");
  int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)throw std::runtime_error("无法创建更新临时文件");
  struct File {int fd;std::string path;bool complete=false;~File(){close(fd);if(!complete)unlink(path.c_str());}} file{fd,path};
  if(ftruncate(fd,off_t(total)))throw std::runtime_error("更新包预分配失败");
  struct Segment {uint64_t start,end;unsigned attempts=0;int64_t due=0;bool active=false,done=false;};
  std::vector<Segment> segments;
  for(uint64_t at=0;at<total;at+=1024*1024)segments.push_back({at,std::min(total-1,at+1024*1024-1)});
  std::map<CURL *,std::unique_ptr<Transfer>> active;
  struct Remove {Impl *self;decltype(active) *entries;~Remove(){for(auto &t:*entries)curl_multi_remove_handle(self->multi,t.first);}} remove{this,&active};
  const auto started=ticks();int64_t firstBody=0,lastNotify=0;uint64_t metered[2]={0,0};
  struct Rate {int64_t time;uint64_t primary,total;};
  std::deque<Rate> samples;bool dual=false,whole=false,rangeReady=false,forceFull=false;unsigned round=0,peak=1;
  unsigned failures[2]={0,0};Json statistics={{"peak",1},{"dual",false},{"range",true}};
  auto progress=[&] {
    uint64_t done=0;for(const auto &s:segments)if(s.done)done+=s.end-s.start+1;
    for(const auto &t:active)done+=t.second->received;
    const auto time=ticks();samples.push_back({time,metered[0],metered[0]+metered[1]});while(samples.size()>2&&samples[1].time<time-3000)samples.pop_front();
    uint64_t speed=0,primarySpeed=0;
    if(samples.size()>1&&time>samples.front().time){const auto elapsed=uint64_t(time-samples.front().time);primarySpeed=(metered[0]-samples.front().primary)*1000/elapsed;speed=(metered[0]+metered[1]-samples.front().total)*1000/elapsed;}
    if(!whole&&!dual&&nodes.size()==2&&firstBody&&time-firstBody>=3000&&primarySpeed<250000)dual=true;
    if(time-lastNotify>=100||done==total) {
      notify({{"downloaded",done},{"total",total},{"speed",speed},{"lanes",active.size()},{"nodes",dual?2:1},{"elapsed_ms",time-started}});lastNotify=time;
    }
    return done;
  };
  auto start=[&](size_t index,size_t node) {
    if(cancel)throw std::runtime_error("cancelled");
    auto &s=segments[index];Request request{release.at("url"),nodes[node].value("address",std::string()),false,0};auto t=create(request);
    t->fd=fd;t->start=s.start;t->end=s.end;t->total=total;t->segment=index;t->node=node;
    t->meter=[&](size_t n,uint64_t bytes){if(!firstBody)firstBody=ticks();metered[n]+=bytes;};
    const auto range=std::to_string(s.start)+"-"+std::to_string(s.end);
    if(!forceFull)curl_easy_setopt(t->easy,CURLOPT_RANGE,range.c_str());
    curl_easy_setopt(t->easy,CURLOPT_TIMEOUT_MS,120000L);
    curl_easy_setopt(t->easy,CURLOPT_LOW_SPEED_LIMIT,1L);curl_easy_setopt(t->easy,CURLOPT_LOW_SPEED_TIME,20L);
    const auto etag=nodes[node].value("etag",std::string()),modified=nodes[node].value("modified",std::string());
    if(!etag.empty()&&etag.compare(0,2,"W/")!=0)t->headers=curl_slist_append(t->headers,("If-Match: "+etag).c_str());
    else if(!modified.empty())t->headers=curl_slist_append(t->headers,("If-Unmodified-Since: "+modified).c_str());
    curl_easy_setopt(t->easy,CURLOPT_HTTPHEADER,t->headers);
    if(curl_multi_add_handle(multi,t->easy)!=CURLM_OK)throw std::runtime_error("无法开始更新分段");
    s.active=true;++s.attempts;const auto handle=t->easy;active.emplace(handle,std::move(t));peak=std::max<unsigned>(peak,active.size());
  };
  start(0,0);
  for(;;) {
    if(cancel)throw std::runtime_error("cancelled");
    int running=0;if(curl_multi_perform(multi,&running)!=CURLM_OK)throw std::runtime_error("更新下载服务失败");
    for(const auto &t:active)if(t.second->segment==0&&t.second->headerDone) {
      if(t.second->response.status==206&&t.second->valid)rangeReady=true;
      if(t.second->whole)whole=true;
    }
    bool fallback=false;int count=0;
    while(auto message=curl_multi_info_read(multi,&count)) {
      if(message->msg!=CURLMSG_DONE)continue;
      auto it=active.find(message->easy_handle);if(it==active.end())continue;
      auto &t=*it->second;auto &s=segments[t.segment];s.active=false;
      curl_multi_remove_handle(multi,t.easy);
      const bool success=message->data.result==CURLE_OK&&t.valid&&t.received==t.end-t.start+1;
      const bool completeWhole=t.whole;
      const auto status=t.response.status;const auto problem=t.problem;const auto node=t.node;
      if(status==200&&!completeWhole&&!forceFull){active.erase(it);fallback=true;break;}
      if(success) {s.done=true;if(completeWhole){for(auto &part:segments)part.done=true;whole=true;}}
      else {
        const bool temporary=(message->data.result!=CURLE_OK&&problem.empty())||status==0||status==408||status==429||status>=500;
        if(!problem.empty()||!temporary||s.attempts>=3) {
          active.erase(it);throw std::runtime_error(problem.empty()?"更新下载失败，请重试":problem);
        }
        ++failures[node];s.due=ticks()+1000*(s.attempts==1?1:3);
        if(nodes.size()==2)dual=true;
      }
      active.erase(it);
      if(completeWhole&&!success)throw std::runtime_error("完整更新包传输中断，请重试");
    }
    if(fallback) {
      for(auto &t:active)curl_multi_remove_handle(multi,t.first);
      active.clear();for(auto &s:segments){s.active=false;s.done=false;s.attempts=0;s.due=0;}
      forceFull=true;whole=true;rangeReady=false;metered[0]=metered[1]=0;samples.clear();firstBody=0;
      start(0,0);continue;
    }
    const auto done=progress();
    if(done==total&&active.empty())break;
    if(!whole&&rangeReady) {
      const unsigned capacity=std::min<unsigned>(8,1+unsigned((ticks()-started)/1000));
      for(size_t i=0;i<segments.size()&&active.size()<capacity;++i) {
        const auto &s=segments[i];if(s.done||s.active||s.due>ticks())continue;
        size_t node=dual?round++%2:0;
        if(dual&&failures[node]>failures[1-node])node=1-node;
        start(i,node);
      }
    } else if(active.empty()) {
      // Only segment zero can establish a server's Range behavior.
      if(!segments[0].done&&segments[0].due<=ticks())start(0,dual?1:0);
    }
    curl_multi_poll(multi,nullptr,0,40,nullptr);
  }
  if(fsync(fd))throw std::runtime_error("更新包保存失败");
  file.complete=true;statistics["peak"]=peak;statistics["dual"]=dual;statistics["range"]=!whole;statistics["bytes"]=total;return statistics;
}
Transport::Transport(const Config &c,std::atomic<bool> &flag):p_(new Impl(c,flag)){}
Transport::~Transport()=default;
std::vector<Response> Transport::fetch(const std::vector<Request> &requests){return p_->fetch(requests);}
Json Transport::download(const Json &release,const std::string &path,std::function<void(Json)> progress){return p_->download(release,path,std::move(progress));}
}}
