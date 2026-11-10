#pragma once
#include "editor.hpp"
#include <lvgl/lvgl.h>
namespace penchat {
// Selection ends prefer the preceding line at soft wraps and trailing newlines.
// A caret keeps its own affinity; these positions only anchor selection handles.
inline lv_point_t selectionLabelBoundary(lv_obj_t *label, const std::string &text, size_t index,
                                         bool end) {
  index = std::min(index, editLength(text));
  lv_point_t point{};
  lv_label_get_letter_pos(label, index, &point);
  if (end && index) {
    const auto previous = editBoundary(text, index - 1);
    lv_point_t before{};
    lv_label_get_letter_pos(label, previous, &before);
    if (before.y < point.y) {
      point = before;
      const auto cluster =
          text.substr(editByte(text, previous), editByte(text, index) - editByte(text, previous));
      if (cluster.find_first_of("\r\n") == std::string::npos) {
        lv_point_t size{};
        lv_text_get_size(&size, cluster.c_str(), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                         lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX,
                         LV_TEXT_FLAG_EXPAND);
        point.x += size.x;
      }
    }
  }
  return point;
}
inline bool selectionVisibleAnchor(const lv_area_t &clip, const lv_area_t &glyph, bool end,
                                   lv_point_t &point) {
  if (glyph.x2 < clip.x1 || glyph.x1 > clip.x2 || glyph.y2 < clip.y1 || glyph.y1 > clip.y2)
    return false;
  point.x = std::max(int(clip.x1), std::min(int(clip.x2), int(end ? glyph.x2 + 1 : glyph.x1)));
  point.y = std::max(int(clip.y1), std::min(int(clip.y2 + 1), int(glyph.y2 + 1)));
  return true;
}
} // namespace penchat
