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
  // Swapping is a translation of regions, not a mirror of keys or content.
  for(auto size:{std::make_pair(960,266),std::make_pair(936,280)}) {
    const EditorLayout normal(size.first,size.second),swap(size.first,size.second,false,false,true);
    assert(swap.swapped&&!normal.swapped&&swap.keyInset==normal.width-normal.split);
    assert(swap.keySpan==normal.keySpan&&swap.keyWidth==normal.keyWidth);
    assert(swap.textX==8&&swap.textWidth==normal.textWidth&&swap.textY==normal.textY);
    assert(swap.textRight()+8==swap.keyInset);
    assert(swap.editorPanelX()==0&&swap.editorPanelWidth()==normal.editorPanelWidth());
    const auto oldActions=normal.actions(),newActions=swap.actions();
    assert(oldActions.size()==newActions.size());
    for(size_t i=0;i<oldActions.size();++i) {
      assert(std::string(newActions[i].name)!="official");
      assert(newActions[i].x+normal.split==oldActions[i].x);
      assert(newActions[i].width==oldActions[i].width);
      assert(newActions[i].x>=swap.textX&&newActions[i].x+newActions[i].width<=swap.textRight());
    }
    const EditorLayout::Popup cap{swap.keyInset+4,6,54,56};
    const auto hit=swap.keyTouchBounds(cap,true,false);
    assert(hit.x==swap.keyInset&&hit.x+hit.width<=swap.keyInset+swap.keySpan);
    const auto bubble=swap.magnifierPopup(swap.textRight(),120,swap.textX,swap.textRight());
    assert(bubble.x>=swap.textX&&bubble.x+bubble.width<=swap.textRight());
    const EditorLayout selection(size.first,size.second,true,false,true);
    assert(!selection.swapped&&selection.keyInset==0);
  }
  for(auto size:{std::make_pair(568,1210),std::make_pair(640,260)}) {
    const EditorLayout normal(size.first,size.second),ignored(size.first,size.second,false,false,true);
    assert(!ignored.swapped&&normal.keyInset==ignored.keyInset&&normal.textX==ignored.textX);
    assert(normal.scaleX()==ignored.scaleX()&&normal.scaleY()==ignored.scaleY());
  }
  assert(classifyLayout(568,1210)==LayoutClass::Portrait);
  assert(classifyLayout(936,280)==LayoutClass::WideLandscape);
  assert(classifyLayout(960,266)==LayoutClass::WideLandscape);
  assert(classifyLayout(640,260)==LayoutClass::CompactLandscape);
  assert(classifyLayout(500,500)==LayoutClass::CompactLandscape);
  assert(classifyLayout(900,300)==LayoutClass::WideLandscape);
  assert(classifyLayout(899,300)==LayoutClass::CompactLandscape);
  assert(classifyLayout(600,200)==LayoutClass::WideLandscape);
  Json y02={{"screen",{{"width",936},{"height",280},{"direction",270},{"tp_direction",270}}}};
  const auto y02Screen=Screen::fromConfig(y02);
  assert(y02Screen.width==936&&y02Screen.height==280);
  {
    for(auto size:{std::make_pair(568,1210),std::make_pair(936,280),std::make_pair(960,266),std::make_pair(640,260)}) {
      const EditorLayout editor(size.first,size.second);
      for(const auto &a:editor.actions())assert(a.x>=0&&a.x+a.width<=editor.width&&a.y>=0&&a.y+44<=editor.height);
      assert(editor.textWidth>0&&editor.textX+editor.textWidth<=editor.width);
    }
  }
  bool rejected=false;config["screen"]["direction"]=45;
  try{Screen::fromConfig(config,true);}catch(...){rejected=true;}assert(rejected);
  std::cout<<"PLATFORM_GEOMETRY_AND_SHARED_EDITOR_OK\n";
}
