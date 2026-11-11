#pragma once
#include "lvgl.h"
#include "keyboard_design.hpp"

namespace penchat {
namespace keyboardStyle {
inline int candidateWidth(const char *text,const lv_font_t *font,bool corrected,
                          const KeyboardCandidateMetrics &metrics=keyboardDesign().candidates) {
  lv_point_t size{};lv_text_get_size(&size,text,font,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  return std::max(metrics.naturalMinimum,int(size.x)+(corrected?metrics.correctionPadding:metrics.textPadding));
}
inline void apply(lv_obj_t *object,KeyboardRole role,
                  KeyboardVariant variant=KeyboardVariant::Default,bool selected=false,
                  const KeyboardDesign &design=keyboardDesign()) {
  const auto normal=design.resolve(role,variant,{false,false,selected});
  const auto pressed=design.resolve(role,variant,{true,false,selected});
  const auto disabled=design.resolve(role,variant,{false,true,selected});
  lv_obj_set_style_radius(object,normal.radius,0);
  lv_obj_set_style_bg_color(object,lv_color_hex(normal.background),0);
  lv_obj_set_style_bg_opa(object,normal.backgroundOpacity,0);
  lv_obj_set_style_bg_color(object,lv_color_hex(pressed.background),LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(object,pressed.backgroundOpacity,LV_STATE_PRESSED);
  if(disabled.opacity!=255)lv_obj_set_style_opa(object,disabled.opacity,LV_STATE_DISABLED);
  else lv_obj_remove_local_style_prop(object,LV_STYLE_OPA,LV_STATE_DISABLED);
}
inline void keyState(lv_obj_t *object,bool pressed,KeyboardRole role=KeyboardRole::Character,
                     KeyboardVariant variant=KeyboardVariant::Default,bool selected=false) {
  const auto style=keyboardDesign().resolve(role,variant,{pressed,false,selected});
  lv_obj_set_style_bg_color(object,lv_color_hex(style.background),0);
  lv_obj_set_style_bg_opa(object,style.backgroundOpacity,0);
}
inline void key(lv_obj_t *object,KeyboardRole role=KeyboardRole::Character,
                KeyboardVariant variant=KeyboardVariant::Default,bool selected=false) {apply(object,role,variant,selected);}
inline void input(lv_obj_t *object) {apply(object,KeyboardRole::Input);}
inline void composition(lv_obj_t *object) {
  apply(object,KeyboardRole::Composition);lv_obj_set_style_clip_corner(object,true,0);
}
inline void candidates(lv_obj_t *object) {
  apply(object,KeyboardRole::Candidates);lv_obj_set_style_clip_corner(object,true,0);
}
} // namespace keyboardStyle
} // namespace penchat
