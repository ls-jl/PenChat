#include "app.hpp"
#include "usage.hpp"
#include <iomanip>
#include <sstream>
namespace penchat {
namespace {
std::string number(int64_t value) {
  auto text = std::to_string(value);
  for (int i = int(text.size()) - 3; i > 0; i -= 3) text.insert(i, ",");
  return text;
}
std::string tokenValue(const Json &usage, const char *key) {
  auto found = usage.find(key);
  return found != usage.end() && found->is_number_integer()
             ? number(found->get<int64_t>()) : "未提供";
}
std::string ratio(int64_t cached, int64_t input) {
  if (input <= 0) return "不可计算";
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << (double(cached) * 100 / input) << "%";
  return out.str();
}
std::string cacheRate(const Json &usage) {
  if (!usage.contains("cached_tokens") || !usage.contains("input_tokens")) return "未提供";
  return ratio(usage["cached_tokens"], usage["input_tokens"]);
}
}
void App::openSessionInfo() {
  closeAttachments();
  ++sessionInfoGeneration_;
  sessionInfoSession_ = session_;
  sessionInfoData_ = Json::object();
  refreshSessionInfo();
  show("session_info");
}
void App::refreshSessionInfo() {
  if (sessionInfoBusy_) { sessionInfoPending_ = true; return; }
  if (sessionInfoWorker_.joinable()) sessionInfoWorker_.join();
  sessionInfoBusy_ = true;
  sessionInfoPending_ = false;
  const auto token = ++sessionInfoGeneration_;
  const auto source = sessionInfoSession_;
  const auto model = currentModel();
  Json pending = {{"draft", draft_},
                  {"system_prompt", sessionPreferences_.value("system_prompt", std::string())},
                  {"editing", !messageEdit_.empty()},
                  {"pivot", messageEdit_.value("pivot", int64_t(0))}, {"images", messageEditImages_}};
  if (generating_)
    pending["live"] = {{"id", assistant_}, {"output", output_}, {"metadata", reasoningMetadata_}};
  sessionInfoWorker_ = std::thread([this, token, source, model, pending] {
    Json data;
    try {
      // A read snapshot and any pending persistence wait stay off the UI thread.
      db_.flush();
      data = sessionMetrics(db_.contextSnapshot(source), model, pending);
    } catch (...) {
      data = {{"error", "无法读取会话信息。请检查保存状态或会话大小，然后刷新。"}};
    }
    events_.push("session_info_result", {{"token", token}, {"session", source}, {"data", data}});
  });
}
bool App::sessionInfoEvent(const Event &event) {
  if (event.type == "session_info_refresh") {
    if (view_ == "session_info") { refreshSessionInfo(); build(); }
    return true;
  }
  if (event.type != "session_info_result") return false;
  if (view_ == "session_info" && touchDown_) {
    events_.push(event.type, event.data);
    return true; // Never rebuild the reader underneath an active gesture.
  }
  if (sessionInfoWorker_.joinable()) sessionInfoWorker_.join();
  sessionInfoBusy_ = false;
  const bool reader = view_ == "session_info";
  const bool menu = view_ == "chat" && attachments_ && popupKind_ == "attachments";
  if ((reader || menu) && event.data.value("session", std::string()) == sessionInfoSession_ &&
      event.data.value("token", uint64_t(0)) == sessionInfoGeneration_) {
    sessionInfoData_ = event.data.at("data");
    if (reader) build(); else updateContextUsage();
  }
  if ((reader || menu) && sessionInfoPending_) { refreshSessionInfo(); if (reader) build(); }
  return true;
}
void App::updateContextUsage() {
  if (!contextUsageArc_ || !contextUsageLabel_) return;
  const auto &data = sessionInfoData_;
  std::string percent = data.contains("error") ? "未提供" : "未配置";
  int value = 0;
  const auto budget = data.value("budget", Json());
  const bool known = !data.contains("error") && budget.is_number_integer() && budget.get<int64_t>() > 0;
  if (known) {
    const auto estimated = data.value("estimated_tokens", int64_t(0));
    percent = ratio(estimated, budget);
    const double fraction = std::max(0., std::min(1., double(estimated) / budget.get<int64_t>()));
    value = int(fraction * 1000 + .5);
  }
  lv_arc_set_value(contextUsageArc_, value);
  lv_obj_set_style_arc_opa(contextUsageArc_, known ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
  const auto caption = "压缩上下文（" + percent + "）";
  lv_label_set_text(contextUsageLabel_, caption.c_str());
  // Keep the percentage whole, including over-budget values, without changing
  // row heights or the user's menu scroll position.
  lv_point_t measured;
  lv_text_get_size(&measured, caption.c_str(), font(20), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  auto menu = lv_obj_get_parent(lv_obj_get_parent(contextUsageLabel_));
  const int width = std::min(screen_.width - 126, std::max(320, int(measured.x) + 76));
  lv_obj_set_width(menu, width);
  for (const auto &entry : attachmentEntries_) {
    lv_obj_set_width(entry.second, width - 52);
    lv_obj_set_width(lv_obj_get_child(entry.second, 0), width - 76);
  }
}
Json App::contextUsageDiagnostic() const {
  if (!contextUsageArc_ || !contextUsageLabel_) return Json();
  auto bounds = [](lv_obj_t *object) { lv_area_t a; lv_obj_get_coords(object, &a); return Json::array({a.x1,a.y1,a.x2,a.y2}); };
  return {{"caption", lv_label_get_text(contextUsageLabel_)}, {"value", lv_arc_get_value(contextUsageArc_)},
          {"ring", bounds(contextUsageArc_)}, {"label", bounds(contextUsageLabel_)},
          {"ready", !sessionInfoData_.empty()}, {"budget", sessionInfoData_.value("budget", Json())},
          {"estimated_tokens", sessionInfoData_.value("estimated_tokens", Json())}};
}
void App::sessionInfoPage() {
  pageHeader("会话信息");
  auto refresh = pageAction("刷新", 24, 100, [this] { events_.push("session_info_refresh"); });
  if (sessionInfoBusy_) lv_obj_add_state(refresh, LV_STATE_DISABLED);
  const int width = screen_.width - 118, column = (width - 16) / 2;
  const auto data = sessionInfoData_;
  if (data.empty() || data.contains("error")) {
    label(pageContent_, data.empty() ? "正在读取会话信息…" : data["error"].get<std::string>(),
          10, 12, width, 20, theme::muted);
    return;
  }
  auto single = [this](const std::string &text, int x, int y, int width, int size, uint32_t color) {
    auto object = label(pageContent_, text, x, y, width, size, color);
    lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(object, font(size)->line_height);
  };
  auto metric = [&](const std::string &name, const std::string &value, int col, int y) {
    const int x = 10 + col * (column + 16);
    single(name, x, y, column, 17, theme::muted);
    single(value, x, y + 23, column, 21, theme::text);
  };
  auto model = data.value("model", std::string());
  single(model.empty() ? "尚未配置聊天模型" : data.value("provider", std::string()) + " / " + model,
         10, 4, width, 17, theme::muted);
  metric("当前上下文 · 估算 token", "≈ " + number(data.at("estimated_tokens")), 0, 34);
  metric("最大长度 · 本地预算", data["budget"].is_null() ? "未配置模型" : number(data["budget"]) + " token", 1, 34);
  single(data["budget"].is_null() ? "服务端上下文上限未提供" :
         "已占本地预算 " + ratio(data["estimated_tokens"], data["budget"]) + "；服务端上限未提供",
         10, 92, width, 17, theme::muted);
  const auto latestModel = data.value("latest_model", std::string());
  single("最近一次请求" + (latestModel.empty() ? "" : " · " + latestModel), 10, 126, width, 20, theme::text);
  const auto latest = data.at("latest");
  metric("输入 token", tokenValue(latest, "input_tokens"), 0, 160);
  metric("输出 token · 含推理", tokenValue(latest, "output_tokens"), 1, 160);
  metric("缓存命中率 · token 占比", cacheRate(latest), 0, 218);
  metric("其中推理 token", tokenValue(latest, "reasoning_tokens"), 1, 218);
  metric("总 token", tokenValue(latest, "total_tokens"), 0, 276);
  metric("缓存读取 / 写入 token", tokenValue(latest, "cached_tokens") + " / " + tokenValue(latest, "cache_write_tokens"), 1, 276);
  single(data.value("latest_status", std::string()) == "streaming" ? "生成中，用量等待接口返回；可刷新查看快照" :
         "接口未返回的指标显示“未提供”，中断请求也可能缺少用量", 10, 338, width, 17, theme::muted);
  single("会话累计 · 当前保留的回复", 10, 376, width, 20, theme::text);
  const auto totals = data.at("totals");
  metric("已记录输入 token", tokenValue(totals, "input_tokens"), 0, 410);
  metric("已记录输出 token", tokenValue(totals, "output_tokens"), 1, 410);
  metric("已记录总 token", tokenValue(totals, "total_tokens"), 0, 468);
  metric("已记录推理 token", tokenValue(totals, "reasoning_tokens"), 1, 468);
  metric("累计缓存命中率 · 加权", data.value("cache_samples", 0) ?
         ratio(data["cache_read_tokens"], data["cache_input_tokens"]) + " · " + number(data["cache_samples"]) + " 条样本" : "未提供", 0, 526);
  metric("收到用量 / AI 回复数", number(data["reported_replies"]) + " / " + number(data["replies"]), 1, 526);
  metric("消息数 / 图片数", number(data["messages"]) + " / " + number(data["images"]), 0, 584);
  metric("上下文摘要", data.value("editing", false) ? "编辑预览 · 不沿用旧摘要" :
         data.value("compressed", false) ? "已使用有效摘要" :
         data.value("compression_invalid", false) ? "摘要已失效 · 按全文估算" : "未压缩", 1, 584);
  label(pageContent_,
        "上下文按当前模型、系统提示词、有效摘要、历史和待发送内容估算；图片单独估算，非精确分词。发送时的自动压缩或重试会改变上下文。\n\n"
        "用量来自接口，不回填旧消息；各指标只累计已返回该字段的回复。缓存命中率＝已缓存输入 token ÷ 对应输入 token，不是请求成功率。\n\n"
        "累计仅统计当前保留回复，不包含被覆盖回复、自动命名和压缩请求；推理 token 已包含在输出中，不再重复相加。这里不是账单或费用统计。",
        10, 654, width, 17, theme::muted);
}
} // namespace penchat
