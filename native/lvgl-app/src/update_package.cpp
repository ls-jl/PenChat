#include "update.hpp"
#include <miniz.h>
#include <openssl/evp.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cctype>
#include <set>
#include <stdexcept>
namespace penchat { namespace updates {
bool Version::parse(const std::string &text,Version &v) {
  size_t pos=0;
  for(unsigned i=0;i<3;++i) {
    const auto start=pos;uint64_t number=0;
    while(pos<text.size()&&text[pos]>='0'&&text[pos]<='9') {
      number=number*10+unsigned(text[pos++]-'0');if(number>0x7fffffffu)return false;
    }
    if(pos==start||(pos-start>1&&text[start]=='0'))return false;
    v.parts[i]=uint32_t(number);
    if(i<2){if(pos>=text.size()||text[pos++]!='.')return false;}
  }
  return pos==text.size();
}
namespace {
int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
std::string decode(const std::string &s) {
  std::string out;
  for(size_t i=0;i<s.size();++i) {
    if(s[i]=='%'){if(i+2>=s.size()||hex(s[i+1])<0||hex(s[i+2])<0)return {};out+=char(hex(s[i+1])*16+hex(s[i+2]));i+=2;}
    else out+=s[i];
  }
  return out;
}
}
std::vector<Link> directoryLinks(const std::string &html,const std::string &base) {
  if(html.size()>1024*1024)throw std::runtime_error("更新目录过大");
  std::string lower=html;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return char(std::tolower(c));});
  if(lower.find("<html")==std::string::npos||lower.find("index of")==std::string::npos)
    throw std::runtime_error("更新源未返回目录列表");
  std::vector<Link> out;std::set<std::string> seen;size_t pos=0;
  while((pos=lower.find("href",pos))!=std::string::npos) {
    pos+=4;while(pos<html.size()&&std::isspace(static_cast<unsigned char>(html[pos])))++pos;
    if(pos>=html.size()||html[pos++]!='=')continue;
    while(pos<html.size()&&std::isspace(static_cast<unsigned char>(html[pos])))++pos;
    if(pos>=html.size())break;
    const char quote=html[pos];if(quote!='\''&&quote!='"')continue;
    const auto end=html.find(quote,++pos);if(end==std::string::npos)break;
    auto target=html.substr(pos,end-pos);pos=end+1;
    if(target.compare(0,base.size(),base)==0)target=target.substr(base.size());
    bool directory=!target.empty()&&target.back()=='/';if(directory)target.pop_back();
    target=decode(target);
    if(target.empty()||target.size()>255||target=="."||target==".."||
       target.find_first_of("/\\?#:\r\n")!=std::string::npos||target.find('\0')!=std::string::npos)continue;
    if(seen.insert(target+(directory?"/":"")).second)out.push_back({target,directory});
    if(out.size()>2048)throw std::runtime_error("更新目录条目过多");
  }
  return out;
}
std::string checksum(const std::string &text,const std::string &filename) {
  size_t start=0;while(start<text.size()&&std::isspace(static_cast<unsigned char>(text[start])))++start;
  if(text.size()-start<64)throw std::runtime_error("更新校验文件格式错误");
  std::string hash=text.substr(start,64);for(auto &c:hash){if(hex(c)<0)throw std::runtime_error("更新校验文件格式错误");c=char(std::tolower(static_cast<unsigned char>(c)));}
  auto tail=text.substr(start+64);while(!tail.empty()&&std::isspace(static_cast<unsigned char>(tail.back())))tail.pop_back();
  if(!tail.empty()) {
    if(!std::isspace(static_cast<unsigned char>(tail[0])))throw std::runtime_error("更新校验文件格式错误");
    size_t n=tail.find_first_not_of(" \t");tail=n==std::string::npos?"":tail.substr(n);
    if(!tail.empty()&&tail[0]=='*')tail.erase(0,1);
    if(tail!=filename)throw std::runtime_error("校验文件中的安装包名称不匹配");
  }
  return hash;
}
std::string fileHash(const std::string &path,std::atomic<bool> &cancel) {
  int fd=open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)throw std::runtime_error("无法读取更新包");
  auto context=EVP_MD_CTX_new();
  if(!context){close(fd);throw std::runtime_error("无法初始化更新校验");}
  std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(context,EVP_MD_CTX_free);
  EVP_DigestInit_ex(context,EVP_sha256(),nullptr);unsigned char data[65536],digest[32];unsigned length=0;
  try {
    for(;;){if(cancel)throw std::runtime_error("cancelled");auto n=read(fd,data,sizeof(data));if(n<0&&errno==EINTR)continue;if(n<0)throw std::runtime_error("更新包读取失败");if(!n)break;EVP_DigestUpdate(context,data,size_t(n));}
    EVP_DigestFinal_ex(context,digest,&length);close(fd);
  }catch(...){close(fd);throw;}
  std::string result;for(unsigned i=0;i<length;++i){result+="0123456789abcdef"[digest[i]>>4];result+="0123456789abcdef"[digest[i]&15];}return result;
}
Json validatePackage(const std::string &path,const std::string &appId,const std::string &version,const std::string &hash,std::atomic<bool> &cancel) {
  struct stat file{};if(lstat(path.c_str(),&file)||!S_ISREG(file.st_mode)||file.st_size<=0||uint64_t(file.st_size)>PackageLimit)throw std::runtime_error("更新包大小或文件类型异常");
  const auto actual=fileHash(path,cancel);if(!hash.empty()&&hash!=actual)throw std::runtime_error("更新包 SHA-256 不匹配");
  mz_zip_archive zip{};if(!mz_zip_reader_init_file(&zip,path.c_str(),0))throw std::runtime_error("无效的 AMR 安装包");
  struct Close {mz_zip_archive *zip;~Close(){mz_zip_reader_end(zip);}} close{&zip};
  const auto count=mz_zip_reader_get_num_files(&zip);if(count==0||count>20000)throw std::runtime_error("安装包条目数量异常");
  uint64_t total=0;std::set<std::string> names;Json manifest;
  for(mz_uint i=0;i<count;++i) {
    if(cancel)throw std::runtime_error("cancelled");
    mz_zip_archive_file_stat item{};if(!mz_zip_reader_file_stat(&zip,i,&item))throw std::runtime_error("安装包目录损坏");
    if(mz_zip_reader_get_filename(&zip,i,nullptr,0)>4096)throw std::runtime_error("安装包路径过长");
    char name[4096]{};mz_zip_reader_get_filename(&zip,i,name,sizeof(name));std::string value=name;
    if(value.size()+1!=mz_zip_reader_get_filename(&zip,i,nullptr,0)||value.empty()||value.front()=='/'||value.find('\\')!=std::string::npos||value.find(':')!=std::string::npos||!names.insert(value).second||
       (item.m_external_attr>>16&0170000)==0120000)throw std::runtime_error("安装包路径不安全");
    size_t at=0;while(at<value.size()){const auto end=value.find('/',at);const auto part=value.substr(at,end-at);if(part=="."||part=="..")throw std::runtime_error("安装包路径不安全");if(end==std::string::npos)break;at=end+1;}
    if(item.m_uncomp_size>ExpandedLimit-total)throw std::runtime_error("安装包解压大小超限");
    total+=item.m_uncomp_size;
    if(mz_zip_reader_is_file_a_directory(&zip,i))continue;
    if(!mz_zip_reader_is_file_supported(&zip,i))throw std::runtime_error("安装包压缩格式不支持");
    struct Sink {std::atomic<bool> *cancel;std::string text;bool manifest;};Sink sink{&cancel,{},value=="manifest.json"};
    auto callback=[](void *opaque,mz_uint64,const void *buffer,size_t n)->size_t {
      auto s=static_cast<Sink *>(opaque);if(*s->cancel)return 0;
      if(s->manifest){if(s->text.size()+n>65536)return 0;s->text.append(static_cast<const char *>(buffer),n);}return n;
    };
    if(!mz_zip_reader_extract_to_callback(&zip,i,callback,&sink,0))throw std::runtime_error(cancel?"cancelled":"安装包 CRC 校验失败");
    if(sink.manifest)manifest=Json::parse(sink.text,nullptr,false);
  }
  if(!names.count("app.js.bin"))throw std::runtime_error("安装包缺少应用入口");
  if(!manifest.is_object()||manifest.value("appid",Json())!=appId||manifest.value("version",Json())!=version)
    throw std::runtime_error("安装包 AppID 或版本与发布目录不一致");
  struct statvfs space{};if(statvfs(path.c_str(),&space)||uint64_t(space.f_bavail)*space.f_frsize<total+16*1024*1024)
    throw std::runtime_error("安装剩余空间不足");
  return {{"sha256",actual},{"bytes",file.st_size},{"expanded",total},{"appid",appId},{"version",version}};
}
}}
