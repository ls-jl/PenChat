#include "app.hpp"
#include "icons.hpp"
#include <cstdio>
namespace penchat {
namespace {
std::string hex(uint32_t value) { char out[8];std::snprintf(out,sizeof(out),"#%06x",unsigned(value));return out; }
}
void App::sendAppearance() {
  const auto &p=theme::palette();
  bridge_.send("appearance",{{"mode",theme::name(theme::mode())},{"scale",uiMetrics_.scalePercent()},
    {"colors",{{"background",hex(p.background)},{"text",hex(p.text)},{"muted",hex(p.muted)},
      {"surface",hex(p.surface)},{"primaryText",hex(p.primaryText)},{"caret",hex(p.caret)}}},
    {"metrics",{{"title",uiMetrics_.dp(28)},{"body",uiMetrics_.fontSize(UiFont::Body)},
      {"caption",uiMetrics_.dp(20)},{"gap",uiMetrics_.dp(14)},{"inset",uiMetrics_.dp(32)},
      {"top",uiMetrics_.dp(20)},{"bottom",uiMetrics_.dp(25)},{"retryWidth",uiMetrics_.dp(180)},
      {"leaveX",uiMetrics_.dp(235)},{"leaveWidth",uiMetrics_.dp(120)},
      {"controlHeight",uiMetrics_.touchTarget(45)},{"radius",uiMetrics_.dp(12)}}}});
}
void App::requestTheme(const std::string &name) {
  if(view_!="settings"||!visible_||!inputFocused_||!themeRequest_.empty()||!refreshRequest_.empty()||editor_.active||keyboard_)return;
  if(name!="dark"&&name!="light")return;
  themeTarget_=theme::parse(name);
  if(themeTarget_==theme::mode())return;
  input_.cancel();touchDown_=false;
  themeRequest_=uid("theme");
  for(auto &entry:settingsControls_)lv_obj_add_state(entry.second.row,LV_STATE_DISABLED);
  auto row=settingsControls_.find("theme");
  if(row!=settingsControls_.end())lv_label_set_text(row->second.detail,"正在应用…");
  db_.queueSetting("theme_mode",name,themeRequest_);
}
void App::themeSaved(const Event &event) {
  if(themeRequest_.empty()||event.data.value("request",std::string())!=themeRequest_)return;
  if(event.type=="setting_saved"){themeReady_=true;return;}
  if(event.type=="setting_error") {
    themeRequest_.clear();
    for(auto &entry:settingsControls_)lv_obj_remove_state(entry.second.row,LV_STATE_DISABLED);
    auto row=settingsControls_.find("theme");
    if(row!=settingsControls_.end())lv_label_set_text(row->second.detail,"主题未能保存，请重试");
  }
}
void App::themeTick() {
  if(!themeReady_||!visible_||!inputFocused_||!inputFocus_.allowed(ticks())||keyboard_||editor_.active||touchDown_)return;
  const int scroll=body_?lv_obj_get_scroll_y(body_):settingsScroll_;
  input_.cancel();resetBack();resetPull();readReset();composerShortcut_.pause();
  themeReady_=false;themeRequest_.clear();composerTapAllowed_=pullTrackActivity_=false;
  clear();theme::setMode(themeTarget_);rich_.clearCache();editorLayoutCache_.reset();
  view_="settings";settingsScroll_=scroll;build();sendAppearance();
}
void App::syncEffortGauge() {
  if(!composerGauge_||effortPreview_)return;
  lv_obj_set_hidden(composerGauge_,!sessionPreferences_.value("gauge",!effortMemory_.empty()));
  layoutChatComposer();
  const auto model=currentModel();
  const auto options=effortChoices(model);
  const auto placement=effortGaugePlacement(options,effortChoiceIndex(model));
  setIcon(composerGauge_,"gauge",placement.isDefault?theme::palette().muted:theme::palette().accent,1.f,uiMetrics_);
  setGaugePosition(composerGauge_,placement.index,placement.count);
}
} // namespace penchat
