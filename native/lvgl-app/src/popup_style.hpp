#pragma once
#include "theme.hpp"
#include "layout.hpp"
#include <cmath>
#include <lvgl.h>
namespace penchat {
inline void stylePopupPanel(lv_obj_t *object, bool portrait = false) {
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::popup), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(object, portrait ? theme::portraitPopupRadius : theme::popupRadius, 0);
  lv_obj_set_style_border_width(object, theme::popupBorder, 0);
  lv_obj_set_style_border_color(object, lv_color_hex(theme::border), 0);
}
inline void styleDisabledButton(lv_obj_t *object) {
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::disabledBackground), LV_STATE_DISABLED);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_STATE_DISABLED);
  lv_obj_set_style_opa(object, LV_OPA_COVER, LV_STATE_DISABLED);
  // Labels have explicit normal text colors (including danger/primary). Mirror
  // only the disabled state; restoring normal never erases their semantic color.
  auto update = [](lv_obj_t *button) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(button); ++i) {
      auto child = lv_obj_get_child(button, i);
      if (!lv_obj_check_type(child, &lv_label_class)) continue;
      lv_obj_set_style_text_color(child, lv_color_hex(theme::disabledText), LV_STATE_DISABLED);
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
      lv_obj_set_style_text_color(child, lv_color_hex(theme::disabledText), LV_STATE_DISABLED);
      if (lv_obj_has_state(button, LV_STATE_DISABLED)) lv_obj_add_state(child, LV_STATE_DISABLED);
      else lv_obj_remove_state(child, LV_STATE_DISABLED);
    }
  }, LV_EVENT_STATE_CHANGED, nullptr);
}
inline void stylePopupRow(lv_obj_t *object) {
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::popup), 0);
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::popupHighlight), LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::border), LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::popup), LV_STATE_DISABLED);
  lv_obj_set_style_radius(object, std::max(1, int(lv_obj_get_height(object)) / 2), 0);
}
// Pure logical geometry. Rounded mask is an explicit Y07 capability, never
// inferred from aspect ratio; host tests can exercise both masks independently.
inline LayoutRect popupSafeArea(int width, int height, bool rounded) {
  const AppLayout layout{width,height};
  const int margin = rounded ? int(std::ceil(std::min(width,height)*.015)) : 8;
  return {std::max(margin,layout.safeSide()), std::max(margin,layout.safeTop()),
          width-2*std::max(margin,layout.safeSide()),
          height-std::max(margin,layout.safeTop())-std::max(margin,layout.safeBottom())};
}
inline LayoutRect placeContextPopup(int width,int height,bool rounded,
                                     LayoutRect anchor,int desiredWidth,int desiredHeight) {
  const auto safe=popupSafeArea(width,height,rounded);
  const int h=std::min(desiredHeight,std::max(44,safe.height*4/5));
  int y=anchor.y+anchor.height+8;
  if(y+h>safe.y+safe.height)y=anchor.y-h-8;
  y=std::max(safe.y,std::min(safe.y+safe.height-h,y));
  int side=safe.x;
  if(rounded) {
    const int radius=int(std::ceil(std::min(width,height)*.20));
    const int margin=int(std::ceil(std::min(width,height)*.015));
    const int t=std::max(0,std::min(y,height-y-h));
    if(t<radius) side=std::max(side,int(std::ceil(radius-std::sqrt(double(radius*radius-(radius-t)*(radius-t)))+margin)));
  }
  const int w=std::min(desiredWidth,std::max(44,width-2*side));
  const int x=std::max(side,std::min(width-side-w,anchor.x));
  return {x,y,w,h};
}
} // namespace penchat
