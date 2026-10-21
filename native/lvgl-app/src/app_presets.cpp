#include "app.hpp"
namespace penchat {
static bool presetHasText(const std::string &text) {
  return text.find_first_not_of(" \t\r\n") != std::string::npos;
}
bool App::emptyForPreset() {
  return !sessionPreferences_.value("has_sent", false) &&
         db_.query("SELECT id FROM chat_messages WHERE session_id=? AND role='assistant' LIMIT 1", {session_}).empty();
}
std::string App::presetSummary(const Json &p) {
  auto id = p.value("model_id", std::string());
  std::string model = id.empty() ? "沿用模型" : "模型不可用";
  for (const auto &m : db_.models()) if (m["id"] == id) model = m.value("name", std::string());
  auto effort = p.value("effort", std::string());
  auto mode = p.value("prompt_mode", std::string("inherit"));
  return model + " · " + (effort.empty() ? "沿用强度" : effort == "api_default" ? "API 默认" : effortLabel(effort)) +
         " · " + (mode == "custom" ? "自定义提示词" : mode == "clear" ? "清空提示词" : "沿用提示词");
}
void App::applyPreset(const Json &preset) {
  std::string warning;
  sessionPreferences_ = applyPresetSnapshot(sessionPreferences_, preset, db_.models(),
                                            currentModel(), emptyForPreset(), warning);
  savePreferences();
  build();
  if (!warning.empty()) modal("预设已部分应用", warning);
}
void App::presetPicker() {
  closeAttachments();
  resetPull();
  attachments_ = box(screenObj_, 86, 0, screen_.width - 86, screen_.height);
  popupKind_ = "presets";
  lv_obj_set_style_bg_color(attachments_, lv_color_hex(theme::scrim), 0);
  lv_obj_set_style_bg_opa(attachments_, LV_OPA_50, 0);
  lv_obj_set_scrollable(attachments_, false);
  auto backdrop = button(attachments_, "", 0, 0, screen_.width - 86, screen_.height,
                         [this] { closeAttachments(); });
  lv_obj_set_style_bg_opa(backdrop, LV_OPA_TRANSP, 0);
  auto panel = box(attachments_, 20, 12, screen_.width - 130, 188, theme::surface);
  lv_obj_set_style_radius(panel, 16, 0);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  lv_obj_set_scroll_elastic(panel, false);
  lv_obj_set_scroll_momentum(panel, false);
  auto current = sessionPreferences_.value("preset_name", std::string("未选择预设"));
  if (sessionPreferences_.value("preset_adjusted", false)) current += " · 已调整";
  auto heading = label(panel, "当前：" + current, 14, 4, screen_.width - 168, 17, theme::muted);
  lv_label_set_long_mode(heading, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(heading, 24);
  int y = 32;
  for (const auto &p : presetList(db_.settings())) {
    bool selected = p["id"] == sessionPreferences_.value("preset_id", std::string());
    auto title = (selected ? "● " : "○ ") + p.value("name", std::string());
    if (selected && sessionPreferences_.value("preset_adjusted", false)) title += " · 已调整";
    button(panel, title, 8, y, screen_.width - 154, 44,
           [this, p] { applyPreset(p); }, theme::surface, LV_TEXT_ALIGN_LEFT);
    auto caption = label(panel, presetSummary(p), 22, y + 44, screen_.width - 180, 16, theme::muted);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(caption, 22);
    lv_obj_set_clickable(caption, false);
    y += 72;
  }
}
void App::presetsPage() {
  pageHeader("会话预设");
  const int width = screen_.width - 118, column = (width - 14) / 2;
  bool enabled = db_.settings().value("presets_enabled", false);
  button(body_, enabled ? "启用预设入口：开" : "启用预设入口：关", 10, 4, column, 48,
         [this, enabled] { db_.setting("presets_enabled", !enabled); build(); });
  auto add = button(screenObj_, "新增预设", 806, 2, 130, 44, [this] {
    presetForm_ = defaultPreset();
    presetForm_["id"] = uid("preset");
    presetForm_["name"] = "新预设";
    show("preset_edit");
  });
  primary(add);
  label(body_, "关闭只隐藏入口\n已有会话配置继续生效", 24 + column, 7, column, 17, theme::muted);
  int y = 64;
  for (const auto &p : presetList(db_.settings())) {
    auto row = button(body_, p.value("name", std::string()), 10, y, width, 64,
                      [this, p] { presetForm_ = p; show("preset_edit"); }, theme::surface, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_TOP_LEFT, 12, 6);
    auto detail = label(row, presetSummary(p), 12, 36, width - 24, 16, theme::muted);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(detail, 21);
    lv_obj_set_clickable(detail, false);
    y += 72;
  }
}
void App::presetChoices(const std::string &field) {
  const bool modelField = field == "model_id";
  modal(modelField ? "预设模型" : field == "effort" ? "预设推理强度" : "系统提示词", "");
  auto panel = lv_obj_get_child(modal_, 1);
  lv_obj_clean(panel);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  lv_obj_set_scroll_elastic(panel, false);
  lv_obj_set_scroll_momentum(panel, false);
  lv_obj_set_height(lv_obj_get_child(modal_, -1), 44);
  int y = 0;
  auto option = [&](const std::string &value, const std::string &name) {
    button(panel, (presetForm_.value(field, std::string()) == value ? "● " : "○ ") + name,
           4, y, screen_.width - 144, 44, [this, field, value] {
             presetForm_[field] = value;
             build();
           }, theme::surface, LV_TEXT_ALIGN_LEFT);
    y += 48;
  };
  if (field == "prompt_mode") {
    option("inherit", "沿用会话"); option("custom", "自定义"); option("clear", "清空");
  } else if (modelField) {
    option("", "沿用会话");
    for (const auto &provider : db_.providers()) {
      label(panel, provider.value("name", std::string()), 12, y, screen_.width - 160, 17, theme::muted);
      y += 26;
      for (const auto &m : db_.models()) if (m["provider_id"] == provider["id"])
        option(m["id"], m.value("name", std::string()));
    }
  } else {
    option("", "沿用会话");
    auto id = presetForm_.value("model_id", std::string());
    Json model;
    for (const auto &m : db_.models()) if (m["id"] == id) model = m;
    std::vector<std::string> choices;
    if (id.empty()) { choices = effortOrder(); choices.insert(choices.begin() + 1, "api_default"); }
    else if (model.is_object()) choices = effortChoices(model);
    else choices = {"api_default"};
    for (const auto &key : choices) option(key, key == "api_default" ? "API 默认（不传参数）" : effortLabel(key));
  }
}
void App::presetEditor() {
  pageHeader("编辑预设");
  const int width = screen_.width - 118, column = (width - 14) / 2, right = 24 + column;
  auto save = button(screenObj_, "保存", 806, 2, 130, 44, [this] {
    if (!presetHasText(presetForm_.value("name", std::string()))) { modal("请输入预设名称", "名称不能为空。"); return; }
    if (presetForm_.value("prompt_mode", std::string()) == "custom" && !presetHasText(presetForm_.value("prompt", std::string()))) {
      modal("请输入系统提示词", "若需要移除提示词，请选择“清空”。"); return;
    }
    auto id = presetForm_.value("model_id", std::string());
    if (!id.empty()) {
      Json model;
      for (const auto &m : db_.models()) if (m["id"] == id) model = m;
      if (model.is_null() || !presetSupports(model, presetForm_.value("effort", std::string()))) {
        modal("请检查模型与强度", "模型已不可用或不支持所选强度，请重新选择。"); return;
      }
    }
    auto presets = presetList(db_.settings());
    bool replaced = false;
    for (auto &p : presets) if (p["id"] == presetForm_["id"]) { p = presetForm_; replaced = true; break; }
    if (!replaced) presets.push_back(presetForm_);
    db_.setting("chat_presets", presets);
    presetForm_ = Json::object();
    show("presets");
  });
  primary(save);
  auto action = [&](const std::string &text, int x, int y, std::function<void()> callback) {
    auto separator = text.find("：");
    if (separator != std::string::npos)
      fieldButton(body_, text.substr(0, separator), text.substr(separator + 3), x, y, column, callback);
    else {
      auto object = button(body_, text, x, y, column, 48, callback, theme::surface, LV_TEXT_ALIGN_LEFT);
      if (text == "删除预设") lv_obj_set_style_text_color(lv_obj_get_child(object, 0), lv_color_hex(theme::danger), 0);
    }
  };
  action("名称：" + presetForm_.value("name", std::string()), 10, 4, [this] {
    if (presetForm_["id"] == "default") { modal("默认预设", "默认预设始终存在，名称固定；其他配置可以修改。"); return; }
    edit(presetForm_.value("name", std::string()), "预设名称", [this](const std::string &v) { presetForm_["name"] = v; build(); }, 32);
  });
  auto mode = presetForm_.value("prompt_mode", std::string("inherit"));
  action(std::string("系统提示词：") + (mode == "custom" ? "自定义" : mode == "clear" ? "清空" : "沿用会话"), right, 4,
         [this] { presetChoices("prompt_mode"); });
  std::string name = "沿用会话";
  auto id = presetForm_.value("model_id", std::string());
  if (!id.empty()) {
    name = "模型不可用";
    for (const auto &m : db_.models()) if (m["id"] == id) name = m.value("name", std::string());
  }
  action("模型：" + name, 10, 62, [this] { presetChoices("model_id"); });
  auto effort = presetForm_.value("effort", std::string());
  action("推理强度：" + (effort.empty() ? std::string("沿用会话") : effort == "api_default" ? "API 默认" : effortLabel(effort)), right, 62,
         [this] { presetChoices("effort"); });
  auto timing = [&](const std::string &field, int x) {
    bool always = presetForm_.value(field, std::string("empty")) == "always";
    label(body_, "应用时机", x, 120, column, 16, theme::muted);
    for (int i = 0; i < 2; ++i) {
      const std::string value = i ? "always" : "empty";
      const int segment = (column - 8) / 2;
      auto choice = button(body_, ((always == bool(i)) ? "● " : "○ ") + std::string(i ? "每次切换" : "仅空白会话"),
                           x + i * (segment + 8), 144, segment, 44,
                           [this, field, value] { presetForm_[field] = value; build(); }, theme::surface);
      if (always == bool(i)) {
        lv_obj_set_style_border_width(choice, 1, 0);
        lv_obj_set_style_border_color(choice, lv_color_hex(theme::muted), 0);
      }
    }
  };
  if (!id.empty()) timing("model_when", 10);
  if (!effort.empty()) timing("effort_when", right);
  if (mode == "custom") {
    fieldButton(body_, "系统提示词", presetForm_.value("prompt", std::string("点击编辑")), 10, 202, width, [this] {
      edit(presetForm_.value("prompt", std::string()), "系统提示词（可输入多行）", [this](const std::string &v) { presetForm_["prompt"] = v; build(); }, 8192, "ZhCNPreferred", {}, true);
    });
    label(body_, "应用后替换会话提示词；需要移除时请选择“清空”。", 10, 260, width, 16, theme::muted);
  }
  if (presetForm_["id"] != "default") action("删除预设", right, mode == "custom" ? 294 : 202, [this] {
    modal("删除预设", "已有会话仍保留当前配置。确定删除此预设？");
    button(modal_, "确定删除", 16, screen_.height - 66, 160, 44, [this] {
      auto presets = presetList(db_.settings());
      Json kept = Json::array();
      for (const auto &p : presets) if (p["id"] != presetForm_["id"]) kept.push_back(p);
      db_.setting("chat_presets", kept);
      presetForm_ = Json::object();
      show("presets");
    });
  });
}
} // namespace penchat
