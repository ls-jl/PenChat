#pragma once
#include "ui_motion.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/core/lv_obj_draw_private.h"
namespace penchat {
// Fade a detached raster, never a retiring editor's callbacks or field pointers.
class SurfaceSnapshot {
  lv_draw_buf_t *buffer_=nullptr;
  lv_area_t area_{};
  int opacity_=255,scaleX_=256,scaleY_=256,pivotX_=0,pivotY_=0;
public:
  explicit SurfaceSnapshot(lv_obj_t *object) {
    motion::cancel(object);lv_obj_update_layout(object);
    opacity_=lv_obj_get_style_opa(object,LV_PART_MAIN);
    scaleX_=lv_obj_get_style_transform_scale_x(object,LV_PART_MAIN);
    scaleY_=lv_obj_get_style_transform_scale_y(object,LV_PART_MAIN);
    const int extra=lv_obj_get_ext_draw_size(object);
    pivotX_=lv_obj_get_style_transform_pivot_x(object,LV_PART_MAIN)+extra;
    pivotY_=lv_obj_get_style_transform_pivot_y(object,LV_PART_MAIN)+extra;
    lv_obj_get_coords(object,&area_);
    area_.x1-=extra;area_.y1-=extra;
    buffer_=lv_snapshot_take(object,LV_COLOR_FORMAT_ARGB8888);
  }
  ~SurfaceSnapshot(){if(buffer_)lv_draw_buf_destroy(buffer_);}
  SurfaceSnapshot(const SurfaceSnapshot &)=delete;
  SurfaceSnapshot &operator=(const SurfaceSnapshot &)=delete;
  void show() {
    if(!buffer_)return;
    auto layer=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer,LV_PCT(100),LV_PCT(100));lv_obj_set_scrollable(layer,false);
    auto picture=lv_image_create(layer);lv_obj_remove_style_all(picture);
    lv_image_set_src(picture,buffer_);lv_obj_set_pos(picture,area_.x1,area_.y1);
    lv_obj_set_style_opa(picture,opacity_,0);
    lv_obj_set_style_transform_pivot_x(picture,pivotX_,0);
    lv_obj_set_style_transform_pivot_y(picture,pivotY_,0);
    lv_obj_set_style_transform_scale_x(picture,scaleX_,0);
    lv_obj_set_style_transform_scale_y(picture,scaleY_,0);
    lv_obj_add_event_cb(picture,[](lv_event_t *event){
      auto buffer=static_cast<lv_draw_buf_t *>(lv_event_get_user_data(event));
      lv_image_cache_drop(buffer);lv_draw_buf_destroy(buffer);
    },LV_EVENT_DELETE,buffer_);
    buffer_=nullptr;motion::dismiss(layer,picture);
  }
};
inline void fadeSurfaceOut(lv_obj_t *object) {
  if(object){SurfaceSnapshot snapshot(object);snapshot.show();}
}
inline void fadeSurfaceIn(lv_obj_t *object) {
  lv_obj_set_style_opa(object,0,0);
  motion::fade(object,LV_OPA_COVER,180);
}
} // namespace penchat
