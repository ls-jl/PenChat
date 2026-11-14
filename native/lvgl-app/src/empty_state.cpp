#include "empty_state.hpp"
#include "icons.hpp"
#include "theme.hpp"
#include <algorithm>

namespace penchat {
namespace {
struct StatusState {
  UiMetrics metrics;
  lv_obj_t *title = nullptr, *detail = nullptr, *icon = nullptr;
  int availableHeight = 0;
};
void destroyStatusState(lv_event_t *event) {
  delete static_cast<StatusState *>(lv_event_get_user_data(event));
}
StatusState *state(lv_obj_t *object) {
  if (!object) return nullptr;
  for (uint32_t i = 0; i < lv_obj_get_event_count(object); ++i) {
    auto event = lv_obj_get_event_dsc(object, i);
    if (lv_event_dsc_get_cb(event) == destroyStatusState)
      return static_cast<StatusState *>(lv_event_dsc_get_user_data(event));
  }
  return nullptr;
}
lv_obj_t *caption(lv_obj_t *parent, const std::string &text,
                  const lv_font_t *font, uint32_t color) {
  auto object = lv_label_create(parent);
  lv_obj_remove_style_all(object);
  lv_obj_set_clickable(object, false);
  lv_obj_set_style_text_font(object, font, 0);
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_WRAP);
  lv_label_set_text(object, text.c_str());
  return object;
}
} // namespace
bool isStatusState(lv_obj_t *object) { return state(object) != nullptr; }
bool statusStateFillsViewport(lv_obj_t *object) {
  auto data = state(object);
  return data && data->availableHeight > 0;
}
bool arrangeStatusState(lv_obj_t *object, int width, int availableHeight) {
  auto data = state(object);
  if (!data) return false;
  if (availableHeight >= 0) data->availableHeight = availableHeight;
  const auto &m = data->metrics;
  const bool full = data->availableHeight > 0;
  const int pad = m.dp(8), gap = m.dp(8), iconGap = m.dp(12);
  width = std::max(1, width);
  const int measure = std::max(1, std::min(width - 2 * pad, m.dp(520)));
  lv_obj_set_width(object, width);
  for (auto label : {data->title, data->detail}) {
    lv_obj_set_width(label, measure);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(label, full ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_update_layout(label);
  }
  const int titleHeight = lv_obj_get_height(data->title);
  const int detailHeight = lv_obj_get_height(data->detail);
  const int textHeight = titleHeight + (detailHeight ? gap + detailHeight : 0);
  const int iconSize = m.dp(48);
  const bool iconFits = data->icon && full && width >= iconSize + 2 * pad &&
      data->availableHeight >= textHeight + iconSize + iconGap + 2 * pad;
  const int blockHeight = textHeight + (iconFits ? iconSize + iconGap : 0);
  const int height = std::max(blockHeight + 2 * pad, data->availableHeight);
  lv_obj_set_height(object, height);
  int y = full ? std::max(pad, (height - blockHeight) / 2) : pad;
  if (data->icon) {
    lv_obj_set_hidden(data->icon, !iconFits);
    lv_obj_set_size(data->icon, iconSize, iconSize);
    lv_obj_set_pos(data->icon, (width - iconSize) / 2, y);
    if (iconFits) y += iconSize + iconGap;
  }
  const int x = full ? (width - measure) / 2 : pad;
  lv_obj_set_pos(data->title, x, y);
  lv_obj_set_pos(data->detail, x, y + titleHeight + gap);
  return true;
}
lv_obj_t *statusState(lv_obj_t *parent, const StatusStateSpec &spec,
                      const UiMetrics &metrics, const lv_font_t *titleFont,
                      const lv_font_t *detailFont, int x, int y, int width,
                      int availableHeight) {
  auto object = lv_obj_create(parent);
  lv_obj_remove_style_all(object);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_scrollable(object, false);
  lv_obj_set_clickable(object, false);
  auto data = new StatusState;
  data->metrics = metrics;
  data->availableHeight = std::max(0, availableHeight);
  const auto &p = theme::palette();
  data->title = caption(object, spec.title, titleFont,
                        spec.kind == StatusKind::Error ? p.danger : p.text);
  data->detail = caption(object, spec.detail, detailFont, p.muted);
  if (!spec.icon.empty()) {
    data->icon = lv_obj_create(object);
    lv_obj_remove_style_all(data->icon);
    lv_obj_set_scrollable(data->icon, false);
    lv_obj_set_clickable(data->icon, false);
    attachIcon(data->icon, spec.icon, spec.kind == StatusKind::Error ? p.danger : p.muted, 1.5f, metrics);
  }
  lv_obj_add_event_cb(object, destroyStatusState, LV_EVENT_DELETE, data);
  arrangeStatusState(object, width);
  return object;
}
} // namespace penchat
