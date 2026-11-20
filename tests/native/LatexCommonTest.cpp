#include "markdown.hpp"
#include "latex_extensions.hpp"
#include "theme.hpp"
#include "stb_image_write.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace penchat;
int main(int argc,char **argv){
  assert(argc==4||argc==5);RichText rich(argv[1],argv[2]);std::ifstream input(argv[3]);Json fixtures;input>>fixtures;
  auto emit=[&](const Bitmap &b,const std::string &name,uint32_t bg){
    if(argc!=5)return;ensureDirectory(argv[4]);std::vector<uint8_t> rgb(size_t(b.width)*b.height*3);
    for(size_t px=0;px<b.pixels.size();px+=4)for(int c=0;c<3;++c)rgb[px/4*3+c]=std::min(255,int(b.pixels[px+2-c])+int((bg>>(16-c*8))&255)*(255-b.pixels[px+3])/255);
    assert(stbi_write_png(join(argv[4],name+".png").c_str(),b.width,b.height,3,rgb.data(),b.width*3));
  };
  assert(latex::expand(R"(\num[round-mode=places,round-precision=2]{9.995})")=="10.00");
  assert(latex::expand(R"(\num[round-mode=places,round-precision=0]{-2.5})")=="-2");
  assert(latex::expand(R"(\num[round-mode=figures,round-precision=2]{0.00999})")=="0.010");
  assert(latex::expand(R"(\num[separate-uncertainty=true]{1.23(4)})")==R"(1.23\pm 0.04)");
  assert(latex::expand(R"(\sisetup{output-decimal-marker={,}}\num{1.2})")=="1{,}2");
  assert(latex::expand(R"(\num{1.2})")=="1.2");
  assert(latex::expand(R"(\newcommand{\abs}[1]{#1^2}\abs{x})")=="x^2");
  assert(latex::expand(R"(\div)")==R"(\div)");
  auto deriv=rich.formula(R"(\dv{f}{x})",21),flat=rich.formula(R"(\dv*{f}{x})",21);assert(deriv.pixels!=flat.pixels&&deriv.height>flat.height);
  for(const auto &test:fixtures.at("cases")){
    std::cerr<<test.at("category").get<std::string>()<<":"<<test.at("id").get<std::string>()<<"\n";
    for(auto color:{0xd5d7deu,0x30343bu}){
      const auto source=test.at("source").get<std::string>();const auto b=rich.formula(source,21,color);assert(!b.pixels.empty()&&b.width>0&&b.height>0&&b.baseline>0);
      bool ink=false;for(size_t px=0;px<b.pixels.size();px+=4){ink|=b.pixels[px+3]!=0;assert(!(b.pixels[px+2]>180&&b.pixels[px+1]<60&&b.pixels[px]<60));}assert(ink);
      if(test.count("equivalent")){const auto other=rich.formula(test.at("equivalent").get<std::string>(),21,color);assert(b.width==other.width&&b.height==other.height&&b.baseline==other.baseline&&b.pixels==other.pixels);}
      emit(b,test.at("category").get<std::string>()+"-"+std::to_string(&test-&fixtures.at("cases")[0])+"-"+std::to_string(color),color==0xd5d7de?0x15171d:0xffffff);
    }
  }
  auto explanation=rich.formula(R"(\begin{align}a&=b\\\intertext{Explanation 中文 $x$}c&=d\end{align})",21,0x30343b);
  for(size_t px=0;px<explanation.pixels.size();px+=4)assert(explanation.pixels[px]<=60&&explanation.pixels[px+1]<=60&&explanation.pixels[px+2]<=60);
  auto top=rich.formula(R"(\begin{multlined}[t]a\\b\end{multlined})",21),bottom=rich.formula(R"(\begin{multlined}[b]a\\b\end{multlined})",21);assert(top.height==bottom.height&&top.baseline<bottom.baseline);
  const std::vector<std::string> documents={
    R"(公式引用 \eqref{b} 与 \eqref{group}。
\begin{subequations}\label{group}\begin{align}a&=b\label{a}\\\intertext{For all $x>0$，由条件可得}c&=d\label{b}\\\shortintertext{therefore}e&=f\end{align}\end{subequations}

$$\underbracket[1pt][3pt]{x+y}_{n}+\begin{dcases}\frac12&x>0\\0&x<0\end{dcases}$$)",
    R"(物理与单位

$$\pdv[2,3]{f}{x}{y}+\matrixel{\psi}{A}{\phi}+\eval{\frac{x^2}{2}}_0^1$$

$$\SI[separate-uncertainty=true,per-mode=fraction]{1.234(56)e3}{\joule\per\mole\kelvin}$$

$$\num[round-mode=figures,round-precision=2]{0.00999}+\pu{75.3 J // mol K}$$)"};
  Json matrix=Json::array();
  for(auto mode:{theme::Mode::Dark,theme::Mode::Light})for(int scale:{75,100,125,150})for(auto screen:std::vector<std::pair<int,int>>{{960,266},{640,260},{568,1210}}){
    theme::setMode(mode);const UiMetrics metrics(100,scale);rich.setMetrics(metrics);const int size=metrics.dp(21),pad=metrics.dp(12),width=screen.first-pad*2,visible=screen.second-pad*2;
    for(size_t d=0;d<documents.size();++d){auto document=parseMarkdown(documents[d]);assert(document.formulaDiagnostics.empty());rich.prepare(document,width,size);
      std::vector<int> ys,heights;int total=0;size_t hits=0;
      for(size_t i=0;i<document.blocks.size();++i){const auto &block=document.blocks[i];total+=rich.blockGap(i?&document.blocks[i-1]:nullptr,block,size);ys.push_back(total);auto info=rich.layout(block,width,size,true);heights.push_back(info.height);total+=info.height;hits+=info.textHits.size();
        for(const auto &hit:info.textHits)assert(hit.begin<=hit.end&&hit.end<=blockPlainText(block).size());}
      for(int edge:{0,1}){
        const int top=edge?std::max(0,total-visible):0;Bitmap image;image.width=screen.first;image.height=screen.second;image.pixels.resize(size_t(image.width)*image.height*4);
        auto surface=cairo_image_surface_create_for_data(image.pixels.data(),CAIRO_FORMAT_ARGB32,image.width,image.height,image.width*4);auto context=cairo_create(surface);const auto bg=theme::palette().background;
        cairo_set_source_rgb(context,((bg>>16)&255)/255.,((bg>>8)&255)/255.,(bg&255)/255.);cairo_paint(context);cairo_rectangle(context,pad,pad,width,visible);cairo_clip(context);
        for(size_t i=0;i<document.blocks.size();++i){const int start=std::max(0,top-ys[i]),end=std::min(heights[i],top+visible-ys[i]);if(end<=start)continue;auto b=rich.paint(document.blocks[i],width,start,end-start,size);
          auto part=cairo_image_surface_create_for_data(b.pixels.data(),CAIRO_FORMAT_ARGB32,b.width,b.height,b.width*4);cairo_set_source_surface(context,part,pad,pad+ys[i]+start-top);cairo_paint(context);cairo_surface_destroy(part);}
        cairo_surface_flush(surface);cairo_destroy(context);cairo_surface_destroy(surface);
        emit(image,"matrix-"+std::to_string(screen.first)+"-"+std::to_string(scale)+"-"+theme::name(mode)+"-"+std::to_string(d)+"-"+std::to_string(edge),bg);
      }
      matrix.push_back({{"width",screen.first},{"height",screen.second},{"scale",scale},{"theme",theme::name(mode)},{"scene",d},{"content_height",total},{"hits",hits}});
    }
  }
  rich.setMetrics(UiMetrics());theme::setMode(theme::Mode::Dark);
  if(argc==5){std::ofstream output(join(argv[4],"matrix.json"));output<<matrix.dump(2);}
  for(const auto &test:fixtures.at("cases")){bool rejected=false;try{rich.formula(test.at("source").get<std::string>()+"{",21);}catch(const std::exception &){rejected=true;}assert(rejected);}
  for(const auto &source:fixtures.at("invalid")){bool rejected=false;try{rich.formula(source.get<std::string>(),21);}catch(const std::exception &){rejected=true;}if(!rejected)std::cerr<<"UNEXPECTED_VALID "<<source<<"\n";assert(rejected);}
  std::cout<<"LATEX_COMMON_COMMANDS_PARAMETERS_EQUIVALENCE_AND_INVALID_CASES_OK\n";
}
