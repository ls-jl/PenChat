#include "keyboard_style.hpp"
#include "editor_layout.hpp"
#include "candidate_rows.hpp"
#include "platform.hpp"
#include <cassert>
#include <iostream>
#include <set>
using namespace penchat;
static const KeyPlacement &find(const std::vector<KeyPlacement> &keys,const std::string &id) {
  for(const auto &key:keys)if(key.spec.id==id)return key;
  throw std::runtime_error("Missing key: "+id);
}
int main(int argc,char **argv) {
  assert(argc==3);
  const auto &defaults=keyboardDesign();
  KeyboardDesign design=defaults;
  design.base.radius=13;
  design.roles[size_t(KeyboardRole::Function)].background=theme::accent;
  design.roles[size_t(KeyboardRole::Function)].disabledOpacity=102;
  design.variants[size_t(KeyboardVariant::DeleteKey)].background=theme::danger;
  design.profiles[size_t(KeyboardView::Portrait)].fonts[size_t(KeyboardFont::Character)]=28;
  assert(design.font(KeyboardFont::Character,KeyboardView::Portrait)==28);
  assert(design.font(KeyboardFont::Character,KeyboardView::Wide)==23);
  assert(design.font(KeyboardFont::Character,KeyboardView::Compact)==23);
  assert(design.resolve(KeyboardRole::Function).background==theme::accent);
  assert(design.resolve(KeyboardRole::Character).background==theme::surface);
  assert(design.resolve(KeyboardRole::Function,KeyboardVariant::DeleteKey).background==theme::danger);
  assert(design.resolve(KeyboardRole::Function,KeyboardVariant::DeleteKey,{true,false,false}).background==theme::border);
  const auto disabled=design.resolve(KeyboardRole::Function,KeyboardVariant::DeleteKey,{true,true,false});
  assert(disabled.background==theme::danger&&disabled.opacity==102);
  assert(design.resolve(KeyboardRole::Menu,{}, {false,false,true}).background==theme::surface);
  for(auto size:{std::make_pair(960,266),{936,280},{640,260},{720,280},{568,1210},{600,1280}})
    for(bool expanded:{false,true})for(bool swapped:{false,true}) {
      const EditorLayout layout(size.first,size.second,false,false,swapped,expanded,false,design);
      for(int mode=0;mode<3;++mode)for(bool nine:{false,true}) {
        if(nine&&mode)continue;
        auto model=keyboardKeys(mode,nine,layout.view());
        auto before=layout.placeKeys(model);
        for(const auto &key:before) {
          assert(design.resolve(key.spec.role,key.spec.variant).radius==13);
          assert(key.cap.width>0&&key.cap.height>0);
          assert(key.hit.contains(key.cap.x+key.cap.width/2,key.cap.y+key.cap.height/2));
        }
        const auto oldSpace=find(before,"space"),oldEnter=find(before,"enter");
        // One key's width changes without moving the row's final edge. Both its
        // visible and hit bounds grow, funded by the flexible space key.
        for(auto &key:model.bottom.keys)if(key.id=="enter")key.width=KeyWidth::fixed(oldEnter.cap.width+8);
        const auto after=layout.placeKeys(model);
        assert(find(after,"enter").cap.width==oldEnter.cap.width+8);
        assert(find(after,"enter").cap.x==oldEnter.cap.x-8);
        assert(find(after,"enter").hit.x==oldEnter.hit.x-8);
        assert(find(after,"space").cap.width==oldSpace.cap.width-8);
        assert(find(after,mode||nine?"mode":"q").cap.x==find(before,mode||nine?"mode":"q").cap.x);
        const auto oldFirst=model.bottom.keys.front().id,oldLast=model.bottom.keys.back().id;
        std::swap(model.bottom.keys.front(),model.bottom.keys.back());
        const auto reordered=layout.placeKeys(model);
        assert(find(reordered,oldLast).cap.x<find(reordered,oldFirst).cap.x);
        assert(find(reordered,oldLast).spec.action=="enter");
      }
    }
  // Layouts own resolved geometry; a temporary design never leaves a dangling reference.
  auto temporary=[] {KeyboardDesign d;d.composition.gap=7;return EditorLayout(960,266,false,false,false,false,false,d); }();
  assert(temporary.compositionCandidates(false,30).x==temporary.textX+37);
  const EditorLayout wide(960,266);
  auto impossible=keyboardKeys(0,false,wide.view());impossible.bottom.keys.front().width=KeyWidth::fixed(2000);
  bool rejected=false;try {wide.placeKeys(impossible);}catch(const std::invalid_argument &){rejected=true;}assert(rejected);

  lv_init();initializeFonts(argv[1],argv[2]);
  auto measure=[&](const KeyboardDesign &d) {
    return keyboardStyle::candidateWidth("你好世界",font(d.font(KeyboardFont::Candidate,KeyboardView::Wide)),false,d.candidates);
  };
  KeyboardDesign larger=defaults;larger.typography.sizes[size_t(KeyboardFont::Candidate)]=28;
  const int normalWidth=measure(defaults),largerWidth=measure(larger);
  assert(largerWidth>normalWidth);
  const int rowWidth=2*normalWidth;
  const auto normal=candidateRows({normalWidth,normalWidth},rowWidth,true);
  larger.candidates.height=52;larger.candidates.rowGap=6;
  const auto changed=candidateRows({largerWidth,largerWidth},rowWidth,true,larger.candidates);
  assert(normal[1].y==0&&changed[1].y==58);
  // The same role resolver used by widgets applies to real LVGL styles.
  auto display=lv_display_create(960,266);
  auto object=lv_obj_create(lv_screen_active());
  keyboardStyle::apply(object,KeyboardRole::Function,KeyboardVariant::DeleteKey,false,design);
  assert(lv_obj_get_style_radius(object,LV_PART_MAIN)==13);
  lv_obj_add_state(object,LV_STATE_DISABLED);
  assert(lv_obj_get_style_opa(object,LV_PART_MAIN)==102);
  lv_display_delete(display);lv_deinit();shutdownFonts();
  std::cout<<"Keyboard design: shared/role/variant/state overrides, profiles, key widths/order/hits, lifetime and real font measurement passed\n";
}
