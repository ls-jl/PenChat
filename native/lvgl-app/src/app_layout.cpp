#include "app.hpp"
namespace penchat {
void App::arrangeModal() {
  if(!modal_||!layout().portrait())return;
  lv_obj_update_layout(modal_);
  const int w=lv_obj_get_width(modal_);
  lv_area_t bounds;lv_obj_get_coords(modal_,&bounds);
  int buttonTop=screen_.height-66;
  for(size_t i=0;i<modalButtons_.size();++i) {
    auto area=layout().edgeRect({bounds.x1+16,screen_.height-(layout().safety().enabled()?layout().safeBottom()+44:66)-int(i)*52,w-32,44});
    auto b=modalButtons_[i];lv_obj_set_pos(b,area.x-bounds.x1,area.y-bounds.y1);lv_obj_set_size(b,area.width,area.height);
    buttonTop=std::min(buttonTop,area.y);
    auto l=lv_obj_get_child(b,0);if(l)lv_obj_set_width(l,area.width-24);
  }
  if(modalScroll_)lv_obj_set_height(modalScroll_,std::max(80,layout().safety().enabled()?
    buttonTop-bounds.y1-int(lv_obj_get_y(modalScroll_))-10:screen_.height-76-int(modalButtons_.size())*52));
}
void App::portraitPage() {
  if (view_ == "balances") return; // The balance reader measures full-width rows itself.
  // Short landscape has too little label width for the wide-screen two-column
  // forms (especially after reserving switches and arrows). Reuse the measured
  // single-column flow, not smaller type or clipped captions. Readers/grids keep
  // their own layout; the font slider also keeps its endpoint labels together.
  const bool compactForm=layout().compact() &&
      (view_=="settings" || view_=="presets" || view_=="preset_edit" ||
       view_=="keyboard_settings" || view_=="http_settings" ||
       view_=="feedback" || view_=="update" || view_=="transfer" ||
       view_=="provider" || view_=="model" || view_=="api_compatibility" ||
       view_=="anthropic_connection");
  if(!pageContent_||!pageHeader_||(!layout().portrait()&&!compactForm)||view_=="provider_models"||view_=="album"||view_=="archives"||view_=="archive_chat"||(view_=="provider"&&providerForm_.empty()))return;
  lv_obj_update_layout(pageContent_);
  struct Item {lv_obj_t *o;int x,y;};std::vector<Item> items;
  std::map<lv_obj_t *,lv_obj_t *> anchors;
  for(unsigned i=0;i<lv_obj_get_child_count(pageContent_);++i){auto o=lv_obj_get_child(pageContent_,i);
    if(lv_obj_is_floating(o)&&lv_obj_get_user_data(o))anchors[static_cast<lv_obj_t *>(lv_obj_get_user_data(o))]=o;
    else items.push_back({o,lv_obj_get_x(o),lv_obj_get_y(o)});}
  std::stable_sort(items.begin(),items.end(),[](const Item &a,const Item &b){return a.y==b.y?a.x<b.x:a.y<b.y;});
  int y=8;const int width=layout().contentWidth();
  for(const auto &item:items) {
    auto o=item.o;const int rowWidth=width-(anchors.count(o)?56:0);
    lv_obj_set_pos(o,16,y);lv_obj_set_width(o,rowWidth);
    if(anchors.count(o)){auto control=anchors.at(o);lv_obj_set_pos(control,16+width-44,y+6);lv_obj_set_size(control,44,44);}
    if(lv_obj_check_type(o,&lv_label_class)) {
      lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_height(o,LV_SIZE_CONTENT);
    } else {
      // Buttons and field rows keep their own interaction and state, only their
      // measured frame changes. Labels preserve the same typography.
      for(unsigned i=0;i<lv_obj_get_child_count(o);++i){auto child=lv_obj_get_child(o,i);
        if(lv_obj_check_type(child,&lv_label_class)) {
          const int padding=lv_obj_get_style_align(child,LV_PART_MAIN)==LV_ALIGN_LEFT_MID?52:28;
          lv_obj_set_width(child,std::max(44,rowWidth-padding));
        }
      }
      for(auto &entry:settingsControls_)if(entry.second.row==o){
        auto &c=entry.second;lv_obj_set_width(c.title,rowWidth-80);lv_obj_set_width(c.detail,rowWidth-80);
        lv_obj_set_x(c.indicator,rowWidth-(c.toggle?56:50));
      }
    }
    lv_obj_update_layout(o);y+=std::max(20,int(lv_obj_get_height(o)))+10;
  }
  const int viewport=std::max(0,int(lv_obj_get_height(body_))-int(lv_obj_get_y(pageContent_))-12);
  lv_obj_set_style_min_height(pageContent_,viewport,0);
  lv_obj_set_height(pageContent_,std::max(y,viewport));
}
} // namespace penchat
