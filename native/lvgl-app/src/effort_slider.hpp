#pragma once
#include <lvgl/lvgl.h>
#include "theme.hpp"
#include "ui_metrics.hpp"
#include <algorithm>
#include <functional>
namespace penchat {
enum class EffortSliderEvent { Preview, Commit, Cancel };
struct EffortSlider {
  int count=1,index=0,original=0;
  bool dragging=false;
  UiMetrics metrics;
  std::function<void(int,EffortSliderEvent)> changed;
};
// One geometry defines painted tick centres, knob centres and pointer mapping.
// Coordinates are already logical LVGL pixels; only design distances use dp.
inline int effortSliderCenter(const lv_area_t &area,int count,int index) {
  const int radius=(area.y2-area.y1+1)/2;
  const int first=area.x1+radius,last=std::max(first,area.x2-radius);
  return count<=1?(first+last)/2:first+int(int64_t(last-first)*std::max(0,std::min(count-1,index))/(count-1));
}
inline int effortSliderIndex(const lv_area_t &area,int count,int x) {
  if(count<=1)return 0;
  const int first=effortSliderCenter(area,count,0),last=effortSliderCenter(area,count,count-1);
  const int distance=std::max(1,last-first),position=std::max(0,std::min(distance,x-first));
  return int((int64_t(position)*(count-1)+distance/2)/distance);
}
inline void effortSliderEvent(lv_event_t *event) {
  auto data=static_cast<EffortSlider *>(lv_event_get_user_data(event));
  auto object=lv_event_get_current_target_obj(event);
  const auto code=lv_event_get_code(event);
  if(code==LV_EVENT_DELETE){delete data;return;}
  lv_area_t area;lv_obj_get_coords(object,&area);
  if(code==LV_EVENT_DRAW_MAIN) {
    lv_draw_rect_dsc_t track;lv_draw_rect_dsc_init(&track);
    track.bg_color=lv_color_hex(theme::palette().border);track.bg_opa=LV_OPA_COVER;
    track.radius=LV_RADIUS_CIRCLE;lv_draw_rect(lv_event_get_layer(event),&track,&area);
    const int cy=(area.y1+area.y2)/2;
    for(int index=0;index<data->count;++index) {
      const int x=effortSliderCenter(area,data->count,index),radius=data->metrics.dp(3);
      lv_draw_rect_dsc_t tick;lv_draw_rect_dsc_init(&tick);
      tick.bg_color=lv_color_hex(theme::palette().muted);tick.bg_opa=LV_OPA_COVER;tick.radius=LV_RADIUS_CIRCLE;
      lv_area_t bounds{x-radius,cy-radius,x+radius,cy+radius};
      lv_draw_rect(lv_event_get_layer(event),&tick,&bounds);
    }
    const int x=effortSliderCenter(area,data->count,data->index),radius=data->metrics.dp(17);
    lv_draw_rect_dsc_t knob;lv_draw_rect_dsc_init(&knob);
    knob.bg_color=lv_color_hex(theme::palette().text);knob.bg_opa=LV_OPA_COVER;knob.radius=LV_RADIUS_CIRCLE;
    lv_area_t bounds{x-radius,cy-radius,x+radius,cy+radius};
    lv_draw_rect(lv_event_get_layer(event),&knob,&bounds);
    return;
  }
  if(code==LV_EVENT_PRESSED) {data->dragging=true;data->original=data->index;}
  if((code==LV_EVENT_PRESSED||code==LV_EVENT_PRESSING)&&data->dragging) {
    auto input=lv_indev_active();if(!input)return;
    lv_point_t point;lv_indev_get_point(input,&point);
    const int next=effortSliderIndex(area,data->count,point.x);
    data->index=next;lv_obj_invalidate(object);
    if(data->changed)data->changed(next,EffortSliderEvent::Preview);
  }
  if(code==LV_EVENT_RELEASED&&data->dragging) {
    data->dragging=false;data->original=data->index;
    if(data->changed)data->changed(data->index,EffortSliderEvent::Commit);
  }
  if(code==LV_EVENT_PRESS_LOST&&data->dragging) {
    data->dragging=false;data->index=data->original;lv_obj_invalidate(object);
    if(data->changed)data->changed(data->index,EffortSliderEvent::Cancel);
  }
}
inline EffortSlider *effortSliderData(lv_obj_t *object) {
  for(uint32_t i=0;i<lv_obj_get_event_count(object);++i) {
    auto event=lv_obj_get_event_dsc(object,i);
    if(lv_event_dsc_get_cb(event)==effortSliderEvent)
      return static_cast<EffortSlider *>(lv_event_dsc_get_user_data(event));
  }
  return nullptr;
}
inline lv_obj_t *createEffortSlider(lv_obj_t *parent,int x,int y,int width,int count,int index,
    const UiMetrics &metrics,std::function<void(int,EffortSliderEvent)> changed) {
  auto slider=lv_obj_create(parent);lv_obj_remove_style_all(slider);
  lv_obj_set_pos(slider,x,y);lv_obj_set_size(slider,width,metrics.dp(44));
  lv_obj_set_scrollable(slider,false);lv_obj_set_clickable(slider,true);
  lv_obj_remove_flag(slider,LV_OBJ_FLAG_SCROLL_ON_FOCUS);
  lv_obj_add_flag(slider,LV_OBJ_FLAG_PRESS_LOCK);
  auto data=new EffortSlider;
  data->count=std::max(1,count);data->index=data->original=std::max(0,std::min(data->count-1,index));
  data->metrics=metrics;data->changed=std::move(changed);
  lv_obj_add_event_cb(slider,effortSliderEvent,LV_EVENT_ALL,data);
  return slider;
}
} // namespace penchat
