#include "popup_style.hpp"
#include "markdown.hpp"
#include "keyboard_design.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
int main(int argc,char **argv) {
  assert(theme::background==0x000000 && theme::keyboardCanvas==0x212121);
  assert(keyboardDesign().background==0x111111);
  assert(keyboardDesign().style(KeyboardRole::Primary).foreground==0x212121);
  assert(keyboardDesign().style(KeyboardRole::Composition).background==0x212121);
  for(const auto size:{std::make_pair(960,266),std::make_pair(640,260),std::make_pair(568,1210),std::make_pair(320,640)}) {
    for(bool rounded:{false,true})for(int x:{0,size.first/2,size.first-1})for(int y:{0,size.second/2,size.second-1}) {
      const auto p=placeContextPopup(size.first,size.second,rounded,{x,y,1,1},320,600);
      const auto safe=popupSafeArea(size.first,size.second,rounded);
      assert(p.x>=safe.x && p.y>=safe.y && p.x+p.width<=size.first-safe.x);
      assert(p.y+p.height<=safe.y+safe.height && p.height<=safe.height*4/5);
      assert(p.width>=44 && p.height>=44);
      if(rounded) {
        const double r=std::ceil(std::min(size.first,size.second)*.20);
        for(int px:{p.x,p.x+p.width-1})for(int py:{p.y,p.y+p.height-1}) {
          const double a=std::min(px,size.first-1-px),b=std::min(py,size.second-1-py);
          if(a<r && b<r)assert((r-a)*(r-a)+(r-b)*(r-b)<=r*r);
        }
      }
    }
  }
  assert(argc>=3);RichText rich(argv[1],argv[2]);
  Block code;code.kind="code";code.runs={{"    ","",Mono}};
  const auto pixels=rich.paint(code,160,0,40,21);
  const auto i=(10*pixels.width+10)*4;
  assert(pixels.pixels[i]==0x18 && pixels.pixels[i+1]==0x18 && pixels.pixels[i+2]==0x18);
  code.kind="p";
  // Leading whitespace is deliberately omitted in paragraphs; use an inline
  // non-space glyph with a blank corner of its character box instead.
  code.runs={{"X","",Mono}};const auto inlineGlyph=rich.paint(code,160,0,40,21);
  assert(inlineGlyph.pixels[i]==0x30 && inlineGlyph.pixels[i+1]==0x30 && inlineGlyph.pixels[i+2]==0x30);
  std::cout<<"Popup safe bounds, Y07 corner mask, keyboard palette isolation and code backgrounds passed\n";
}
