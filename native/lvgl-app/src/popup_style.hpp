#pragma once
#include "theme.hpp"
#include "layout.hpp"
#include <cmath>
#include <lvgl.h>
namespace penchat {
inline void stylePopupPanel(lv_obj_t *object, bool portrait = false, const UiMetrics &metrics = UiMetrics()) {
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::palette().popup), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(object, metrics.dp(portrait ? theme::portraitPopupRadius : theme::popupRadius), 0);
  lv_obj_set_style_border_width(object, metrics.dp(theme::popupBorder), 0);
  lv_obj_set_style_border_color(object, lv_color_hex(theme::palette().border), 0);
}
inline void styleDisabledButton(lv_obj_t *object) {
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::palette().disabledBackground), LV_STATE_DISABLED);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_STATE_DISABLED);
  lv_obj_set_style_opa(object, LV_OPA_COVER, LV_STATE_DISABLED);
  // Labels have explicit normal text colors (including danger/primary). Mirror
  // only the disabled state; restoring normal never erases their semantic color.
  auto update = [](lv_obj_t *button) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(button); ++i) {
      auto child = lv_obj_get_child(button, i);
      if (!lv_obj_check_type(child, &lv_label_class)) continue;
      lv_obj_set_style_text_color(child, lv_color_hex(theme::palette().disabledText), LV_STATE_DISABLED);
      if (lv_obj_has_state(button, LV_STATE_DISABLED)) lv_obj_add_state(child, LV_STATE_DISABLED);
      else lv_obj_remove_state(child, LV_STATE_DISABLED);
    }
  };
  update(object);
  lv_obj_add_event_cb(object, [](lv_event_t *event) {
    auto button = lv_event_get_current_target_obj(event);
    for (uint32_t i = 0; i < lv_obj_get_child_count(button); ++i) {
      auto child = lv_obj_get_child(button, i);
      if (!lv_obj_check_type(child, &lv_label_class)) continue;
      lv_obj_set_style_text_color(child, lv_color_hex(theme::palette().disabledText), LV_STATE_DISABLED);
      if (lv_obj_has_state(button, LV_STATE_DISABLED)) lv_obj_add_state(child, LV_STATE_DISABLED);
      else lv_obj_remove_state(child, LV_STATE_DISABLED);
    }
  }, LV_EVENT_STATE_CHANGED, nullptr);
}
inline void stylePopupRow(lv_obj_t *object) {
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::palette().popup), 0);
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::palette().popupHighlight), LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::palette().border), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::palette().popup), LV_STATE_DISABLED);
  lv_obj_set_style_radius(object, std::max(1, int(lv_obj_get_height(object)) / 2), 0);
}
// Pure logical geometry. Rounded mask is an explicit Y07 capability, never
// inferred from aspect ratio; host tests can exercise both masks independently.
inline LayoutRect popupSafeArea(int width, int height, bool rounded, const UiMetrics &metrics = UiMetrics()) {
  const AppLayout layout{width,height,rounded,metrics};
  const int margin = rounded ? int(std::ceil(std::min(width,height)*.015)) : metrics.dp(8);
  return {std::max(margin,layout.safeSide()), std::max(margin,layout.safeTop()),
          width-2*std::max(margin,layout.safeSide()),
          height-std::max(margin,layout.safeTop())-std::max(margin,layout.safeBottom())};
}
inline LayoutRect placeContextPopup(int width,int height,bool rounded,
                                     LayoutRect anchor,int desiredWidth,int desiredHeight,int bottomLimit=0, const UiMetrics &metrics = UiMetrics()) {
  const auto safe=popupSafeArea(width,height,rounded,metrics);
  const int bottom=bottomLimit>0?std::min(safe.y+safe.height,bottomLimit):safe.y+safe.height;
  const int h=std::min(desiredHeight,std::max(metrics.dp(44),(bottom-safe.y)*4/5));
  int y=anchor.y+anchor.height+metrics.dp(8);
  if(y+h>bottom)y=anchor.y-h-metrics.dp(8);
  y=std::max(safe.y,std::min(bottom-h,y));
  const int side=std::max(safe.x,ScreenSafety{width,height,rounded}.sideInset(y,h));
  const int w=std::min(desiredWidth,std::max(metrics.dp(44),width-2*side));
  const int x=std::max(side,std::min(width-side-w,anchor.x));
  return {x,y,w,h};
}
} // namespace penchat
