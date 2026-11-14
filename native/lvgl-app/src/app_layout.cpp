#include "app.hpp"
#include "empty_state.hpp"
namespace penchat {
void App::arrangeModal() {
  if(!modal_||!layout().portrait())return;
  lv_obj_update_layout(modal_);
  const int w=lv_obj_get_width(modal_);
  lv_area_t bounds;lv_obj_get_coords(modal_,&bounds);
  int buttonTop=screen_.height-uiMetrics_.dp(66);
  for(size_t i=0;i<modalButtons_.size();++i) {
    auto area=layout().edgeRect({bounds.x1+uiMetrics_.dp(16),screen_.height-(layout().safety().enabled()?layout().safeBottom()+uiMetrics_.dp(44):uiMetrics_.dp(66))-int(i)*uiMetrics_.dp(52),w-uiMetrics_.dp(32),uiMetrics_.dp(44)});
    auto b=modalButtons_[i];lv_obj_set_pos(b,area.x-bounds.x1,area.y-bounds.y1);lv_obj_set_size(b,area.width,area.height);
    buttonTop=std::min(buttonTop,area.y);
    auto l=lv_obj_get_child(b,0);if(l)lv_obj_set_width(l,area.width-uiMetrics_.dp(24));
  }
  if(modalScroll_)lv_obj_set_height(modalScroll_,std::max(uiMetrics_.dp(80),layout().safety().enabled()?
    buttonTop-bounds.y1-int(lv_obj_get_y(modalScroll_))-uiMetrics_.dp(10):screen_.height-uiMetrics_.dp(76)-int(modalButtons_.size())*uiMetrics_.dp(52)));
}
void App::portraitPage() {
  if (view_ == "settings" || view_ == "context_summary" || view_ == "ui_scale") return; // Own section flow preserves row and group spacing in all layouts.
  // Short landscape has too little label width for the wide-screen two-column
  // forms (especially after reserving switches and arrows). Reuse the measured
  // single-column flow, not smaller type or clipped captions. Readers/grids keep
  // their own layout; the font slider also keeps its endpoint labels together.
  const bool compactForm=layout().compact() &&
      (view_=="presets" || view_=="preset_edit" ||
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
  const auto form = uiMetrics_.form();
  const auto settings = uiMetrics_.settings();
  using Control=decltype(settingsControls_)::mapped_type;
  std::map<lv_obj_t *,Control *> controls;
  for(auto &entry:settingsControls_)controls.emplace(entry.second.row,&entry.second);
  const int width=layout().contentWidth();
  // Width-dependent text measurement is one batch. Positions do not affect line
  // wrapping, so no per-item forced layout or repeated settings-table scan.
  for(const auto &item:items) {
    auto o=item.o;const int rowWidth=width-(anchors.count(o)?form.actionReserve:0);
    lv_obj_set_x(o,form.inset);lv_obj_set_width(o,rowWidth);
    if(anchors.count(o))lv_obj_set_size(anchors.at(o),form.actionSize,form.actionSize);
    if(arrangeStatusState(o,rowWidth))continue;
    if(lv_obj_has_flag(o,LV_OBJ_FLAG_USER_1))continue;
    if(lv_obj_check_type(o,&lv_label_class)) {
      lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_height(o,LV_SIZE_CONTENT);
    } else {
      for(unsigned i=0;i<lv_obj_get_child_count(o);++i) {
        auto child=lv_obj_get_child(o,i);
        if(lv_obj_check_type(child,&lv_label_class)) {
          const int padding=lv_obj_get_style_align(child,LV_PART_MAIN)==LV_ALIGN_LEFT_MID?form.leadingCaptionPadding:form.captionPadding;
          lv_obj_set_width(child,std::max(form.actionSize,rowWidth-padding));
        }
      }
      const auto found=controls.find(o);
      if(found!=controls.end()) {
        auto &c=*found->second;
        lv_obj_set_width(c.title,std::max(1,rowWidth-settings.textTrailing));
        lv_obj_set_width(c.detail,std::max(1,rowWidth-settings.textTrailing));
        lv_obj_set_x(c.indicator,rowWidth-(c.toggle?settings.toggleTrailing:settings.arrowTrailing));
      }
    }
  }
  lv_obj_update_layout(pageContent_);++formLayoutBatches_;
  int y=form.top;
  const int viewport=std::max(0,int(lv_obj_get_height(body_))-int(lv_obj_get_y(pageContent_))-form.bottom);
  for(const auto &item:items) {
    auto o=item.o;lv_obj_set_y(o,y);
    if(anchors.count(o))lv_obj_set_pos(anchors.at(o),form.inset+width-form.actionSize,y+form.actionTop);
    if(statusStateFillsViewport(o))arrangeStatusState(o,lv_obj_get_width(o),std::max(1,viewport-y));
    const int height=lv_obj_get_height(o);
    if(lv_obj_has_flag(o,LV_OBJ_FLAG_USER_1)) { if(height>0)y+=height+form.gap; }
    else y+=std::max(form.minimumItemHeight,height)+form.gap;
    ++formLayoutRows_;
  }
  lv_obj_set_style_min_height(pageContent_,viewport,0);
  lv_obj_set_height(pageContent_,std::max(y,viewport));
}
} // namespace penchat
