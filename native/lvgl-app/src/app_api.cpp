#include "app.hpp"
namespace penchat {
void App::apiCompatibilityPage() {
  pageHeader("API 兼容模式");
  const int width = screen_.width - 118;
  const int column = (width - 36) / 2;
  const int right = 26 + column;
  const auto config = objectField(providerForm_, "compatibility");
  const bool anthropic = providerForm_.value("api", std::string()) == "anthropic";
  const bool enabled = config.value("enabled", false);
  auto toggle = [&](const char *key, const char *title, const char *detail,
                    int x, int y, int span, bool fallback) {
    const bool checked = config.value(key, fallback);
    settingsRow(key, title, detail, x, y, span, [this, key, checked] {
      auto next = objectField(providerForm_, "compatibility");
      next[key] = !checked; providerForm_["compatibility"] = next; build();
    }, checked);
  };
  toggle("enabled", "启用兼容模式", "仅此 Provider 生效，返回后保存 Provider", 10, 4, width - 20, false);
  int timeoutY = 76;
  if (enabled) {
    toggle("streaming", "流式输出", "关闭后等待完整回复", 10, 76, column, true);
    if (!anthropic) toggle("usage", "流式用量统计", "不支持 stream_options 时关闭", right, 76, column, true);
    toggle("summary", anthropic ? "请求可读思考" : "公开思考摘要",
           anthropic ? "支持 display 参数时请求总结" : "关闭后不请求公开总结", 10, 140, column, true);
    toggle("replay", "加密思考续传", "关闭后仅用历史正文继续", right, 140, column, true);
    toggle("http1", "强制 HTTP/1.1", "仅在代理连接不良时启用", 10, 204, column, false);
    timeoutY = 204;
  }
  const auto timeout = apiOptions({{"provider_metadata", Json({{"compatibility", config}}).dump()}}).timeout;
  settingsRow("timeout", "请求超时", std::to_string(timeout) + "秒 · 连接超时15秒",
              enabled ? right : 10, timeoutY, enabled ? column : width - 20,
              [this, timeout] {
    edit(std::to_string(timeout), "请求超时（30～3600秒）", [this](const std::string &value) {
      try {
        size_t used = 0; const auto seconds = std::stoi(value, &used);
        if (used != value.size() || seconds < 30 || seconds > 3600) throw std::runtime_error("range");
        auto next = objectField(providerForm_, "compatibility"); next["timeout"] = seconds;
        providerForm_["compatibility"] = next; build();
      } catch (...) { modal("超时未修改", "请输入30～3600之间的整数秒数。"); }
    }, 4, "EnUSOnly", {}, false);
  });
  label(pageContent_, "不自动重发、不降低推理强度。关闭统计或思考扩展会减少对应信息；TLS 校验始终开启。",
        10, timeoutY + 72, width - 20, 16, theme::muted);
}
void App::httpSettingsPage() {
  pageHeader("连接保留时间");
  pageAction("恢复默认", 24, 120, [this] {
    db_.setting("http_idle_seconds", 90); setHttpIdleSeconds(90); build();
  });
  const int width = screen_.width - 118;
  const auto seconds = httpIdleSeconds();
  settingsRow("http_idle_seconds", "保留秒数", std::to_string(seconds) + "秒 · 允许15～600秒", 10, 4, width-20, [this, seconds] {
    edit(std::to_string(seconds), "连接保留时间（15～600秒）", [this](const std::string &text) {
      try {
        size_t used=0;const int value=std::stoi(text,&used);
        if(used!=text.size()||value<15||value>600||text.find_first_not_of("0123456789")!=std::string::npos)
          throw std::runtime_error("range");
        db_.setting("http_idle_seconds",value);setHttpIdleSeconds(value);build();
      } catch (...) {modal("保留时间未修改","请输入15～600之间的整数秒数。");}
    },3,"EnUSOnly",{},false);
  });
  label(pageContent_, "请求结束后计时，服务器可能提前断开", 10, 76, width-20, 18, theme::muted);
  label(pageContent_, "全应用生效；后台及官方键盘期间保留至到期，退出应用时释放。", 10, 112, width-20, 16, theme::muted);
}
} // namespace penchat
