#include "latex_extensions.hpp"
#include "markdown.hpp"
#include "stb_image_write.h"
#include <cassert>
#include <iostream>
using namespace penchat;
static std::vector<const Run *> math(const Document &doc){std::vector<const Run *> out;for(const auto &b:doc.blocks)for(const auto &r:b.runs)if(r.style&Math)out.push_back(&r);return out;}
int main(int argc,char **argv) {
  assert(argc==3||argc==4);RichText rich(argv[1],argv[2]);int picture=0;
  auto emit=[&](Bitmap &b,const std::string &name){
    if(argc!=4)return;ensureDirectory(argv[3]);std::vector<uint8_t> rgba(b.pixels.size());
    for(size_t i=0;i<rgba.size();i+=4){const int alpha=b.pixels[i+3];for(int j=0;j<3;++j)rgba[i+j]=alpha?std::min(255,int(b.pixels[i+2-j])*255/alpha):0;rgba[i+3]=alpha;}
    assert(stbi_write_png(join(argv[3],name+".png").c_str(),b.width,b.height,4,rgba.data(),b.width*4));
  };
  auto doc=parseMarkdown(R"(See \eqref{later}.
\begin{equation}x=1\label{first}\end{equation}
\begin{align}a&=b\label{later}\\c&=d\notag\\e&=f\tag{A}\label{manual}\end{align}
\begin{equation*}z=0\end{equation*}
\begin{equation}y=2\end{equation}
Reference $\ref{manual}+\eqref{missing}$.)");
  assert(doc.formulaLabels.at("first")=="1"&&doc.formulaLabels.at("later")=="2"&&doc.formulaLabels.at("manual")=="A");
  auto runs=math(doc);assert(runs.size()==6);assert(runs[0]->bareMath&&runs[0]->text==R"(\eqref{later})");
  assert(runs[0]->formulaRows[0].cells[0].find("2")!=std::string::npos);
  assert(runs[2]->formulaRows.size()==3&&runs[2]->formulaRows[0].cells.size()==2);
  assert(!runs[2]->formulaRows[1].showTag&&runs[2]->formulaRows[2].tag=="A");
  assert(!runs[3]->formulaRows[0].showTag&&runs[4]->formulaRows[0].tag=="3");
  assert(runs.back()->formulaRows[0].cells[0].find("??")!=std::string::npos);
  auto isolated=parseMarkdown(R"($\eqref{first}$)");assert(isolated.formulaLabels.empty()&&math(isolated)[0]->formulaRows[0].cells[0].find("??")!=std::string::npos);
  auto spaced=parseMarkdown("$$\n  \\begin{equation}\nx=1\\label{spaced}\n\\end{equation}\n$$");assert(spaced.formulaLabels.at("spaced")=="1");
  auto duplicate=parseMarkdown(R"($$x\tag{a}\label{q}$$ $$y\tag{b}\label{q}$$)");assert(duplicate.formulaLabels.at("q")=="a"&&!duplicate.formulaDiagnostics.empty());
  auto code=parseMarkdown("`\\eqref{first}`\n\n```tex\n\\begin{equation}x\\label{a}\\end{equation}\n```\n");assert(math(code).empty()&&code.formulaLabels.empty());
  auto indented=parseMarkdown("    \\begin{equation}x\\end{equation}\n    \\ce{H2O}\n");assert(math(indented).empty());assert(blockPlainText(indented.blocks[0]).find("$$")==std::string::npos);
  auto macros=parseMarkdown(R"($$\begin{align}\newcommand{\sq}[1]{#1^2}\sq{x}&=1\\\sq{y}&=2\end{align}$$)");
  auto macroRuns=math(macros);assert(macroRuns.size()==1&&macroRuns[0]->formulaRows[1].cells[0].find("y^2")!=std::string::npos);
  auto stream=parseMarkdown(R"(\begin{equation}x=1\label{a}\end{equation}
$$unclosed)",true);assert(stream.formulaLabels.at("a")=="1");
  auto unfinishedCode=parseMarkdown("```tex\n$$x$$",true);assert(unfinishedCode.blocks.size()==1&&unfinishedCode.blocks[0].kind=="p"&&math(unfinishedCode).empty());
  for(const auto *tex:{R"(x^2\tag{1})",R"(x\tag*{A})",R"(x\tag{})",R"(\begin{align}a&=b\\cc&=d\end{align})",R"(\begin{gather}a=b\\c=d\end{gather})",R"(\begin{multline}a+b\\=c\end{multline})",
      R"(\coloneqq+\eqqcolon)",R"(\mathclap{x}+\overset{a}{b}+\underset{a}{b})",R"(\abs{x}+\norm{x}+\qty(x+1)+\qty[x]+\qty|x|)",R"(\vb{x}+\va{x}+\bra{x}+\ket{y}+\braket{x}{y})",R"(\dv[2]{f}{x}+\pdv{f}{x}{y})",R"(\num{1.2e-3}+\SI{9.8}{\metre\per\second\squared})",R"(\unit{\kilo\gram}+\qty{3}{\ohm}+\pu{1.2e3 J})",R"(\begin{aligned}a&=b\\c&=d\end{aligned})",R"(\begin{pmatrix}a&b\\c&d\end{pmatrix})",R"(\boxed{\mathbb{R}}+\cancel{x})",R"(\begin{pmatrix*}[r]1&22\\333&4\end{pmatrix*})",R"(\DeclareMathOperator*{\argmax}{argmax}\argmax_x f(x))",R"(\pu{2.3e-3 kg m-2})"}) {
    for(auto color:{0xd5d7deu,0x30343bu}){auto b=rich.formula(tex,21,color);assert(b.width>0&&b.height>0&&!b.pixels.empty());for(size_t px=0;px<b.pixels.size();px+=4)assert(!(b.pixels[px+2]>180&&b.pixels[px+1]<60&&b.pixels[px]<60));emit(b,"formula-"+std::to_string(picture++));}
  }
  for(int width:{140,300,900})for(auto &b:doc.blocks){auto l=rich.layout(b,width,21,true);assert(l.height>=0&&l.width>=0);if(l.height){auto image=rich.paint(b,width,0,l.height,21);assert(!image.pixels.empty());emit(image,"document-"+std::to_string(width)+"-"+std::to_string(picture++));}}
  auto displayOp=rich.formula(R"(\displaystyle\operatorname*{argmax}_x f(x))",21),textOp=rich.formula(R"(\textstyle\operatorname*{argmax}_x f(x))",21);assert(displayOp.height>textOp.height);
  auto star=rich.formula(R"(x\tag*{1})",21),tag=rich.formula(R"(x\tag{1})",21);assert(tag.width>star.width);
  bool rejected=false;try{latex::expand("\\qty"+std::string(34,'(')+"x"+std::string(34,')'));}catch(...){rejected=true;}assert(rejected);
  assert(latex::expand(R"(\newcommand{\foo}[1]{#1^2}\foo{x})")=="x^2");
  bool unknown=false;try{rich.formula(R"(\missingextension{x})",21);}catch(...){unknown=true;}assert(unknown);
  assert(latex::expand(R"(\foo{x})")==R"(\foo{x})"); // Never leaks definitions to another formula.
  rejected=false;try{latex::expand(R"(\newcommand{\foo}{\foo}\foo)");}catch(...){rejected=true;}assert(rejected);
  for(const auto &input:{std::string(33,'{')+"x"+std::string(33,'}'),std::string(65537,'x')}){rejected=false;try{latex::expand(input);}catch(...){rejected=true;}assert(rejected);}
  rejected=false;try{latex::expand(R"(\includegraphics{/outside/formula})");}catch(...){rejected=true;}assert(rejected);
  uint32_t random=12345;
  for(int sample=0;sample<2000;++sample){std::string text;for(int i=0;i<sample%257;++i){random=random*1664525u+1013904223u;text+="{}[]()\\#%abc012"[random%15];}try{assert(latex::expand(text).size()<=65536);}catch(const std::exception &) {}}
  std::cout<<"LATEX_NATIVE_EXTENSIONS_NUMBERING_REFERENCES_AND_RENDERING_OK\n";
}
