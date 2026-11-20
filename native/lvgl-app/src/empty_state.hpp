#pragma once
#include "ui_metrics.hpp"
#include <lvgl/lvgl.h>
#include <string>

namespace penchat {
enum class StatusKind { Empty, Loading, Error, Filtered };
struct StatusStateSpec {
  StatusKind kind;
  std::string title, detail, icon;
};
// Coordinates are resolved logical pixels. A zero availableHeight creates a
// content-sized, left-aligned notice for a form or popup. Positive height uses
// only the caller's remaining viewport, centering when all measured text fits.
// Icons are optional and appear only when their complete optical box fits.
lv_obj_t *statusState(lv_obj_t *parent, const StatusStateSpec &spec,
                      const UiMetrics &metrics, const lv_font_t *titleFont,
                      const lv_font_t *detailFont, int x, int y, int width,
                      int availableHeight = 0);
bool isStatusState(lv_obj_t *object);
bool statusStateFillsViewport(lv_obj_t *object);
// Re-measure after a form/viewport width change. -1 retains the existing mode.
bool arrangeStatusState(lv_obj_t *object, int width, int availableHeight = -1);
} // namespace penchat
