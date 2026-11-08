#include "editor_layout.hpp"
#include <cassert>
#include <iostream>
#include <string>
int main() {
  using namespace penchat;
  for(auto size:{std::make_pair(960,266),std::make_pair(936,280),std::make_pair(568,1210),std::make_pair(600,1280)}) {
    const EditorLayout l{size.first,size.second};const AppLayout safe{size.first,size.second};
    const int side=std::max(4,safe.safeSide()),top=std::max(4,safe.safeTop()),bottom=std::max(4,safe.safeBottom());
    if(l.portrait) {
      const auto globe=l.globeKey();
      for(const auto &action:l.actions(true))if(std::string(action.name)=="done") {
        assert(2*globe.x+globe.width-1+2*action.x+action.width-1==2*(size.first-1));
        assert(2*globe.y+globe.height-1==2*action.y+43);
        assert(action.width>=44&&action.x>=safe.safeSide());
      }
    }
    for(int x:{-100,0,side,size.first/2,size.first-side,size.first,size.first+100})
      for(int y:{-100,0,top,size.second/2,size.second-bottom,size.second,size.second+100}) {
        const auto p=l.magnifierPopup(x,y);
        assert(p.x>=side&&p.y>=top&&p.x+p.width<=size.first-side&&p.y+p.height<=size.second-bottom);
        assert(p.width==180&&p.height==58);
      }
    const auto middle=l.magnifierPopup(size.first/2,size.second/2);
    assert(middle.x==size.first/2-90);
    assert(middle.y==std::max(top,size.second/2-96-(l.portrait?58:0)));
    for(auto edges:{std::make_pair(side+12,size.first-side-12),std::make_pair(100,160)}) {
      for(int x:{-100,edges.first,edges.second,size.first+100}) {
        const auto p=l.magnifierPopup(x,size.second/2,edges.first,edges.second);
        assert(p.x>=edges.first&&p.x+p.width<=edges.second);
        assert(p.width==std::min(180,edges.second-edges.first));
      }
    }
  }
  // A constrained viewport must resize the lens instead of preserving dimensions
  // that cannot fit. Safe edges take precedence even over its normal height.
  for(auto size:{std::make_pair(120,42),std::make_pair(40,120),std::make_pair(1,1)}) {
    const EditorLayout l{size.first,size.second};
    const auto p=l.magnifierPopup(size.first,size.second);
    assert(p.x>=0&&p.y>=0&&p.width>0&&p.height>0&&p.x+p.width<=size.first&&p.y+p.height<=size.second);
    assert(p.width<180&&p.height<=58);
  }
  std::cout<<"Magnifier: all safe edges, off-screen anchors, preferred size/offset and constrained viewports passed\n";
}
