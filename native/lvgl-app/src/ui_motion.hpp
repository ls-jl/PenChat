#pragma once
#include <lvgl/lvgl.h>
#include "icons.hpp"
#include <algorithm>
#include <functional>
#include <set>
#include <vector>
namespace penchat {
namespace motion {
// UI-thread only. Each animation is owned by an LVGL object; deleting that
// object cancels its animation before freeing the closure. Reversal starts at
// the currently painted value, including partially completed transitions.
struct State {
  lv_obj_t *object;
  lv_obj_t *retiredPanel=nullptr;
  bool closing=false;
  int value = 0,to = 0;
  std::function<void(int)> paint;
  std::function<void()> done;
};
inline std::set<State *> &live() { static std::set<State *> states;return states; }
inline void owner(lv_event_t *event) {
  if(lv_event_get_target_obj(event)!=lv_event_get_current_target_obj(event))return;
  auto state=static_cast<State *>(lv_event_get_user_data(event));
  lv_anim_delete(state,nullptr);
  live().erase(state);delete state;
}
inline State *state(lv_obj_t *object) {
  for(uint32_t i=0;i<lv_obj_get_event_count(object);++i) {
    auto descriptor=lv_obj_get_event_dsc(object,i);
    if(lv_event_dsc_get_cb(descriptor)==owner)
      return static_cast<State *>(lv_event_dsc_get_user_data(descriptor));
  }
  auto value=new State{object};live().insert(value);
  lv_obj_add_event_cb(object,owner,LV_EVENT_DELETE,value);
  return value;
}
inline void cancel(lv_obj_t *object) {
  if(!object)return;
  auto value=state(object);
  lv_anim_delete(value,nullptr);value->paint={};value->done={};
}
inline void animate(lv_obj_t *object,int from,int to,int duration,
                    std::function<void(int)> paint,std::function<void()> done={}) {
  auto value=state(object);lv_anim_delete(value,nullptr);
  value->value=from;value->to=to;value->paint=std::move(paint);value->done=std::move(done);
  value->paint(from);
  if(from==to||duration<=0) {
    value->value=to;value->paint(to);auto completed=value->done;value->done={};
    if(completed)completed();return;
  }
  lv_anim_t animation;lv_anim_init(&animation);lv_anim_set_var(&animation,value);
  lv_anim_set_values(&animation,from,to);lv_anim_set_duration(&animation,duration);
  lv_anim_set_path_cb(&animation,lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation,[](void *data,int32_t position){auto value=static_cast<State *>(data);value->value=position;if(value->paint)value->paint(position);});
  lv_anim_set_completed_cb(&animation,[](lv_anim_t *animation){
    auto value=static_cast<State *>(animation->var);auto completed=value->done;
    value->done={};if(completed)completed();
  });
  lv_anim_start(&animation);
}
// Flush before application clear/background/theme/scale changes. Completion
// callbacks are moved out before invocation and may delete their owner.
inline void finishAll() {
  std::vector<State *> pending(live().begin(),live().end());
  for(auto value:pending) {
    if(!live().count(value))continue;
    lv_anim_delete(value,nullptr);
    auto paint=value->paint;auto done=value->done;
    value->paint={};value->done={};value->value=value->to;
    if(paint)paint(value->to);
    if(done)done();
  }
}
inline void reveal(lv_obj_t *object,int targetHeight,bool animated=true) {
  lv_obj_update_layout(object);
  animate(object,lv_obj_get_height(object),std::max(0,targetHeight),animated?180:0,
          [object](int value){lv_obj_set_height(object,value);});
}
inline void disclosure(lv_obj_t *content,lv_obj_t *arrow,bool expanded,int expandedHeight,bool animated=true) {
  lv_obj_update_layout(content);
  // The arrow has its own object-owned animation, so deleting either subtree
  // cannot leave a callback into the other one.
  reveal(content,expanded?expandedHeight:0,animated);
  if(arrow)animate(arrow,disclosureProgress(arrow),expanded?1000:0,animated?180:0,
                    [arrow](int value){setDisclosureProgress(arrow,value);});
}
inline void fade(lv_obj_t *object,int opacity,int duration,std::function<void()> done={}) {
  animate(object,lv_obj_get_style_opa(object,LV_PART_MAIN),opacity,duration,
          [object](int value){lv_obj_set_style_opa(object,value,0);},std::move(done));
}
inline void block(lv_obj_t *object) {
  auto blocker=lv_obj_create(object);lv_obj_remove_style_all(blocker);
  lv_obj_set_size(blocker,LV_PCT(100),LV_PCT(100));lv_obj_set_clickable(blocker,true);
  lv_obj_set_scrollable(blocker,false);
}
inline void present(lv_obj_t *panel,int direction=1,const UiMetrics &metrics=UiMetrics()) {
  auto layer=lv_obj_get_parent(panel);
  // Adopt a retiring menu into the new overlay. Its scrim is deleted before
  // painting the replacement, leaving exactly one stable overlay throughout.
  std::vector<State *> pending(live().begin(),live().end());
  for(auto outgoing:pending) {
    if(!live().count(outgoing)||!outgoing->retiredPanel||outgoing->object==layer)continue;
    auto oldPanel=outgoing->retiredPanel;
    lv_area_t before,parent;lv_obj_get_coords(oldPanel,&before);lv_obj_get_coords(layer,&parent);
    lv_obj_set_parent(oldPanel,layer);lv_obj_set_pos(oldPanel,before.x1-parent.x1,before.y1-parent.y1);
    outgoing->retiredPanel=nullptr;
    lv_obj_delete(outgoing->object);
    block(oldPanel);lv_obj_move_foreground(panel);
    fade(oldPanel,0,100,[oldPanel]{lv_obj_delete(oldPanel);});
  }
  // Translation is a style transform and does not change the scroll extent.
  const int distance=metrics.dp(6)*direction;
  animate(panel,0,1000,150,[panel,distance](int value){
    lv_obj_set_style_opa(panel,value*255/1000,0);
    lv_obj_set_style_translate_y(panel,distance*(1000-value)/1000,0);
  });
}
inline void dismiss(lv_obj_t *layer,lv_obj_t *panel) {
  if(!layer)return;
  // A closing popup still owns the complete contact sequence. A transparent
  // front child absorbs late releases until the layer is actually deleted.
  block(layer);
  state(layer)->retiredPanel=panel;state(layer)->closing=true;
  if(panel)fade(panel,0,100);
  fade(layer,0,100,[layer]{lv_obj_delete(layer);});
}
inline bool retiring() {
  for(auto value:live())if(value->closing)return true;
  return false;
}
// Replace content under an existing, unchanged scrim. The outgoing object
// becomes noninteractive for the full crossfade and is deleted by its own
// animation; no completion callback can reset the caller's current pointers.
inline void replace(lv_obj_t *oldPanel,lv_obj_t *newPanel) {
  if(oldPanel) {
    block(oldPanel);
    fade(oldPanel,0,100,[oldPanel]{lv_obj_delete(oldPanel);});
  }
  if(newPanel)present(newPanel);
}
} // namespace motion
} // namespace penchat
