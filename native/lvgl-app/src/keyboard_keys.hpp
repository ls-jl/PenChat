#pragma once
#include "keyboard_design.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace penchat {
struct KeyboardRect {
  int x,y,width,height;
  bool contains(int px,int py) const {return px>=x&&px<x+width&&py>=y&&py<y+height;}
};
enum class KeyWidthKind { Equal, Fixed, Relative, Remaining };
struct KeyWidth {
  KeyWidthKind kind=KeyWidthKind::Equal;
  double factor=1,offset=0,gapFactor=0;
  static KeyWidth fixed(int pixels) {return {KeyWidthKind::Fixed,0,double(pixels),0};}
  static KeyWidth relative(double factor,double offset=0,double gapFactor=0) {return {KeyWidthKind::Relative,factor,offset,gapFactor};}
  static KeyWidth remaining() {return {KeyWidthKind::Remaining,0,0,0};}
  double measure(double equal,int gap) const {
    return kind==KeyWidthKind::Equal?equal:kind==KeyWidthKind::Fixed?offset:
      kind==KeyWidthKind::Relative?std::lround(equal*factor+offset+gap*gapFactor):0;
  }
};
struct KeySpec {
  std::string id,caption,action,icon;
  KeyboardRole role=KeyboardRole::Character;
  KeyboardVariant variant=KeyboardVariant::Default;
  KeyWidth width;
};
struct KeyRow {
  std::vector<KeySpec> keys;
  bool centered=false,edgeHits=false,pixelAdvance=false;
};
struct KeyboardKeys {
  std::array<KeyRow,3> rows;
  KeyRow bottom;
  bool nine=false;
};
struct KeyPlacement { KeySpec spec;KeyboardRect cap,hit; };
inline KeySpec keyboardKey(const std::string &action,const std::string &caption,
                           KeyWidth width={},const std::string &icon="") {
  const bool character=action.size()==1||action.find("t9:")==0;
  return {action,caption,action,icon,character?KeyboardRole::Character:KeyboardRole::Function,
          action=="delete"?KeyboardVariant::DeleteKey:KeyboardVariant::Default,width};
}
// The template declares keys and order, never performs an input action. Shift,
// punctuation and translated captions are resolved by the existing UI state.
inline KeyboardKeys keyboardKeys(int mode,bool nine,KeyboardView view) {
  KeyboardKeys result;result.nine=nine;
  const bool compact=view==KeyboardView::Compact,portrait=view==KeyboardView::Portrait;
  if(nine) {
    const char *groups[]={"1 标点","2 ABC","3 DEF","4 GHI","5 JKL","6 MNO","7 PQRS","8 TUV","9 WXYZ"};
    for(int n=0;n<9;++n)result.rows[n/3].keys.push_back(keyboardKey(n?"t9:"+std::to_string(n+1):"punctuation",groups[n]));
    result.bottom.keys.push_back(keyboardKey("mode","123",KeyWidth::fixed(compact?48:68)));
    if(!portrait)result.bottom.keys.push_back(keyboardKey("language","",KeyWidth::fixed(compact?44:64),"globe"));
    result.bottom.keys.push_back(keyboardKey("space","拼音",KeyWidth::remaining()));
    result.bottom.keys.push_back(keyboardKey("delete","",KeyWidth::fixed(compact?64:88),"delete"));
    result.bottom.keys.push_back(keyboardKey("enter","",KeyWidth::fixed(compact?64:104),"enter"));
  } else {
    const std::array<std::string,3> letters=mode==0?std::array<std::string,3>{{"qwertyuiop","asdfghjkl","zxcvbnm"}}:
      mode==1?std::array<std::string,3>{{"1234567890","@#%&*()-+","/=:;!?_"}}:
              std::array<std::string,3>{{"[]{}<>^~|\\","$&*_+=`\"'","!?:;/-@"}};
    result.rows[1].centered=true;result.rows[1].edgeHits=mode==0;
    result.rows[2].keys.push_back(keyboardKey(mode?"symbols":"shift",mode?(mode==1?"#+=":"123"):"",
                                            KeyWidth::relative(1,0,.5),mode?"":"shift"));
    for(int r=0;r<3;++r)for(char c:letters[r])result.rows[r].keys.push_back(keyboardKey(std::string(1,c),std::string(1,c)));
    result.rows[2].keys.push_back(keyboardKey("delete","",KeyWidth::remaining(),"delete"));
    const auto modeWidth=compact?KeyWidth::fixed(48):KeyWidth::relative(1,14);
    const auto punctuationWidth=compact?KeyWidth::fixed(44):KeyWidth::relative(1,10);
    result.bottom.keys.push_back(keyboardKey("mode",mode?"ABC":"123",modeWidth));
    result.bottom.keys.push_back(keyboardKey(",",",",punctuationWidth));
    if(!portrait)result.bottom.keys.push_back(keyboardKey("language","",punctuationWidth,"globe"));
    result.bottom.keys.push_back(keyboardKey("space","",KeyWidth::remaining()));
    result.bottom.keys.push_back(keyboardKey(".",".",punctuationWidth));
    result.bottom.keys.push_back(keyboardKey("enter","",compact?KeyWidth::fixed(64):KeyWidth::relative(2,-4),"enter"));
  }
  result.bottom.pixelAdvance=true;
  return result;
}
} // namespace penchat
