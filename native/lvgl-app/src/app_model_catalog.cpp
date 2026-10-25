#include "app.hpp"
#include "context.hpp"
#include "icons.hpp"
#include "model_catalog.hpp"
#include <stdexcept>
namespace penchat {
void App::saveModelForm() {
  auto caps = objectField(modelForm_, "capabilities_json");
  const auto previous = caps;
  caps["reasoning"] = modelForm_.at("reasoning");
  for (const auto &key : {"context_budget", "input_modalities", "vision", "max_input_tokens"})
    if (modelForm_.contains(key)) caps[key] = modelForm_[key];
  if (!caps.contains("context_budget")) caps["context_budget"] = contextBudget(modelForm_);
  if (anthropicProtocol(modelForm_)) {
    caps["max_output_tokens"] = modelForm_.value("max_output_tokens", anthropicMaxOutput(modelForm_));
    auto next = modelForm_; next["capabilities_json"] = caps;
    validateAnthropic(next, caps["reasoning"]);
  }
  auto origin = objectField(caps, "catalog");
  auto overrides = origin.value("overrides", Json::array());
  const auto filled = modelForm_.value("catalog_filled", Json::array());
  const auto recommended = modelRecommendation(modelForm_).at("fields");
  const auto oldOverrides = origin.value("overrides", Json::array());
  for (const std::string key : {"context_budget", "input_modalities", "vision", "max_input_tokens",
                               "reasoning.mode", "reasoning.levels", "reasoning.summary", "reasoning.disable",
                               "reasoning.request_mode", "reasoning.token_budget"}) {
    auto field = [&](const Json &object) {
      return key.compare(0, 10, "reasoning.") == 0 ? objectField(object, "reasoning").value(key.substr(10), Json()) : object.value(key, Json());
    };
    overrides.erase(std::remove(overrides.begin(), overrides.end(), key), overrides.end());
    const bool fromFill = std::find(filled.begin(), filled.end(), key) != filled.end() && field(recommended) == field(caps);
    if (!fromFill && (field(previous) != field(caps) || std::find(oldOverrides.begin(), oldOverrides.end(), key) != oldOverrides.end()))
      overrides.push_back(key);
  }
  origin["overrides"] = overrides; caps["catalog"] = origin;
  db_.saveModelSettings(modelForm_.at("id"), caps);
}
void App::modelCapabilityRows(int y) {
  const int width = screen_.width - 118, col = (width - 36) / 2, right = col + 26;
  const auto recommendation = modelRecommendation(modelForm_);
  const auto match = recommendation.at("match"), fields = recommendation.at("fields");
  const auto stored = objectField(modelForm_, "capabilities_json");
  auto value = [this, stored](const std::string &key) { return modelForm_.value(key, stored.value(key, Json())); };
  auto recommended = [fields](const std::string &key) {
    if (key.compare(0, 10, "reasoning.") == 0) return fields.contains("reasoning") && fields["reasoning"].contains(key.substr(10));
    return fields.contains(key);
  };
  auto fill = [this, fields, match, recommended](const std::string &key) {
    if (!recommended(key)) return;
    if (key.compare(0, 10, "reasoning.") == 0) modelForm_["reasoning"][key.substr(10)] = fields.at("reasoning").at(key.substr(10));
    else modelForm_[key] = fields.at(key);
    if (key == "input_modalities") modelForm_["vision"] = fields.at("vision");
    auto caps = objectField(modelForm_, "capabilities_json");
    caps["catalog"] = {{"rule", match.value("rule", std::string())},
                       {"revision", match.value("revision", std::string())},
                       {"overrides", objectField(caps, "catalog").value("overrides", Json::array())}};
    modelForm_["capabilities_json"] = caps;
    auto done = modelForm_.value("catalog_filled", Json::array()); done.push_back(key);
    if (key == "input_modalities") done.push_back("vision");
    modelForm_["catalog_filled"] = done;
    build();
  };
  auto row = [&](const std::string &key, const std::string &title, const std::string &detail,
                 int x, int at, std::function<void()> action) {
    settingsRow("model_" + key, title, detail, x, at, col - 52, std::move(action));
    auto held = std::make_shared<bool>(false);
    auto object = button(pageContent_, "", x + col - 44, at + 6, 44, 44,
                         [fill, held, key] { if (*held) { *held = false; return; } fill(key); });
    attachIcon(object, "autofill", theme::text);
    lv_obj_set_style_opa(object, LV_OPA_50, LV_STATE_DISABLED);
    if (!recommended(key)) lv_obj_add_state(object, LV_STATE_DISABLED);
    struct Help { App *app; std::shared_ptr<bool> held; lv_point_t start; int64_t distance; };
    auto help = new Help{this, held, {0, 0}, 0};
    lv_obj_add_event_cb(object, [](lv_event_t *e) {
      const auto code = lv_event_get_code(e);
      if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_PRESS_LOST) return;
      auto h = static_cast<Help *>(lv_event_get_user_data(e));
      if (code == LV_EVENT_PRESS_LOST) { h->distance = int64_t(1) << 60; return; }
      auto input = lv_indev_active();
      if (!input) return;
      lv_point_t point; lv_indev_get_point(input, &point);
      if (code == LV_EVENT_PRESSED) { *h->held = false; h->start = point; h->distance = 0; }
      const int64_t dx = point.x - h->start.x, dy = point.y - h->start.y;
      h->distance = std::max(h->distance, dx * dx + dy * dy);
      if (code == LV_EVENT_LONG_PRESSED && h->distance <= 144) {
        *h->held = true;
        h->app->events_.push("catalog_help", {{"id", h->app->modelForm_.value("id", std::string())}});
      }
    }, LV_EVENT_ALL, help);
    lv_obj_add_event_cb(object, [](lv_event_t *e) { delete static_cast<Help *>(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, help);
  };
  row("context_budget", "最大上下文", std::to_string(value("context_budget").is_number_integer() ? value("context_budget").get<int>() : contextBudget(modelForm_)) + " token · 用于压缩判断",
      10, y, [this, value] {
    const auto current = value("context_budget");
    edit(current.is_number_integer() ? std::to_string(current.get<int>()) : std::to_string(contextBudget(modelForm_)),
         "最大上下文（1024～2097152）", [this](const std::string &text) {
      try {
        size_t used; const int n = std::stoi(text, &used);
        if (used != text.size() || n < 1024 || n > 2097152) throw std::runtime_error("range");
        modelForm_["context_budget"] = n; build();
      } catch (...) { modal("最大上下文未修改", "请输入1024～2097152之间的整数。"); }
    }, 7, "EnUSOnly", {}, false);
  });
  const auto inputs = value("input_modalities");
  std::string caption;
  for (const auto &entry : {std::make_pair("text", "文本"), {"image", "图片"}, {"video", "视频"}, {"audio", "音频"}, {"file", "文件"}})
    if (inputs.is_array() && std::find(inputs.begin(), inputs.end(), entry.first) != inputs.end()) caption += (caption.empty() ? "" : "、") + std::string(entry.second);
  row("input_modalities", "输入类型", caption.empty() ? "未知 · 点击配置" : caption, right, y, [this, inputs] {
    // The first release can send images/text only. Other modality facts remain
    // in the catalog without adding unsupported upload paths.
    modal("图片输入能力", "只改变此模型的图片能力；未知不等于不支持。视频、音频和文件记录暂不启用。 ");
    auto list = lv_obj_get_child(modal_, 1); lv_obj_clean(list);
    int at = 0;
    for (const auto &choice : {std::make_pair("unknown", "未知"), {"yes", "支持图片"}, {"no", "仅文本"}}) {
      const std::string key = choice.first;
      button(list, choice.second, 4, at, screen_.width - 145, 44, [this, key, inputs] {
        auto next = inputs.is_array() ? inputs : Json::array({"text"});
        next.erase(std::remove(next.begin(), next.end(), "image"), next.end());
        if (key == "yes") next.push_back("image");
        modelForm_["input_modalities"] = key == "unknown" ? Json() : next;
        modelForm_["vision"] = key == "unknown" ? Json() : Json(key == "yes"); build();
      });
      at += 48;
    }
  });
  row("max_input_tokens", "最大输入", value("max_input_tokens").is_number_integer() ? value("max_input_tokens").dump() + " token" : "未知 · 可手填",
      10, y + 64, [this, value] {
    edit(value("max_input_tokens").is_number_integer() ? value("max_input_tokens").dump() : "", "最大输入 token（留空为未知）", [this](const std::string &text) {
      try {
        if (text.empty()) modelForm_["max_input_tokens"] = nullptr;
        else {
          size_t used; auto n = std::stoi(text, &used);
          if (used != text.size() || n < 1 || n > 2097152) throw std::runtime_error("range");
          modelForm_["max_input_tokens"] = n;
        }
        build();
      } catch (...) { modal("最大输入未修改", "请输入1～2097152之间的整数，或留空。"); }
    }, 7, "EnUSOnly", {}, false);
  });
  const auto mode = modelForm_.at("reasoning").value("mode", std::string("off"));
  row("reasoning.mode", "思考模式", mode == "adaptive" ? "自适应" : mode == "budget" ? "固定预算" : mode == "toggle" ? "思考开关 · 无强度档位" : "强度档位 · 手动配置优先",
      right, y + 64, [this, match] {
    std::string detail = match.value("status", std::string()) == "matched" ? "命中：" + match.value("rule", std::string()) : "没有唯一可靠的匹配结果";
    detail += "\n点击右侧填充图标，仅替换对应字段。";
    if (match.contains("sources")) for (const auto &s : match["sources"]) detail += "\n" + s.get<std::string>();
    modal("能力来源", detail);
  });
  std::string levels;
  auto editedModel = modelForm_;
  auto editedCapabilities = objectField(editedModel, "capabilities_json");
  editedCapabilities["reasoning"] = modelForm_["reasoning"];
  editedModel["capabilities_json"] = editedCapabilities;
  for (const auto &key : supportedEfforts(editedModel))
    levels += (levels.empty() ? "" : "、") + effortLabel(key);
  row("reasoning.levels", "支持的强度", levels.empty() ? "无已配置档位" : levels,
      10, y + 128, [this, levels] { modal("支持的强度", levels.empty() ? "未配置强度档位，可在上方手动选择。" : levels); });
  row("reasoning.summary", "公开思考总结", modelForm_["reasoning"].value("summary", false) ? "请求总结" : "不请求总结",
      right, y + 128, [this] { auto &r = modelForm_["reasoning"]; r["summary"] = !r.value("summary", false); build(); });
  row("reasoning.disable", "支持关闭思考", modelForm_["reasoning"].value("disable", false) ? "支持" : "未声明支持",
      10, y + 192, [this] { auto &r = modelForm_["reasoning"]; r["disable"] = !r.value("disable", false); build(); });
  auto &reasoning = modelForm_["reasoning"];
  const auto required = reasoning.value("adapter", std::string("standard"));
  if (required != "standard" && modelAdapter(modelForm_) == required && !anthropicProtocol(modelForm_)) {
    const auto request = reasoning.value("request_mode", std::string());
    settingsRow("thinking_request", "思考开关", request == "enabled" ? "明确开启" : request == "disabled" ? "明确关闭" : "API 默认 · 不发送开关",
                10, y + 256, col, [this, request] {
      auto &r = modelForm_["reasoning"];
      r["request_mode"] = request.empty() ? "enabled" : request == "enabled" && r.value("disable", false) ? "disabled" : "";
      build();
    });
    if (reasoning.value("budget_supported", false)) {
      settingsRow("thinking_budget", "思考 token 预算", reasoning.contains("token_budget") ? reasoning["token_budget"].dump() : "API 默认 · 不发送预算",
                  right, y + 256, col, [this] {
        const auto &r = modelForm_["reasoning"];
        edit(r.contains("token_budget") ? r["token_budget"].dump() : "", "思考预算（留空为API默认）", [this](const std::string &text) {
          try {
            if (text.empty()) modelForm_["reasoning"].erase("token_budget");
            else { size_t used; int n = std::stoi(text, &used); if (used != text.size() || n < 1 || n > 262144) throw std::runtime_error("range"); modelForm_["reasoning"]["token_budget"] = n; }
            build();
          } catch (...) { modal("预算未修改", "请输入1～262144之间的整数，或留空。"); }
        }, 6, "EnUSOnly", {}, false);
      });
    }
  }
}
void App::modelAdapterPicker() {
  modal("厂商参数适配", "自定义中转默认只发标准字段；明确选厂商后才发送它的扩展。不自动重试或切换协议。");
  auto list = lv_obj_get_child(modal_, 1);
  lv_obj_clean(list);
  int y = 0;
  const auto current = providerForm_.value("model_adapter", std::string("auto"));
  for (const auto &entry : {std::make_pair("auto", "自动 · 仅信任官方地址"), {"standard", "仅标准参数"}, {"deepseek", "DeepSeek"}, {"qwen", "Qwen"}, {"glm", "GLM"}, {"kimi", "Kimi"}, {"minimax", "MiniMax"}}) {
    const std::string key = entry.first;
    button(list, std::string(current == key ? "● " : "○ ") + entry.second, 4, y, screen_.width - 145, 44,
           [this, key] { providerForm_["model_adapter"] = key; build(); }, theme::surface, LV_TEXT_ALIGN_LEFT);
    y += 48;
  }
}
void App::compressionSettings() {
  const int percent = compressionPercent(db_.settings());
  edit(std::to_string(percent), "自动压缩阈值（10%～95%）", [this](const std::string &text) {
    try {
      size_t used; int n = std::stoi(text, &used);
      if (used != text.size() || n < 10 || n > 95) throw std::runtime_error("range");
      db_.setting("context_compression_percent", n); build();
    } catch (...) { modal("阈值未修改", "请输入10～95之间的整数百分比。下一次发送时检查，包含待发内容。"); }
  }, 2, "EnUSOnly", {}, false);
}
} // namespace penchat
