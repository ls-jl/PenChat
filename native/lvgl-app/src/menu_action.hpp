#pragma once
#include <lvgl/lvgl.h>
#include <functional>
#include <iostream>
namespace penchat {
inline void menuAction(lv_obj_t *object,std::function<void()> action) {
  auto callback=new std::function<void()>(std::move(action));
  lv_obj_set_clickable(object,true);
  lv_obj_add_event_cb(object,[](lv_event_t *event){
    // Copy before invocation: an action may synchronously delete its own row.
    auto action=*static_cast<std::function<void()> *>(lv_event_get_user_data(event));
    try {action();}catch(const std::exception &error){std::cerr<<"UI action: "<<error.what()<<'\n';}
  },LV_EVENT_CLICKED,callback);
  lv_obj_add_event_cb(object,[](lv_event_t *event){delete static_cast<std::function<void()> *>(lv_event_get_user_data(event));},LV_EVENT_DELETE,callback);
}
} // namespace penchat
