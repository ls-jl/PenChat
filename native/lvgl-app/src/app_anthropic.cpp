#include "app.hpp"
#include "context.hpp"
namespace penchat {
void App::protocolPicker() {
  modal("选择接口", "");
  const auto current = providerForm_.value("api", std::string("responses"));
  int y = 42;
  for (const auto &entry : {std::make_pair("responses", "Responses"),
                            std::make_pair("chat", "Chat Completions"),
                            std::make_pair("anthropic", "Anthropic Messages")}) {
    const std::string key = entry.first, title = entry.second;
    button(modal_, std::string(current == key ? "● " : "○ ") + title, 15, y,
           screen_.width - 130, 44, [this, key] {
      const auto previous = providerForm_.value("api", std::string("responses"));
      const auto address = providerForm_.value("baseURL", std::string());
      if (address.empty() || (previous != "anthropic" && address == "https://api.openai.com/v1") ||
          (previous == "anthropic" && address == "https://api.anthropic.com/v1"))
        providerForm_["baseURL"] = key == "anthropic" ? "https://api.anthropic.com/v1" : "https://api.openai.com/v1";
      if (providerForm_.value("name", std::string()) == "OpenAI" && key == "anthropic") providerForm_["name"] = "Anthropic";
      providerForm_["api"] = key; build();
    }, theme::surface, LV_TEXT_ALIGN_LEFT);
    y += 48;
  }
}
void App::anthropicConnectionPage() {
  pageHeader("Anthropic 连接设置");
  const int width = screen_.width - 118, column = (width - 16) / 2;
  const auto config = objectField(providerForm_, "anthropic");
  const auto auth = config.value("auth", std::string("x-api-key"));
  const auto version = config.value("version", std::string("2023-06-01"));
  settingsRow("anthropic_auth", "鉴权方式", auth == "bearer" ? "Authorization · Bearer" : "x-api-key",
              10, 4, column, [this, auth] {
    auto next = objectField(providerForm_, "anthropic");
    next["auth"] = auth == "bearer" ? "x-api-key" : "bearer";
    providerForm_["anthropic"] = next; build();
  });
  settingsRow("anthropic_version", "API 版本", version, 26 + column, 4, column, [this, version] {
    auto next = objectField(providerForm_, "anthropic");
    next["version"] = version == "2023-06-01" ? "2026-10-21" : "2023-06-01";
    providerForm_["anthropic"] = next; build();
  });
  const auto headers = providerForm_.value("headers", Json::object());
  settingsRow("anthropic_headers", "自定义请求头", headers.empty() ? "未设置 · 可填写 Workspace 或中转参数" :
              "已设置 " + std::to_string(headers.size()) + " 项 · 点击编辑", 10, 68, width - 20, [this, headers] {
    edit(headers.empty() ? "{}" : headers.dump(), "自定义请求头（JSON 对象）", [this](const std::string &value) {
      const auto parsed = Json::parse(value, nullptr, false);
      bool valid = parsed.is_object() && parsed.size() <= 32;
      if (valid) for (auto i = parsed.begin(); i != parsed.end(); ++i)
        if (!i->is_string() || i.key().empty() || i.key().find_first_of("\r\n:") != std::string::npos ||
            i->get<std::string>().find_first_of("\r\n") != std::string::npos) valid = false;
      if (!valid) { modal("请求头无效", "请填写最多32项的 JSON 对象，名称与值均为文本且不含换行。"); return; }
      providerForm_["headers"] = parsed; build();
    }, 4096, "EnUSOnly", {}, false, true);
  });
  label(pageContent_, "返回后保存 Provider 才会生效。两种鉴权不能同时发送；自定义请求头不进入聊天导出或诊断。"
        "\n\n2023-06-01 用于广泛兼容；2026-10-21 按官方新版协议发送。连接失败不会自动切换版本或重试。",
        10, 140, width - 20, 17, theme::muted);
}
void App::anthropicModelPage() {
  pageHeader("模型设置");
  const int width = screen_.width - 118, column = (width - 16) / 2;
  auto &caps = modelForm_["reasoning"];
  const auto mode = caps.value("mode", std::string("off"));
  auto save = pageAction("保存", 24, 130, [this] {
    try {
      auto next = modelForm_;
      auto capabilities = objectField(next, "capabilities_json");
      capabilities["max_output_tokens"] = modelForm_.value("max_output_tokens", anthropicMaxOutput(modelForm_));
      next["capabilities_json"] = capabilities;
      validateAnthropic(next, modelForm_["reasoning"]);
      db_.saveModelCapabilities(modelForm_.at("id"), modelForm_["reasoning"],
          modelForm_.value("context_budget", contextBudget(modelForm_)), anthropicMaxOutput(next));
      show("provider");
    } catch (const std::exception &e) { modal("模型设置未保存", e.what()); }
  });
  primary(save);
  pageAction(modelAdvanced_ ? "档位选择" : "参数映射", 170, 140, [this] { modelAdvanced_ = !modelAdvanced_; build(); });
  auto title = label(pageContent_, modelForm_.value("name", std::string()), 10, 0, width, 19, theme::muted);
  lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS); lv_obj_set_height(title, 22);
  settingsRow("thinking_mode", "思考模式", mode == "adaptive" ? "自适应强度" : mode == "budget" ? "固定 token 预算" : "不声明 · API 默认",
              10, 30, column, [this, mode] {
    const auto nextMode = mode == "off" ? "adaptive" : mode == "adaptive" ? "budget" : "off";
    Json levels = Json::object();
    if (std::string(nextMode) != "off") levels = {{"medium", std::string(nextMode) == "budget" ? "4096" : "medium"},
                                                {"high", std::string(nextMode) == "budget" ? "8192" : "high"}};
    auto next = modelForm_["reasoning"]; next["mode"] = nextMode; next["levels"] = levels;
    modelForm_["reasoning"] = next; build();
  });
  auto maximum = modelForm_.value("max_output_tokens", anthropicMaxOutput(modelForm_));
  settingsRow("max_output", "最大输出 token", std::to_string(maximum) + " · 含思考和正文",
              26 + column, 30, column, [this, maximum] {
    edit(std::to_string(maximum), "最大输出 token", [this](const std::string &value) {
      try {
        size_t used; const int n = std::stoi(value, &used);
        if (used != value.size() || n < 1 || n > 1048576) throw std::runtime_error("请输入1～1048576之间的整数");
        const int limit = anthropicOutputLimit(modelForm_);
        if (limit && n > limit) throw std::runtime_error("超过此模型已知的最大输出上限");
        modelForm_["max_output_tokens"] = n; build();
      } catch (const std::exception &e) { modal("最大输出未修改", e.what()); }
    }, 7, "EnUSOnly", {}, false);
  });
  int y = 102, n = 0;
  if (mode != "off") {
    auto levels = caps.value("levels", Json::object());
    for (const auto &key : effortOrder()) {
      if (modelAdvanced_ && !levels.contains(key)) continue;
      if (modelAdvanced_) {
        settingsRow("thinking_" + key, effortLabel(key), levels[key].get<std::string>() + (mode == "budget" && key != "none" ? " token" : ""),
                    10, y, width - 20, [this, key] {
          edit(modelForm_["reasoning"]["levels"][key].get<std::string>(), "此档 API 参数值", [this, key](const std::string &value) {
            if (value.find_first_not_of(" \t\r\n") != std::string::npos) modelForm_["reasoning"]["levels"][key] = value;
            build();
          }, 32, "EnUSOnly", {}, false);
        });
        y += 64;
      } else {
        auto b = button(pageContent_, std::string(levels.contains(key) ? "● " : "○ ") + effortLabel(key),
               10 + (n % 2) * (column + 16), y + (n / 2) * 48, column, 44, [this, key, mode] {
          auto &choices = modelForm_["reasoning"]["levels"];
          if (choices.contains(key)) choices.erase(key);
          else choices[key] = mode == "budget" || key == "none" ? anthropicBudget(key) : key;
          build();
        }, theme::surface, LV_TEXT_ALIGN_LEFT);
        if (key == "none" && !caps.value("disable", false)) lv_obj_add_state(b, LV_STATE_DISABLED);
        ++n;
      }
    }
    if (!modelAdvanced_) y += ((n + 1) / 2) * 48;
    y += 8;
    settingsRow("thinking_disable", "支持关闭思考", "仅在模型确实支持时启用即时档", 10, y, column, [this] {
      auto &next = modelForm_["reasoning"]; const bool enabled = next.value("disable", false);
      next["disable"] = !enabled; if (enabled) next["levels"].erase("none"); build();
    }, caps.value("disable", false));
    settingsRow("thinking_summary", "请求可读思考", "仅影响下一次请求", 26 + column, y, column, [this] {
      auto &next = modelForm_["reasoning"]; const bool enabled = next.value("summary", false);
      next["summary"] = !enabled; build();
    }, caps.value("summary", false)); y += 64;
    if (modelAdvanced_) {
      settingsRow("thinking_display", "支持 display 参数", "未知中转默认不发送该扩展", 10, y, width - 20, [this] {
        auto &next = modelForm_["reasoning"]; const bool enabled = next.value("display_supported", false);
        next["display_supported"] = !enabled; build();
      }, caps.value("display_supported", false)); y += 64;
    }
  }
  settingsRow("anthropic_context_budget", "上下文预算 · 本地估算",
              std::to_string(modelForm_.value("context_budget", contextBudget(modelForm_))) + " token · 75%时自动压缩",
              10, y, width - 20, [this] {
    edit(std::to_string(modelForm_.value("context_budget", contextBudget(modelForm_))), "上下文预算（1024～2097152）",
         [this](const std::string &value) {
      try { size_t used; const int n = std::stoi(value, &used);
        if (used != value.size() || n < 1024 || n > 2097152) throw std::runtime_error("请输入1024～2097152之间的整数");
        modelForm_["context_budget"] = n; build();
      } catch (const std::exception &e) { modal("预算未修改", e.what()); }
    }, 7, "EnUSOnly", {}, false);
  });
  label(pageContent_, "API 默认不发送强度或预算，也不擅自开启思考。固定预算须小于最大输出；修改模式会初始化档位映射。"
        "\n未知模型请按服务商文档配置，不会自动降档或重试。", 10, y + 72, width - 20, 17, theme::muted);
}
} // namespace penchat
