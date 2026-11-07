#include "common.hpp"
#include "editor_layout.hpp"
#include "native_touch.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
int main() {
  Json config={{"screen",{{"width",260},{"height",640},{"direction",270},{"tp_direction",270},{"tp_xoffset",110},{"tp_yoffset",0}}}};
  auto s=Screen::fromConfig(config,true);assert(s.width==640&&s.height==260&&s.panelWidth==260&&s.panelHeight==640);
  for(auto p:{std::make_pair(0,0),std::make_pair(639,259),std::make_pair(320,130)})
    assert(logicalTouch(s,p.second+110,639-p.first)==p);
  config["screen"]["width"]=240;config["screen"]["height"]=720;
  auto other=Screen::fromConfig(config,true);assert(other.width==720&&other.height==240);
  auto logical=Screen::fromConfig(config);assert(logical.width==240&&logical.height==720);
  EditorLayout compact(640,260);assert(compact.scaled&&compact.width==960&&compact.height==266&&!compact.portrait);
  assert(compact.fromNativeX(320)==480&&compact.fromNativeY(130)==133);
  assert(compact.textX+compact.textWidth<=compact.width);
  for(auto a:compact.actions())assert(a.x>=compact.textX&&a.x+a.width<=compact.width);
  EditorLayout landscape(960,266);assert(!landscape.scaled&&landscape.split==584&&landscape.textX==592);
  EditorLayout portrait(568,1210);assert(!portrait.scaled&&portrait.portrait&&portrait.docked);
  bool rejected=false;config["screen"]["direction"]=45;
  try{Screen::fromConfig(config,true);}catch(...){rejected=true;}assert(rejected);
  std::cout<<"PLATFORM_GEOMETRY_AND_SHARED_EDITOR_OK\n";
}
