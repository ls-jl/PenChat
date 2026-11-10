#pragma once
#include "editor.hpp"
#include "selection_menu.hpp"
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
// Query only the visible label span; use LVGL's own positions and advances.
inline LayoutRect selectionLabelBounds(lv_obj_t *label, size_t first, size_t last, lv_area_t clip) {
  LayoutRect bounds{};
  if (last <= first || clip.x2 < clip.x1 || clip.y2 < clip.y1)
    return bounds;
  lv_area_t area;
  lv_obj_get_coords(label, &area);
  lv_point_t start{std::max(0, int(clip.x1 - area.x1)), std::max(0, int(clip.y1 - area.y1))};
  lv_point_t end{std::max(0, int(clip.x2 - area.x1)), std::max(0, int(clip.y2 - area.y1))};
  const size_t visibleFirst = lv_label_get_letter_on(label, &start, false);
  const size_t visibleLast = lv_label_get_letter_on(label, &end, false);
  first = std::max(first, visibleFirst ? visibleFirst - 1 : 0);
  last = std::min(last, visibleLast + 1);
  const std::string text = lv_label_get_text(label);
  std::vector<size_t> bytes;
  for (size_t i = 0; i < text.size(); ++i)
    if ((static_cast<unsigned char>(text[i]) & 192) != 128)
      bytes.push_back(i);
  bytes.push_back(text.size());
  const auto font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  for (size_t i = first; i < last && i + 1 < bytes.size(); ++i) {
    lv_point_t p{}, q{};
    lv_label_get_letter_pos(label, i, &p);
    lv_label_get_letter_pos(label, i + 1, &q);
    int advance = q.x - p.x;
    if (q.y != p.y) {
      const auto ch = text.substr(bytes[i], bytes[i + 1] - bytes[i]);
      lv_point_t size{};
      if (ch != "\n" && ch != "\r")
        lv_text_get_size(&size, ch.c_str(), font,
                         lv_obj_get_style_text_letter_space(label, LV_PART_MAIN), 0, LV_COORD_MAX,
                         LV_TEXT_FLAG_EXPAND);
      advance = size.x;
    }
    const int x1 = std::max(int(clip.x1), int(area.x1 + p.x));
    const int x2 = std::min(int(clip.x2), int(area.x1 + p.x + std::max(1, advance) - 1));
    const int y1 = std::max(int(clip.y1), int(area.y1 + p.y));
    const int y2 = std::min(int(clip.y2), int(area.y1 + p.y + font->line_height - 1));
    includeSelectionRect(bounds, {x1, y1, x2 - x1 + 1, y2 - y1 + 1});
  }
  return bounds;
}
} // namespace penchat
