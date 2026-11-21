#include "backup_restore.hpp"
#include "database.hpp"
#include "presets.hpp"
#include "editor.hpp"
#include <sqlite3.h>
#include <openssl/sha.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/file.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <limits>
namespace penchat {
namespace {
using RestoreError=BackupRestoreError;
void need(bool ok,const char *text){if(!ok)throw RestoreError(text);}
void cancelled(const std::atomic<bool> &flag){need(!flag,"操作已取消，现有数据没有改变");}
bool exists(const std::string &path){struct stat s{};return lstat(path.c_str(),&s)==0;}
bool relative(const std::string &p){
  if(p.empty()||p.size()>4096||p.front()=='/'||p.back()=='/'||p.find('\\')!=std::string::npos)return false;
  for(unsigned char c:p)if(c<32||c==127)return false;
  for(size_t i=0;i<p.size();){const auto n=p.find('/',i),end=n==std::string::npos?p.size():n;const auto part=p.substr(i,end-i);if(part.empty()||part=="."||part==".."||part.find(':')!=std::string::npos)return false;i=end+1;}
  return true;
}
void safe(const std::string &p,bool file=false){
  need(!p.empty()&&p.front()=='/',"备份路径无效");
  for(size_t at=1;;){const auto end=p.find('/',at);const auto part=end==std::string::npos?p:p.substr(0,end);struct stat s{};
    need(!lstat(part.c_str(),&s)&&!S_ISLNK(s.st_mode),"目录包含符号链接或已变化");
    need(end==std::string::npos?(file?(S_ISREG(s.st_mode)&&s.st_nlink==1):S_ISDIR(s.st_mode)):S_ISDIR(s.st_mode),"备份文件类型不受支持");
    if(end==std::string::npos)break;
    at=end+1;
  }
}
int openSafe(const std::string &p,int flags,mode_t mode=0600){
  need(!p.empty()&&p.front()=='/',"文件路径无效");int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);need(fd>=0,"无法打开文件根目录");
  // Falcon may spell its owned data directory with repeated/trailing '/'.
  // Preserve that spelling in DB asset references; skip only empty components
  // in fd-relative traversal, retaining O_NOFOLLOW and rejection of '.'/'..'.
  try{for(size_t at=1;at<p.size();){auto end=p.find('/',at);if(end==at){++at;continue;}const auto leaf=p.substr(at,end==std::string::npos?p.size()-at:end-at);need(relative(leaf)&&leaf.find('/')==std::string::npos,"文件路径无效");
    const int next=openat(fd,leaf.c_str(),(end==std::string::npos?flags:O_RDONLY|O_DIRECTORY)|O_NOFOLLOW|O_CLOEXEC,mode);need(next>=0,"文件路径已变化或不可访问");close(fd);fd=next;if(end==std::string::npos)return fd;at=end+1;}
    return fd;
  }catch(...){close(fd);throw;}
}
std::string readFile(const std::string &p,size_t limit){const int fd=openSafe(p,O_RDONLY);std::string out;char buffer[65536];try{struct stat st{};need(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=0&&uint64_t(st.st_size)<=limit,"备份文本超过大小限制");for(;;){auto n=read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;need(n>=0&&size_t(n)<=limit-out.size(),"备份文本读取失败或过大");if(!n)break;out.append(buffer,size_t(n));}close(fd);return out;}catch(...){close(fd);throw;}}
DIR *openDirSafe(const std::string &p){const int fd=openSafe(p,O_RDONLY|O_DIRECTORY);DIR *d=fdopendir(fd);if(!d){close(fd);throw RestoreError("无法读取目录");}return d;}
void dir(const std::string &p){
  need(!p.empty()&&p.front()=='/',"还原目录无效");int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);need(fd>=0,"无法打开目录");
  try{for(size_t at=1;at<p.size();){const auto end=p.find('/',at);if(end==at){++at;continue;}const auto part=p.substr(at,end==std::string::npos?p.size()-at:end-at);need(relative(part)&&part.find('/')==std::string::npos,"还原目录无效");
    int next=openat(fd,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(next<0&&errno==ENOENT){need(mkdirat(fd,part.c_str(),0700)==0&&fsync(fd)==0,"无法创建或同步还原目录");next=openat(fd,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);}need(next>=0,"还原目录包含链接或不可访问");close(fd);fd=next;if(end==std::string::npos)break;
    at=end+1;}
    close(fd);
  }catch(...){close(fd);throw;}
}
std::string parent(const std::string &p){return p.substr(0,p.find_last_of('/'));}
void syncDir(const std::string &p){safe(p);const int fd=openSafe(p,O_RDONLY|O_DIRECTORY);need(fd>=0,"目录无法同步");const bool ok=fsync(fd)==0;close(fd);need(ok,"目录同步失败");}
void durable(const std::string &p,const Json &j){
  const auto folder=parent(p),leaf=p.substr(p.find_last_of('/')+1),temporary=uid(".restore-journal");
  const int directory=openSafe(folder,O_RDONLY|O_DIRECTORY);int fd=-1;
  try{
    fd=openat(directory,temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);need(fd>=0,"无法写入还原事务记录");
    const auto data=j.dump();size_t offset=0;
    while(offset<data.size()){const auto n=write(fd,data.data()+offset,data.size()-offset);if(n<0&&errno==EINTR)continue;need(n>0,"还原事务记录写入失败");offset+=size_t(n);}
    need(fsync(fd)==0,"还原事务记录同步失败");close(fd);fd=-1;
    need(renameat(directory,temporary.c_str(),directory,leaf.c_str())==0&&fsync(directory)==0,"还原事务记录提交失败");close(directory);
  }catch(...){if(fd>=0)close(fd);unlinkat(directory,temporary.c_str(),0);close(directory);throw;}
}
uint64_t bytes(const std::string &p){safe(p,true);struct stat s{};need(!lstat(p.c_str(),&s)&&s.st_size>=0,"无法读取备份大小");return uint64_t(s.st_size);}
void space(const std::string &p,uint64_t amount){struct statvfs v{};need(!statvfs(p.c_str(),&v)&&uint64_t(v.f_bavail)*v.f_frsize>=amount+16ull*1024*1024,"还原所需空间不足");}
std::string shaText(const std::string &s){unsigned char h[32];SHA256(reinterpret_cast<const unsigned char*>(s.data()),s.size(),h);static const char hex[]="0123456789abcdef";std::string out;for(auto c:h){out+=hex[c>>4];out+=hex[c&15];}return out;}
std::string hashFile(const std::string &p,const std::atomic<bool> &cancel){
  const int fd=openSafe(p,O_RDONLY);struct stat before{},after{};SHA256_CTX ctx;SHA256_Init(&ctx);char buffer[65536];
  try{need(!fstat(fd,&before)&&S_ISREG(before.st_mode)&&before.st_nlink==1,"不是独立的普通备份文件");for(;;){cancelled(cancel);auto n=read(fd,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;need(n>=0,"读取校验文件失败");if(!n)break;SHA256_Update(&ctx,buffer,size_t(n));}
    need(!fstat(fd,&after)&&before.st_size==after.st_size&&before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec,"文件在校验时变化");close(fd);
  }catch(...){close(fd);throw;}
  unsigned char raw[32];SHA256_Final(raw,&ctx);char hex[65]{};for(unsigned i=0;i<32;++i)std::snprintf(hex+2*i,3,"%02x",raw[i]);return hex;
}
bool digest(const std::string &s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
bool uuid(const std::string &s){return s.size()==36&&s[8]=='-'&&s[13]=='-'&&s[18]=='-'&&s[23]=='-'&&s.find_first_not_of("0123456789abcdef-")==std::string::npos;}
Json parse(const std::string &s){
  size_t budget=1000000;
  try{return Json::parse(s,[&](int depth,Json::parse_event_t,Json&){need(depth<=64&&budget--,"备份结构过于复杂");return true;});}
  catch(const RestoreError&){throw;}catch(...){throw RestoreError("备份中的JSON数据无效");}
}
std::string title(const std::string &s){std::string out;unsigned n=0;for(const auto &c:characters(s)){if(n++>=80)break;if(c.size()==1&&static_cast<unsigned char>(c[0])<32)out+=' ';else out+=c;}return out;}
void eraseAt(int parentFd,const std::string &name){
  struct stat s{};if(fstatat(parentFd,name.c_str(),&s,AT_SYMLINK_NOFOLLOW))return;
  if(S_ISDIR(s.st_mode)){const int fd=openat(parentFd,name.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return;DIR *d=fdopendir(fd);if(!d){close(fd);return;}while(auto e=readdir(d)){const std::string n=e->d_name;if(n!="."&&n!="..")eraseAt(dirfd(d),n);}closedir(d);unlinkat(parentFd,name.c_str(),AT_REMOVEDIR);}
  else unlinkat(parentFd,name.c_str(),0);
}
void eraseTree(const std::string &p){if(!exists(p))return;const int fd=openSafe(parent(p),O_RDONLY|O_DIRECTORY);eraseAt(fd,p.substr(p.find_last_of('/')+1));close(fd);}
void copyFile(const std::string &from,const std::string &to,const std::string &expected,uint64_t size,const std::atomic<bool> &cancel){
  safe(from,true);dir(parent(to));space(parent(to),size);need(!exists(to),"还原暂存文件已存在");
  int in=openSafe(from,O_RDONLY),out=-1;
  need(in>=0,"备份文件无法打开");struct stat before{},after{};
  try{
    need(!fstat(in,&before)&&S_ISREG(before.st_mode)&&before.st_nlink==1&&uint64_t(before.st_size)==size,"备份文件大小已变化");
    out=openSafe(to,O_WRONLY|O_CREAT|O_EXCL);need(out>=0,"还原文件无法创建");
    SHA256_CTX ctx;SHA256_Init(&ctx);uint64_t total=0;char buffer[65536];
    for(;;){cancelled(cancel);ssize_t n=read(in,buffer,sizeof(buffer));if(n<0&&errno==EINTR)continue;need(n>=0,"备份文件读取失败");if(!n)break;
      need(uint64_t(n)<=size-total,"备份文件在读取时增长");SHA256_Update(&ctx,buffer,size_t(n));size_t off=0;
      while(off<size_t(n)){auto w=write(out,buffer+off,size_t(n)-off);if(w<0&&errno==EINTR)continue;need(w>0,"还原文件写入失败");off+=size_t(w);}total+=uint64_t(n);
    }
    need(total==size&&!fstat(in,&after)&&before.st_ino==after.st_ino&&before.st_size==after.st_size&&before.st_mtim.tv_sec==after.st_mtim.tv_sec&&before.st_mtim.tv_nsec==after.st_mtim.tv_nsec,"备份文件在复制时变化");
    unsigned char raw[32];SHA256_Final(raw,&ctx);char hex[65]{};for(unsigned i=0;i<32;++i)std::snprintf(hex+i*2,3,"%02x",raw[i]);need(expected==hex,"备份文件校验失败");need(fsync(out)==0,"还原文件无法同步");close(out);out=-1;close(in);in=-1;syncDir(parent(to));
  }catch(...){if(in>=0)close(in);if(out>=0)close(out);unlink(to.c_str());throw;}
}
struct File {std::string name,hash;uint64_t size=0;bool directory=false;};
struct Manifest {
  std::string root,identity;Json info;std::map<std::string,File> files;
  Manifest(const std::string &path,std::atomic<bool> &cancel,const std::function<void(size_t,size_t)> &progress={}):root(path){
    safe(root);safe(join(root,"manifest.json"),true);const auto text=readFile(join(root,"manifest.json"),16*1024*1024);need(!text.empty(),"备份清单不存在或过大");identity=shaText(text);info=parse(text);
    need(info.is_object()&&info.value("format",0)==1&&info.value("files",Json()).is_array()&&info["files"].size()<=100000,"备份清单格式不受支持");
    if(info.contains("appid"))need(info["appid"].is_string()&&info["appid"]==PENCHAT_APP_ID,"备份属于其他应用");
    for(const auto &row:info["files"]){need(row.is_object()&&row.value("path",Json()).is_string(),"备份文件记录无效");const auto name=row["path"].get<std::string>();
      need(relative(name)&&(name.compare(0,5,"data/")==0||name.compare(0,7,"public/")==0)&&name!="data/.rescue"&&name.compare(0,13,"data/.rescue/")!=0,"备份包含越界路径");
      File f;f.name=name;
      if(row.contains("directory")){need(row["directory"].is_boolean()&&row["directory"].get<bool>(),"备份目录记录无效");f.directory=true;safe(join(root,name));}
      else{need(row.value("sha256",Json()).is_string()&&digest(row["sha256"])&&row.value("size",Json()).is_number_integer(),"备份校验记录无效");need(row["size"].get<int64_t>()>=0&&row["size"].get<uint64_t>()<=512ull*1024*1024,"备份单个文件超过512MiB或大小无效");f.hash=row["sha256"];f.size=row["size"].get<uint64_t>();}
      need(files.emplace(name,f).second,"备份有重复路径");
    }
    need(files.count("data/penchat.db")&&!files.at("data/penchat.db").directory,"备份缺少聊天数据库");
    std::function<void(const std::string&,const std::string&,unsigned)> walk=[&](const std::string &path,const std::string &rel,unsigned depth){
      need(depth<=64,"备份目录嵌套过深");safe(path);DIR *d=openDirSafe(path);
      try{while(auto e=readdir(d)){const std::string n=e->d_name;if(n=="."||n==".."||(rel.empty()&&n=="manifest.json"))continue;cancelled(cancel);const auto key=rel.empty()?n:rel+"/"+n;struct stat s{};need(!fstatat(dirfd(d),n.c_str(),&s,AT_SYMLINK_NOFOLLOW)&&!S_ISLNK(s.st_mode),"备份包含符号链接");
        if(key=="data"||key=="public")need(S_ISDIR(s.st_mode),"备份分组不是目录");else{need(files.count(key),"备份包含清单未记录的文件");need(files.at(key).directory?S_ISDIR(s.st_mode):S_ISREG(s.st_mode),"备份文件类型与清单不一致");}
        if(S_ISDIR(s.st_mode))walk(join(path,n),key,depth+1);
      }}catch(...){closedir(d);throw;}closedir(d);
    };walk(root,"",0);
    size_t n=0;for(const auto &entry:files){cancelled(cancel);const auto &f=entry.second;if(!f.directory)need(bytes(join(root,f.name))==f.size&&hashFile(join(root,f.name),cancel)==f.hash,"备份文件大小或SHA-256不一致");if(progress)progress(++n,files.size());}
  }
  void copy(const std::string &name,const std::string &to,const std::atomic<bool> &cancel)const{auto f=files.find(name);need(f!=files.end()&&!f->second.directory,"备份缺少引用的文件");copyFile(join(root,name),to,f->second.hash,f->second.size,cancel);}
};
std::string manifestReference(const Manifest &m,const std::string &path){
  if(path.empty())throw RestoreError("备份图片路径为空");
  if(relative(path)&&m.files.count("data/"+path))return "data/"+path;
  if(m.info.value("data_root",Json()).is_string()){const auto prefix=m.info["data_root"].get<std::string>()+"/";if(path.compare(0,prefix.size(),prefix)==0&&relative(path.substr(prefix.size()))&&m.files.count("data/"+path.substr(prefix.size())))return "data/"+path.substr(prefix.size());}
  for(const auto &prefix:{std::string("/assets/"),std::string("/assets-drafts/")}){const auto pos=path.find(prefix);if(pos!=std::string::npos){const auto rel=path.substr(pos+1);if(relative(rel)&&m.files.count("data/"+rel))return "data/"+rel;}}
  throw RestoreError("备份缺少图片文件或图片引用越界");
}
class Sql {
  sqlite3 *db_=nullptr;bool owned_=true;
public:
  explicit Sql(sqlite3 *db):db_(db),owned_(false){}
  explicit Sql(const std::string &path,bool writable=false){
    safe(path,true);if(sqlite3_open_v2(path.c_str(),&db_,writable?SQLITE_OPEN_READWRITE:SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){if(db_)sqlite3_close(db_);db_=nullptr;throw RestoreError("备份数据库无法打开");}
    sqlite3_busy_timeout(db_,3000);sqlite3_limit(db_,SQLITE_LIMIT_LENGTH,16*1024*1024);sqlite3_limit(db_,SQLITE_LIMIT_SQL_LENGTH,64*1024);
  }
  void cancelWith(const std::atomic<bool> *flag){sqlite3_progress_handler(db_,2000,[](void *p){return *static_cast<const std::atomic<bool>*>(p)?1:0;},const_cast<std::atomic<bool>*>(flag));}
  void close(){if(db_&&owned_)sqlite3_close(db_);db_=nullptr;}
  ~Sql(){close();}
  void each(const std::string &sql,const Json &args,const std::function<void(Json)> &fn)const{
    sqlite3_stmt *s=nullptr;need(sqlite3_prepare_v2(db_,sql.c_str(),-1,&s,nullptr)==SQLITE_OK,"备份数据库结构无效");
    try{
      for(size_t i=0;i<args.size();++i){const auto &v=args[i];int rc=SQLITE_ERROR;if(v.is_null())rc=sqlite3_bind_null(s,i+1);else if(v.is_string()){const auto text=v.get<std::string>();rc=sqlite3_bind_text(s,i+1,text.data(),int(text.size()),SQLITE_TRANSIENT);}else if(v.is_number_integer())rc=sqlite3_bind_int64(s,i+1,v.get<int64_t>());else if(v.is_boolean())rc=sqlite3_bind_int(s,i+1,v.get<bool>());need(rc==SQLITE_OK,"备份查询参数无效");}
      int rc;while((rc=sqlite3_step(s))==SQLITE_ROW){Json row=Json::object();for(int i=0;i<sqlite3_column_count(s);++i){const auto name=sqlite3_column_name(s,i);switch(sqlite3_column_type(s,i)){
        case SQLITE_NULL:row[name]=nullptr;break;case SQLITE_INTEGER:row[name]=sqlite3_column_int64(s,i);break;case SQLITE_FLOAT:row[name]=sqlite3_column_double(s,i);break;
        case SQLITE_TEXT:{const int n=sqlite3_column_bytes(s,i);row[name]=std::string(reinterpret_cast<const char*>(sqlite3_column_text(s,i)),size_t(n));break;}
        default:throw RestoreError("备份数据库包含不支持的二进制列");}}fn(std::move(row));}
      need(rc==SQLITE_DONE,"备份数据库读取失败");sqlite3_finalize(s);
    }catch(...){sqlite3_finalize(s);throw;}
  }
  Json q(const std::string &sql,const Json &args=Json::array())const{Json out=Json::array();each(sql,args,[&](Json row){need(out.size()<100000,"备份记录过多");out.push_back(std::move(row));});return out;}
  void snapshot(const std::string &path){
    need(!exists(path),"快照目标已存在");sqlite3 *to=nullptr;if(sqlite3_open(path.c_str(),&to)!=SQLITE_OK){if(to)sqlite3_close(to);throw RestoreError("无法创建备份数据库快照");}
    auto b=sqlite3_backup_init(to,"main",db_,"main");const int rc=b?sqlite3_backup_step(b,-1):SQLITE_ERROR;const int end=b?sqlite3_backup_finish(b):SQLITE_ERROR;const bool sealed=rc==SQLITE_DONE&&end==SQLITE_OK&&sqlite3_exec(to,"PRAGMA journal_mode=DELETE",nullptr,nullptr,nullptr)==SQLITE_OK;sqlite3_close(to);need(sealed,"备份数据库快照失败");chmod(path.c_str(),0600);
  }
};
const std::vector<std::string> Tables={"schema_meta","providers","model_configs","app_settings","chat_sessions","chat_messages","message_parts","message_assets","stream_events","chat_drafts","draft_attachments","reply_groups","reply_results","reply_asset_refs"};
Json tableShape(const Sql &db,const std::string &table){return db.q("PRAGMA table_info("+table+")");}
std::string fingerprint(const Sql &db){
  SHA256_CTX ctx;SHA256_Init(&ctx);
  for(const auto &table:Tables){if(table=="schema_meta")continue;const auto columns=tableShape(db,table);std::string order;for(const auto &c:columns){if(!order.empty())order+=',';order+='"'+c["name"].get<std::string>()+'"';}
    db.each("SELECT * FROM "+table+" ORDER BY "+order,Json::array(),[&](Json row){const auto text=table+":"+row.dump()+"\n";SHA256_Update(&ctx,text.data(),text.size());});}
  unsigned char raw[32];SHA256_Final(raw,&ctx);char hex[65]{};for(unsigned i=0;i<32;++i)std::snprintf(hex+2*i,3,"%02x",raw[i]);return hex;
}
void validateDatabase(const Sql &source,const Sql &current){
  for(const auto &row:source.q("SELECT name,type,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'")){
    const auto kind=row["type"].get<std::string>(),name=row["name"].get<std::string>();
    need(kind=="table"||kind=="index","备份含不支持的触发器或视图");
    if(kind=="table"){need(name=="asset_gc"||std::find(Tables.begin(),Tables.end(),name)!=Tables.end(),"备份含未知数据表");const auto sql=row["sql"].is_string()?row["sql"].get<std::string>():"";need(sql.find("VIRTUAL")==std::string::npos&&sql.find("virtual")==std::string::npos,"备份含虚拟数据表");}
  }
  const auto version=source.q("SELECT value FROM schema_meta WHERE key='schema_version'");need(version.size()==1&&version[0]["value"]=="3","此备份的数据结构不受当前版本支持，请使用兼容版本");
  std::map<std::string,Json> shapes;
  for(const auto &table:Tables){const auto a=tableShape(source,table),b=tableShape(current,table);need(!a.empty()&&a==b,"备份的数据表结构与当前版本不兼容");shapes[table]=a;}
  const auto check=source.q("PRAGMA integrity_check");need(check.size()==1&&check[0].begin().value()=="ok","备份数据库完整性检查失败");need(source.q("PRAGMA foreign_key_check").empty(),"备份数据库外键检查失败");
  const std::vector<std::string> checks={
    "SELECT 1 FROM chat_messages m WHERE NOT EXISTS(SELECT 1 FROM chat_sessions s WHERE s.id=m.session_id) LIMIT 1",
    "SELECT 1 FROM message_parts p WHERE NOT EXISTS(SELECT 1 FROM chat_messages m WHERE m.id=p.message_id AND m.session_id=p.session_id) OR (p.asset_id IS NOT NULL AND NOT EXISTS(SELECT 1 FROM message_assets a WHERE a.id=p.asset_id)) LIMIT 1",
    "SELECT 1 FROM model_configs m WHERE NOT EXISTS(SELECT 1 FROM providers p WHERE p.id=m.provider_id) LIMIT 1",
    "SELECT 1 FROM message_assets a WHERE (a.session_id IS NOT NULL AND NOT EXISTS(SELECT 1 FROM chat_sessions s WHERE s.id=a.session_id)) OR (a.message_id IS NOT NULL AND NOT EXISTS(SELECT 1 FROM chat_messages m WHERE m.id=a.message_id AND (a.session_id IS NULL OR a.session_id=m.session_id))) LIMIT 1",
    "SELECT 1 FROM stream_events e WHERE NOT EXISTS(SELECT 1 FROM chat_messages m WHERE m.id=e.message_id AND m.session_id=e.session_id) LIMIT 1",
    "SELECT 1 FROM chat_messages GROUP BY session_id,seq HAVING count(*)>1 LIMIT 1",
    "SELECT 1 FROM message_parts GROUP BY message_id,part_index HAVING count(*)>1 LIMIT 1",
    "SELECT 1 FROM model_configs GROUP BY provider_id,name HAVING count(*)>1 LIMIT 1",
    "SELECT 1 FROM draft_attachments d WHERE NOT EXISTS(SELECT 1 FROM chat_drafts x WHERE x.draft_key=d.draft_key) LIMIT 1",
    "SELECT 1 FROM chat_drafts d WHERE d.draft_key!='new' AND NOT EXISTS(SELECT 1 FROM chat_sessions s WHERE s.id=d.draft_key) LIMIT 1",
    "SELECT 1 FROM draft_attachments GROUP BY draft_key,position HAVING count(*)>1 LIMIT 1",
    "SELECT 1 FROM reply_groups g WHERE NOT EXISTS(SELECT 1 FROM chat_messages m WHERE m.id=g.message_id AND m.session_id=g.session_id AND m.role='assistant') OR NOT EXISTS(SELECT 1 FROM reply_results r WHERE r.id=g.selected_id AND r.group_id=g.id) LIMIT 1",
    "SELECT 1 FROM reply_asset_refs a WHERE NOT EXISTS(SELECT 1 FROM reply_groups g WHERE g.id=a.group_id) OR NOT EXISTS(SELECT 1 FROM message_assets i WHERE i.id=a.asset_id) OR (a.owner_id!=a.group_id AND NOT EXISTS(SELECT 1 FROM reply_results r WHERE r.id=a.owner_id AND r.group_id=a.group_id)) LIMIT 1",
    "SELECT 1 FROM providers WHERE is_default=1 LIMIT 1 OFFSET 1",
    "SELECT 1 FROM model_configs WHERE is_default=1 LIMIT 1 OFFSET 1"
  };
  for(const auto &sql:checks)need(source.q(sql).empty(),"备份存在重复身份或缺失的数据关联");
  const auto currentCheck=current.q("PRAGMA integrity_check");need(currentCheck.size()==1&&currentCheck[0].begin().value()=="ok","当前数据库完整性检查失败，请先维护当前数据");
  std::set<std::string> assets;for(const auto &a:source.q("SELECT id FROM message_assets"))assets.insert(a.at("id"));
  auto snapshotRow=[&](const Json &r,const std::string &table,const std::set<std::string> &extras){need(r.is_object(),"备份快照记录不是对象");std::set<std::string> fields;for(const auto &c:shapes.at(table)){const std::string key=c.at("name"),type=c.at("type");fields.insert(key);need(r.contains(key),"备份快照缺少完整字段");const auto &v=r.at(key);if(v.is_null())need(c.at("notnull")==0&&c.at("pk")==0,"备份快照必填字段为空");else need(type=="TEXT"?v.is_string():type=="INTEGER"&&v.is_number_integer(),"备份快照字段类型无效");}for(auto i=r.begin();i!=r.end();++i)need(fields.count(i.key())||extras.count(i.key()),"备份快照包含未知字段");};
  auto parts=[&](const Json &r,const std::string &sid,const std::string &mid){
    need(r.value("_parts",Json()).is_array()&&r.value("_events",Json()).is_array(),"备份回复快照不完整");std::set<int64_t> indices;
    for(const auto &p:r["_parts"]){snapshotRow(p,"message_parts",{});need(p.is_object()&&p.value("session_id",Json())==sid&&p.value("message_id",Json())==mid&&p.value("part_index",Json()).is_number_integer()&&indices.insert(p["part_index"].get<int64_t>()).second,"备份回复片段身份或顺序无效");if(p.value("asset_id",Json()).is_string())need(assets.count(p["asset_id"]),"备份回复图片关联缺失");}
    for(const auto &e:r["_events"]){snapshotRow(e,"stream_events",{});need(e.value("session_id",Json())==sid&&e.value("message_id",Json())==mid,"备份事件归属无效");}
  };
  source.each("SELECT * FROM reply_groups",{},[&](Json group){const auto baseline=parse(group.at("baseline_json"));const std::string sid=group.at("session_id");need(baseline.is_object()&&baseline.value("session",Json()).is_object()&&baseline["session"].value("id",Json())==sid&&baseline.value("rows",Json()).is_array(),"备份回复基线格式无效");snapshotRow(baseline["session"],"chat_sessions",{});for(const auto &row:baseline["rows"]){snapshotRow(row,"chat_messages",{"parts","_parts","_events","_checkpoint"});need(row.is_object()&&row.value("id",Json()).is_string()&&row.value("session_id",Json())==sid,"备份回复基线身份无效");parts(row,sid,row.at("id"));}});
  source.each("SELECT r.payload_json,g.session_id,g.message_id FROM reply_results r JOIN reply_groups g ON g.id=r.group_id",{},[&](Json r){const auto value=parse(r.at("payload_json"));need(value.is_object()&&value.value("id",Json())==r.at("message_id")&&value.value("session_id",Json())==r.at("session_id")&&value.value("role",Json())=="assistant","备份回复结果身份无效");snapshotRow(value,"chat_messages",{"parts","_parts","_events","_checkpoint","_previous_result","_new_group"});parts(value,r.at("session_id"),r.at("message_id"));});
  for(const auto &table:Tables){
    const auto &columns=shapes.at(table);std::vector<std::string> jsonColumns;
    for(const auto &c:columns){const auto name=c["name"].get<std::string>();if(name!="raw_json"&&(name=="json"||(name.size()>5&&name.substr(name.size()-5)=="_json")))jsonColumns.push_back(name);}
    source.each("SELECT * FROM "+table,Json::array(),[&](Json row){
      for(const auto &column:columns){const std::string key=column.at("name"),type=column.at("type");const auto &value=row.at(key);if(value.is_null())need(column.at("notnull")==0&&column.at("pk")==0,"备份必填列为空");else if(type=="INTEGER")need(value.is_number_integer(),"备份整数列类型错误");else if(type=="TEXT")need(value.is_string(),"备份文本列类型错误");else throw RestoreError("备份包含未知列类型");}
      for(const auto &key:{"id","session_id","message_id","provider_id","group_id","draft_key"})if(row.contains(key)&&!row[key].is_null())need(row[key].is_string()&&!row[key].get<std::string>().empty()&&row[key].get<std::string>().size()<=256,"备份中的稳定身份无效");
      for(const auto &name:jsonColumns)if(row[name].is_string()&&!row[name].get<std::string>().empty()){
        const auto value=parse(row[name]);
        if(name=="metadata_json"||name=="headers_json"||name=="capabilities_json"||name=="default_parameters_json")need(value.is_object(),"备份配置JSON类型无效");
      }
    });
  }
}
const std::map<std::string,std::string> SettingNames={
  {"ui_scale_percent","界面缩放"},{"reading_font_percent","字体大小"},{"theme_mode","主题"},{"ui_refresh_hz","刷新率"},
  {"penchat_settings","扫描与拍照设置"},{"ocr_multiline","文字扫描模式"},{"pinyin_chinese_layout","中文键盘布局"},{"wide_keyboard_swap","横屏键盘布局"},
  {"pinyin_correction","拼音纠错"},{"pinyin_learning","拼音学习"},{"prefer_official_keyboard","默认官方键盘"},{"quick_official_keyboard","官方键盘快捷入口"},
  {"history_time_display","历史时间显示"},{"landscape_composer_shortcut","快捷输入框"},{"pull_sensitivity_offset","新聊天手势灵敏度"},
  {"reasoning_display","思考过程显示"},{"retry_archive_prompt","重试归档提示"},{"message_edit_default","消息编辑偏好"},
  {"auto_context_compression","自动压缩上下文"},{"context_compression_percent","上下文压缩阈值"},{"auto_title_enabled","自动会话标题"},{"title_model_id","会话标题模型"},
  {"http_idle_seconds","连接保留时间"},{"camera_auto_orientation","相机自动方向"},{"camera_orientation","相机方向"},
  {"persistent_export_directory","持久化导出目录"},{"presets_enabled","会话预设开关"},{"new_chat_web","新聊天联网偏好"},{"last_web_enabled","联网开关"}
};
void validateSetting(const std::string &key,const Json &value){
  const std::set<std::string> booleans={"ocr_multiline","wide_keyboard_swap","pinyin_correction","pinyin_learning","prefer_official_keyboard","quick_official_keyboard","retry_archive_prompt","auto_context_compression","auto_title_enabled","camera_auto_orientation","persistent_export_directory","presets_enabled","last_web_enabled"};
  if(booleans.count(key))need(value.is_boolean(),"备份中的开关配置类型无效");
  const std::map<std::string,std::set<std::string>> enums={{"pinyin_chinese_layout",{"zh26","zh9"}},{"history_time_display",{"time","title","off"}},{"landscape_composer_shortcut",{"auto","manual","off"}},{"reasoning_display",{"collapsed","during","always"}},{"message_edit_default",{"ask","archive","overwrite"}},{"new_chat_web",{"on","off","recent"}}};
  if(enums.count(key))need(value.is_string()&&enums.at(key).count(value),"备份配置选项无效");
  if(key=="title_model_id")need(value.is_string(),"备份标题模型配置无效");
  if(key=="http_idle_seconds")need(value.is_number_integer()&&value.get<int64_t>()>=15&&value.get<int64_t>()<=600,"备份连接保留时间无效");
  if(key=="pull_sensitivity_offset")need(value.is_number_integer()&&value.get<int64_t>()>=-25&&value.get<int64_t>()<=25,"备份手势灵敏度无效");
  if(key=="context_compression_percent")need(value.is_number_integer()&&value.get<int64_t>()>=1&&value.get<int64_t>()<=100,"备份压缩阈值无效");
  if(key=="camera_orientation")need(value.is_number_integer()&&(value==0||value==90||value==180||value==270),"备份相机方向无效");
  if(key=="ui_scale_percent"||key=="reading_font_percent")need(value.is_number_integer()&&value.get<int64_t>()>=75&&value.get<int64_t>()<=150,"备份中的缩放或字号超出范围");
  if(key=="theme_mode")need(value.is_string()&&(value=="dark"||value=="light"),"备份主题无效");
  if(key=="ui_refresh_hz")need(value.is_number_integer()&&(value==30||value==60),"备份刷新率无效");
  if(key=="penchat_settings"){
    need(value.is_object(),"备份中的应用配置格式无效");
    for(const auto &name:{"autoSendCapturedContent","saveCameraToAlbum"})if(value.contains(name))need(value[name].is_boolean(),"备份中的拍照或自动发送开关无效");
  }
}
void validatePresets(const Json &items){
  need(items.is_array()&&items.size()<=10000,"备份会话预设格式无效");std::set<std::string> ids;
  for(const auto &p:items){need(p.is_object()&&p.value("id",Json()).is_string()&&p.value("name",Json()).is_string(),"备份预设身份或名称无效");const auto id=p["id"].get<std::string>();need(!id.empty()&&id.size()<=256&&ids.insert(id).second,"备份预设身份重复");
    for(const auto &name:{"prompt_mode","prompt","model_id","model_when","effort","effort_when","compression_model_id"})if(p.contains(name))need(p[name].is_string(),"备份预设字段类型无效");
    if(p.contains("prompt_mode"))need(p["prompt_mode"]=="inherit"||p["prompt_mode"]=="clear"||p["prompt_mode"]=="custom","备份预设提示词模式无效");
    for(const auto &key:{"model_when","effort_when"})if(p.contains(key))need(p[key]=="empty"||p[key]=="always","备份预设应用方式无效");
  }
}
struct Unit {std::string key,kind,id,target,name;bool conflict=false;Json extra;};
Json publicUnit(const Unit &u){return {{"key",u.key},{"kind",u.kind},{"name",u.name},{"conflict",u.conflict}};}
void insert(Database &db,const std::string &table,const Json &row){std::string columns,values;Json args=Json::array();for(auto i=row.begin();i!=row.end();++i){if(!columns.empty()){columns+=',';values+=',';}columns+='"'+i.key()+'"';values+='?';args.push_back(i.value());}db.execute("INSERT INTO "+table+"("+columns+") VALUES("+values+")",args);}
} // namespace
struct BackupRestore::Impl {
  Database &db;Events &events;std::string data,publicRoot,root,stage,op,backupId,currentHash,sourceHash;
  std::unique_ptr<Manifest> manifest;
  std::vector<Unit> units;std::map<std::string,std::string> models;
  std::thread worker;std::atomic<bool> cancel{false},active{false};uint64_t generation=0;bool lease=false,applied=false;
  Impl(Database &d,Events &e,std::string path,std::string publicPath):db(d),events(e),data(std::move(path)),publicRoot(std::move(publicPath)),root(join(data,".rescue")){}
  ~Impl(){try{stop(false);}catch(...){}}
  void emit(const std::string &kind,Json value){value["token"]=generation;events.push(kind,std::move(value));}
  void progress(const std::string &message,size_t n=0,size_t total=0){events.pushLatest("backup_progress",{{"token",generation},{"message",message},{"done",n},{"total",total}});}
  void reset(){
    if(lease){db.releaseTransferSnapshot();lease=false;}
    if(!stage.empty()&&!exists(join(root,"restore-journal.json")))eraseTree(stage);
    manifest.reset();units.clear();models.clear();stage.clear();op.clear();applied=false;
  }
  void stop(bool keep){cancel=true;if(worker.joinable())worker.join();if(!keep)reset();}
  Json list(){
    const auto folder=join(root,"backups");Json rows=Json::array();if(!exists(folder))return {{"items",rows}};safe(folder);
    DIR *d=openDirSafe(folder);
    try{while(auto e=readdir(d)){cancelled(cancel);const std::string name=e->d_name;if(!uuid(name))continue;need(rows.size()<1000,"备份数量过多，请先整理");
      const auto path=join(folder,name);struct stat s{};if(fstatat(dirfd(d),name.c_str(),&s,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(s.st_mode))continue;
      Json row={{"id",name},{"valid",false},{"time",int64_t(0)},{"version",""},{"files",0}};
      try{const auto m=parse(readFile(join(path,"manifest.json"),16*1024*1024));need(m.value("format",0)==1&&m.value("files",Json()).is_array(),"备份清单无效");if(m.contains("appid"))need(m["appid"]==PENCHAT_APP_ID,"备份属于其他应用");
        row["valid"]=true;row["time"]=m.value("time_ms",int64_t(0));row["version"]=title(m.value("source_version",std::string()));row["files"]=m["files"].size();
      }catch(...){row["error"]="备份清单缺失或无效";}rows.push_back(row);
    }}catch(...){closedir(d);throw;}closedir(d);
    std::sort(rows.begin(),rows.end(),[](const Json&a,const Json&b){return a.at("time").get<int64_t>()>b.at("time").get<int64_t>();});return {{"items",rows}};
  }
  void add(std::string kind,std::string id,std::string target,std::string name,bool conflict,Json extra=Json::object()){
    need(units.size()<10000,"备份项目过多，无法安全建立还原清单");const auto key=shaText(kind+":"+id);for(const auto &u:units)need(u.key!=key,"备份项目身份重复");units.push_back({key,std::move(kind),std::move(id),std::move(target),title(name),conflict,std::move(extra)});
  }
  Json preview(const std::string &id){
    reset();need(uuid(id),"备份身份无效");backupId=id;op=uid("restore");stage=join(join(root,"restore-stage"),op);dir(stage);
    progress("正在校验备份文件…");manifest.reset(new Manifest(join(join(root,"backups"),id),cancel,[&](size_t n,size_t count){if(n%8==0||n==count)progress("正在校验备份文件…",n,count);}));
    const auto dbBytes=manifest->files.at("data/penchat.db").size;space(stage,dbBytes*3);
    manifest->copy("data/penchat.db",join(stage,"input.db"),cancel);
    for(const auto &suffix:{"-wal","-journal"})if(manifest->files.count(std::string("data/penchat.db")+suffix))manifest->copy(std::string("data/penchat.db")+suffix,join(stage,std::string("input.db")+suffix),cancel);
    // SQLite is allowed to reconstruct WAL/SHM only in this private copy.
    {Sql input(join(stage,"input.db"),true);input.cancelWith(&cancel);input.snapshot(join(stage,"source.db"));}
    db.restoreSnapshot(join(stage,"current.db"));lease=true;
    Sql source(join(stage,"source.db")),current(join(stage,"current.db"));source.cancelWith(&cancel);current.cancelWith(&cancel);
    progress("正在核验数据库与关联…");validateDatabase(source,current);currentHash=fingerprint(current);
    source.each("SELECT id,name FROM providers ORDER BY id",{},[&](Json p){const std::string id=p["id"];add("provider",id,id,"服务商 · "+p["name"].get<std::string>(),!current.q("SELECT id FROM providers WHERE id=?",{id}).empty());});
    source.each("SELECT id,provider_id,name,display_name FROM model_configs ORDER BY id",{},[&](Json m){
      const std::string id=m["id"];const auto byId=current.q("SELECT id FROM model_configs WHERE id=?",{id}),byName=current.q("SELECT id FROM model_configs WHERE provider_id=? AND name=?",{m["provider_id"],m["name"]});
      need(byId.empty()||byName.empty()||byId[0]["id"]==byName[0]["id"],"模型身份存在歧义，请先处理同名模型");
      const std::string target=!byId.empty()?byId[0]["id"].get<std::string>():!byName.empty()?byName[0]["id"].get<std::string>():id;models[id]=target;
      add("model",id,target,"模型 · "+m["display_name"].get<std::string>(),!byId.empty()||!byName.empty());
    });
    for(const auto &spec:{std::make_pair(std::string("providers"),std::string("默认服务商")),std::make_pair(std::string("model_configs"),std::string("默认模型"))}){
      const auto s=source.q("SELECT id FROM "+spec.first+" WHERE is_default=1"),c=current.q("SELECT id FROM "+spec.first+" WHERE is_default=1");
      if(!s.empty())add("default",spec.first,spec.first,spec.second,!c.empty(),{{"id",s[0]["id"]}});
    }
    source.each("SELECT id,title,status FROM chat_sessions ORDER BY created_at,id",{},[&](Json s){const std::string id=s["id"];add("chat",id,id,"聊天 · "+s["title"].get<std::string>(),!current.q("SELECT id FROM chat_sessions WHERE id=?",{id}).empty());});
    const auto srcPresets=source.q("SELECT value_json FROM app_settings WHERE key='chat_presets'"),curPresets=current.q("SELECT value_json FROM app_settings WHERE key='chat_presets'");std::set<std::string> presetIds;
    if(!curPresets.empty())for(const auto &p:presetList({{"chat_presets",parse(curPresets[0]["value_json"])}}))presetIds.insert(p.at("id"));
    else presetIds.insert("default");
    if(!srcPresets.empty()){const auto list=parse(srcPresets[0]["value_json"]);validatePresets(list);for(const auto &p:list){const std::string id=p["id"];add("preset",id,id,"会话预设 · "+p["name"].get<std::string>(),presetIds.count(id),p);}}
    source.each("SELECT key,value_json FROM app_settings ORDER BY key",{},[&](Json row){const std::string key=row["key"];if(!SettingNames.count(key))return;validateSetting(key,parse(row["value_json"]));add("setting",key,key,SettingNames.at(key),!current.q("SELECT key FROM app_settings WHERE key=?",{key}).empty());});
    if(!source.q("SELECT draft_key FROM chat_drafts WHERE draft_key='new'").empty())add("draft","new","new","新聊天草稿",!current.q("SELECT draft_key FROM chat_drafts WHERE draft_key='new'").empty());
    for(const auto &entry:manifest->files){const auto &f=entry.second;if(f.directory)continue;
      if(f.name=="data/ime/user.json"){
        need(f.size<=2*1024*1024,"输入法记忆超过支持范围");const auto p=parse(readFile(join(manifest->root,f.name),2*1024*1024));need(p.value("version",0)==1&&p.value("records",Json()).is_array()&&p["records"].size()<=10000,"输入法记忆格式无效");
        std::set<std::string> seen;for(const auto &r:p["records"]){need(r.is_object()&&r.value("pinyin",Json()).is_string()&&r.value("word",Json()).is_string()&&r.value("count",Json()).is_number_integer()&&r.value("last",Json()).is_number_integer(),"输入法记忆记录无效");const std::string py=r["pinyin"],word=r["word"];need(!py.empty()&&py.size()<=128&&py.front()!='\''&&py.back()!='\''&&py.find("''")==std::string::npos&&py.find_first_not_of("abcdefghijklmnopqrstuvwxyz'")==std::string::npos&&!word.empty()&&word.size()<=128&&r["count"].get<int64_t>()>=1&&r["count"].get<int64_t>()<=1000000&&seen.insert(py+":"+word).second,"输入法记忆记录无效");for(auto cp:editScalars(word))need((cp>=0x3400&&cp<=0x9fff)||(cp>=0x20000&&cp<=0x323af),"输入法记忆汉字无效");}
        add("ime",f.name,f.name,"输入法学习记忆",exists(join(data,"ime/user.json")));
      }else if(f.name.compare(0,7,"public/")==0){const auto name=f.name.substr(7),target=join(publicRoot,name);if(exists(target))safe(target,true);add("public",f.name,name,"传输文件 · "+name,exists(target));}
    }
    source.each("SELECT * FROM message_assets a WHERE a.session_id IS NOT NULL OR a.message_id IS NOT NULL OR EXISTS(SELECT 1 FROM message_parts p WHERE p.asset_id=a.id) OR EXISTS(SELECT 1 FROM reply_asset_refs r WHERE r.asset_id=a.id)",{},[&](Json a){need(a.value("stored_path",Json()).is_string()&&!a["stored_path"].get<std::string>().empty(),"备份未包含图片原文件");const auto file=manifestReference(*manifest,a["stored_path"]);need(!manifest->files.at(file).directory,"备份图片不是文件");if(a.value("display_src",Json()).is_string()&&!a["display_src"].get<std::string>().empty())manifestReference(*manifest,a["display_src"]);});
    source.each("SELECT image_json FROM draft_attachments",{},[&](Json a){const auto image=parse(a.at("image_json"));need(image.is_object()&&image.value("path",Json()).is_string()&&!image["path"].get<std::string>().empty(),"备份草稿图片缺少路径");manifestReference(*manifest,image["path"]);if(image.contains("previewPath")){need(image["previewPath"].is_string()&&!image["previewPath"].get<std::string>().empty(),"备份草稿预览路径无效");manifestReference(*manifest,image["previewPath"]);}});
    size_t conflicts=0;Json rows=Json::array();for(const auto &u:units){conflicts+=u.conflict;rows.push_back(publicUnit(u));}
    source.close();current.close();sourceHash=hashFile(join(stage,"source.db"),cancel);
    for(const auto &name:{"input.db","input.db-wal","input.db-shm","input.db-journal","current.db","current.db-wal","current.db-shm"}){const auto path=join(stage,name);if(exists(path)){safe(path,true);need(unlink(path.c_str())==0,"无法清理校验快照");}}
    return {{"plan",op},{"backup",id},{"items",rows},{"new",units.size()-conflicts},{"conflicts",conflicts}};
  }
  bool start(const std::string &kind,Json args){
    if(active)return false;
    if(worker.joinable())worker.join();
    cancel=false;active=true;++generation;
    worker=std::thread([this,kind,args]{try{Json value;if(kind=="list"){reset();value=list();}else if(kind=="preview")value=preview(args.value("id",std::string()));else if(kind=="discard"){reset();value=Json::object();}else throw RestoreError("未知备份操作");value["operation"]=kind;emit("backup_result",value);}catch(const RestoreError &e){emit("backup_error",{{"message",cancel?"操作已取消":e.what()}});try{reset();}catch(...){}}catch(...){emit("backup_error",{{"message","备份校验失败或数据不兼容，请检查备份完整性"}});try{reset();}catch(...){}}active=false;});return true;
  }
  Json commit(const Json &,const std::function<void(const Json&)> &);
};
namespace {
void testFault(const char *stage){
#ifndef PENCHAT_NATIVE_DEVICE
  if(const auto value=getenv("PENCHAT_TEST_RESTORE_FAULT"))if(std::string(value)==stage){if(std::string(stage)=="file_error"||std::string(stage)=="db_error")throw RestoreError("还原写入故障注入");_exit(77);}
#else
  (void)stage;
#endif
}
bool privatePayload(const std::string &key){return key.find("opaque")!=std::string::npos||key.find("anthropic")!=std::string::npos||key.find("response")!=std::string::npos||key.find("request")!=std::string::npos||key=="raw_json"||key=="data_json"||key=="encrypted_content"||key=="signature"||key=="arguments"||key=="input"||key=="tool_calls"||key=="tool_input";}
void assetReferences(const Json &value,const std::function<void(const std::string&)> &visit,unsigned depth=0){
  need(depth<=64,"备份图片引用层次过深");
  if(value.is_array()){for(const auto &v:value)assetReferences(v,visit,depth+1);return;}
  if(!value.is_object())return;
  for(auto i=value.begin();i!=value.end();++i){
    const auto &key=i.key();if(privatePayload(key))continue;
    if(key=="asset_id"&&i.value().is_string()&&!i.value().get<std::string>().empty())visit(i.value());
    else if(i.value().is_string()&&(key=="metadata_json"||key=="json"||key=="baseline_json"||key=="payload_json")){
      const std::string raw=i.value();if(!raw.empty())assetReferences(parse(raw),visit,depth+1);
    }else assetReferences(i.value(),visit,depth+1);
  }
}
bool operationId(const std::string &s){return s.compare(0,8,"restore_")==0&&s.size()<160&&s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;}
std::string liveFingerprint(Database &db){
  return db.restoreRead([](sqlite3 *connection){Sql live(connection);return Json(fingerprint(live));}).get<std::string>();
}
void upsert(Database &db,const std::string &table,Json row,const std::string &key,const std::string &id){
  const auto old=db.query("SELECT "+key+" FROM "+table+" WHERE "+key+"=?",{id});row[key]=id;
  if(old.empty()){insert(db,table,row);return;}
  std::string fields;Json args=Json::array();for(auto i=row.begin();i!=row.end();++i){if(i.key()==key)continue;if(!fields.empty())fields+=',';fields+='"'+i.key()+"\"=?";args.push_back(i.value());}args.push_back(id);db.execute("UPDATE "+table+" SET "+fields+" WHERE "+key+"=?",args);
}
bool below(const std::string &path,const std::string &root){const auto prefix=root+"/";return path.compare(0,prefix.size(),prefix)==0&&relative(path.substr(prefix.size()));}
void targetSafe(const std::string &path){
  need(!path.empty()&&path.front()=='/',"还原目标路径无效");
  for(size_t at=1;at<path.size();){const auto end=path.find('/',at);const auto part=end==std::string::npos?path:path.substr(0,end);struct stat s{};
    if(lstat(part.c_str(),&s)){need(errno==ENOENT,"还原目标无法访问");return;}
    need(!S_ISLNK(s.st_mode)&&(end==std::string::npos?S_ISREG(s.st_mode):S_ISDIR(s.st_mode)),"还原目标存在链接或文件类型冲突");if(end==std::string::npos)break;
    at=end+1;
  }
}
void replaceFile(const std::string &source,const std::string &target,const std::string &hash,uint64_t size,const std::string &op,const std::atomic<bool> &cancel){
  targetSafe(target);dir(parent(target));const auto leaf=".penchat-restore-"+shaText(target+op)+".part";const auto temporary=join(parent(target),leaf);need(!exists(temporary),"还原临时文件已存在");
  copyFile(source,temporary,hash,size,cancel);testFault("before_file_rename");const int fd=openSafe(parent(target),O_RDONLY|O_DIRECTORY);
  const bool ok=renameat(fd,leaf.c_str(),fd,target.substr(target.find_last_of('/')+1).c_str())==0;const bool sync=ok&&fsync(fd)==0;close(fd);if(!ok)unlink(temporary.c_str());need(ok&&sync,"还原文件替换或同步失败");
}
Json journalRead(const std::string &data,const std::string &publicRoot){
  const auto p=join(join(data,".rescue"),"restore-journal.json");const auto j=parse(readFile(p,16*1024*1024));
  need(j.value("format",0)==1&&j.value("appid",std::string())==PENCHAT_APP_ID&&j.value("operation",Json()).is_string()&&operationId(j["operation"]),"还原事务记录无效");
  need(j.value("files",Json()).is_array()&&j.value("assets",Json()).is_array(),"还原文件事务记录无效");
  const auto stage=join(join(join(data,".rescue"),"restore-stage"),j.at("operation"));
  std::set<std::string> targets;
  for(const auto &f:j["files"]){need(f.is_object()&&f.value("target",Json()).is_string(),"还原目标记录无效");const std::string target=f["target"];need((below(target,publicRoot)||target==join(data,"ime/user.json"))&&targets.insert(target).second,"还原目标越界或重复");
    for(const auto &key:{"new_path","old_path"})if(f.contains(key)&&!f[key].is_null())need(f[key].is_string()&&below(f[key],join(stage,"files")),"还原暂存路径越界");
    need(f.value("new_hash",Json()).is_string()&&digest(f["new_hash"])&&f.value("new_size",Json()).is_number_unsigned(),"还原新文件记录无效");
    if(f.value("had_old",false))need(f.value("old_hash",Json()).is_string()&&digest(f["old_hash"])&&f.value("old_size",Json()).is_number_unsigned(),"还原旧文件记录无效");
  }
  for(const auto &a:j["assets"]){need(a.value("path",Json()).is_string()&&a.value("hash",Json()).is_string()&&digest(a["hash"]),"还原图片记录无效");const std::string p=a["path"];need(below(p,join(data,"assets"))||below(p,join(data,"assets-drafts")),"还原图片路径越界");}
  return j;
}
void fileRollback(const Json &j,const std::string &data,const std::string &publicRoot,const std::atomic<bool> &cancel){
  for(auto it=j["files"].rbegin();it!=j["files"].rend();++it){const auto &f=*it;const std::string target=f["target"];targetSafe(target);
    if(f.value("had_old",false)){
      if(exists(target)&&hashFile(target,cancel)==f.at("old_hash"))continue;
      need(exists(target)&&hashFile(target,cancel)==f.at("new_hash"),"还原目标被其他操作修改，已保留事务记录");
      const std::string old=f.at("old_path");need(bytes(old)==f.at("old_size")&&hashFile(old,cancel)==f.at("old_hash"),"还原回滚副本校验失败");replaceFile(old,target,f.at("old_hash"),f.at("old_size"),j.at("operation"),cancel);
    }else if(exists(target)){
      need(hashFile(target,cancel)==f.at("new_hash"),"新文件被其他操作修改，已保留事务记录");const int fd=openSafe(parent(target),O_RDONLY|O_DIRECTORY);const bool removed=unlinkat(fd,target.substr(target.find_last_of('/')+1).c_str(),0)==0;const bool synced=fsync(fd)==0;close(fd);need(removed&&synced,"无法回滚新增文件");
    }
  }
  for(const auto &f:j["files"]){const std::string target=f.at("target");const auto temporary=join(parent(target),".penchat-restore-"+shaText(target+j.at("operation").get<std::string>())+".part");if(exists(temporary)){safe(temporary,true);need(unlink(temporary.c_str())==0,"无法清理中断的还原临时文件");syncDir(parent(temporary));}}
  // New asset names are never replacements. Keep any externally referenced
  // files; normal GC can collect otherwise unreferenced files after recovery.
  (void)data;(void)publicRoot;
}
void fileVerify(const Json &j,const std::atomic<bool> &cancel){for(const auto &f:j["files"]){const std::string path=f["target"];need(bytes(path)==f.at("new_size")&&hashFile(path,cancel)==f.at("new_hash"),"已提交还原的文件校验失败");}for(const auto &a:j["assets"]){const std::string path=a["path"];need(hashFile(path,cancel)==a.at("hash"),"已提交图片的校验失败");}}
}
Json BackupRestore::Impl::commit(const Json &choices,const std::function<void(const Json&)> &notify){
  need(!active&&!applied&&manifest&&choices.value("plan",std::string())==op,"还原清单已失效，请重新选择备份");cancel=false;applied=true;
  const auto actions=choices.value("actions",Json());need(actions.is_object(),"还原冲突选择无效");std::set<std::string> expected,selected;
  size_t added=0,replaced=0,skipped=0;
  for(const auto &u:units){if(u.conflict){expected.insert(u.key);need(actions.contains(u.key)&&actions[u.key].is_string()&&(actions[u.key]=="replace"||actions[u.key]=="skip"),"尚有冲突未选择");if(actions[u.key]=="skip"){++skipped;continue;}++replaced;}else ++added;selected.insert(u.key);}
  need(actions.size()==expected.size(),"还原选择含未知项目");
  if(selected.empty())return {{"added",0},{"replaced",0},{"skipped",skipped}};
  const int lock=openSafe(join(root,"restore.lock"),O_RDWR|O_CREAT);
  struct Unlock {int fd;~Unlock(){flock(fd,LOCK_UN);close(fd);}} unlock{lock};need(flock(lock,LOCK_EX|LOCK_NB)==0,"已有还原操作正在进行");
  need(!exists(join(root,"restore-journal.json")),"上次还原结果尚未核对");
  if(notify)notify({{"message","正在复核备份…"}});
  Manifest fresh(manifest->root,cancel);need(fresh.identity==manifest->identity&&hashFile(join(stage,"source.db"),cancel)==sourceHash,"备份在确认后发生变化");
  Sql source(join(stage,"source.db"));space(root,bytes(join(data,"penchat.db"))*2);
  Json journal={{"format",1},{"appid",PENCHAT_APP_ID},{"operation",op},{"files",Json::array()},{"assets",Json::array()},{"counts",{{"added",added},{"replaced",replaced},{"skipped",skipped}}}};
  const auto journalPath=join(root,"restore-journal.json");dir(join(stage,"files"));
  auto saveJournal=[&]{durable(journalPath,journal);};
  for(const auto &u:units)if(selected.count(u.key)&&(u.kind=="ime"||u.kind=="public")){
    const auto &entry=fresh.files.at(u.id);const auto target=u.kind=="ime"?join(data,"ime/user.json"):join(publicRoot,u.target);targetSafe(target);need(exists(target)==u.conflict,"文件冲突状态发生变化，请重新核对");
    const auto newPath=join(join(stage,"files"),shaText(u.key+"new"));fresh.copy(u.id,newPath,cancel);
    Json f={{"target",target},{"new_path",newPath},{"new_hash",entry.hash},{"new_size",entry.size},{"had_old",u.conflict}};
    if(u.conflict){const auto hash=hashFile(target,cancel),old=join(join(stage,"files"),shaText(u.key+"old"));const auto size=bytes(target);copyFile(target,old,hash,size,cancel);f["old_path"]=old;f["old_hash"]=hash;f["old_size"]=size;}journal["files"].push_back(f);
  }
  saveJournal();
  std::map<std::string,std::string> assetIds,paths;std::set<std::string> chats;
  for(const auto &u:units)if(selected.count(u.key)&&u.kind=="chat")chats.insert(u.id);
  auto sourceFile=[&](const std::string &path){return manifestReference(fresh,path);};
  auto mapFile=[&](const std::string &old)->std::string{
    if(old.empty())return old;
    const auto found=paths.find(old);if(found!=paths.end())return found->second;const auto rel=sourceFile(old);const auto &f=fresh.files.at(rel);
    std::string ext="img";const auto dot=old.find_last_of('.');if(dot!=std::string::npos){auto suffix=old.substr(dot+1);if(!suffix.empty()&&suffix.size()<=8&&suffix.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")==std::string::npos)ext=suffix;}
    const auto target=join(join(data,"assets"),uid("asset")+"."+ext);journal["assets"].push_back({{"path",target},{"hash",f.hash}});fresh.copy(rel,target,cancel);paths[old]=target;return target;
  };
  std::function<void(Json&,unsigned)> rewrite=[&](Json &value,unsigned depth){
    need(depth<=64,"图片或配置引用层次过深");
    if(value.is_array()){for(auto &v:value)rewrite(v,depth+1);return;}if(!value.is_object())return;
    if(value.contains("role")&&value.value("status",Json())=="streaming")value["status"]="interrupted";
    for(auto i=value.begin();i!=value.end();++i){const auto &key=i.key();
      if(privatePayload(key))continue;
      if(i.value().is_string()){
        const std::string s=i.value();if(key=="asset_id"&&assetIds.count(s))i.value()=assetIds.at(s);
        else if((key=="model_id"||key=="model_config_id"||key=="default_model_config_id"||key=="compression_model_id"||key=="title_model_id")&&models.count(s))i.value()=models.at(s);
        else if((key=="path"||key=="previewPath"||key=="stored_path"||key=="display_src"||key=="local_path")&&!s.empty()&&s.compare(0,7,"http://")!=0&&s.compare(0,8,"https://")!=0)i.value()=mapFile(s);
        else if((key=="metadata_json"||key=="json"||key=="baseline_json"||key=="payload_json")&&!s.empty()){auto decoded=parse(s);rewrite(decoded,depth+1);i.value()=decoded.dump();}
      }else rewrite(i.value(),depth+1);
    }
  };
  bool committed=false;
  try{
    if(notify)notify({{"message","正在还原数据…"}});
    auto result=db.restoreTransaction([&]{
      need(liveFingerprint(db)==currentHash,"现有数据在确认期间发生变化，请重新核对");
      for(const auto &u:units)if(selected.count(u.key)&&(u.kind=="provider"||u.kind=="model")){
        const auto table=u.kind=="provider"?"providers":"model_configs";auto row=source.q(std::string("SELECT * FROM ")+table+" WHERE id=?",{u.id}).at(0);
        const auto old=db.query(std::string("SELECT is_default FROM ")+table+" WHERE id=?",{u.target});row["is_default"]=old.empty()?Json(0):old[0]["is_default"];upsert(db,table,row,"id",u.target);
      }
      // Retained messages can reference an image owned by a replaced chat.
      // Move that old record to a retained owner instead of deleting shared data.
      std::map<std::string,std::pair<std::string,std::string>> retainedOwners;
      db.restoreRead([&](sqlite3 *connection){
        Sql live(connection);
        auto retain=[&](Json r,const std::string &sid,const std::string &mid){
          if(chats.count(sid))return;
          assetReferences(r,[&](const std::string &id){retainedOwners.emplace(id,std::make_pair(sid,mid));});
        };
        live.each("SELECT * FROM chat_sessions",{},[&](Json r){const std::string sid=r.at("id");retain(std::move(r),sid,"");});
        live.each("SELECT * FROM chat_messages",{},[&](Json r){const std::string sid=r.at("session_id"),mid=r.at("id");retain(std::move(r),sid,mid);});
        live.each("SELECT r.*,g.session_id owner_session,g.message_id owner_message FROM reply_results r JOIN reply_groups g ON g.id=r.group_id",{},[&](Json r){const std::string sid=r.at("owner_session"),mid=r.at("owner_message");retain(std::move(r),sid,mid);});
        live.each("SELECT * FROM reply_groups",{},[&](Json r){const std::string sid=r.at("session_id"),mid=r.at("message_id");retain(std::move(r),sid,mid);});
        return Json();
      });
      for(const auto &sid:chats){
        db.execute("DELETE FROM reply_groups WHERE session_id=?",{sid});db.execute("DELETE FROM message_parts WHERE session_id=?",{sid});db.execute("DELETE FROM stream_events WHERE session_id=?",{sid});
        for(const auto &a:db.query("SELECT id,metadata_json FROM message_assets WHERE session_id=? OR message_id IN (SELECT id FROM chat_messages WHERE session_id=?)",{sid,sid})){
          const auto used=db.query("SELECT 1 FROM message_parts WHERE asset_id=? UNION ALL SELECT 1 FROM reply_asset_refs WHERE asset_id=? LIMIT 1",{a["id"],a["id"]});
          const std::string id=a.at("id");
          if(retainedOwners.count(id)){const auto owner=retainedOwners.at(id);auto meta=parse(a.at("metadata_json"));meta.erase("_gc_detached");db.execute("UPDATE message_assets SET session_id=?,message_id=?,metadata_json=? WHERE id=?",{owner.first,owner.second.empty()?Json():Json(owner.second),meta.dump(),id});}
          else if(used.empty())db.execute("DELETE FROM message_assets WHERE id=?",{id});
          else{auto meta=parse(a["metadata_json"]);meta["_gc_detached"]=true;db.execute("UPDATE message_assets SET session_id=NULL,message_id=NULL,metadata_json=? WHERE id=?",{meta.dump(),id});}
        }
        db.execute("DELETE FROM chat_messages WHERE session_id=?",{sid});db.execute("DELETE FROM draft_attachments WHERE draft_key=?",{sid});db.execute("DELETE FROM chat_drafts WHERE draft_key=?",{sid});db.execute("DELETE FROM chat_sessions WHERE id=?",{sid});
      }
      std::set<std::string> ids;
      for(const auto &sid:chats)for(const auto &a:source.q("SELECT id FROM message_assets WHERE session_id=? OR message_id IN (SELECT id FROM chat_messages WHERE session_id=?) UNION SELECT asset_id id FROM message_parts WHERE session_id=? AND asset_id IS NOT NULL UNION SELECT asset_id id FROM reply_asset_refs WHERE group_id IN (SELECT id FROM reply_groups WHERE session_id=?)",{sid,sid,sid,sid}))ids.insert(a["id"]);
      std::set<std::string> allAssets;for(const auto &a:source.q("SELECT id FROM message_assets"))allAssets.insert(a.at("id"));
      std::map<std::string,std::pair<std::string,std::string>> metadataOwners;
      std::function<void(const Json&,const std::string&,const std::string&,unsigned)> collect=[&](const Json &value,const std::string &sid,const std::string &mid,unsigned depth){
        assetReferences(value,[&](const std::string &id){need(allAssets.count(id),"备份的图片关联缺失");ids.insert(id);metadataOwners.emplace(id,std::make_pair(sid,mid));},depth);
      };
      for(const auto &sid:chats){
        source.each("SELECT * FROM chat_sessions WHERE id=?",{sid},[&](Json r){collect(r,sid,"",0);});
        for(const auto &table:{"chat_messages","message_parts"})source.each(std::string("SELECT * FROM ")+table+" WHERE session_id=?",{sid},[&](Json r){const std::string mid=std::string(table)=="chat_messages"?r.at("id").get<std::string>():r.at("message_id").get<std::string>();collect(r,sid,mid,0);});
        source.each("SELECT * FROM reply_groups WHERE session_id=?",{sid},[&](Json r){collect(r,sid,r.at("message_id"),0);});
        source.each("SELECT r.*,g.message_id owner_message FROM reply_results r JOIN reply_groups g ON g.id=r.group_id WHERE g.session_id=?",{sid},[&](Json r){collect(r,sid,r.at("owner_message"),0);});
      }
      std::set<std::string> scanned;for(;;){auto it=std::find_if(ids.begin(),ids.end(),[&](const std::string &id){return !scanned.count(id);});if(it==ids.end())break;const auto id=*it;scanned.insert(id);const auto row=source.q("SELECT * FROM message_assets WHERE id=?",{id}).at(0);const auto owner=metadataOwners.count(id)?metadataOwners.at(id):std::make_pair(*chats.begin(),std::string());collect(row,owner.first,owner.second,0);}
      for(const auto &id:ids)assetIds[id]=uid("asset-restore");
      for(const auto &sid:chats){
        auto row=source.q("SELECT * FROM chat_sessions WHERE id=?",{sid}).at(0);auto meta=parse(row["metadata_json"]);meta["title_state"]="manual";for(const auto &key:{"title_token","title_model_id","title_assistant_id"})meta.erase(key);row["metadata_json"]=meta.dump();rewrite(row,0);insert(db,"chat_sessions",row);
        source.each("SELECT * FROM chat_messages WHERE session_id=? ORDER BY seq",{sid},[&](Json m){if(m["status"]=="streaming")m["status"]="interrupted";rewrite(m,0);insert(db,"chat_messages",m);});
      }
      for(const auto &id:ids){auto a=source.q("SELECT * FROM message_assets WHERE id=?",{id}).at(0);a["id"]=assetIds.at(id);if(a["session_id"].is_string()&&!chats.count(a["session_id"])) {if(metadataOwners.count(id)){a["session_id"]=metadataOwners.at(id).first;a["message_id"]=metadataOwners.at(id).second.empty()?Json():Json(metadataOwners.at(id).second);}else{a["session_id"]=nullptr;a["message_id"]=nullptr;}}if(a["session_id"].is_null()&&metadataOwners.count(id)){a["session_id"]=metadataOwners.at(id).first;a["message_id"]=metadataOwners.at(id).second.empty()?Json():Json(metadataOwners.at(id).second);}if(a["session_id"].is_null()&&a["message_id"].is_null()){auto meta=parse(a["metadata_json"]);meta["_gc_detached"]=true;a["metadata_json"]=meta.dump();}rewrite(a,0);insert(db,"message_assets",a);}
      for(const auto &sid:chats){
        for(const auto &table:{"message_parts","stream_events"})source.each(std::string("SELECT * FROM ")+table+" WHERE session_id=?",{sid},[&](Json row){if(std::string(table)=="message_parts")rewrite(row,0);insert(db,table,row);});
        source.each("SELECT * FROM reply_groups WHERE session_id=?",{sid},[&](Json g){rewrite(g,0);insert(db,"reply_groups",g);});
        source.each("SELECT r.* FROM reply_results r JOIN reply_groups g ON g.id=r.group_id WHERE g.session_id=?",{sid},[&](Json r){rewrite(r,0);insert(db,"reply_results",r);});
        source.each("SELECT r.* FROM reply_asset_refs r JOIN reply_groups g ON g.id=r.group_id WHERE g.session_id=?",{sid},[&](Json r){r["asset_id"]=assetIds.at(r.at("asset_id"));insert(db,"reply_asset_refs",r);});
      }
      testFault("db_error");
      auto restoreDraft=[&](const std::string &key){
        const auto rows=source.q("SELECT * FROM chat_drafts WHERE draft_key=?",{key});if(rows.empty())return;auto row=rows[0];const auto old=db.query("SELECT epoch FROM chat_drafts WHERE draft_key=?",{key});const auto epoch=old.empty()?int64_t(0):old[0]["epoch"].get<int64_t>();need(epoch<INT64_MAX-1,"草稿版本超出范围");const auto sourceEpoch=row["epoch"].get<int64_t>();need(sourceEpoch<INT64_MAX-1,"备份草稿版本超出范围");row["epoch"]=std::max(epoch,sourceEpoch)+1;
        db.execute("DELETE FROM draft_attachments WHERE draft_key=?",{key});db.execute("DELETE FROM chat_drafts WHERE draft_key=?",{key});insert(db,"chat_drafts",row);
        source.each("SELECT * FROM draft_attachments WHERE draft_key=? ORDER BY position",{key},[&](Json a){auto image=parse(a["image_json"]);const auto folder=join(join(data,"assets-drafts"),uid("image"));
          for(const auto &field:{"path","previewPath"})if(image.value(field,Json()).is_string()){
            const std::string oldPath=image[field];const auto rel=sourceFile(oldPath);const auto &f=fresh.files.at(rel);const auto target=join(folder,std::string(field)+".img");journal["assets"].push_back({{"path",target},{"hash",f.hash}});fresh.copy(rel,target,cancel);image[field]=target;
          }
          a["id"]=uid("draft-image");image.erase("id");a["image_json"]=image.dump();insert(db,"draft_attachments",a);
        });
      };
      for(const auto &sid:chats)restoreDraft(sid);
      Json presets=presetList(db.settings());bool changedPresets=false;
      for(const auto &u:units)if(selected.count(u.key)){
        if(u.kind=="draft")restoreDraft("new");
        else if(u.kind=="setting"){auto row=source.q("SELECT * FROM app_settings WHERE key=?",{u.id}).at(0);if(u.id=="title_model_id"){auto id=parse(row["value_json"]);if(id.is_string()&&models.count(id))row["value_json"]=Json(models.at(id)).dump();}upsert(db,"app_settings",row,"key",u.id);}
        else if(u.kind=="preset"){auto p=u.extra;rewrite(p,0);bool found=false;for(auto &old:presets)if(old["id"]==u.id){old=p;found=true;break;}if(!found)presets.push_back(p);changedPresets=true;}
        else if(u.kind=="default"){const auto table=u.id;std::string id=u.extra.at("id");if(table=="model_configs")id=models.at(id);need(!db.query("SELECT id FROM "+table+" WHERE id=?",{id}).empty(),"默认配置目标缺失");db.execute("UPDATE "+table+" SET is_default=0");db.execute("UPDATE "+table+" SET is_default=1 WHERE id=?",{id});}
      }
      if(changedPresets)db.setting("chat_presets",presets);
      // Private images are immutable new files: an earlier crash leaves only
      // collectible orphans. Publish their complete ledger once, before any
      // public/IME replacement or SQLite COMMIT, rather than fsync per image.
      saveJournal();
      for(const auto &f:journal["files"]){const std::string target=f["target"];targetSafe(target);if(f.value("had_old",false))need(exists(target)&&hashFile(target,cancel)==f.at("old_hash"),"当前文件已变化，已取消还原");else need(!exists(target),"出现了新的文件冲突，已取消还原");replaceFile(f.at("new_path"),target,f.at("new_hash"),f.at("new_size"),op,cancel);testFault("file_error");}
      db.setting("_restore_operation",op);
      testFault("before_commit");
      return Json{{"added",added},{"replaced",replaced},{"skipped",skipped}};
    });
    committed=true;testFault("after_commit");fileVerify(journal,cancel);durable(join(root,"restore-receipt.json"),{{"operation",op},{"ok",true},{"result",result}});need(unlink(journalPath.c_str())==0,"还原完成但事务记录无法清理");syncDir(root);return result;
  }catch(...){
    if(!committed){const auto marker=db.query("SELECT value_json FROM app_settings WHERE key='_restore_operation'");if(!marker.empty()&&parse(marker[0]["value_json"])==op)throw;std::atomic<bool> neverCancel{false};fileRollback(journal,data,publicRoot,neverCancel);unlink(journalPath.c_str());syncDir(root);}
    throw;
  }
}
BackupRestore::BackupRestore(Database &db,Events &events,std::string data,std::string publicRoot)
  :p_(new Impl(db,events,std::move(data),std::move(publicRoot))){}
BackupRestore::~BackupRestore()=default;
bool BackupRestore::start(const std::string &kind,Json args){return p_->start(kind,std::move(args));}
void BackupRestore::stop(bool keepPlan){p_->stop(keepPlan);}
void BackupRestore::cancel(){if(p_->active)p_->cancel=true;else p_->start("discard",Json::object());}
bool BackupRestore::busy()const{return p_->active;}
uint64_t BackupRestore::token()const{return p_->generation;}
Json BackupRestore::commit(const Json &decisions,const std::function<void(const Json&)> &progress){
  try{return p_->commit(decisions,progress);}catch(...){if(!p_->op.empty()&&!exists(join(p_->root,"restore-journal.json")))try{durable(join(p_->root,"restore-receipt.json"),{{"operation",p_->op},{"ok",false},{"result",Json::object()}});}catch(...){}throw;}
}
Json BackupRestore::lastResult(const std::string &data,const std::string &operation){
  const auto path=join(join(data,".rescue"),"restore-receipt.json");if(!exists(path))return Json::object();
  try{const auto r=parse(readFile(path,8192));if(!r.is_object()||r.value("operation",std::string())!=operation||!r.value("ok",Json()).is_boolean())return Json::object();const auto result=r.value("result",Json::object());if(!result.is_object())return Json::object();for(auto i=result.begin();i!=result.end();++i)if((i.key()!="added"&&i.key()!="replaced"&&i.key()!="skipped")||!i.value().is_number_integer()||i.value().get<int64_t>()<0||i.value().get<int64_t>()>10000)return Json::object();return {{"ok",r.at("ok")},{"result",result},{"recovered",r.value("recovered",false)}};}catch(...){return Json::object();}
}
std::string BackupRestore::publicDirectory(const std::string &data){
#ifdef PENCHAT_NATIVE_DEVICE
  (void)data;return "/userdisk/Favorite/PenChat";
#else
  if(const auto p=getenv("PENCHAT_TRANSFER_ROOT"))return p;
  return join(data,"transfer-files");
#endif
}
void BackupRestore::recover(const std::string &data,const std::string &publicRoot){
  const auto root=join(data,".rescue"),path=join(root,"restore-journal.json");if(!exists(path))return;safe(root);
  const int lock=openSafe(join(root,"restore.lock"),O_RDWR|O_CREAT);struct Unlock{int fd;~Unlock(){flock(fd,LOCK_UN);close(fd);}} unlock{lock};need(flock(lock,LOCK_EX|LOCK_NB)==0,"另一个还原操作尚未结束");
  const auto journal=journalRead(data,publicRoot);Sql current(join(data,"penchat.db"),true);const auto rows=current.q("SELECT value_json FROM app_settings WHERE key='_restore_operation'");
  const bool committed=!rows.empty()&&parse(rows[0]["value_json"])==journal.at("operation");std::atomic<bool> neverCancel{false};
  if(committed)fileVerify(journal,neverCancel);else fileRollback(journal,data,publicRoot,neverCancel);
  durable(join(root,"restore-receipt.json"),{{"operation",journal.at("operation")},{"ok",committed},{"recovered",true},{"result",journal.value("counts",Json::object())},{"message",committed?"上次还原已完成":"上次还原未提交，已恢复原状"}});
  need(unlink(path.c_str())==0,"无法清理还原事务记录");syncDir(root);
  eraseTree(join(join(root,"restore-stage"),journal.at("operation")));
}
} // namespace penchat
