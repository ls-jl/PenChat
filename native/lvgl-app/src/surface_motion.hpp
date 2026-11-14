#pragma once
#include "ui_motion.hpp"
#include <chrono>
#include "src/misc/cache/instance/lv_image_cache.h"
#include "src/core/lv_obj_draw_private.h"
namespace penchat {
struct SurfaceSnapshotTiming { uint64_t count=0,totalUs=0,maxUs=0; };
inline SurfaceSnapshotTiming &surfaceSnapshotTiming() {static SurfaceSnapshotTiming value;return value;}

// The viewport must not overwrite an entrance translation with dismissal=0.
inline bool surfaceEntering(lv_obj_t *object) {
  if(!object)return false;
  for(uint32_t i=0;i<lv_obj_get_event_count(object);++i) {
    auto event=lv_obj_get_event_dsc(object,i);
    if(lv_event_dsc_get_cb(event)==motion::owner) {
      auto state=static_cast<motion::State *>(lv_event_dsc_get_user_data(event));
      return state->closing;
    }
  }
  return false;
}
// Slide detached pixels on exit; never retain editor callbacks or field pointers.
class SurfaceSnapshot {
  lv_draw_buf_t *buffer_=nullptr;
  lv_area_t area_{};
  int opacity_=255,scaleX_=256,scaleY_=256,pivotX_=0,pivotY_=0;
  int x_=0,y_=0;
public:
  explicit SurfaceSnapshot(lv_obj_t *object) {
    motion::cancel(object);lv_obj_update_layout(object);
    x_=lv_obj_get_style_translate_x(object,LV_PART_MAIN);
    y_=lv_obj_get_style_translate_y(object,LV_PART_MAIN);
    opacity_=lv_obj_get_style_opa(object,LV_PART_MAIN);
    scaleX_=lv_obj_get_style_transform_scale_x(object,LV_PART_MAIN);
    scaleY_=lv_obj_get_style_transform_scale_y(object,LV_PART_MAIN);
    const int extra=lv_obj_get_ext_draw_size(object);
    pivotX_=lv_obj_get_style_transform_pivot_x(object,LV_PART_MAIN)+extra;
    pivotY_=lv_obj_get_style_transform_pivot_y(object,LV_PART_MAIN)+extra;
    lv_obj_get_coords(object,&area_);
    area_.x1-=extra+x_;area_.y1-=extra+y_;
    const auto started=std::chrono::steady_clock::now();
    buffer_=lv_snapshot_take(object,LV_COLOR_FORMAT_ARGB8888);
    const auto elapsed=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count());
    auto &timing=surfaceSnapshotTiming();++timing.count;timing.totalUs+=elapsed;timing.maxUs=std::max(timing.maxUs,elapsed);
  }
  ~SurfaceSnapshot(){if(buffer_)lv_draw_buf_destroy(buffer_);}
  SurfaceSnapshot(const SurfaceSnapshot &)=delete;
  SurfaceSnapshot &operator=(const SurfaceSnapshot &)=delete;
  void show(int dx,int dy,bool scrim=false) {
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
    lv_obj_set_style_translate_x(picture,x_,0);lv_obj_set_style_translate_y(picture,y_,0);
    buffer_=nullptr;motion::block(layer);motion::state(layer)->closing=true;
    if(scrim)lv_obj_set_style_bg_color(layer,lv_color_hex(0),0);
    const int startX=x_,startY=y_;
    motion::animate(layer,0,1000,180,[layer,picture,startX,startY,dx,dy,scrim](int progress){
      lv_obj_set_style_translate_x(picture,startX+(dx-startX)*progress/1000,0);
      lv_obj_set_style_translate_y(picture,startY+(dy-startY)*progress/1000,0);
      if(scrim)lv_obj_set_style_bg_opa(layer,120*(1000-progress)/1000,0);
    },[layer]{lv_obj_delete(layer);});
  }
};
inline void slideSurfaceIn(lv_obj_t *object,int dx,int dy) {
  lv_obj_set_style_opa(object,LV_OPA_COVER,0);
  motion::state(object)->closing=true; // Native key hit bounds remain at their resting geometry.
  motion::animate(object,0,1000,180,[object,dx,dy](int progress){
    lv_obj_set_style_translate_x(object,dx*(1000-progress)/1000,0);
    lv_obj_set_style_translate_y(object,dy*(1000-progress)/1000,0);
  },[object]{motion::state(object)->closing=false;});
}
} // namespace penchat
