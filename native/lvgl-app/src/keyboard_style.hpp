#pragma once
#include "lvgl.h"
#include "theme.hpp"

namespace penchat {
namespace keyboardStyle {
constexpr lv_opa_t keyOpacity=LV_OPA_60;
constexpr lv_opa_t headerOpacity=LV_OPA_20;

inline void keyState(lv_obj_t *object,bool pressed) {
  lv_obj_set_style_bg_color(object,lv_color_hex(pressed?theme::border:theme::surface),0);
  lv_obj_set_style_bg_opa(object,keyOpacity,0);
}
inline void key(lv_obj_t *object) {
  lv_obj_set_style_radius(object,9,0);
  keyState(object,false);
}
inline void control(lv_obj_t *object) {
  lv_obj_set_style_bg_opa(object,headerOpacity,0);
  lv_obj_set_style_bg_opa(object,LV_OPA_80,LV_STATE_PRESSED);
}
inline void input(lv_obj_t *object) {
  lv_obj_set_style_bg_color(object,lv_color_hex(theme::surface),0);
  lv_obj_set_style_bg_opa(object,headerOpacity,0);
  lv_obj_set_style_radius(object,8,0);
}
inline void composition(lv_obj_t *object) {
  lv_obj_set_style_bg_opa(object,headerOpacity,0);
  lv_obj_set_style_radius(object,8,0);
  lv_obj_set_style_clip_corner(object,true,0);
}
inline void candidates(lv_obj_t *object) {
  lv_obj_set_style_bg_opa(object,LV_OPA_TRANSP,0);
  lv_obj_set_style_radius(object,8,0);
  lv_obj_set_style_clip_corner(object,true,0);
}
inline void candidate(lv_obj_t *object) {
  lv_obj_set_style_bg_opa(object,LV_OPA_TRANSP,0);
  lv_obj_set_style_bg_opa(object,LV_OPA_COVER,LV_STATE_PRESSED);
  lv_obj_set_style_radius(object,6,0);
}
} // namespace keyboardStyle
} // namespace penchat
