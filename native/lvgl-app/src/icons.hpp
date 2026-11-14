#pragma once
#include <lvgl/lvgl.h>
#include "ui_metrics.hpp"
#include <string>
namespace penchat {
// Draw in a shared 24-unit optical box; parent dimensions remain the hit area.
void attachIcon(lv_obj_t *object, const std::string &name, uint32_t ink, float scale = 1.f, const UiMetrics &metrics = UiMetrics());
// Update one existing icon in place; do not accumulate draw/delete callbacks.
void setIcon(lv_obj_t *object, const std::string &name, uint32_t ink, float scale = 1.f, const UiMetrics &metrics = UiMetrics());
// Update the gauge needle without replacing callbacks or its fixed frame.
void setGaugePosition(lv_obj_t *object, int index, int count);
float gaugePosition(lv_obj_t *object);
// Chevron interpolation, right (0) to down (1000), with fixed optical centre.
void setDisclosureProgress(lv_obj_t *object, int progress);
int disclosureProgress(lv_obj_t *object);
// Top of a selection stem relative to the unchanged handle hit box.
void setSelectionHandleTop(lv_obj_t *object, int top, int corner, int x, int diameterX = 24, int diameterY = 24);
lv_point_t selectionHandleGrip(lv_obj_t *object);
lv_area_t selectionHandleShape(lv_obj_t *object);
bool selectionHandleContains(lv_obj_t *object, int x, int y);
} // namespace penchat
