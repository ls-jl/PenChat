#include "app.hpp"
namespace penchat {
void App::arrangeModal() {
  if(!modal_||!layout().portrait())return;
  lv_obj_update_layout(modal_);
  const int w=lv_obj_get_width(modal_);
  for(size_t i=0;i<modalButtons_.size();++i) {
    auto b=modalButtons_[i];lv_obj_set_pos(b,16,screen_.height-66-int(i)*52);lv_obj_set_size(b,w-32,44);
    auto l=lv_obj_get_child(b,0);if(l)lv_obj_set_width(l,w-56);
  }
  if(modalScroll_)lv_obj_set_height(modalScroll_,std::max(80,screen_.height-76-int(modalButtons_.size())*52));
}
void App::portraitPage() {
  const bool compactForm=layout().compact() &&
      (view_=="provider" || view_=="model" || view_=="api_compatibility" || view_=="anthropic_connection");
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
        if(lv_obj_check_type(child,&lv_label_class))lv_obj_set_width(child,std::max(44,rowWidth-28));
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
