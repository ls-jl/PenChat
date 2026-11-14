#include "surface_motion.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
static uint32_t now=1000;
int main(){
  lv_init();lv_tick_set_cb([](){return now;});
  auto display=lv_display_create(240,160);
  std::vector<uint8_t> pixels(240*160*4);
  lv_display_set_color_format(display,LV_COLOR_FORMAT_ARGB8888);
  lv_display_set_buffers(display,pixels.data(),nullptr,pixels.size(),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
  auto screen=lv_screen_active();
  const auto count=lv_obj_get_child_count(screen);
  for(int i=0;i<8;++i){
    auto object=lv_obj_create(screen);lv_obj_remove_style_all(object);
    lv_obj_set_size(object,240,160);lv_obj_set_style_bg_opa(object,255,0);
    lv_obj_set_style_bg_color(object,lv_color_hex(0x303030),0);
    lv_obj_set_style_transform_pivot_x(object,0,0);lv_obj_set_style_transform_pivot_y(object,0,0);
    lv_obj_set_style_transform_scale_x(object,i%2?128:256,0);
    lv_obj_set_style_transform_scale_y(object,i%2?128:256,0);
    fadeSurfaceIn(object);now+=70;lv_timer_handler();lv_refr_now(display);
    const auto before=pixels;
    const auto opacity=lv_obj_get_style_opa(object,LV_PART_MAIN);
    assert(opacity>0&&opacity<255);
    SurfaceSnapshot snapshot(object);lv_obj_delete(object);
    // A page rebuild can happen before the detached raster is shown.
    motion::finishAll();snapshot.show();
    lv_refr_now(display);
    // LVGL's raster opacity path rounds one channel step differently from
    // direct rectangle blending; geometry and all other pixels must agree.
    for(size_t byte=0;byte<pixels.size();++byte)
      assert(std::abs(int(pixels[byte])-int(before[byte]))<=1);
    assert(motion::retiring());
    assert(lv_obj_get_child_count(screen)==count+1);
    auto picture=lv_obj_get_child(lv_obj_get_child(screen,-1),0);
    assert(lv_obj_get_style_opa(picture,LV_PART_MAIN)==opacity);
    now+=150;lv_timer_handler();motion::finishAll();
    assert(!motion::retiring());assert(lv_obj_get_child_count(screen)==count);
  }
  lv_display_delete(display);lv_deinit();
  std::cout<<"SURFACE_MOTION_REVERSE_SNAPSHOT_REBUILD_AND_CLEANUP_OK\n";
}
