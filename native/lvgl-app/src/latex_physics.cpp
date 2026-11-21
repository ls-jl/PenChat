#include "latex_physics.hpp"
#include <map>
#include <set>
namespace penchat { namespace latex {
namespace {
using namespace syntax;
int positive(const std::string &s){if(s.empty()||s.size()>4||s.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Expected positive integer");int n=std::stoi(s);if(n<1||n>1024)throw std::runtime_error("Integer exceeds limit");return n;}
std::string fence(std::string value,const std::string &left,const std::string &right,bool fixed,const std::string &size=""){
  if(fixed||!size.empty()){for(size_t at=0;(at=value.find("\\middle",at))!=std::string::npos;)value.erase(at,7);}
  if(!size.empty()){static const std::set<std::string> sizes={"\\big","\\Big","\\bigg","\\Bigg"};if(!sizes.count(size))throw std::runtime_error("Invalid delimiter size");return size+"l"+left+" "+value+size+"r"+right+" ";}
  return (fixed?left:"\\left"+left)+" "+value+(fixed?right:"\\right"+right)+" ";
}
std::string delimited(const std::string &s,size_t &p,const Expand &expand,char &open){
  space(s,p);if(p==s.size())throw std::runtime_error("Missing physical quantity");open=s[p];
  if(open=='{')return expand(group(s,p));if(open=='('||open=='[')return expand(group(s,p,open,open=='('?')':']'));
  if(open=='|'){++p;const auto at=p;unsigned braces=0;while(p<s.size()){if(s[p]=='\\'){p+=std::min(size_t(2),s.size()-p);continue;}if(s[p]=='{')++braces;else if(s[p]=='}'&&braces)--braces;if(s[p]=='|'&&!braces){auto value=expand(s.substr(at,p-at));++p;return value;}++p;}throw std::runtime_error("Unclosed physical delimiter");}
  throw std::runtime_error("Invalid physical delimiter");
}
}
bool physicsCommand(const std::string &original,const std::string &s,size_t &p,std::string &out,const syntax::Expand &expand){
  static const std::map<std::string,std::string> aliases={
    {"absolutevalue","abs"},{"quantity","qty"},{"vectorbold","vb"},{"vectorarrow","va"},{"vectorunit","vu"},{"differential","dd"},{"derivative","dv"},{"partialderivative","pdv"},{"pderivative","pdv"},{"functionalderivative","fdv"},{"fderivative","fdv"},{"evaluated","eval"},{"expectationvalue","expval"},{"ev","expval"},{"matrixelement","matrixel"},{"mel","matrixel"},{"dyad","ketbra"},{"outerproduct","ketbra"},{"op","ketbra"},{"innerproduct","braket"},{"ip","braket"},{"commutator","comm"},{"anticommutator","acomm"},{"poissonbracket","pb"},{"gradient","grad"},{"cross","crossproduct"},{"cp","crossproduct"},{"vdot","dotproduct"},{"matrixquantity","mqty"},{"matrixdeterminant","mdet"},{"smallmatrixquantity","smqty"},{"identitymatrix","imat"},{"zeromatrix","zmat"},{"diagonalmatrix","dmat"},{"antidiagonalmatrix","admat"},{"xmatrix","xmat"},{"trace","tr"},{"Trace","Tr"},{"orderof","order"}};
  auto found=aliases.find(original);const auto name=found==aliases.end()?original:found->second;
  static const std::set<std::string> known={"abs","norm","qty","pqty","bqty","Bqty","vqty","vb","va","vu","dd","dv","pdv","fdv","eval","expval","matrixel","bra","ket","braket","ketbra","comm","acomm","pb","grad","divergence","curl","laplacian","dotproduct","crossproduct","order","flatfrac","mqty","pmqty","Pmqty","bmqty","vmqty","smqty","spmqty","sPmqty","sbmqty","svmqty","mdet","smdet","imat","zmat","dmat","admat","xmat","tr","Tr","rank","Res"};
  if(!known.count(name))return false;
  auto arg=[&](){return expand(group(s,p));};
  if(name=="dotproduct"||name=="crossproduct"){out+=name=="dotproduct"?"\\boldsymbol{\\cdot}":"\\boldsymbol{\\times}";return true;}
  if(name=="grad"||name=="divergence"||name=="curl"||name=="laplacian"){
    out+="\\nabla ";if(name=="divergence")out+="\\boldsymbol{\\cdot} ";if(name=="curl")out+="\\boldsymbol{\\times} ";if(name=="laplacian")out+="^{2}";
    if(has(s,p))out+=arg();else if(has(s,p,'(')||has(s,p,'[')){char open;auto value=delimited(s,p,expand,open);out+=fence(value,std::string(1,open),std::string(1,open=='('?')':']'),false);}return true;
  }
  if(name=="tr"||name=="Tr"||name=="rank"||name=="Res"){out+="\\mathop{\\mathrm{"+name+"}}\\nolimits ";return true;}
  const bool fixed=star(s,p);
  if(fixed&&(name=="dd"||name=="flatfrac"||name=="imat"||name=="zmat"||name=="dmat"||name=="admat"))throw std::runtime_error("Unsupported starred physics form");
  if(name=="flatfrac"){auto a=arg(),b=arg();out+="\\left."+a+"\\middle/"+b+"\\right.";return true;}
  if(name=="vb"||name=="va"||name=="vu"){
    auto a=arg();const auto bold=(fixed?"\\boldsymbol{":"\\mathbf{")+a+"}";
    out+=name=="vb"?bold:name=="va"?"\\vec{"+(fixed?bold:a)+"}":(fixed?"\\boldsymbol{\\hat{":"\\mathbf{\\hat{")+a+"}}";return true;
  }
  if(name=="dd"){
    const auto order=has(s,p,'[')?expand(group(s,p,'[',']')):"";out+="\\mathrm{d}"+(order.empty()?"":"^{"+order+"}");if(has(s,p))out+=" "+arg();else if(has(s,p,'(')){auto a=expand(group(s,p,'(',')'));out+=fence(a,"(",")",false);}return true;
  }
  if(name=="dv"||name=="pdv"||name=="fdv"){
    auto order=has(s,p,'[')?expand(group(s,p,'[',']')):"";auto a=arg();const bool two=has(s,p);auto b=two?arg():a;std::vector<std::string> variables{b};
    if(name=="pdv")while(has(s,p)){variables.push_back(arg());if(variables.size()>32)throw std::runtime_error("Too many partial derivative variables");}
    const std::string d=name=="pdv"?"\\partial ":name=="fdv"?"\\delta ":"\\mathrm{d}";
    std::vector<std::string> orders;if(!order.empty())orders=list(order,',');
    if(orders.size()>1&&orders.size()!=variables.size())throw std::runtime_error("Derivative order mismatch");
    std::string total=order;if(variables.size()>1){int sum=0;bool numeric=true;std::string symbolic;for(size_t i=0;i<variables.size();++i){const auto n=orders.empty()?"1":orders.size()==1?orders[0]:orders[i];if(!symbolic.empty())symbolic+='+';symbolic+=n;if(n.find_first_not_of("0123456789")!=std::string::npos)numeric=false;else sum+=positive(n);}total=numeric?std::to_string(sum):symbolic;}
    auto power=[](const std::string &n){return n.empty()||n=="1"?"":"^{"+n+"}";};
    std::string numerator=d+power(total)+(two?a:""),denominator;
    for(size_t i=0;i<variables.size();++i){auto n=orders.empty()?"":orders.size()==1?orders[0]:orders[i];denominator+=d+variables[i]+power(n);}
    out+=fixed?"\\left."+numerator+"\\middle/"+denominator+"\\right.":"\\frac{"+numerator+"}{"+denominator+"}";
    if(has(s,p,'(')){auto value=expand(group(s,p,'(',')'));out+=fence(value,"(",")",false);}return true;
  }
  if(name=="eval"){
    char open='.';std::string value;if(has(s,p))value=arg();else if(has(s,p,'(')||has(s,p,'[')){space(s,p);open=s[p];value=expand(group(s,p,open,'|'));}
    out+=(fixed?"\\vphantom{"+value+"}\\left.":"\\left"+std::string(1,open))+(fixed?"\\smash{"+value+"}":value)+"\\vphantom{\\int}\\right\\rvert ";return true;
  }
  if(name=="expval"||name=="matrixel"){
    const bool both=fixed&&star(s,p);auto a=arg();std::string body;
    if(name=="matrixel"){auto b=arg(),c=arg();body=both?a+"\\middle|"+b+"\\middle|"+c:a+"\\middle|\\smash{"+b+"}\\middle|"+c;if(fixed&&!both)body="\\smash{"+a+"}\\middle|\\smash{"+b+"}\\middle|\\smash{"+c+"}";}
    else if(has(s,p)){auto state=arg();body=both?state+"\\middle|"+a+"\\middle|"+state:state+"\\middle|\\smash{"+a+"}\\middle|"+state;if(fixed&&!both)body="\\smash{"+state+"}\\middle|\\smash{"+a+"}\\middle|\\smash{"+state+"}";}
    else body=fixed?"\\smash{"+a+"}":a;
    out+=(fixed&&!both?"\\vphantom{"+a+"}":"")+std::string("\\left\\langle ")+body+"\\right\\rangle ";return true;
  }
  if(name=="imat"||name=="zmat"||name=="dmat"||name=="admat"||name=="xmat"){
    std::vector<std::string> diagonal;std::string filler="",entry;int rows=0,cols=0;
    if(name=="dmat"||name=="admat"){if(has(s,p,'['))filler=expand(group(s,p,'[',']'));diagonal=list(group(s,p),',');rows=cols=int(diagonal.size());for(auto &v:diagonal)v=expand(v);}
    else if(name=="xmat"){entry=arg();rows=positive(group(s,p));cols=positive(group(s,p));}
    else{rows=positive(group(s,p));cols=name=="zmat"&&has(s,p)?positive(group(s,p)):rows;entry="0";}
    if(int64_t(rows)*cols>8192)throw std::runtime_error("Generated matrix exceeds limit");std::string body;
    for(int i=0;i<rows;++i){if(i)body+="\\\\";for(int j=0;j<cols;++j){if(j)body+='&';if(!diagonal.empty())body+=(name=="dmat"?i==j:i+j==cols-1)?diagonal[i]:filler;else if(name=="imat")body+=i==j?"1":"0";else body+=entry;bounded(body);}}
    out+="\\begin{matrix}"+body+"\\end{matrix}";return true;
  }
  if(name.find("mqty")!=std::string::npos||name=="mdet"||name=="smdet"){
    const bool small=name[0]=='s';char open='{';auto body=delimited(s,p,expand,open);const auto content="\\begin{"+std::string(small?"smallmatrix":"matrix")+"}"+body+"\\end{"+(small?"smallmatrix":"matrix")+"}";
    std::string left,right;if(name=="mdet"||name=="smdet"||name=="vmqty"||name=="svmqty"||open=='|')left=right="|";
    else if(name=="bmqty"||name=="sbmqty"||open=='['){left="[";right="]";}
    else if(name=="pmqty"||name=="Pmqty"||name=="spmqty"||name=="sPmqty"||open=='('){left="(";right=")";}
    out+=left.empty()?content:fence(content,left,right,false);return true;
  }
  std::string size;if(has(s,p,'[')&&name!="qty"){size=group(s,p,'[',']');}else {size_t q=p;space(s,q);if(q<s.size()&&s[q]=='\\'){auto n=command(s,q);if(n=="big"||n=="Big"||n=="bigg"||n=="Bigg"){size="\\"+n;p=q;}}}
  char open='{';std::string a;
  if(name=="qty")a=delimited(s,p,expand,open);else a=arg();
  if(name=="bra"||name=="ket")out+=fence(a,name=="bra"?"\\langle":"|",name=="bra"?"|":"\\rangle",fixed,size);
  else if(name=="braket"||name=="ketbra"){
    auto b=has(s,p)?arg():a;
    if(name=="ketbra")out+=fence(a,"|","\\rangle",fixed,size)+fence(b,"\\langle","|",fixed,size);
    else out+=fence(a+"\\middle|"+b,"\\langle","\\rangle",fixed,size);
  }else if(name=="comm"||name=="acomm"||name=="pb"){auto b=arg();out+=fence(a+","+b,name=="comm"?"[":"\\{",name=="comm"?"]":"\\}",fixed,size);}
  else if(name=="order")out+="\\mathcal{O}"+fence(a,"(",")",fixed,size);
  else{
    std::string left,right;if(name=="norm")left=right="\\|";else if(name=="abs"||name=="vqty"||open=='|')left=right="|";
    else if(name=="bqty"||open=='['){left="[";right="]";}else if(name=="Bqty"||(name=="qty"&&open=='{')){left="\\{";right="\\}";}else{left="(";right=")";}out+=fence(a,left,right,fixed,size);
  }
  bounded(out);return true;
}
} }
