#pragma once
#include <algorithm>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace penchat { namespace latex { namespace syntax {
constexpr size_t Limit=65536;
inline void bounded(const std::string &s){if(s.size()>Limit)throw std::runtime_error("LaTeX expansion exceeds limit");}
inline void space(const std::string &s,size_t &p){while(p<s.size()&&std::isspace(static_cast<unsigned char>(s[p])))++p;}
inline bool has(const std::string &s,size_t p,char open='{'){space(s,p);return p<s.size()&&s[p]==open;}
inline std::string command(const std::string &s,size_t &p){++p;const auto at=p;while(p<s.size()&&std::isalpha(static_cast<unsigned char>(s[p])))++p;if(p==at&&p<s.size())++p;return s.substr(at,p-at);}
inline std::string group(const std::string &s,size_t &p,char open='{',char close='}'){
  space(s,p);if(p>=s.size()||s[p]!=open)throw std::runtime_error("Missing LaTeX argument");const auto at=++p;unsigned depth=1,braces=0;
  while(p<s.size()){
    if(s[p]=='\\'){p+=std::min(size_t(2),s.size()-p);continue;}
    // Braces protect a closing optional-argument bracket.
    if(open!='{'){if(s[p]=='{'){if(++braces>32)throw std::runtime_error("LaTeX nesting exceeds limit");}else if(s[p]=='}'){if(!braces)throw std::runtime_error("Unbalanced argument");--braces;}}
    if(!braces&&s[p]==open&&++depth>32)throw std::runtime_error("LaTeX nesting exceeds limit");
    if(!braces&&s[p]==close&&!--depth){auto value=s.substr(at,p-at);++p;return value;}++p;
  }throw std::runtime_error("Unclosed LaTeX argument");
}
inline std::string atom(const std::string &s,size_t &p){space(s,p);if(p==s.size())throw std::runtime_error("Missing LaTeX atom");if(s[p]=='{')return group(s,p);const auto at=p;if(s[p]=='\\')command(s,p);else ++p;return s.substr(at,p-at);}
inline bool star(const std::string &s,size_t &p){if(p<s.size()&&s[p]=='*'){++p;return true;}return false;}
inline std::string trim(const std::string &s){const auto a=s.find_first_not_of(" \t\r\n");return a==std::string::npos?"":s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
inline std::vector<std::string> list(const std::string &s,char separator){std::vector<std::string> out;size_t at=0;unsigned depth=0;for(size_t p=0;p<s.size();++p){if(s[p]=='\\'){++p;continue;}if(s[p]=='{'||s[p]=='['){if(++depth>32)throw std::runtime_error("LaTeX nesting exceeds limit");}else if(s[p]=='}'||s[p]==']'){if(!depth)throw std::runtime_error("Unbalanced list");--depth;}else if(!depth&&s[p]==separator){out.push_back(trim(s.substr(at,p-at)));at=p+1;}}if(depth)throw std::runtime_error("Unclosed list");out.push_back(trim(s.substr(at)));return out;}
using Expand=std::function<std::string(const std::string &)>;
} } }
