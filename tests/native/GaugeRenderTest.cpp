#include "icons.hpp"
#include "platform.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace penchat;
struct Draws {
  lv_point_t origin{}, needle{};
  lv_area_t hub{};
  int arcs = 0, lines = 0, hubs = 0;
};
int main(int argc, char **argv) {
  lv_init();
  {
    Screen screen; screen.width = 320; screen.height = 240;
    Display display(screen, true);
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), 0);
    for (int percent : {75, 100, 125, 150}) for (int size : {27, 28, 43, 44}) {
      UiMetrics metrics(100, percent);
      auto object = lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(object);
      lv_obj_set_pos(object, 40, 40); lv_obj_set_size(object, metrics.dp(size), metrics.dp(size));
      attachIcon(object, "gauge", 0xeeeeee, 1.f, metrics);
      lv_obj_add_flag(object, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
      Draws draws;
      lv_obj_add_event_cb(object, [](lv_event_t *event) {
        auto &d = *static_cast<Draws *>(lv_event_get_user_data(event));
        auto task = lv_event_get_draw_task(event);
        if (auto arc = lv_draw_task_get_arc_dsc(task)) { d.origin = arc->center; ++d.arcs; }
        if (auto line = lv_draw_task_get_line_dsc(task)) {
          d.needle = {int32_t(std::lround(line->p1.x)), int32_t(std::lround(line->p1.y))}; ++d.lines;
        }
        if (lv_draw_task_get_border_dsc(task)) { lv_draw_task_get_area(task, &d.hub); ++d.hubs; }
      }, LV_EVENT_DRAW_TASK_ADDED, &draws);
      lv_obj_update_layout(object);
      const auto callbacks = lv_obj_get_event_count(object);
      for (int count : {1, 2, 3, 8}) for (int index = 0; index < count; ++index) {
        draws = {}; setGaugePosition(object, index, count); lv_obj_invalidate(object);
        lv_refr_now(nullptr);
        assert(draws.arcs && draws.lines && draws.hubs);
        assert(draws.origin.x == draws.needle.x && draws.origin.y == draws.needle.y);
        assert(draws.hub.x1 + draws.hub.x2 == 2 * draws.origin.x);
        assert(draws.hub.y1 + draws.hub.y2 == 2 * draws.origin.y);
        assert(lv_obj_get_event_count(object) == callbacks);
        if (argc == 2 && size == 44 && count == 8 && (index == 0 || index == 3 || index == 7))
          display.screenshot(join(argv[1], std::to_string(percent) + "-" + std::to_string(index) + ".png"));
      }
      lv_obj_delete(object);
    }
  }
  lv_deinit();
  std::cout << "Gauge actual LVGL frame/hub/needle draw origins: all angles, odd/even targets, 75/100/125/150 passed\n";
}
