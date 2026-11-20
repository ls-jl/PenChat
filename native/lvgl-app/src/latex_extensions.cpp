#include "latex_extensions.hpp"
#include "markdown.hpp"
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace penchat { namespace latex {
namespace {
constexpr size_t Limit=65536;
void bounded(const std::string &s){if(s.size()>Limit)throw std::runtime_error("LaTeX expansion exceeds limit");}
void space(const std::string &s,size_t &p){while(p<s.size()&&std::isspace(static_cast<unsigned char>(s[p])))++p;}
std::string command(const std::string &s,size_t &p) {
  ++p;size_t start=p;while(p<s.size()&&std::isalpha(static_cast<unsigned char>(s[p])))++p;
  if(p==start&&p<s.size())++p;return s.substr(start,p-start);
}
std::string group(const std::string &s,size_t &p,char open='{',char close='}') {
  space(s,p);if(p>=s.size()||s[p]!=open)throw std::runtime_error("Missing LaTeX argument");
  const size_t start=++p;unsigned depth=1;
  while(p<s.size()) {
    if(s[p]=='\\'){p+=std::min(size_t(2),s.size()-p);continue;}
    if(s[p]==open&&++depth>32)throw std::runtime_error("LaTeX nesting exceeds limit");
    if(s[p]==close&&!--depth){auto result=s.substr(start,p-start);++p;return result;}
    ++p;
  }
  throw std::runtime_error("Unclosed LaTeX argument");
}
bool hasGroup(const std::string &s,size_t p,char open='{'){space(s,p);return p<s.size()&&s[p]==open;}
std::vector<std::string> split(const std::string &,bool);
std::pair<size_t,size_t> environmentBody(const std::string &s,size_t p,const std::string &root) {
  std::vector<std::string> stack{root};
  while(p<s.size()) {
    if(s[p]=='%'){while(p<s.size()&&s[p]!='\n')++p;continue;}
    if(s[p]!='\\'){++p;continue;}
    const auto start=p;auto name=command(s,p);
    if(name!="begin"&&name!="end")continue;auto env=group(s,p);
    if(name=="begin"){stack.push_back(env);if(stack.size()>32)throw std::runtime_error("LaTeX nesting exceeds limit");}
    else {if(stack.back()!=env)throw std::runtime_error("Mismatched LaTeX environment");stack.pop_back();if(stack.empty())return {start,p};}
  }
  throw std::runtime_error("Unclosed LaTeX environment");
}
std::string number(std::string s) {
  size_t p=0;space(s,p);const size_t start=p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;
  bool digits=false;while(p<s.size()&&(std::isdigit(static_cast<unsigned char>(s[p]))||s[p]=='.'||s[p]==',')){digits|=s[p]!='.'&&s[p]!=',';++p;}
  if(!digits)throw std::runtime_error("Invalid numerical quantity");
  std::string result=s.substr(start,p-start);
  if(p<s.size()&&(s[p]=='e'||s[p]=='E')) {
    ++p;const size_t power=p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;const size_t d=p;
    while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;
    if(d==p)throw std::runtime_error("Missing numerical exponent");
    result+="\\times 10^{"+s.substr(power,p-power)+"}";
  }
  space(s,p);if(p!=s.size())throw std::runtime_error("Unsupported numerical syntax");return result;
}
std::string unit(const std::string &s) {
  static const std::map<std::string,std::string> names={
    {"metre","m"},{"meter","m"},{"second","s"},{"gram","g"},{"kilogram","kg"},{"ampere","A"},{"kelvin","K"},{"mole","mol"},{"candela","cd"},
    {"radian","rad"},{"steradian","sr"},{"hertz","Hz"},{"newton","N"},{"pascal","Pa"},{"joule","J"},{"watt","W"},{"coulomb","C"},{"volt","V"},{"farad","F"},{"siemens","S"},{"weber","Wb"},{"tesla","T"},{"henry","H"},{"lumen","lm"},{"lux","lx"},{"becquerel","Bq"},{"gray","Gy"},{"sievert","Sv"},{"katal","kat"},
    {"litre","L"},{"liter","L"},{"minute","min"},{"hour","h"},{"day","d"},{"electronvolt","eV"},{"percent","\\%"},{"degree","{}^{\\circ}"},{"degreeCelsius","{}^{\\circ}C"},{"ohm","\\Omega"},
    {"quecto","q"},{"ronto","r"},{"yocto","y"},{"zepto","z"},{"atto","a"},{"femto","f"},{"pico","p"},{"nano","n"},{"micro","\\mu"},{"milli","m"},{"centi","c"},{"deci","d"},{"deca","da"},{"deka","da"},{"hecto","h"},{"kilo","k"},{"mega","M"},{"giga","G"},{"tera","T"},{"peta","P"},{"exa","E"},{"zetta","Z"},{"yotta","Y"},{"ronna","R"},{"quetta","Q"}};
  std::string out;size_t p=0;
  while(p<s.size()) {
    char c=s[p];
    if(c=='\\') {
      auto n=command(s,p);auto it=names.find(n);
      if(it!=names.end())out+="\\mathrm{"+it->second+"}";
      else if(n=="per")out+="/";
      else if(n=="squared"||n=="cubed")out+=n=="squared"?"^{2}":"^{3}";
      else if(n=="tothe"||n=="raiseto"){auto power=group(s,p);out+="^{"+number(power)+"}";}
      else if(n==","||n==" ")out+="\\,";
      else throw std::runtime_error("Unsupported unit command");
    } else if(std::isalpha(static_cast<unsigned char>(c))) {
      const auto start=p;while(p<s.size()&&std::isalpha(static_cast<unsigned char>(s[p])))++p;out+="\\mathrm{"+s.substr(start,p-start)+"}";
    } else if(std::isspace(static_cast<unsigned char>(c))||c=='*'||c=='.'){out+="\\,";++p;}
    else if(c=='^'||c=='_') {
      out+=c;++p;
      if(hasGroup(s,p)){auto power=group(s,p);out+="{"+number(power)+"}";}
      else {const auto start=p;if(p<s.size()&&(s[p]=='-'||s[p]=='+'))++p;while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;out+="{"+number(s.substr(start,p-start))+"}";}
    } else if((c=='-'||c=='+')&&p+1<s.size()&&std::isdigit(static_cast<unsigned char>(s[p+1]))) {
      const auto start=p++;while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;out+="^{"+number(s.substr(start,p-start))+"}";
    } else if(c=='/'||c=='('||c==')'||std::isdigit(static_cast<unsigned char>(c))){out+=c;++p;}
    else throw std::runtime_error("Unsupported unit syntax");
    bounded(out);
  }
  return out;
}
struct LocalMacro {int count=0;bool optional=false;std::string defaultArg,body;};
using Macros=std::map<std::string,LocalMacro>;
std::string expansion(const std::string &s,unsigned depth,Macros &macros) {
  if(depth>32)throw std::runtime_error("LaTeX nesting exceeds limit");
  std::string out;size_t p=0;
  auto arg=[&](){return expansion(group(s,p),depth+1,macros);};
  while(p<s.size()) {
    if(s[p]!='\\'){out+=s[p++];bounded(out);continue;}
    auto name=command(s,p);
    static const std::set<std::string> directives={"input","include","includegraphics","usepackage","require","write","openout","read","catcode","csname","endcsname","def","gdef","edef","xdef","let","newenvironment","renewenvironment"};
    if(directives.count(name))throw std::runtime_error("External or unbounded TeX directive is unsupported");
    const auto local=macros.find(name);
    if(name=="newcommand"||name=="renewcommand"||name=="DeclareMathOperator") {
      const bool starred=p<s.size()&&s[p]=='*';if(starred)++p;
      auto id=group(s,p);if(id.empty()||id[0]!='\\')throw std::runtime_error("Invalid local macro name");
      id.erase(0,1);if(id.empty()||id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")!=std::string::npos)throw std::runtime_error("Invalid local macro name");
      LocalMacro macro;
      if(name!="DeclareMathOperator"&&hasGroup(s,p,'[')) {auto count=group(s,p,'[',']');if(count.size()!=1||count[0]<'0'||count[0]>'9')throw std::runtime_error("Invalid macro arity");macro.count=count[0]-'0';}
      if(macro.count&&hasGroup(s,p,'[')){macro.optional=true;macro.defaultArg=group(s,p,'[',']');}
      macro.body=group(s,p);
      if(name=="DeclareMathOperator")macro.body="\\mathop{\\mathrm{"+macro.body+"}}"+(starred?"\\normal ":"\\nolimits ");
      if(name=="newcommand"&&macros.count(id))throw std::runtime_error("Duplicate local macro");
      macros[id]=std::move(macro);
    } else if(local!=macros.end()) {
      const auto macro=local->second;std::vector<std::string> args;
      if(macro.optional)args.push_back(hasGroup(s,p,'[')?group(s,p,'[',']'):macro.defaultArg);
      while(int(args.size())<macro.count)args.push_back(group(s,p));
      std::string body;
      for(size_t i=0;i<macro.body.size();++i) {
        if(macro.body[i]=='#'&&i+1<macro.body.size()) {
          auto next=macro.body[++i];if(next=='#')body+='#';else if(next>='1'&&next<='9'&&size_t(next-'1')<args.size())body+=args[next-'1'];else throw std::runtime_error("Invalid local macro parameter");
        } else body+=macro.body[i];bounded(body);
      }
      out+=expansion(body,depth+1,macros);
    } else if(name=="operatorname"&&p<s.size()&&s[p]=='*') {++p;out+="\\mathop{\\mathrm{"+arg()+"}}\\normal ";
    } else if(name=="abs"||name=="norm"||name=="bra"||name=="ket"||name=="braket") {
      auto a=arg();if(name=="abs")out+="\\left|"+a+"\\right|";
      if(name=="norm")out+="\\left\\|"+a+"\\right\\|";
      if(name=="bra")out+="\\left\\langle "+a+"\\right|";
      if(name=="ket")out+="\\left|"+a+"\\right\\rangle ";
      if(name=="braket")out+="\\left\\langle "+a+(hasGroup(s,p)?"\\middle|"+arg():"")+"\\right\\rangle ";
    } else if(name=="vb"||name=="va") {
      const bool star=p<s.size()&&s[p]=='*';if(star)++p;const auto a=arg();
      out+=name=="vb"?(star?"\\boldsymbol{"+a+"}":"\\mathbf{"+a+"}"):(star?"\\vec{\\boldsymbol{"+a+"}}":"\\vec{"+a+"}");
    } else if(name=="dv"||name=="pdv") {
      bool star=p<s.size()&&s[p]=='*';if(star)++p;
      std::string order=hasGroup(s,p,'[')?group(s,p,'[',']'):"";
      if(!order.empty())order=number(order);
      auto a=arg();bool two=hasGroup(s,p);auto b=two?arg():a;const auto d=name=="pdv"?"\\partial ":"\\mathrm{d}";
      std::string exponent=order.empty()?"":"^{"+order+"}";
      std::string numerator=std::string(d)+exponent+(two?a:""),denominator=std::string(d)+b+exponent;
      if(name=="pdv"&&two&&hasGroup(s,p)){auto c=arg();numerator=std::string(d)+"^{2}"+a;denominator=std::string(d)+b+d+c;}
      out+="\\frac{"+numerator+"}{"+denominator+"}";
    } else if(name=="num")out+=number(group(s,p));
    else if(name=="si"||name=="unit")out+=unit(group(s,p));
    else if(name=="SI"){auto n=group(s,p);auto u=group(s,p);out+=number(n)+"\\,"+unit(u);}
    else if(name=="pu") {
      auto body=group(s,p);size_t at=0;space(body,at);if(at<body.size()&&(body[at]=='+'||body[at]=='-'))++at;
      while(at<body.size()&&(std::isdigit(static_cast<unsigned char>(body[at]))||body[at]=='.'||body[at]==','))++at;
      if(at<body.size()&&(body[at]=='e'||body[at]=='E')){++at;if(at<body.size()&&(body[at]=='+'||body[at]=='-'))++at;while(at<body.size()&&std::isdigit(static_cast<unsigned char>(body[at])))++at;}
      auto n=body.substr(0,at),u=body.substr(at);out+=number(n)+(u.empty()?"":"\\,"+unit(u));
    } else if(name=="qty") {
      space(s,p);if(p==s.size())throw std::runtime_error("Missing quantity");
      const char open=s[p];
      if(open=='{') {auto a=group(s,p);if(hasGroup(s,p)){auto u=group(s,p);out+=number(a)+"\\,"+unit(u);}else out+="\\left\\{"+expansion(a,depth+1,macros)+"\\right\\}";}
      else if(open=='('||open=='['){auto a=group(s,p,open,open=='('?')':']');out+="\\left"+std::string(1,open)+expansion(a,depth+1,macros)+"\\right"+(open=='('?')':']');}
      else if(open=='|'){++p;const size_t start=p;while(p<s.size()&&s[p]!='|')++p;if(p==s.size())throw std::runtime_error("Unclosed quantity");auto a=expansion(s.substr(start,p-start),depth+1,macros);++p;out+="\\left|"+a+"\\right|";}
      else throw std::runtime_error("Unsupported quantity delimiter");
    } else if(name=="begin"||name=="end") {
      auto env=group(s,p);
      static const std::map<std::string,std::pair<std::string,std::string>> matrices={
        {"matrix*",{"",""}},{"pmatrix*",{"(",")"}},{"bmatrix*",{"[","]"}},{"Bmatrix*",{"\\{","\\}"}},{"vmatrix*",{"|","|"}},{"Vmatrix*",{"\\|","\\|"}}};
      const auto matrix=matrices.find(env);
      if(name=="begin"&&matrix!=matrices.end()&&hasGroup(s,p,'[')) {
        const auto align=group(s,p,'[',']');if(align.size()!=1||align.find_first_not_of("lcr")!=std::string::npos)throw std::runtime_error("Unsupported matrix alignment");
        const auto end=environmentBody(s,p,env);const auto body=s.substr(p,end.first-p);size_t columns=1;
        for(const auto &row:split(body,false))columns=std::max(columns,split(row,true).size());
        if(!matrix->second.first.empty())out+="\\left"+matrix->second.first;
        out+="\\begin{array}{"+std::string(columns,align[0])+"}"+expansion(body,depth+1,macros)+"\\end{array}";
        if(!matrix->second.second.empty())out+="\\right"+matrix->second.second;
        p=end.second;bounded(out);continue;
      }
      static const std::set<std::string> starred={"equation*","align*","alignat*","aligned*","alignedat*","gather*","gathered*","multline*","matrix*","pmatrix*","bmatrix*","Bmatrix*","vmatrix*","Vmatrix*"};
      if(starred.count(env))env.pop_back();out+="\\"+name+"{"+env+"}";
    } else if(name=="displaylimits")out+="\\normal ";
    else if(name=="coloneqq")out+="\\mathrel{:=}";
    else if(name=="eqqcolon")out+="\\mathrel{=:}";
    else out+="\\"+name;
    bounded(out);
  }
  return out;
}
} // namespace
std::string expand(const std::string &source){
  if(source.size()>Limit)throw std::runtime_error("Formula exceeds limit");
  unsigned braces=0;
  for(size_t p=0;p<source.size();++p){
    if(source[p]=='\\'){p+=std::min(size_t(1),source.size()-p-1);continue;}
    if(source[p]=='%'){while(p<source.size()&&source[p]!='\n')++p;continue;}
    if(source[p]=='{'&&++braces>32)throw std::runtime_error("LaTeX nesting exceeds limit");
    if(source[p]=='}'){if(!braces)throw std::runtime_error("Unbalanced LaTeX braces");--braces;}
  }
  if(braces)throw std::runtime_error("Unclosed LaTeX braces");
  Macros macros;return expansion(source,0,macros);
}
} }

namespace penchat { namespace latex {
namespace {
const std::set<std::string> environments={"equation","equation*","align","align*","alignat","alignat*","gather","gather*","multline","multline*"};
std::vector<std::string> split(const std::string &s,bool columns) {
  std::vector<std::string> rows;size_t start=0,p=0;unsigned braces=0,nested=0;
  while(p<s.size()) {
    const char c=s[p];
    if(c=='%'){while(p<s.size()&&s[p]!='\n')++p;continue;}
    if(c=='\\') {
      const auto at=p;auto n=command(s,p);
      if((n=="begin"||n=="end")&&hasGroup(s,p)){group(s,p);if(n=="begin")++nested;else if(nested)--nested;}
      else if(!braces&&!nested&&!columns&&n=="\\"){rows.push_back(s.substr(start,at-start));if(p<s.size()&&s[p]=='*')++p;start=p;}
      continue;
    }
    if(c=='{'){if(++braces>32)throw std::runtime_error("LaTeX nesting exceeds limit");}
    else if(c=='}'){if(!braces)throw std::runtime_error("Unbalanced LaTeX braces");--braces;}
    else if(columns&&c=='&'&&!braces&&!nested){rows.push_back(s.substr(start,p-start));start=p+1;}
    ++p;
  }
  if(braces||nested)throw std::runtime_error("Unclosed LaTeX structure");
  rows.push_back(s.substr(start));return rows;
}
struct Binding {std::vector<std::string> labels;bool suppressed=false,tagged=false;};
std::string dimension(const std::string &s) {
  size_t p=0;space(s,p);const auto start=p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;
  bool digits=false;while(p<s.size()&&(std::isdigit(static_cast<unsigned char>(s[p]))||s[p]=='.')){digits|=s[p]!='.';++p;}
  if(!digits||p-start>32)throw std::runtime_error("Invalid formula row spacing");
  const auto number=s.substr(start,p-start);size_t used=0;const double value=std::stod(number,&used);
  if(used!=number.size()||!std::isfinite(value)||std::abs(value)>2048)throw std::runtime_error("Formula row spacing exceeds limit");
  space(s,p);size_t tail=s.size();while(tail>p&&std::isspace(static_cast<unsigned char>(s[tail-1])))--tail;const auto unit=s.substr(p,tail-p);if(unit.empty()&&value==0)return "0pt";static const std::set<std::string> units={"pt","em","ex","mu","px","mm","cm","in","pc","dd","cc","sp"};
  if(!units.count(unit))throw std::runtime_error("Unsupported formula row spacing unit");return number+unit;
}
std::string annotations(const std::string &s,FormulaRow &row,Binding &binding) {
  std::string out;size_t p=0;unsigned braces=0;
  while(p<s.size()) {
    if(s[p]=='%'){while(p<s.size()&&s[p]!='\n')++p;continue;}
    if(s[p]!='\\'){if(s[p]=='{')++braces;else if(s[p]=='}'&&braces)--braces;out+=s[p++];continue;}
    const auto start=p;auto n=command(s,p);
    if(!braces&&(n=="tag"||n=="label"||n=="notag"||n=="nonumber")) {
      if(n=="tag") {
        if(binding.tagged)throw std::runtime_error("Duplicate formula tag");
        binding.tagged=true;row.tagStar=p<s.size()&&s[p]=='*';if(row.tagStar)++p;row.tag=group(s,p);
      } else if(n=="label")binding.labels.push_back(group(s,p));
      else binding.suppressed=true;
    } else out+=s.substr(start,p-start);
  }
  return out;
}
std::string references(const std::string &s,const Document &doc) {
  std::string out;size_t p=0;
  while(p<s.size()) {
    if(s[p]!='\\'){out+=s[p++];continue;}
    const auto start=p;auto name=command(s,p);
    if(name=="ref"||name=="eqref") {
      auto label=group(s,p);auto i=doc.formulaLabels.find(label);auto tag=i==doc.formulaLabels.end()?"\\text{??}":"\\text{"+i->second+"}";
      out+=name=="eqref"?"\\left("+tag+"\\right)":"{"+tag+"}";
    } else out+=s.substr(start,p-start);
    bounded(out);
  }
  return out;
}
void bareReferences(std::vector<Run> &runs) {
  std::vector<Run> result;
  for(const auto &run:runs) {
    if(run.style&(Mono|Math|Image)){result.push_back(run);continue;}
    size_t p=0,start=0;
    while(p<run.text.size()) {
      if(run.text[p]!='\\'){++p;continue;}
      const auto at=p;auto name=command(run.text,p);
      if((name!="ref"&&name!="eqref")||!hasGroup(run.text,p))continue;
      try{group(run.text,p);}catch(...){continue;}
      if(at>start){auto prefix=run;prefix.text=run.text.substr(start,at-start);result.push_back(std::move(prefix));}
      Run reference{run.text.substr(at,p-at),run.link,run.style|Math};reference.bareMath=true;result.push_back(std::move(reference));start=p;
    }
    if(start<run.text.size()){auto suffix=run;suffix.text=run.text.substr(start);result.push_back(std::move(suffix));}
  }
  runs=std::move(result);
}
}
size_t environmentEnd(const std::string &s,size_t at) {
  try {
    size_t p=at;if(s.compare(p,6,"\\begin"))return std::string::npos;p+=6;auto first=group(s,p);
    if(!environments.count(first))return std::string::npos;
    return environmentBody(s,p,first).second;
  }catch(...){}
  return std::string::npos;
}
void prepare(Document &doc) {
  doc.formulaLabels.clear();doc.formulaDiagnostics.clear();int counter=0;
  std::vector<Run *> formulas;
  auto visit=[&](std::vector<Run> &runs) {
    bareReferences(runs);
    for(auto &run:runs) {
      if(!(run.style&Math))continue;
      run.mathInvalid=false;run.formulaRows.clear();run.mathKind.clear();
      const int previousCounter=counter;std::vector<std::string> inserted;
      try {
        if(run.text.size()>8192)throw std::runtime_error("Formula too large");
        std::string body=run.text,kind;bool numbered=false;
        size_t at=0;space(body,at);
        if(body.compare(at,6,"\\begin")==0) {
          size_t p=at+6;auto env=group(body,p);
          size_t tail=body.size();while(tail>at&&std::isspace(static_cast<unsigned char>(body[tail-1])))--tail;
          if(environments.count(env)&&environmentEnd(body,at)==tail) {
            kind=env;numbered=env.back()!='*';if(!numbered)kind.pop_back();
            const auto end=body.rfind("\\end");body=body.substr(p,end-p);
            if(kind=="alignat"){size_t n=0;group(body,n);body=body.substr(n);}
          }
        }
        // Expand once for the whole formula, so local definitions also work
        // across alignment cells/rows without touching MicroTeX globals.
        body=expand(body);
        FormulaRow multlineTag;Binding multlineBinding;
        if(kind=="multline")body=annotations(body,multlineTag,multlineBinding);
        auto rows=(kind=="align"||kind=="alignat"||kind=="gather"||kind=="multline")?split(body,false):std::vector<std::string>{body};
        while(rows.size()>1&&rows.back().find_first_not_of(" \t\r\n")==std::string::npos)rows.pop_back();
        std::vector<FormulaRow> prepared;
        for(const auto &source:rows) {
          FormulaRow row;Binding binding;size_t rowStart=0;std::string gap;
          if(!prepared.empty()&&hasGroup(source,rowStart,'['))gap=dimension(group(source,rowStart,'[',']'));
          std::string clean;
          if(kind=="multline") {
            clean=source.substr(rowStart);
            if(prepared.size()+1==rows.size()){row=multlineTag;binding=multlineBinding;}else binding.suppressed=true;
          } else clean=annotations(source.substr(rowStart),row,binding);
          row.gapBefore=gap;
          row.showTag=binding.tagged;
          if(numbered&&!binding.suppressed&&!binding.tagged&&(kind!="multline"||prepared.size()+1==rows.size())){row.tag=std::to_string(++counter);row.showTag=true;}
          for(const auto &label:binding.labels) {
            if(!row.showTag)continue;
            if(!doc.formulaLabels.emplace(label,row.tag).second)doc.formulaDiagnostics.push_back("duplicate_label");else inserted.push_back(label);
          }
          row.cells=(kind=="align"||kind=="alignat")?split(clean,true):std::vector<std::string>{clean};
          prepared.push_back(std::move(row));
        }
        run.formulaRows=std::move(prepared);run.mathKind=kind;formulas.push_back(&run);
      }catch(const std::exception &) {counter=previousCounter;for(const auto &key:inserted)doc.formulaLabels.erase(key);run.formulaRows.clear();run.mathKind.clear();run.mathInvalid=true;doc.formulaDiagnostics.push_back("invalid_formula");}
    }
  };
  for(auto &block:doc.blocks){visit(block.runs);for(auto &cell:block.cells)visit(cell);}
  for(auto run:formulas) {
    try {
      for(auto &row:run->formulaRows){for(auto &cell:row.cells)cell=references(cell,doc);row.tag=references(row.tag,doc);}
    }catch(...){run->formulaRows.clear();run->mathInvalid=true;doc.formulaDiagnostics.push_back("invalid_reference");}
  }
}
} }
