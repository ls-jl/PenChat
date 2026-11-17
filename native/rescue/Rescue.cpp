#include "Rescue.hpp"
#include "update.hpp"
#include "layout.hpp"
#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <miniz.h>
#include <cerrno>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <set>
#include <sqlite3.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
namespace penchat { namespace rescue {
namespace {
void fail(const char *message){throw std::runtime_error(message);}
Json readJson(const std::string &path,size_t limit=65536) {
  const int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return Json();
  struct stat st{};if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||uint64_t(st.st_size)>limit){close(fd);return Json();}
  std::string text;char buffer[4096];
  for(;;){const auto n=read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;
    if(n<0||text.size()+size_t(n)>limit){close(fd);return Json();}if(!n)break;text.append(buffer,size_t(n));}
  close(fd);auto value=Json::parse(text,nullptr,false);return value.is_discarded()?Json():value;
}
void privateDir(const std::string &path) {
  struct stat st{};if(!lstat(path.c_str(),&st)&&!S_ISDIR(st.st_mode))fail("降级目录不是普通目录");
  ensureDirectory(path);
  const auto parent=path.substr(0,path.find_last_of('/'));
  const int fd=open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
  if(fd<0)fail("降级目录无法同步");const bool ok=fsync(fd)==0;close(fd);if(!ok)fail("降级目录同步失败");
}
bool privateDirectory(const std::string &path) {
  struct stat st{};return !lstat(path.c_str(),&st)&&S_ISDIR(st.st_mode);
}
void durable(const std::string &path,const Json &value) {
  const auto temporary=path+".part";const auto text=value.dump();
  int fd=open(temporary.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0)fail("无法保存降级状态");
  size_t at=0;while(at<text.size()) {
    const auto n=write(fd,text.data()+at,text.size()-at);
    if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);fail("降级状态写入失败");}at+=size_t(n);
  }
  const bool synced=fsync(fd)==0;close(fd);
  if(!synced||rename(temporary.c_str(),path.c_str()))fail("降级状态持久化失败");
  fd=open(path.substr(0,path.find_last_of('/')).c_str(),O_DIRECTORY|O_RDONLY|O_CLOEXEC);
  if(fd<0)fail("降级目录无法同步");const bool ok=fsync(fd)==0;close(fd);if(!ok)fail("降级目录同步失败");
}
std::string uuid() {
  unsigned char bytes[16];if(RAND_bytes(bytes,sizeof(bytes))!=1)fail("无法生成降级操作编号");
  bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;std::string out;
  for(unsigned i=0;i<16;++i){if(i==4||i==6||i==8||i==10)out+='-';out+="0123456789abcdef"[bytes[i]>>4];out+="0123456789abcdef"[bytes[i]&15];}return out;
}
std::string digest(const std::string &text) {
  unsigned char out[EVP_MAX_MD_SIZE];unsigned size=0;EVP_Digest(text.data(),text.size(),out,&size,EVP_sha256(),nullptr);
  std::string result;for(unsigned i=0;i<size;++i){result+="0123456789abcdef"[out[i]>>4];result+="0123456789abcdef"[out[i]&15];}return result;
}
bool leaf(const std::string &s) {
  return !s.empty()&&s!="."&&s!=".."&&s.size()<240&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-")==std::string::npos;
}
bool hash(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
void elfBytes(const std::string &bytes,const std::string &platform) {
  if(bytes.size()<52||bytes.compare(0,4,"\177ELF")||bytes[5]!=1)fail("原生文件不是有效的 ELF");
  const auto machine=unsigned(uint8_t(bytes[18]))|(unsigned(uint8_t(bytes[19]))<<8);
  if((platform=="rk3562"&&(bytes[4]!=2||machine!=183))||
     (platform=="cv1826-s"&&(bytes[4]!=1||machine!=40))||
     (platform!="rk3562"&&platform!="cv1826-s"))fail("安装包原生架构与当前设备不匹配");
  if(platform=="cv1826-s"&&!(uint8_t(bytes[37])&4))fail("ARM32 安装包不符合 hard-float ABI");
}
std::string prefix(const std::string &file) {
  char data[64];const int fd=open(file.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
  if(fd<0)fail("无法读取原生组件");const auto n=read(fd,data,sizeof(data));close(fd);
  return n>0?std::string(data,size_t(n)):std::string();
}
Json diagnosticSummary(const std::string &data) {
  Json out=Json::array();
  const std::set<std::string> events={"lifecycle.start","lifecycle.startup_error","lifecycle.runtime_error","lifecycle.exit","lifecycle.stop","lifecycle.background","lifecycle.foreground","ui.error","storage.save_error","scanner.error","scanner.review_error","camera.error"};
  for(const char *file:{"diagnostic-previous.json","diagnostic-current.json"}) {
    const auto source=readJson(join(join(data,".feedback"),file),280*1024);if(!source.is_array())continue;
    for(const auto &entry:source) {
      if(!entry.is_object()||!entry.value("category",Json()).is_string()||!entry.value("event",Json()).is_string())continue;
      const auto category=entry["category"].get<std::string>(),event=entry["event"].get<std::string>();
      if(!events.count(category+"."+event))continue;
      Json record={{"category",category},{"event",event},{"fields",Json::object()}};
      if(entry.value("time_ms",Json()).is_number_integer())record["time_ms"]=entry["time_ms"];
      const auto fields=entry.value("fields",Json());
      for(const char *key:{"signal","status","code","stage","elapsed_ms","rss_bytes","rss_peak_bytes","failed","active","count"})
        if(fields.is_object()&&fields.contains(key)&&(fields[key].is_number_integer()||fields[key].is_boolean()))record["fields"][key]=fields[key];
      if(out.size()>=24)out.erase(out.begin());out.push_back(record);
    }
  }
  return out;
}
struct File {int fd=-1;~File(){if(fd>=0)close(fd);}};
struct HttpResult {long status=0;int code=0;std::string text;curl_off_t length=-1;};
HttpResult http(const std::string &url,const std::string &node,std::atomic<bool> &cancel,
                const std::string &file={},uint64_t expected=0,std::function<void(uint64_t)> progress={},
                const std::string &body={},const std::string &token={},bool head=false) {
  const bool secure=url.compare(0,8,"https://")==0;
#ifdef PENCHAT_NATIVE_DEVICE
  if(!secure)fail("降级服务必须使用 HTTPS");
#else
  if(!secure&&url.compare(0,17,"http://127.0.0.1:")!=0)fail("测试地址必须为 loopback");
#endif
  static const bool initialized=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;
  if(!initialized)fail("降级网络初始化失败");
  std::unique_ptr<CURL,decltype(&curl_easy_cleanup)> easy(curl_easy_init(),curl_easy_cleanup);
  if(!easy)fail("降级连接创建失败");
  struct Transfer {HttpResult result;std::atomic<bool> *cancel;int fd=-1;uint64_t size=0,limit=0;std::function<void(uint64_t)> progress;};
  File output;
  if(!file.empty()){output.fd=open(file.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(output.fd<0)fail("降级下载文件无法创建");}
  Transfer t{{},&cancel,output.fd,0,expected?expected:2*1024*1024,std::move(progress)};
  auto *c=easy.get();curl_easy_setopt(c,CURLOPT_URL,url.c_str());curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
  if(head)curl_easy_setopt(c,CURLOPT_NOBODY,1L);
  curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT_MS,8000L);curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,120000L);
  curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,1L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,20L);
  curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,0L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
  curl_easy_setopt(c,CURLOPT_USERAGENT,"PenChat-Rescue/1");curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);
  curl_easy_setopt(c,CURLOPT_XFERINFODATA,&t);
  curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,+[](void *p,curl_off_t,curl_off_t,curl_off_t,curl_off_t)->int{return *static_cast<Transfer *>(p)->cancel?1:0;});
  curl_easy_setopt(c,CURLOPT_WRITEDATA,&t);
  curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,+[](char *data,size_t a,size_t b,void *p)->size_t {
    auto &x=*static_cast<Transfer *>(p);const auto n=a*b;
    if(*x.cancel||n>x.limit-x.size)return 0;
    if(x.fd<0)x.result.text.append(data,n);
    else {size_t at=0;while(at<n){const auto w=write(x.fd,data+at,n-at);if(w<0&&errno==EINTR)continue;if(w<=0)return 0;at+=size_t(w);}}
    x.size+=n;if(x.progress)x.progress(x.size);return n;
  });
  curl_slist *routes=nullptr,*headers=nullptr;
  struct Lists {curl_slist **a,**b;~Lists(){curl_slist_free_all(*a);curl_slist_free_all(*b);}} cleanup{&routes,&headers};
  {
    CURLU *u=curl_url();curl_url_set(u,CURLUPART_URL,url.c_str(),0);char *host=nullptr,*port=nullptr;
    curl_url_get(u,CURLUPART_HOST,&host,0);curl_url_get(u,CURLUPART_PORT,&port,0);
    if(!host){curl_url_cleanup(u);fail("降级地址无效");}
    auto target=node;
    if(target.empty()){auto addresses=updates::resolve(host,cancel);if(!addresses.empty())target=addresses.front();}
    if(target.empty()){curl_free(host);curl_free(port);curl_url_cleanup(u);fail("降级服务域名解析失败");}
    const std::string route=std::string(host)+":"+(port?port:(secure?"443":"80"))+":"+(target.find(':')==std::string::npos?target:"["+target+"]")+":"+(port?port:(secure?"443":"80"));
    curl_free(host);curl_free(port);curl_url_cleanup(u);routes=curl_slist_append(routes,route.c_str());curl_easy_setopt(c,CURLOPT_CONNECT_TO,routes);
  }
  if(!body.empty()) {
    headers=curl_slist_append(headers,"Content-Type: application/json");
    if(!token.empty())headers=curl_slist_append(headers,("Authorization: Bearer "+token).c_str());
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,headers);curl_easy_setopt(c,CURLOPT_POSTFIELDS,body.data());curl_easy_setopt(c,CURLOPT_POSTFIELDSIZE_LARGE,curl_off_t(body.size()));
  }
  t.result.code=int(curl_easy_perform(c));curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&t.result.status);
  curl_easy_getinfo(c,CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,&t.result.length);
  if(output.fd>=0&&(!t.result.code&&t.result.status==200)&&fsync(output.fd))fail("下载文件同步失败");
  if(output.fd>=0&&(t.result.code||t.result.status!=200||t.size!=expected)) {unlink(file.c_str());if(cancel)fail("cancelled");fail("降级包下载失败，请重试");}
  return t.result;
}
struct Entry {std::string relative;uint64_t bytes;bool directory=false;};
void entries(const std::string &root,const std::string &relative,std::vector<Entry> &out,uint64_t &bytes,unsigned depth=0,bool privateRoot=false) {
  if(depth>64||out.size()>200000)fail("备份目录过深或文件过多");
  const auto path=relative.empty()?root:join(root,relative);struct stat st{};
  if(lstat(path.c_str(),&st)){if(errno==ENOENT&&relative.empty())return;fail("备份文件无法读取");}
  if(S_ISREG(st.st_mode)){if(st.st_size<0||uint64_t(st.st_size)>UINT64_MAX-bytes)fail("备份大小异常");bytes+=uint64_t(st.st_size);out.push_back({relative,uint64_t(st.st_size)});return;}
  if(!S_ISDIR(st.st_mode))fail("备份遇到符号链接或特殊文件，未安装");
  if(!relative.empty())out.push_back({relative,0,true});
  std::unique_ptr<DIR,decltype(&closedir)> dir(opendir(path.c_str()),closedir);if(!dir)fail("备份目录无法打开");
  while(auto item=readdir(dir.get())) {
    const std::string name=item->d_name;if(name=="."||name==".."||(privateRoot&&relative.empty()&&(name==".rescue"||name==".rescue-preserve-public")))continue;
    entries(root,relative.empty()?name:join(relative,name),out,bytes,depth+1,privateRoot);
  }
}
void copy(const std::string &from,const std::string &to,std::atomic<bool> &cancel) {
  privateDir(to.substr(0,to.find_last_of('/')));File in,out;
  in.fd=open(from.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);out.fd=open(to.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
  if(in.fd<0||out.fd<0)fail("备份文件创建失败");struct stat before{},after{};
  if(fstat(in.fd,&before)||!S_ISREG(before.st_mode))fail("备份来源不是普通文件");
  char buffer[65536];for(;;){if(cancel)fail("cancelled");const auto n=read(in.fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;if(n<0)fail("备份读取失败");if(!n)break;
    size_t at=0;while(at<size_t(n)){const auto w=write(out.fd,buffer+at,size_t(n)-at);if(w<0&&errno==EINTR)continue;if(w<=0)fail("备份写入失败");at+=size_t(w);}}
  if(fstat(in.fd,&after)||before.st_size!=after.st_size||before.st_mtim.tv_sec!=after.st_mtim.tv_sec||before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec||fsync(out.fd))fail("备份期间文件变化或同步失败");
}
}
void verifyElf(const std::string &file,const std::string &platform){elfBytes(prefix(file),platform);}
int installationFlags(const std::string &version,const std::string &falconSha) {
  updates::Version target,current;
  if(!updates::Version::parse(version,target)||!updates::Version::parse(PENCHAT_VERSION,current))fail("无效的安装版本");
  if(!(target<current))return 0;
  // Verified CVI PMS already accepts older versions with flags=0. RK bit 14
  // skips only versionCompare, after AMR verification and processArchLibs.
  // Neither policy changes data/schema or integrity checks.
  if(falconSha=="11e46687d3c7301d3049d02d84144ee37e5a9994ca5ec1e9f5eaf278db92519b")return 0;
  static const std::set<std::string> verified={
    "2baaeac4c3cd6d6b990a5694431a367d8946d6ce7631a6929e92de5d64f0f4f4",
    "91e23dcef405d849e17757656cd2603ecc21cc9d20437bf38937c6b13b664a58",
    "5a1017889987de01315d685b28fb832e197359aad48b2e7b9b585eb8f3995e26"};
  if(!verified.count(falconSha))fail("当前 Falcon 固件尚未核验低版本覆盖接口；备份已保留，可选择同版本匹配包");
  return 0x4000;
}
Json environment(const std::string &workspace,const std::string &modelPath,const std::string &screenPath) {
  Json env={{"platform",""},{"abi_bits",sizeof(void*)*8},{"version",PENCHAT_VERSION},{"appid",PENCHAT_APP_ID},{"compatible",false},{"failure","unsupported_hardware"}};
  std::string model,compatible;
  try {model=fileText(modelPath,4096);if(modelPath=="/proc/device-tree/model")compatible=fileText("/proc/device-tree/compatible",4096);}catch(...){}
  auto board=model+compatible;std::transform(board.begin(),board.end(),board.begin(),[](unsigned char c){return char(std::tolower(c));});
  std::string platform;if(board.find("rk3562")!=std::string::npos)platform="rk3562";
  else if(board.find("cv1826")!=std::string::npos)platform="cv1826-s";
  env["platform"]=platform;
  try {
    const auto screen=Screen::fromConfig(readJson(screenPath),platform=="cv1826-s");env["screen"]=screen.descriptor();
    const ScreenSafety safety{screen.width,screen.height,board.find("rk3562 y07 ")!=std::string::npos};
    env["rescue_inset"]=std::max(16,safety.cornerInset());
  }catch(...){env["screen"]=nullptr;env["failure"]="screen_configuration";}
  auto payload=readJson(join(workspace,"assets/rescue/payload.json"));env["payload"]=payload;
  if(platform.empty())return env;
  if(!payload.is_object()||!payload.value("platform",Json()).is_string()){env["failure"]="payload_metadata";return env;}
  if(payload.value("platform",std::string())!=platform||(platform=="rk3562"?64:32)!=int(sizeof(void*)*8)){env["failure"]="architecture_mismatch";return env;}
  try {verifyElf(join(workspace,"assets/bin/penchat-lvgl"),platform);env["compatible"]=!env["screen"].is_null();if(env["compatible"])env["failure"]="";}
  catch(...){env["failure"]="native_elf";}
  return env;
}
Json validate(const std::string &path,const Json &release,const std::string &platform,std::atomic<bool> &cancel) {
  if(!hash(release.value("sha256",std::string()))||release.value("platform",std::string())!=platform)fail("降级包平台或校验信息无效");
  auto result=updates::validatePackage(path,PENCHAT_APP_ID,release.at("version"),release.at("sha256"),cancel);
  if(result["bytes"]!=release.at("size"))fail("降级包大小与目录不一致");
  mz_zip_archive zip{};if(!mz_zip_reader_init_file(&zip,path.c_str(),0))fail("无法检查安装包原生架构");
  struct Close{mz_zip_archive *p;~Close(){mz_zip_reader_end(p);}} close{&zip};
  const int index=mz_zip_reader_locate_file(&zip,"assets/bin/penchat-lvgl",nullptr,0);if(index<0)fail("安装包缺少 PenChat 主程序");
  std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),EVP_MD_CTX_free);
  if(!context||EVP_DigestInit_ex(context.get(),EVP_sha256(),nullptr)!=1)fail("主程序校验初始化失败");
  struct Prefix {std::string data;EVP_MD_CTX *hash;};Prefix p{{},context.get()};
  auto readPrefix=[](void *opaque,mz_uint64,const void *bytes,size_t n)->size_t {
    auto &s=*static_cast<Prefix*>(opaque);if(s.data.size()<64)s.data.append(static_cast<const char *>(bytes),std::min(n,64-s.data.size()));
    return EVP_DigestUpdate(s.hash,bytes,n)==1?n:0;
  };
  if(!mz_zip_reader_extract_to_callback(&zip,mz_uint(index),readPrefix,&p,0))fail("主程序解压校验失败");elfBytes(p.data,platform);
  // Validate every ELF, including support libraries. The sole cross-ABI
  // exception is the two standard-directory bootstrap modules.
  for(mz_uint i=0;i<mz_zip_reader_get_num_files(&zip);++i) {
    if(cancel)fail("cancelled");if(int(i)==index||mz_zip_reader_is_file_a_directory(&zip,i))continue;
    char filename[4096]{};mz_zip_reader_get_filename(&zip,i,filename,sizeof(filename));const std::string name=filename;
    struct Header {std::string bytes;std::atomic<bool> *cancel;};Header header{{},&cancel};
    if(!mz_zip_reader_extract_to_callback(&zip,i,+[](void *opaque,mz_uint64,const void *bytes,size_t n)->size_t {
      auto &h=*static_cast<Header*>(opaque);if(*h.cancel)return 0;
      if(h.bytes.size()<64)h.bytes.append(static_cast<const char*>(bytes),std::min(n,64-h.bytes.size()));return n;
    },&header,0))fail("组件解压校验失败");
    if(header.bytes.compare(0,4,"\177ELF"))continue;
    auto expected=platform;
    for(const auto &abi:std::vector<std::pair<std::string,std::string>>{{"arm","cv1826-s"},{"arm64","rk3562"}}) {
      const auto start="libs/"+abi.first+"/libjsapi_penchat_rescue_";
      if(name.compare(0,start.size(),start)==0&&name.size()==start.size()+19&&name.substr(name.size()-3)==".so"&&
         name.substr(start.size(),16).find_first_not_of("0123456789abcdef")==std::string::npos)expected=abi.second;
    }
    elfBytes(header.bytes,expected);
  }
  unsigned char sum[EVP_MAX_MD_SIZE];unsigned length=0;EVP_DigestFinal_ex(context.get(),sum,&length);
  std::string sha;for(unsigned i=0;i<length;++i){sha+="0123456789abcdef"[sum[i]>>4];sha+="0123456789abcdef"[sum[i]&15];}result["native_sha256"]=sha;return result;
}
int schema(const std::string &data) {
  struct stat st{};if(lstat(join(data,"penchat.db").c_str(),&st)||!S_ISREG(st.st_mode))return -1;
  void *library=dlopen("libsqlite3.so.0",RTLD_NOW|RTLD_LOCAL);if(!library)return -1;
  using Open=int(*)(const char*,sqlite3**,int,const char*);using Prepare=int(*)(sqlite3*,const char*,int,sqlite3_stmt**,const char**);
  auto open=reinterpret_cast<Open>(dlsym(library,"sqlite3_open_v2"));auto close=reinterpret_cast<int(*)(sqlite3*)>(dlsym(library,"sqlite3_close"));
  auto prepare=reinterpret_cast<Prepare>(dlsym(library,"sqlite3_prepare_v2"));auto step=reinterpret_cast<int(*)(sqlite3_stmt*)>(dlsym(library,"sqlite3_step"));
  auto column=reinterpret_cast<const unsigned char*(*)(sqlite3_stmt*,int)>(dlsym(library,"sqlite3_column_text"));auto finalize=reinterpret_cast<int(*)(sqlite3_stmt*)>(dlsym(library,"sqlite3_finalize"));
  int value=-1;sqlite3 *db=nullptr;sqlite3_stmt *statement=nullptr;
  if(open&&close&&prepare&&step&&column&&finalize&&open(join(data,"penchat.db").c_str(),&db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK&&
     prepare(db,"SELECT value FROM schema_meta WHERE key='schema_version'",-1,&statement,nullptr)==SQLITE_OK&&step(statement)==SQLITE_ROW) {
    const auto text=column(statement,0);if(text&&text[0]>='0'&&text[0]<='9'&&!text[1])value=text[0]-'0';
  }
  if(statement&&finalize)finalize(statement);if(db&&close)close(db);dlclose(library);return value;
}
bool nativeAlive(const std::string &data) {
  auto normalize=[](std::string path){while(path.size()>1&&path.back()=='/')path.pop_back();return path;};
  std::unique_ptr<DIR,decltype(&closedir)> dir(opendir("/proc"),closedir);if(!dir)fail("无法确认原生程序已停止");
  while(auto p=readdir(dir.get())) {
    const std::string pid=p->d_name;if(pid.find_first_not_of("0123456789")!=std::string::npos)continue;
    const auto cmd=fileText("/proc/"+pid+"/cmdline",65536);std::vector<std::string> args;size_t at=0;
    while(at<cmd.size()){const auto end=cmd.find('\0',at);args.push_back(cmd.substr(at,end-at));if(end==std::string::npos)break;at=end+1;}
    if(args.empty()||args[0].find("penchat-lvgl")==std::string::npos)continue;
    for(size_t i=1;i+1<args.size();++i)if(args[i]=="--data-dir"&&normalize(args[i+1])==normalize(data))return true;
  }return false;
}
struct Service::Impl {
  std::string workspace,data,publicRoot,root;Config config;Json env;
  mutable std::mutex mutex;std::condition_variable cv;std::thread worker,reporter;
  std::atomic<bool> cancelled{false},closing{false};
  Json published={{"status","idle"},{"releases",Json::array()}},reportState={{"status","idle"}};
  Json command;std::deque<Json> reportQueue;bool busy=false,reportBusy=false;
  Impl(std::string w,std::string d,std::string p,Config c):workspace(std::move(w)),data(std::move(d)),publicRoot(std::move(p)),root(join(data,".rescue")),config(std::move(c)),env(environment(workspace)) {
#ifndef PENCHAT_NATIVE_DEVICE
    if(config.fixture.is_object())env=config.fixture;
#endif
    try {privateDir(root);privateDir(join(root,"backups"));privateDir(join(root,"reports"));}
    catch(...){published["storage_error"]="降级存储不可写，请检查可用空间和权限";}
    auto old=privateDirectory(root)?readJson(join(root,"install.json")):Json();
    if(old.is_object()&&old.value("operation",Json()).is_string()&&old.value("state",Json()).is_string())published["ticket"]=old;
    const bool spoolSafe=privateDirectory(root)&&privateDirectory(join(root,"reports"));
    std::unique_ptr<DIR,decltype(&closedir)> spool(spoolSafe?opendir(join(root,"reports").c_str()):nullptr,closedir);
    if(spool)while(auto item=readdir(spool.get())) {
      const std::string name=item->d_name;
      if(name.size()!=69||name.substr(64)!=".json"||!hash(name.substr(0,64))||reportQueue.size()>=32)continue;
      const auto record=readJson(join(join(root,"reports"),name));
      try {
        if(record.is_object()&&record.value("state",std::string())=="pending"&&record.value("attempts",0)<4&&record.value("payload",Json()).is_object())
          reportQueue.push_back({{"code",record["payload"].value("rescue",Json::object()).value("code",std::string("unknown"))},{"fingerprint",name.substr(0,64)}});
      }catch(...){} // A malformed receipt must not prevent loading the rescue module.
    }
    worker=std::thread([this]{run();});
    try{reporter=std::thread([this]{reports();});}catch(...){closing=true;cv.notify_all();worker.join();throw;}
  }
  ~Impl(){closing=true;cancelled=true;cv.notify_all();if(worker.joinable())worker.join();if(reporter.joinable())reporter.join();}
  void status(const std::string &state,const std::string &message="") {
    Json snapshot;{std::lock_guard<std::mutex> lock(mutex);published["status"]=state;published["message"]=message;snapshot=published;}
    try{privateDir(root);durable(join(root,"state.json"),snapshot);}catch(...){} // Best-effort diagnostic; tickets/backup manifests are mandatory.
  }
  std::vector<std::string> routes()const{auto n=config.nodes;n.push_back("");return n;}
  HttpResult get(const std::string &url) {
    for(const auto &node:routes()){if(cancelled||closing)fail("cancelled");auto r=http(url,node,cancelled);if(r.code==0&&r.status==200)return r;}
    fail("无法读取降级目录，请检查网络后重试");return {};
  }
  bool complete(const std::string &url,uint64_t bytes) {
    for(const auto &node:routes()){if(cancelled||closing)fail("cancelled");try{const auto r=http(url,node,cancelled,{},0,{},"","",true);if(!r.code&&r.status==200&&r.length>=0&&uint64_t(r.length)==bytes)return true;}catch(...){}}
    return false;
  }
  std::string base() const {const auto platform=env.value("platform",std::string());if(platform.empty())fail("无法识别当前硬件平台");return config.root+"penchat-"+platform+"/";}
  void list() {
    status("listing","正在获取可用版本…");const auto b=base();const auto links=updates::directoryLinks(get(b).text,b);
    Json releases=Json::array();
    for(const auto &link:links) {
      updates::Version version;if(!link.directory||!updates::Version::parse(link.name,version))continue;
      if(releases.size()>=100)break;
      try {
        const auto folder=b+link.name+"/";const auto raw=get(folder+"release.json").text;if(raw.size()>32768)continue;auto r=Json::parse(raw);
        const auto file=r.value("file",std::string()),sha=r.value("sha256",std::string());
        if(r.value("format",0)!=1||r.value("version",std::string())!=link.name||r.value("platform",std::string())!=env["platform"]||
           !leaf(file)||file.size()<5||file.substr(file.size()-4)!=".amr"||!hash(sha)||!r.contains("size")||!r["size"].is_number_unsigned()||r["size"]==0||r["size"]>updates::PackageLimit)continue;
        if(updates::checksum(get(folder+file+".sha256").text,file)!=sha)continue;
        if(!complete(folder+file,r["size"].get<uint64_t>()))continue;
        const auto issues=r.value("known_issues",std::string()),build=r.value("build",std::string());
        if(issues.size()>2048||build.size()>1024)continue;
        releases.push_back({{"format",1},{"version",link.name},{"platform",env["platform"]},{"file",file},{"size",r["size"]},
          {"sha256",sha},{"url",folder+file},{"id",sha},{"channel",r.value("channel",std::string())=="release"?"release":"candidate"},
          {"schema",r.value("schema",Json())},{"known_issues",issues},{"build",build}});
      }catch(...){if(cancelled||closing)throw;}
    }
    std::sort(releases.begin(),releases.end(),[](const Json &a,const Json &b){updates::Version x,y;updates::Version::parse(a.at("version"),x);updates::Version::parse(b.at("version"),y);return y<x;});
    const int current=schema(data);
    for(auto &r:releases) {
      const auto range=r.value("schema",Json());bool compatible=false;
      if(current>=0&&range.is_object()&&range.value("min",Json()).is_number_integer()&&range.value("max",Json()).is_number_integer())compatible=current>=range["min"].get<int>()&&current<=range["max"].get<int>();
      r["compatible"]=compatible;
    }
    {std::lock_guard<std::mutex> lock(mutex);published["releases"]=releases;published["schema"]=current;}
    status("available",releases.empty()?"暂无可用降级版本，请稍后刷新或联系开发者提供对应机型安装包。":"请选择要安装的版本");
  }
  Json backup(const std::string &operation) {
    if(nativeAlive(data))fail("原生程序尚未完全停止，不能备份或安装");
    const auto target=join(join(root,"backups"),operation);privateDir(target);
    std::vector<Entry> files,shared;uint64_t bytes=0;entries(data,"",files,bytes,0,true);entries(publicRoot,"",shared,bytes);
    struct statvfs available{};if(statvfs(root.c_str(),&available)||uint64_t(available.f_bavail)*available.f_frsize<bytes+32*1024*1024)fail("完整备份空间不足，未安装");
    Json manifest={{"format",1},{"operation",operation},{"time_ms",nowMs()},{"files",Json::array()}};
    std::vector<std::string> directories{target};
    for(const auto &source:std::vector<std::pair<std::string,std::vector<Entry>>>{{data,files},{publicRoot,shared}}) {
      const auto group=source.first==data?"data":"public";privateDir(join(target,group));
      directories.push_back(join(target,group));
      for(const auto &file:source.second) {
        const auto dest=join(join(target,group),file.relative);
        if(file.directory){privateDir(dest);directories.push_back(dest);manifest["files"].push_back({{"path",join(group,file.relative)},{"directory",true}});continue;}
        copy(join(source.first,file.relative),dest,cancelled);
        const auto sha=updates::fileHash(dest,cancelled);manifest["files"].push_back({{"path",join(group,file.relative)},{"size",file.bytes},{"sha256",sha}});
      }
    }
    for(auto it=directories.rbegin();it!=directories.rend();++it){const int fd=open(it->c_str(),O_DIRECTORY|O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)fail("备份目录无法同步");const bool ok=fsync(fd)==0;close(fd);if(!ok)fail("备份目录同步失败");}
    durable(join(target,"manifest.json"),manifest);return {{"path",target},{"files",manifest["files"].size()},{"bytes",bytes}};
  }
  void prepare(const Json &args) {
    privateDir(root);privateDir(join(root,"backups"));
    Json release;
    {std::lock_guard<std::mutex> lock(mutex);for(const auto &r:published["releases"])if(r["id"]==args.value("id",std::string()))release=r;}
    if(release.is_null())fail("版本列表已变化，请重新选择");
    {std::lock_guard<std::mutex> lock(mutex);published["selected"]=release;published["total"]=release.at("size");published["downloaded"]=0;}
    if(!release.value("compatible",false)&&!args.value("force",false))fail("此版本不兼容或兼容性未知，需要先确认完整备份并强制降级");
    if(nativeAlive(data))fail("请先停止 PenChat 原生程序再进行降级安装");
    const auto operation=uuid(),temporary=join(root,operation+".part");
    status("downloading","正在下载降级安装包…");bool downloaded=false;
    for(const auto &node:routes()) {
      try{http(release.at("url"),node,cancelled,temporary,release.at("size"),[this](uint64_t size){std::lock_guard<std::mutex> lock(mutex);published["downloaded"]=size;});downloaded=true;break;}
      catch(...){unlink(temporary.c_str());if(cancelled||closing)throw;}
    }
    if(!downloaded)fail("降级包下载失败，请重试");
    try {
      status("validating","正在校验包身份及架构…");const auto validated=validate(temporary,release,env.at("platform"),cancelled);
      status("backing_up","正在完整备份，备份失败不会安装…");const auto saved=backup(operation);
      if(cancelled||closing)fail("cancelled");
      const auto path=join(root,"package.amr");if(rename(temporary.c_str(),path.c_str()))fail("降级包无法暂存");
      Json ticket={{"operation",operation},{"appid",PENCHAT_APP_ID},{"version",release.at("version")},{"platform",env.at("platform")},
        {"sha256",release.at("sha256")},{"size",release.at("size")},{"native_sha256",validated.at("native_sha256")},
        {"path",path},{"backup",saved},{"state","prepared"},{"force",args.value("force",false)}};
      durable(join(root,"install.json"),ticket);
      {std::lock_guard<std::mutex> lock(mutex);published["ticket"]=ticket;}
      status("ready_to_install","备份和校验完成，正在准备安装…");
    }catch(...){unlink(temporary.c_str());throw;}
  }
  void run() {
    try{privateDir(root);durable(join(root,"environment.json"),env);}catch(...){}
    for(;;) {
      Json job;{std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[this]{return closing||!command.is_null();});if(closing)return;job=command;command=nullptr;}
      try{if(job["op"]=="list")list();else prepare(job["args"]);}catch(const std::exception &e){status(cancelled?"cancelled":"error",cancelled?"操作已取消":e.what());}
      {std::lock_guard<std::mutex> lock(mutex);busy=false;}
    }
  }
  void reports() {
    for(;;) {
      Json failure;{std::unique_lock<std::mutex> lock(mutex);cv.wait(lock,[this]{return closing||!reportQueue.empty();});if(closing)return;failure=reportQueue.front();reportQueue.pop_front();reportBusy=true;}
      try {
        privateDir(root);privateDir(join(root,"reports"));
        const std::set<std::string> codes={"startup","runtime","architecture_mismatch","module","native_elf","payload_metadata","screen_configuration","timeout","unknown"};
        auto code=failure.value("code",std::string("unknown"));if(!codes.count(code))code="unknown";
        const auto last=readJson(join(data,"last-exit.json"));Json signature=Json::object();
        for(const char *key:{"status","signal"})if(last.is_object()&&last.contains(key)&&last[key].is_number_integer())signature[key]=last[key];
        const auto resumed=failure.value("fingerprint",std::string());
        const auto fingerprint=hash(resumed)?resumed:digest(std::string(PENCHAT_VERSION)+env.value("platform",std::string())+env.value("payload",Json()).dump()+code+signature.dump());
        const auto reportFile=join(join(root,"reports"),fingerprint+".json");
        auto record=readJson(reportFile);const bool retry=failure.value("retry",false);
        const bool recent=record.is_object()&&record.value("fingerprint",std::string())==fingerprint&&nowMs()-record.value("time_ms",int64_t(0))<86400000;
        if(!retry&&recent&&record.value("state",std::string())!="pending") {
          std::lock_guard<std::mutex> lock(mutex);reportState={{"status",record.value("state",std::string("pending"))},{"id",record.value("id",std::string())},{"deduplicated",true}};
        } else {
          const auto id=recent?record.value("id",uuid()):uuid();
          Json payload={{"format_version",1},{"event_id",id},{"time_ms",nowMs()},{"type","rescue_startup_failure"},
            {"app",{{"appid",PENCHAT_APP_ID},{"name",PENCHAT_NAME},{"version",PENCHAT_VERSION},{"platform","linux-rescue"}}},{"description","启动降级自动精简诊断"},
            {"screen",env.value("screen",Json())},{"rescue",{{"code",code},{"platform",env["platform"]},{"abi_bits",env["abi_bits"]},
              {"package_platform",env.value("payload",Json()).is_object()?env["payload"].value("platform",std::string()):std::string()}}}};
          auto exit=readJson(join(data,"last-exit.json"));for(const char *key:{"status","pid","runtimeId","ownerGeneration","time_ms"})if(exit.is_object()&&exit.contains(key)&&exit[key].is_number())payload["rescue"][key]=exit[key];
          payload["diagnostics"]=diagnosticSummary(data);
          const auto buildTest=env.value("payload",Json()).is_object()?env["payload"].value("test_run",std::string()):std::string();
          if(buildTest.size()==36&&buildTest.find_first_not_of("0123456789abcdef-")==std::string::npos)payload["rescue"]["test_run"]=buildTest;
          if(recent)payload=record.at("payload");
          else {
            unsigned retained=0;privateDir(join(root,"reports"));
            std::unique_ptr<DIR,decltype(&closedir)> dir(opendir(join(root,"reports").c_str()),closedir);
            if(dir)while(auto entry=readdir(dir.get())) {
              const std::string name=entry->d_name;if(name.size()!=69||name.substr(64)!=".json"||!hash(name.substr(0,64)))continue;
              const auto path=join(join(root,"reports"),name);const auto old=readJson(path);
              if(old.is_object()&&old.value("state",std::string())!="pending"&&nowMs()-old.value("time_ms",int64_t(0))>=86400000)unlink(path.c_str());else ++retained;
            }
            if(retained>=32)fail("降级诊断队列已满");
            record={{"id",id},{"fingerprint",fingerprint},{"time_ms",nowMs()},{"payload",payload},{"attempts",0},{"due_ms",0}};
          }
          if(retry){record["attempts"]=0;record["due_ms"]=0;}
          record["state"]="pending";durable(reportFile,record);
          if(config.endpoint.empty()||config.token.empty()) {
            std::lock_guard<std::mutex> lock(mutex);reportState={{"status","disabled"},{"message","此构建未配置降级日志服务"}};
          } else {
            bool sent=false;
            for(unsigned attempt=record.value("attempts",0u);attempt<4&&!closing;++attempt) {
              auto delay=record.value("due_ms",int64_t(0))-nowMs();
              if(delay>0){std::unique_lock<std::mutex> lock(mutex);cv.wait_for(lock,std::chrono::milliseconds(std::min<int64_t>(120000,delay)),[this]{return closing.load();});}
              if(closing)break;
              record["attempts"]=attempt+1;durable(reportFile,record);
              {std::lock_guard<std::mutex> lock(mutex);reportState={{"status","sending"},{"id",id},{"attempt",attempt+1}};}
              const auto nodes=routes();HttpResult response;
              try{response=http(config.endpoint,nodes[attempt%nodes.size()],closing,{},0,{},payload.dump(),config.token);}catch(...){response.code=7;}
              auto reply=Json::parse(response.text,nullptr,false);
              if(response.code==0&&response.status==201&&reply.is_object()&&reply.value("ok",Json())==true){sent=true;break;}
              const bool temporary=response.code||response.status==429||response.status==507||response.status>=500;
              if(!temporary)break;
              int unit=1000;
#ifndef PENCHAT_NATIVE_DEVICE
              unit=std::max(1,config.retryUnitMs);
#endif
              record["due_ms"]=nowMs()+int64_t(unit)*(attempt==0?10:attempt==1?30:120);durable(reportFile,record);
            }
            record["state"]=sent?"sent":closing?"pending":"failed";durable(reportFile,record);
            {std::lock_guard<std::mutex> lock(mutex);reportState={{"status",sent?"sent":"failed"},{"id",id}};}
          }
        }
      }catch(...){std::lock_guard<std::mutex> lock(mutex);reportState={{"status","failed"},{"message","精简诊断暂未发送，可稍后重试"}};}
      {std::lock_guard<std::mutex> lock(mutex);reportBusy=false;}
    }
  }
};
Service::Service(std::string w,std::string d,std::string p,Config c):p_(new Impl(std::move(w),std::move(d),std::move(p),std::move(c))){}
Service::~Service()=default;
Json Service::bootstrap()const{return p_->env;}
Json Service::state()const {
  auto disk=privateDirectory(p_->root)?readJson(join(p_->root,"install.json")):Json();
  if(disk.is_object()&&(!disk.value("operation",Json()).is_string()||!disk.value("state",Json()).is_string()))disk=nullptr;
  std::lock_guard<std::mutex> lock(p_->mutex);
  auto ticket=p_->published.value("ticket",Json());
  if(disk.is_object()&&ticket.is_object()&&disk.value("operation",std::string())==ticket.value("operation",std::string())&&disk!=ticket&&
     (disk.value("state",std::string())=="failed"||disk.value("state",std::string())=="unknown")) {
    p_->published["ticket"]=disk;p_->published["status"]=disk.at("state");
  }
  auto result=p_->published;result["report"]=p_->reportState;return result;
}
Json Service::request(const std::string &op,const Json &args) {
  if(op!="list"&&op!="prepare")fail("未知降级操作");
  std::lock_guard<std::mutex> lock(p_->mutex);if(p_->busy)fail("已有降级操作正在执行");
  if(p_->published.value("status",std::string())=="installing")fail("安装已提交，不能重复操作");
  const auto ticket=p_->published.value("ticket",Json());
  if(op=="prepare"&&ticket.is_object()&&(ticket.value("state",std::string())=="requested"||ticket.value("state",std::string())=="unknown"))fail("上次安装结果尚未确认，请先核对结果");
  p_->cancelled=false;p_->busy=true;const auto id=uuid();p_->published["job"]=id;p_->published["status"]="queued";p_->command={{"op",op},{"args",args}};p_->cv.notify_all();return {{"job",id}};
}
void Service::cancel(){
  std::lock_guard<std::mutex> lock(p_->mutex);const auto ticket=p_->published.value("ticket",Json());
  if(p_->published.value("status",std::string())=="installing"||(ticket.is_object()&&
     (ticket.value("state",std::string())=="requested"||ticket.value("state",std::string())=="unknown")))return;
  p_->cancelled=true;p_->published["status"]="cancelled";
}
void Service::report(const Json &failure,bool retry) {
  std::lock_guard<std::mutex> lock(p_->mutex);if(p_->reportQueue.size()>=32)return;
  Json report={{"code",failure.value("code",std::string("unknown"))},{"retry",retry}};p_->reportQueue.push_back(report);p_->cv.notify_all();
}
Json Service::authorize(const Json &ticket) {
  if(p_->cancelled)fail("降级操作已取消");
  Json own;
  {std::lock_guard<std::mutex> lock(p_->mutex);own=p_->published.value("ticket",Json());
    if(!own.is_object()||own!=ticket||own.value("state",std::string())!="prepared")fail("安装票据无效或已提交");
    p_->published["ticket"]["state"]="validating_install";}
  try {
    privateDir(p_->root);
    if(nativeAlive(p_->data))fail("原生程序仍运行，安装未提交");
    validate(own.at("path"),own,p_->env.at("platform"),p_->cancelled);
    own["state"]="requested";durable(join(p_->root,"install.json"),own);
    {std::lock_guard<std::mutex> lock(p_->mutex);p_->published["ticket"]=own;p_->published["status"]="installing";}
    return own;
  }catch(...){installResult(own.at("operation"),"failed",-1);throw;}
}
Json Service::verifyInstalled(const Json &info) {
  if(!privateDirectory(p_->root))fail("降级存储目录不可用");
  auto ticket=readJson(join(p_->root,"install.json"));if(!ticket.is_object())return {{"verified",false}};
  const auto state=ticket.value("state",std::string());
  if(state!="requested"&&state!="unknown"&&state!="installed")return {{"verified",false}};
  if(info.value("appid",std::string())!=PENCHAT_APP_ID||info.value("version",std::string())!=ticket.at("version"))return {{"verified",false},{"status","unknown"}};
  const auto path=info.value("installPath",std::string());
  if(path.empty()||path.front()!='/'||path.find("..")!=std::string::npos||
     (path!=p_->workspace&&(path.find(std::string("/")+PENCHAT_APP_ID+"/")==std::string::npos&&path.substr(path.find_last_of('/')+1)!=PENCHAT_APP_ID)))fail("安装目录不属于 PenChat");
  const auto file=join(path,"assets/bin/penchat-lvgl");verifyElf(file,p_->env.at("platform"));
  if(updates::fileHash(file,p_->cancelled)!=ticket.at("native_sha256"))return {{"verified",false},{"status","unknown"}};
  installResult(ticket.at("operation"),"installed",0);return {{"verified",true},{"version",ticket.at("version")}};
}
void Service::installResult(const std::string &operation,const std::string &state,int code) {
  privateDir(p_->root);
  auto ticket=readJson(join(p_->root,"install.json"));if(!ticket.is_object()||ticket.value("operation",std::string())!=operation)return;
  if(ticket.value("state",std::string())=="installed"&&state!="installed")return;
  ticket["state"]=state;ticket["code"]=code;durable(join(p_->root,"install.json"),ticket);
  std::lock_guard<std::mutex> lock(p_->mutex);p_->published["ticket"]=ticket;p_->published["status"]=state;
}
void recordInstallResult(const std::string &data,const std::string &operation,const std::string &state,int code) {
  privateDir(join(data,".rescue"));
  const auto path=join(join(data,".rescue"),"install.json");auto ticket=readJson(path);
  if(!ticket.is_object()||ticket.value("operation",std::string())!=operation)return;
  if(ticket.value("state",std::string())=="installed"&&state!="installed")return;
  ticket["state"]=state;ticket["code"]=code;durable(path,ticket);
}
} }
