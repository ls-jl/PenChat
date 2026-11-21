#pragma once
#include <cctype>
#include <string>
namespace penchat { namespace platform {
enum class Family { Unknown, Rockchip, Cvi, Ambiguous };
struct Identity {
  Family family=Family::Unknown;
  std::string soc;
  const char *packageTarget() const {
    return family==Family::Rockchip?"rk3562":family==Family::Cvi?"cv1826-s":"";
  }
  const char *name() const {
    return family==Family::Rockchip?"rockchip":family==Family::Cvi?"cvi":family==Family::Ambiguous?"ambiguous":"unknown";
  }
};
// Device-tree strings are NUL-separated. Match complete SoC tokens, not a model
// name, screen size, serial, kernel bitness or an arbitrary "cvi" substring.
inline Identity identify(const std::string &model,const std::string &compatible="") {
  std::string text=model+std::string(1,'\0')+compatible;
  for(auto &c:text)c=char(std::tolower(static_cast<unsigned char>(c)));
  Identity result;bool rk=false,cvi=false;
  for(size_t p=0;p<text.size();++p){
    if(p&&std::isalnum(static_cast<unsigned char>(text[p-1])))continue;
    if(text.compare(p,6,"rk3562")==0&&(p+6==text.size()||!std::isalnum(static_cast<unsigned char>(text[p+6]))))rk=true;
    // CV18xx is a hardware family hint only. Actual host ABI and the verified
    // Falcon profile must independently pass before an ARM32 UI can start.
    if(text.compare(p,4,"cv18")==0&&p+6<=text.size()){
      const auto a=text[p+4],b=text[p+5];const bool tail=(a>='0'&&a<='9'&&((b>='0'&&b<='9')||b=='x'))||(a=='x'&&b=='x');
      size_t end=p+6;
      if(a>='0'&&a<='9'&&b>='0'&&b<='9'&&end<text.size()&&text[end]>='a'&&text[end]<='z')++end;
      if(tail&&(end==text.size()||!std::isalnum(static_cast<unsigned char>(text[end])))){cvi=true;if(result.soc.empty())result.soc=text.substr(p,end-p);}
    }
  }
  result.family=rk&&cvi?Family::Ambiguous:rk?Family::Rockchip:cvi?Family::Cvi:Family::Unknown;
  if(result.family==Family::Rockchip)result.soc="rk3562";
  return result;
}
} }
