#include "app.hpp"
namespace penchat {
void App::pullSensitivityPicker() {
  modal("上滑新建灵敏度", "");
  auto cancel = lv_obj_get_child(modal_, -1);
  lv_label_set_text(lv_obj_get_child(cancel, 0), "取消");
  auto value = std::make_shared<int>(pullSensitivityOffset_);
  auto caption = label(modal_, "", 15, 43, screen_.width - 140, 20);
  auto update = [value, caption] {
    const auto text = std::string("偏移：") + (*value > 0 ? "+" : "") +
        std::to_string(*value) + "%" + (*value ? "" : "（新版基准）");
    lv_label_set_text(caption, text.c_str());
  };
  label(modal_, "负值更稳，正值更灵敏；只调整拉满进度所需的距离。", 15, 76,
        screen_.width - 145, 16, theme::muted);
  auto slider = lv_slider_create(modal_);
  pullSlider_ = slider;
  lv_obj_add_event_cb(modal_, [](lv_event_t *e) {
    static_cast<App *>(lv_event_get_user_data(e))->pullSlider_ = nullptr;
  }, LV_EVENT_DELETE, this);
  lv_obj_remove_style_all(slider);
  lv_obj_set_pos(slider, 100, 120);
  lv_obj_set_size(slider, screen_.width - 320, 16);
  lv_obj_set_ext_click_area(slider, 14);
  lv_slider_set_range(slider, -25, 25);
  lv_slider_set_value(slider, *value, LV_ANIM_OFF);
  for (auto part : {LV_PART_MAIN, LV_PART_INDICATOR, LV_PART_KNOB}) {
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, part);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, part);
    lv_obj_set_style_bg_color(slider, lv_color_hex(part == LV_PART_MAIN ? theme::border :
                                                  part == LV_PART_KNOB ? theme::text : theme::accent), part);
  }
  lv_obj_set_style_pad_all(slider, 7, LV_PART_KNOB);
  struct State { std::shared_ptr<int> value; std::function<void()> update; };
  auto state = new State{value, update};
  lv_obj_add_event_cb(slider, [](lv_event_t *e) {
    auto state = static_cast<State *>(lv_event_get_user_data(e));
    if (lv_event_get_code(e) == LV_EVENT_DELETE) { delete state; return; }
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
      *state->value = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
      state->update();
    }
  }, LV_EVENT_ALL, state);
  label(modal_, "-25%", 15, 109, 80, 18, theme::muted);
  label(modal_, "+25%", screen_.width - 200, 109, 80, 18, theme::muted);
  button(modal_, "恢复基准", 15, screen_.height - 66, 136, 44, [value, slider, update] {
    *value = 0; lv_slider_set_value(slider, 0, LV_ANIM_OFF); update();
  });
  auto save = button(modal_, "保存", screen_.width - 325, screen_.height - 66, 100, 44,
                    [this, value] { events_.push("pull_sensitivity_apply", {{"offset", *value}}); });
  primary(save);
  update();
}
} // namespace penchat
