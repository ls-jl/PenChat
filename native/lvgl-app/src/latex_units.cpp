#include "latex_units.hpp"
#include <map>
#include <set>
namespace penchat { namespace latex {
namespace {
using namespace syntax;
void options(const std::string &s,UnitOptions &o){
  for(const auto &entry:list(s,',')){
    const auto eq=entry.find('=');if(eq==std::string::npos)throw std::runtime_error("Unit option requires value");auto key=trim(entry.substr(0,eq)),value=trim(entry.substr(eq+1));
    if(has(value,0)){size_t p=0;value=group(value,p);if(p!=entry.substr(eq+1).size()&&trim(entry.substr(eq+1))!="{"+value+"}")throw std::runtime_error("Invalid unit option");}
    auto boolean=[&](){if(value!="true"&&value!="false")throw std::runtime_error("Invalid boolean unit option");return value=="true";};
    if(key=="output-decimal-marker"){if(value!="."&&value!=",")throw std::runtime_error("Invalid decimal marker");o.decimal=value;}
    else if(key=="group-digits"){if(value!="true"&&value!="false"&&value!="integer"&&value!="decimal")throw std::runtime_error("Invalid digit grouping");o.group=value;}
    else if(key=="group-separator"){if(value!="\\,"&&value!="\\thinspace"&&value!=" "&&value!=",")throw std::runtime_error("Invalid group separator");o.separator=value;}
    else if(key=="per-mode"){if(value!="reciprocal"&&value!="symbol"&&value!="fraction")throw std::runtime_error("Invalid per mode");o.per=value;}
    else if(key=="inter-unit-product"){if(value!="\\,"&&value!="\\cdot"&&value!="\\cdot "&&value!=" ")throw std::runtime_error("Invalid unit product");o.product=value+" ";}
    else if(key=="separate-uncertainty")o.separate=boolean();
    else if(key=="range-units"||key=="list-units"){if(value!="repeat"&&value!="single")throw std::runtime_error("Invalid unit repetition");o.repeat=value=="repeat";}
    else if(key=="round-mode"){if(value!="none"&&value!="places"&&value!="figures")throw std::runtime_error("Invalid rounding mode");o.round=value;}
    else if(key=="round-half"){if(value!="up"&&value!="even")throw std::runtime_error("Invalid rounding tie rule");o.half=value;}
    else if(key=="round-precision"){if(value.empty()||value.size()>3||value.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid rounding precision");o.precision=std::stoi(value);if(o.precision>100)throw std::runtime_error("Rounding precision exceeds limit");}
    else if(key=="range-phrase"){if(value.size()>32||value.find_first_of("\\{}$")!=std::string::npos)throw std::runtime_error("Invalid range phrase");o.range=value;}
    else throw std::runtime_error("Unsupported unit option: "+key);
  }
  if(o.round=="figures"&&o.precision==0)throw std::runtime_error("Zero significant figures");
}
struct Decimal {std::string digits,sign;int scale=0;};
Decimal decimal(const std::string &s){
  Decimal n;size_t p=0;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))n.sign=s.substr(p++,1);bool dot=false;
  for(;p<s.size();++p){const char c=s[p];if(c=='.'||c==','){if(dot)throw std::runtime_error("Multiple decimal markers");dot=true;}else if(c>='0'&&c<='9'){n.digits+=c;if(dot)++n.scale;}else throw std::runtime_error("Invalid decimal number");}
  if(n.digits.empty()||n.digits.size()>4096)throw std::runtime_error("Invalid decimal length");return n;
}
void round(Decimal &n,const UnitOptions &o){
  if(o.round=="none")return;int places=o.precision;
  if(o.round=="figures"){const auto first=n.digits.find_first_not_of('0');if(first==std::string::npos){places=std::max(0,o.precision-1);}else places=o.precision-int(n.digits.size()-first)+n.scale;}
  const int remove=n.scale-places;
  if(remove>0){
    if(remove>int(n.digits.size()))n.digits="0";
    else{size_t keep=n.digits.size()-remove;const char next=n.digits[keep];bool above=n.digits.find_first_not_of('0',keep+1)!=std::string::npos;const bool up=next>'5'||(next=='5'&&(above||o.half=="up"||(keep&&((n.digits[keep-1]-'0')%2))));n.digits.resize(keep);
      if(n.digits.empty())n.digits="0";if(up){size_t i=n.digits.size();while(i&&n.digits[i-1]=='9')n.digits[--i]='0';if(i)++n.digits[i-1];else n.digits.insert(0,"1");}}
    n.scale=places;
  }else if(remove<0){if(-remove>4096)throw std::runtime_error("Decimal expansion exceeds limit");n.digits.append(-remove,'0');n.scale=places;}
  if(o.round=="figures"){
    auto first=n.digits.find_first_not_of('0');if(first!=std::string::npos)while(int(n.digits.size()-first)>o.precision&&n.digits.back()=='0'){n.digits.pop_back();--n.scale;}
  }
  if(n.scale<0){n.digits.append(-n.scale,'0');n.scale=0;}
}
std::string print(Decimal n,const UnitOptions &o){
  if(n.scale>=int(n.digits.size()))n.digits.insert(0,n.scale-int(n.digits.size())+1,'0');
  const size_t whole=n.digits.size()-n.scale;std::string out=n.sign;
  for(size_t i=0;i<n.digits.size();++i){if(i==whole)out+=o.decimal==","?"{,}":".";
    if(i&&i<whole&&(o.group=="true"||o.group=="integer")&&(whole-i)%3==0)out+=o.separator;
    if(i>whole&&(o.group=="true"||o.group=="decimal")&&(i-whole)%3==0)out+=o.separator;
    out+=n.digits[i];}
  return out;
}
std::string number(std::string s,const UnitOptions &o){
  s=trim(s);std::string exponent,uncertainty;bool explicitUncertainty=false;
  auto e=s.find_last_of("eE");if(e!=std::string::npos){exponent=s.substr(e+1);s.resize(e);if(exponent.empty()||exponent.size()>8)throw std::runtime_error("Invalid exponent");size_t p=(exponent[0]=='+'||exponent[0]=='-')?1:0;if(p==exponent.size()||exponent.find_first_not_of("0123456789",p)!=std::string::npos)throw std::runtime_error("Invalid exponent");}
  size_t pm=s.find("\\pm");size_t length=3;if(pm==std::string::npos){pm=s.find("+-");length=2;}
  if(pm!=std::string::npos){uncertainty=trim(s.substr(pm+length));s=trim(s.substr(0,pm));explicitUncertainty=true;}
  else{auto open=s.find('(');if(open!=std::string::npos){if(s.back()!=')')throw std::runtime_error("Unclosed uncertainty");uncertainty=s.substr(open+1,s.size()-open-2);s.resize(open);if(uncertainty.empty()||uncertainty.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid compact uncertainty");}}
  auto value=decimal(s);const int originalScale=value.scale;round(value,o);auto result=print(value,o);
  if(!uncertainty.empty()){
    auto u=decimal(uncertainty);if(u.sign=="-")throw std::runtime_error("Negative uncertainty");if(!explicitUncertainty)u.scale=originalScale;
    if(o.round!="none"){auto config=o;config.round="places";config.precision=value.scale;round(u,config);}
    if(explicitUncertainty||o.separate)result+="\\pm "+print(u,o);
    else {std::string digits=u.digits;const auto first=digits.find_first_not_of('0');digits=first==std::string::npos?"0":digits.substr(first);result+="("+digits+")";}
  }
  if(!exponent.empty()){if(!uncertainty.empty()&&(explicitUncertainty||o.separate))result="\\left("+result+"\\right)";result+="\\times 10^{"+exponent+"}";}
  bounded(result);return result;
}
static const std::map<std::string,std::string> names={
    {"metre","m"},{"meter","m"},{"second","s"},{"gram","g"},{"kilogram","kg"},{"ampere","A"},{"kelvin","K"},{"mole","mol"},{"candela","cd"},
    {"radian","rad"},{"steradian","sr"},{"hertz","Hz"},{"newton","N"},{"pascal","Pa"},{"joule","J"},{"watt","W"},{"coulomb","C"},{"volt","V"},{"farad","F"},{"siemens","S"},{"weber","Wb"},{"tesla","T"},{"henry","H"},{"lumen","lm"},{"lux","lx"},{"becquerel","Bq"},{"gray","Gy"},{"sievert","Sv"},{"katal","kat"},
    {"litre","L"},{"liter","L"},{"minute","min"},{"hour","h"},{"day","d"},{"electronvolt","eV"},{"percent","\\%"},{"degree","{}^{\\circ}"},{"degreeCelsius","{}^{\\circ}C"},{"ohm","\\Omega"},
    {"quecto","q"},{"ronto","r"},{"yocto","y"},{"zepto","z"},{"atto","a"},{"femto","f"},{"pico","p"},{"nano","n"},{"micro","\\mu"},{"milli","m"},{"centi","c"},{"deci","d"},{"deca","da"},{"deka","da"},{"hecto","h"},{"kilo","k"},{"mega","M"},{"giga","G"},{"tera","T"},{"peta","P"},{"exa","E"},{"zetta","Z"},{"yotta","Y"},{"ronna","R"},{"quetta","Q"}};
struct Unit {std::string symbol;int power=1;std::string join;};
std::string units(const std::string &s,const UnitOptions &o){
  static const std::set<std::string> prefixes={"quecto","ronto","yocto","zepto","atto","femto","pico","nano","micro","milli","centi","deci","deca","deka","hecto","kilo","mega","giga","tera","peta","exa","zetta","yotta","ronna","quetta"};
  std::vector<Unit> atoms;std::string prefix,join;bool per=false,forceFraction=false;size_t p=0;
  auto integer=[&](const std::string &x){auto text=trim(x);size_t used=0;const auto n=std::stoll(text,&used);if(used!=text.size()||n < -1024||n > 1024)throw std::runtime_error("Invalid unit exponent");return int(n);};
  auto power=[&](int n){if(atoms.empty())throw std::runtime_error("Missing powered unit");atoms.back().power=n*(per?-1:1);};
  while(p<s.size()){
    const char c=s[p];if(std::isspace(static_cast<unsigned char>(c))){++p;continue;}if(c=='*'||c=='.'){join="\\cdot ";++p;continue;}
    if(c=='/'){++p;per=true;if(p<s.size()&&s[p]=='/'){++p;forceFraction=true;}continue;}
    if(c=='^'||c=='-'||c=='+'){
      if(c=='^')++p;std::string exp;if(has(s,p))exp=group(s,p);else{const auto at=p;if(p<s.size()&&(s[p]=='-'||s[p]=='+'))++p;while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;exp=s.substr(at,p-at);}power(integer(exp));continue;
    }
    if(std::isdigit(static_cast<unsigned char>(c))){const auto at=p;while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;power(integer(s.substr(at,p-at)));continue;}
    std::string symbol;
    if(c=='\\'){
      auto name=command(s,p);if(name=="per"){per=true;continue;}if(name==","||name==" ")continue;
      if(name=="squared"||name=="cubed"){power(name=="squared"?2:3);continue;}
      if(name=="tothe"||name=="raiseto"){power(integer(group(s,p)));continue;}
      auto found=names.find(name);if(found==names.end())throw std::runtime_error("Unsupported unit command");symbol=found->second;
      if(prefixes.count(name)){if(!prefix.empty())throw std::runtime_error("Multiple unit prefixes");prefix=symbol+" ";continue;}
    }else if(std::isalpha(static_cast<unsigned char>(c))){const auto at=p++;while(p<s.size()&&std::isalpha(static_cast<unsigned char>(s[p])))++p;symbol=s.substr(at,p-at);}
    else if(c=='('||c==')')throw std::runtime_error("Grouped units require explicit commands");
    else throw std::runtime_error("Unsupported unit syntax");
    atoms.push_back({prefix+symbol,per?-1:1,join});prefix.clear();join.clear();if(atoms.size()>8192)throw std::runtime_error("Too many units");
  }
  if(!prefix.empty()||atoms.empty())throw std::runtime_error("Incomplete unit");
  auto render=[&](bool denominator,bool absolute){std::string text;for(const auto &atom:atoms){if(atom.power==0)continue;if(absolute&&(atom.power<0)!=denominator)continue;if(!text.empty())text+=atom.join.empty()?o.product:atom.join;auto n=absolute?std::abs(atom.power):atom.power;text+="\\mathrm{"+atom.symbol+"}";if(n!=1)text+="^{"+std::to_string(n)+"}";bounded(text);}return text;};
  if(forceFraction||o.per=="fraction"||o.per=="symbol"){
    auto top=render(false,true),bottom=render(true,true);if(top.empty())top="1";if(bottom.empty())return top;
    if(forceFraction||o.per=="fraction")return "\\frac{"+top+"}{"+bottom+"}";
    int count=0;for(const auto &a:atoms)count+=a.power<0;return top+"/"+(count>1?"\\left("+bottom+"\\right)":bottom);
  }return render(false,false);
}
}
bool unitCommand(const std::string &name,const std::string &s,size_t &p,std::string &out,UnitOptions &state){
  static const std::set<std::string> commands={"sisetup","num","si","unit","SI","qty","pu","numlist","numrange","qtylist","qtyrange","SIlist","SIrange","ang"};
  if(!commands.count(name))return false;
  // qty with physical delimiters belongs to physics, not the SI grammar.
  if(name=="qty"){
    size_t q=p;if(has(s,q,'['))group(s,q,'[',']');if(!has(s,q))return false;group(s,q);if(!has(s,q))return false;
  }
  if(name=="sisetup"){options(group(s,p),state);return true;}
  auto config=state;if(name!="pu"&&has(s,p,'['))options(group(s,p,'[',']'),config);
  if(name=="num")out+=number(group(s,p),config);
  else if(name=="si"||name=="unit")out+=units(group(s,p),config);
  else if(name=="SI"||name=="qty"){auto n=group(s,p),u=group(s,p);out+=number(n,config)+"\\,"+units(u,config);}
  else if(name=="pu"){
    auto body=trim(group(s,p));size_t q=0;if(q<body.size()&&(body[q]=='+'||body[q]=='-'))++q;
    while(q<body.size()&&(std::isdigit(static_cast<unsigned char>(body[q]))||body[q]=='.'||body[q]==','))++q;
    if(q<body.size()&&body[q]=='('){group(body,q,'(',')');}
    if(q<body.size()&&(body[q]=='e'||body[q]=='E')){++q;if(q<body.size()&&(body[q]=='+'||body[q]=='-'))++q;while(q<body.size()&&std::isdigit(static_cast<unsigned char>(body[q])))++q;}
    auto u=trim(body.substr(q));out+=number(body.substr(0,q),config)+(u.empty()?"":"\\,"+units(u,config));
  }else if(name=="ang"){
    auto values=list(group(s,p),';');if(values.size()>3)throw std::runtime_error("Too many angle components");const char *symbols[]={"{}^{\\circ}","{}^{\\prime}","{}^{\\prime\\prime}"};for(size_t i=0;i<values.size();++i)if(!values[i].empty())out+=number(values[i],config)+symbols[i];
  }else{
    const bool range=name.find("range")!=std::string::npos;std::vector<std::string> values;
    if(range){values.push_back(group(s,p));values.push_back(group(s,p));}else values=list(group(s,p),';');
    const bool quantity=name!="numlist"&&name!="numrange";const auto u=quantity?units(group(s,p),config):"";
    if(values.empty()||values.size()>8192)throw std::runtime_error("Invalid numerical list");
    for(size_t i=0;i<values.size();++i){if(i)out+=range?"\\text{"+config.range+"}":"\\,;\\,";out+=number(values[i],config);if(quantity&&(config.repeat||i+1==values.size()))out+="\\,"+u;bounded(out);}
  }
  bounded(out);return true;
}
} }
