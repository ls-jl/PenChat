#include "latex_extensions.hpp"
#include "markdown.hpp"
#include "latex_syntax.hpp"
#include "latex_units.hpp"
#include "latex_physics.hpp"
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
namespace penchat { namespace latex {
namespace {
using syntax::Limit;
using syntax::bounded;
using syntax::space;
using syntax::command;
using syntax::group;
bool hasGroup(const std::string &s,size_t p,char open='{'){return syntax::has(s,p,open);}
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
struct LocalMacro {int count=0;bool optional=false;std::string defaultArg,body,left,right;bool paired=false;};
struct Macros : std::map<std::string,LocalMacro> {UnitOptions units;};
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
    if(local==macros.end()){
      if(unitCommand(name,s,p,out,macros.units)){bounded(out);continue;}
      if(physicsCommand(name,s,p,out,[&](const std::string &value){return expansion(value,depth+1,macros);})){bounded(out);continue;}
    }
    if(name=="DeclarePairedDelimiter") {
      auto id=group(s,p);size_t q=0;if(id.empty()||id[0]!='\\')throw std::runtime_error("Invalid paired delimiter name");
      id=command(id,q);if(q!=id.size()+1||id.empty())throw std::runtime_error("Invalid paired delimiter name");
      LocalMacro macro;macro.paired=true;macro.left=syntax::atom(s,p);macro.right=syntax::atom(s,p);
      macros[id]=std::move(macro);
    } else if(name=="newcommand"||name=="renewcommand"||name=="DeclareMathOperator") {
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
      const auto macro=local->second;
      if(macro.paired){const bool automatic=syntax::star(s,p);std::string size;if(hasGroup(s,p,'['))size=group(s,p,'[',']');
        if(automatic&&!size.empty())throw std::runtime_error("Conflicting delimiter sizes");
        static const std::set<std::string> sizes={"\\big","\\Big","\\bigg","\\Bigg"};if(!size.empty()&&!sizes.count(size))throw std::runtime_error("Unsupported delimiter size");
        auto value=arg();out+=(automatic?"\\left":size.empty()?"":size+"l")+macro.left+" "+value+(automatic?"\\right":size.empty()?"":size+"r")+macro.right+" ";bounded(out);continue;}
      std::vector<std::string> args;
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
    } else if(name=="overbracket"||name=="underbracket") {
      auto thickness=hasGroup(s,p,'[')?group(s,p,'[',']'):"";auto height=hasGroup(s,p,'[')?group(s,p,'[',']'):"";
      auto value=arg();out+="\\penchatbracket{"+std::string(name=="overbracket"?"over":"under")+"}{"+thickness+"}{"+height+"}{"+value+"}";
    } else if(name=="begin"||name=="end") {
      auto env=group(s,p);
      static const std::set<std::string> cases={"dcases","dcases*","rcases","rcases*","drcases","drcases*","cases*"};
      if(name=="begin"&&cases.count(env)){
        const auto end=environmentBody(s,p,env);auto rows=split(s.substr(p,end.first-p),false);std::string body;
        for(const auto &row:rows){if(!body.empty())body+="\\\\";auto cols=split(row,true);if(cols.size()>2)throw std::runtime_error("Too many cases columns");
          for(size_t i=0;i<cols.size();++i){if(i)body+='&';body+=(i==1&&env.back()=='*'?"\\text{"+cols[i]+"}":(env[0]=='d'?"\\displaystyle ":"")+expansion(cols[i],depth+1,macros));}}
        const bool right=env=="rcases"||env=="rcases*"||env=="drcases"||env=="drcases*";
        out+=(right?"\\left.":"\\left\\{")+std::string("\\begin{array}{ll}")+body+"\\end{array}"+(right?"\\right\\}":"\\right.");p=end.second;continue;
      }

      if(name=="begin"&&env=="multlined"){
        const auto position=hasGroup(s,p,'[')?group(s,p,'[',']'):"c";const auto width=hasGroup(s,p,'[')?group(s,p,'[',']'):"";const auto end=environmentBody(s,p,env);auto body=expansion(s.substr(p,end.first-p),depth+1,macros);
        out+="\\penchatmultlined{"+position+"}{"+width+"}{"+body+"}";p=end.second;bounded(out);continue;
      }
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
      static const std::set<std::string> starred={"aligned*","alignedat*","gathered*","matrix*","pmatrix*","bmatrix*","Bmatrix*","vmatrix*","Vmatrix*"};
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
const std::set<std::string> environments={"equation","equation*","align","align*","alignat","alignat*","gather","gather*","multline","multline*","flalign","flalign*","multlined","subequations"};
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
std::string boundedMultlined(const std::string &source){
  std::string out;for(const auto &row:split(source,false)){if(split(row,true).size()!=1)throw std::runtime_error("multlined requires one column");if(!out.empty())out+="\\\\";out+=syntax::trim(row).empty()?"\\vphantom{x}":row;bounded(out);}return out;
}
size_t environmentEnd(const std::string &s,size_t at) {
  try {
    size_t p=at;if(s.compare(p,6,"\\begin"))return std::string::npos;p+=6;auto first=group(s,p);
    if(!environments.count(first))return std::string::npos;
    return environmentBody(s,p,first).second;
  }catch(...){}
  return std::string::npos;
}
namespace {
std::string letters(int n){std::string out;do{--n;out.insert(out.begin(),char('a'+n%26));n/=26;}while(n);return out;}
struct Numbering {int &counter;std::string prefix;int child=0;std::string next(){return prefix.empty()?std::to_string(++counter):prefix+letters(++child);}};
void bindLabels(Document &doc,const std::vector<std::string> &labels,const std::string &tag,std::vector<std::string> &added){
  for(const auto &label:labels)if(!doc.formulaLabels.emplace(label,tag).second)doc.formulaDiagnostics.push_back("duplicate_label");else added.push_back(label);
}
std::vector<std::pair<std::string,int>> sourceRows(const std::string &body){
  std::vector<std::pair<std::string,int>> rows;
  for(const auto &source:split(body,false)){
    size_t p=0,start=0;unsigned braces=0,nested=0;
    while(p<source.size()){
      if(source[p]=='{'){++braces;++p;continue;}if(source[p]=='}'){if(braces)--braces;++p;continue;}
      if(source[p]!='\\'){++p;continue;}auto at=p;auto name=command(source,p);
      if((name=="begin"||name=="end")&&hasGroup(source,p)){group(source,p);if(name=="begin")++nested;else if(nested)--nested;continue;}
      if(!braces&&!nested&&(name=="intertext"||name=="shortintertext")){
        if(!syntax::trim(source.substr(start,at-start)).empty())rows.push_back({source.substr(start,at-start),0});
        rows.push_back({group(source,p),name=="intertext"?1:2});start=p;
      }
    }
    if(!syntax::trim(source.substr(start)).empty())rows.push_back({source.substr(start),0});
  }
  return rows;
}
void prepareSource(std::string body,Document &doc,Numbering &numbering,std::vector<FormulaRow> &prepared,std::vector<std::string> &added,unsigned depth=0){
  if(depth>32)throw std::runtime_error("LaTeX environment depth exceeded");
  std::string kind;bool numbered=false;int pairs=0;size_t at=0;space(body,at);
  if(body.compare(at,6,"\\begin")==0){
    size_t p=at+6;auto env=group(body,p);size_t tail=body.size();while(tail>at&&std::isspace(static_cast<unsigned char>(body[tail-1])))--tail;
    if(environments.count(env)&&environmentEnd(body,at)==tail){
      const auto bounds=environmentBody(body,p,env);body=body.substr(p,bounds.first-p);
      if(env=="subequations"){
        if(!numbering.prefix.empty())throw std::runtime_error("Nested subequations unsupported");
        const auto parent=numbering.next();Numbering child{numbering.counter,parent};size_t q=0;
        while(q<body.size()){
          space(body,q);if(q==body.size())break;
          if(body[q]=='%'){while(q<body.size()&&body[q]!='\n')++q;continue;}
          if(body.compare(q,6,"\\label")==0){q+=6;bindLabels(doc,{group(body,q)},parent,added);continue;}
          const auto end=environmentEnd(body,q);if(end==std::string::npos)throw std::runtime_error("Expected subequations environment");
          prepareSource(body.substr(q,end-q),doc,child,prepared,added,depth+1);q=end;
        }return;
      }
      kind=env;numbered=kind.back()!='*'&&kind!="multlined";if(kind.back()=='*')kind.pop_back();
      if(kind=="multlined"){
        size_t q=0;if(hasGroup(body,q,'[')){auto position=group(body,q,'[',']');if(position!="t"&&position!="b"&&position!="c")throw std::runtime_error("Invalid multlined position");}
        if(hasGroup(body,q,'['))throw std::runtime_error("Explicit multlined width unsupported");body=body.substr(q);kind="multline";
      }
      if(kind=="alignat"){
        size_t n=0;auto count=syntax::trim(group(body,n));size_t used=0;pairs=std::stoi(count,&used);
        if(used!=count.size()||pairs<1||pairs>8192)throw std::runtime_error("Invalid alignat pair count");body=body.substr(n);
      }
    }
  }
  FormulaRow multlineTag;Binding multlineBinding;
  if(kind=="multline")body=annotations(body,multlineTag,multlineBinding);
  const bool multiple=kind=="align"||kind=="alignat"||kind=="flalign"||kind=="gather"||kind=="multline";
  auto rows=multiple?sourceRows(body):std::vector<std::pair<std::string,int>>{{body,0}};
  for(size_t i=0;i<rows.size();++i){
    FormulaRow row;row.kind=kind;row.first=i==0;row.last=i+1==rows.size();row.prose=rows[i].second!=0;row.compact=rows[i].second==2;
    if(row.prose){if(kind!="align"&&kind!="alignat"&&kind!="flalign"&&kind!="gather")throw std::runtime_error("Misplaced intertext");row.cells={rows[i].first};prepared.push_back(std::move(row));continue;}
    Binding binding;size_t start=0;std::string gap;if(i&&hasGroup(rows[i].first,start,'['))gap=dimension(group(rows[i].first,start,'[',']'));
    std::string clean;
    if(kind=="multline"){clean=rows[i].first.substr(start);if(i+1==rows.size()){row.tag=multlineTag.tag;row.tagStar=multlineTag.tagStar;binding=multlineBinding;}else binding.suppressed=true;}
    else clean=annotations(rows[i].first.substr(start),row,binding);
    row.gapBefore=gap;row.showTag=binding.tagged;
    if(numbered&&!binding.suppressed&&!binding.tagged){row.tag=numbering.next();row.showTag=true;}
    if(row.showTag)bindLabels(doc,binding.labels,row.tag,added);
    row.cells=(kind=="align"||kind=="alignat"||kind=="flalign")?split(clean,true):std::vector<std::string>{clean};
    if(pairs&&row.cells.size()>size_t(pairs)*2)throw std::runtime_error("Too many alignat cells");
    if(prepared.size()>8192)throw std::runtime_error("Too many equation rows");prepared.push_back(std::move(row));
  }
}
std::string proseReferences(const std::string &s,const Document &doc){
  std::string out;size_t p=0;while(p<s.size()){if(s[p]!='\\'){out+=s[p++];continue;}const auto at=p;auto name=command(s,p);
    if((name=="ref"||name=="eqref")&&hasGroup(s,p)){auto label=group(s,p);auto found=doc.formulaLabels.find(label);const auto value=found==doc.formulaLabels.end()?"??":found->second;out+=name=="eqref"?"("+value+")":value;}
    else out+=s.substr(at,p-at);bounded(out);
  }return out;
}
}
void prepare(Document &doc) {
  doc.formulaLabels.clear();doc.formulaDiagnostics.clear();int counter=0;Numbering numbering{counter,""};std::vector<Run *> formulas;
  auto visit=[&](std::vector<Run> &runs){
    bareReferences(runs);
    for(auto &run:runs){if(!(run.style&Math))continue;run.mathInvalid=false;run.formulaRows.clear();run.mathKind.clear();
      const int previous=counter;std::vector<std::string> added;
      try{
        if(run.text.size()>8192)throw std::runtime_error("Formula too large");
        // Expand local declarations once before splitting rows; never mutate the engine macro table.
        prepareSource(expand(run.text),doc,numbering,run.formulaRows,added);
        if(!run.formulaRows.empty())run.mathKind=run.formulaRows.front().kind;formulas.push_back(&run);
      }catch(const std::exception &){counter=previous;for(const auto &key:added)doc.formulaLabels.erase(key);run.formulaRows.clear();run.mathInvalid=true;doc.formulaDiagnostics.push_back("invalid_formula");}
    }
  };
  for(auto &block:doc.blocks){visit(block.runs);for(auto &cell:block.cells)visit(cell);}
  for(auto run:formulas)try{for(auto &row:run->formulaRows){for(auto &cell:row.cells)cell=row.prose?proseReferences(cell,doc):references(cell,doc);row.tag=references(row.tag,doc);}}
    catch(...){run->formulaRows.clear();run->mathInvalid=true;doc.formulaDiagnostics.push_back("invalid_reference");}
}
} }
