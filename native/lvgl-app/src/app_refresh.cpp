#include "app.hpp"
#include "refresh_settings.hpp"
namespace penchat {
void App::requestRefreshRate(int hz) {
  if(view_!="settings"||!visible_||!inputFocused_||!refreshRequest_.empty()||
      !themeRequest_.empty()||editor_.active||keyboard_||!refresh::supportedRate(hz))return;
  if(hz==refreshSaved_&&hz==display_.refreshRate())return;
  input_.cancel();touchDown_=false;
  refreshTarget_=hz;refreshError_=false;refreshRequest_=uid("refresh-rate");
  for(auto &entry:settingsControls_)lv_obj_add_state(entry.second.row,LV_STATE_DISABLED);
  auto row=settingsControls_.find("refresh_rate");
  if(row!=settingsControls_.end())lv_label_set_text(row->second.detail,"正在保存…");
  db_.queueSetting("ui_refresh_hz",hz,refreshRequest_);
}
void App::refreshRateSaved(const Event &event) {
  if(refreshRequest_.empty()||event.data.value("request",std::string())!=refreshRequest_)return;
  if(event.type=="setting_saved") {
    refreshSaved_=refreshTarget_;refreshReady_=true;return;
  }
  if(event.type=="setting_error") {
    refreshRequest_.clear();refreshError_=true;
    for(auto &entry:settingsControls_)lv_obj_remove_state(entry.second.row,LV_STATE_DISABLED);
    auto row=settingsControls_.find("refresh_rate");
    if(row!=settingsControls_.end())lv_label_set_text(row->second.detail,"刷新率未能保存，请重试");
  }
}
void App::refreshRateTick() {
  if(!refreshReady_||!visible_||!inputFocused_||!inputFocus_.allowed(ticks())||
      keyboard_||editor_.active||touchDown_)return;
  const bool changed=display_.refreshRate()!=refreshTarget_;
  display_.setRefreshRate(refreshTarget_);
  refreshReady_=false;refreshRequest_.clear();
  if(changed)++refreshChanges_;
  for(auto &entry:settingsControls_)lv_obj_remove_state(entry.second.row,LV_STATE_DISABLED);
  auto row=settingsControls_.find("refresh_rate");
  if(row!=settingsControls_.end())
    lv_label_set_text(row->second.detail,(std::to_string(display_.refreshRate())+"Hz").c_str());
}
} // namespace penchat
