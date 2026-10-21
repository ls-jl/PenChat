#pragma once
#include <lvgl/lvgl.h>
#include <string>
namespace penchat {
// Draw in a shared 24-unit optical box; parent dimensions remain the hit area.
void attachIcon(lv_obj_t *object, const std::string &name, uint32_t ink);
} // namespace penchat
