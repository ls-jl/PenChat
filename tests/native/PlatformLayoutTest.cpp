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
  EditorLayout compact(640,260);assert(!compact.scaled&&compact.compactFull&&compact.width==640&&compact.height==260&&!compact.portrait);
  assert(compact.fromNativeX(320)==320&&compact.fromNativeY(130)==130);
  assert(compact.keySpan==640&&compact.textY==0&&compact.textHeight==44&&compact.keyTop==48);
  assert(compact.rowHeight>=44&&compact.keyWidth>=44&&compact.bottomHeight>=44);
  const EditorLayout expanded(640,260,false,false,false,true);
  assert(expanded.compact&&!expanded.compactFull&&expanded.scaled&&expanded.width==960&&expanded.split==584);
  assert(expanded.fromNativeX(320)==480&&expanded.fromNativeY(130)==133);
  assert(compact.textX+compact.textWidth<=compact.width);
  for(auto a:compact.actions())assert(a.x>=0&&a.x+a.width<=compact.width&&a.width>=44);
  for(auto size:{std::make_pair(640,260),std::make_pair(720,280)}) {
    const EditorLayout full(size.first,size.second);
    assert(full.compactFull&&!full.scaled&&full.keySpan==size.first);
    assert(full.textHeight==44&&full.rowHeight>=44&&full.keyWidth>=44);
    for(auto a:full.actions())assert(a.x>=0&&a.x+a.width<=full.width&&a.y+44<=full.height);
  }
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
    assert(bubble.x>=4&&bubble.x+bubble.width<=swap.width-4);
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
  // Camera: compact bottom row in tall workspaces, a side rail when wide.
  for(auto size:{std::make_pair(568,1210),std::make_pair(600,1280),std::make_pair(960,266),std::make_pair(936,280),std::make_pair(640,260)}) {
    const AppLayout layout{size.first,size.second};
    for(bool quarter:{false,true}) {
      const int w=!layout.portrait()&&quarter?size.second:size.first-(layout.portrait()?0:90);
      const int h=!layout.portrait()&&quarter?size.first-56:size.second;
      const auto c=layout.cameraWorkspace(w,h,quarter);
      for(const auto r:{c.preview,c.direction,c.shutter,c.turn})
        assert(r.x>=0&&r.y>=0&&r.width>=44&&r.height>=44&&r.x+r.width<=w&&r.y+r.height<=h);
      assert(c.shutter.width==c.shutter.height);
      if(layout.portrait()||quarter) {
        assert(c.direction.x+c.direction.width<c.shutter.x);
        assert(c.shutter.x+c.shutter.width<c.turn.x);
        assert(c.direction.y==c.turn.y&&c.direction.height==c.turn.height);
        assert(c.preview.y+c.preview.height<c.direction.y);
        assert(std::abs(2*c.shutter.x+c.shutter.width-w)<=1);
        if(layout.portrait())assert(c.shutter.y+c.shutter.height<=h-layout.safeBottom());
      } else {
        assert(c.direction.y+c.direction.height<c.shutter.y);
        assert(c.shutter.y+c.shutter.height<c.turn.y);
        assert(c.preview.x+c.preview.width<c.direction.x);
      }
      const int cropW=!layout.portrait()&&quarter?size.second:size.first;
      const int cropH=!layout.portrait()&&quarter?size.first:size.second;
      const auto crop=layout.cropWorkspace(cropW,cropH);
      for(const auto r:{crop.viewport,crop.cancel,crop.reset,crop.album,crop.confirm})
        assert(r.x>=0&&r.y>=0&&r.width>=44&&r.height>=44&&r.x+r.width<=cropW&&r.y+r.height<=cropH);
      if(layout.portrait()) {
        assert(crop.viewport.x>=layout.safeSide()&&crop.viewport.y>=layout.safeTop());
        assert(crop.viewport.y+crop.viewport.height+16<=crop.album.y);
        assert(crop.album.y+crop.album.height+12<=crop.cancel.y);
        assert(crop.cancel.x+crop.cancel.width+12<=crop.reset.x);
        assert(crop.reset.x+crop.reset.width+12<=crop.confirm.x);
        assert(crop.confirm.x+crop.confirm.width<=cropW-layout.safeSide());
        assert(crop.confirm.y+crop.confirm.height<=cropH-layout.safeBottom()-12);
      }
    }
  }
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
