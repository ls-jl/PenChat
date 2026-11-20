#include "theme.hpp"
#include "keyboard_design.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace penchat;
static double luminance(uint32_t c) {
  auto linear=[](unsigned v){double s=v/255.;return s<=.04045?s/12.92:std::pow((s+.055)/1.055,2.4);};
  return .2126*linear((c>>16)&255)+.7152*linear((c>>8)&255)+.0722*linear(c&255);
}
static double contrast(uint32_t a,uint32_t b) {auto x=luminance(a),y=luminance(b);return (std::max(x,y)+.05)/(std::min(x,y)+.05);}
int main() {
  assert(theme::parse("light")==theme::Mode::Light);
  assert(theme::parse("invalid")==theme::Mode::Dark);
  for(auto mode:{theme::Mode::Dark,theme::Mode::Light,theme::Mode::Dark}) {
    theme::setMode(mode);const auto &p=theme::palette();
    for(auto fg:{p.text,p.muted,p.richBody,p.richHeading})for(auto bg:{p.background,p.surface,p.popup})assert(contrast(fg,bg)>=4.5);
    assert(contrast(p.primaryText,p.text)>=4.5);
    assert(keyboardDesign().background==p.keyboardBackground);
    assert(keyboardDesign().style(KeyboardRole::Character).foreground==p.text);
    assert(keyboardDesign().style(KeyboardRole::Primary).foreground==p.primaryText);
  }
  std::cout<<"THEME_PALETTES_CONTRAST_AND_KEYBOARD_SWITCH_OK\n";
}
