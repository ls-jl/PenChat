#pragma once
#include "common.hpp"
#include "usage.hpp"
#include <map>
#include <set>
#include <stdexcept>
namespace penchat {
// Keep opaque wire blocks separate from UI text; replay only completed messages.
class AnthropicStream {
  int64_t requested_, firstBody_ = 0, thinkingStart_ = 0, thinkingMs_ = 0;
  bool observed_ = false, explicitTiming_ = false, finished_ = false;
  bool started_ = false, stopped_ = false, fatal_ = false;
  std::map<int, Json> blocks_;
  std::set<int> open_;
  Json usage_ = Json::object();
  std::string reason_, error_;
  static int index(const Json &j) {
    const auto v = j.value("index", Json());
    if (!v.is_number_integer() || v.get<double>() < 0 || v.get<double>() > 4095)
      throw std::runtime_error("Messages 内容块索引无效");
    return v.get<int>();
  }
  static std::string text(const Json &j, const char *key) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) throw std::runtime_error("Messages 内容块字段类型无效");
    return it->get<std::string>();
  }
  void block(int i, Json b, int64_t now, bool streaming) {
    if (!b.is_object()) throw std::runtime_error("Messages 内容块无效");
    const auto kind = text(b, "type");
    if (kind == "text") {
      if (!text(b, "text").empty() && !firstBody_) firstBody_ = now;
    } else if (kind == "thinking") {
      const auto value = text(b, "thinking");
      if (b.contains("signature")) text(b, "signature");
      else b["signature"] = "";
      if (!value.empty() || !b.value("signature", std::string()).empty()) observed_ = true;
      if (streaming) { thinkingStart_ = now; explicitTiming_ = true; }
    } else if (kind == "redacted_thinking") {
      if (text(b, "data").empty()) throw std::runtime_error("Messages 脱敏思考块无效");
      observed_ = true;
      if (streaming) { thinkingStart_ = now; explicitTiming_ = true; }
    } else if (kind == "tool_use" || kind == "server_tool_use") { error_ = "暂不支持 Messages 工具调用"; fatal_ = true; }
    blocks_[i] = std::move(b);
  }
  void usage(const Json &j) {
    if (j.is_null()) return;
    if (!j.is_object()) throw std::runtime_error("Messages 用量字段无效");
    for (auto i = j.begin(); i != j.end(); ++i) usage_[i.key()] = i.value();
  }
  void stopReason(const Json &j) {
    if (!j.is_object()) throw std::runtime_error("Messages 结束信息无效");
    if (j.contains("stop_reason") && !j["stop_reason"].is_null()) reason_ = text(j, "stop_reason");
    if (!reason_.empty() && reason_ != "end_turn" && reason_ != "stop_sequence" && reason_ != "refusal") {
      if (reason_ == "tool_use" || reason_ == "pause_turn") error_ = "暂不支持 Messages 工具调用或工具续接";
      else if (reason_ == "max_tokens" || reason_ == "model_context_window_exceeded")
        error_ = "回复未完成：" + reason_ + "（请检查最大输出或压缩上下文后手动重试）";
      else { error_ = "Messages 返回了未支持的结束原因"; reason_ = "unknown"; }
    }
  }
public:
  explicit AnthropicStream(int64_t requested) : requested_(requested) {}
  void consume(const Json &j, const std::string &event, int64_t now) {
    if (!j.is_object()) throw std::runtime_error("Messages 响应必须为对象");
    const auto kind = j.contains("type") ? text(j, "type") : event;
    if (!event.empty() && j.contains("type") && kind != event)
      throw std::runtime_error("Messages 事件名称与内容不一致");
    if (kind == "ping") return;
    if (kind == "error") {
      const auto e = j.value("error", Json::object());
      error_ = e.is_object() && e.contains("message") && e["message"].is_string() ?
          e["message"].get<std::string>() : "Messages 服务返回了错误";
      fatal_ = true;
      return;
    }
    if (kind == "message") {
      if (!j.contains("content") || !j["content"].is_array() || j["content"].size() > 4096)
        throw std::runtime_error("Messages 完整响应缺少有效内容数组");
      blocks_.clear(); open_.clear();
      int i = 0; for (const auto &b : j["content"]) block(i++, b, now, false);
      usage(j.value("usage", Json())); stopReason(j);
      started_ = true; stopped_ = !reason_.empty();
    } else if (kind == "message_start") {
      if (started_) throw std::runtime_error("Messages 重复开始事件");
      const auto m = j.at("message");
      if (!m.is_object()) throw std::runtime_error("Messages 开始事件无效");
      started_ = true; usage(m.value("usage", Json()));
      const auto content = m.value("content", Json::array());
      if (!content.is_array() || !content.empty()) throw std::runtime_error("Messages 开始事件内容非空");
    } else if (kind == "content_block_start") {
      if (!started_ || stopped_) throw std::runtime_error("Messages 内容块顺序无效");
      const int i = index(j);
      if (blocks_.count(i) || !open_.empty()) throw std::runtime_error("Messages 内容块重叠或重复");
      block(i, j.at("content_block"), now, true); open_.insert(i);
    } else if (kind == "content_block_delta") {
      const int i = index(j);
      if (!open_.count(i)) throw std::runtime_error("Messages 增量没有对应内容块");
      const auto d = j.at("delta"); const auto type = text(d, "type");
      auto &b = blocks_.at(i); const auto blockType = text(b, "type");
      if (type == "text_delta" && blockType == "text") {
        const auto value = text(d, "text"); b["text"].get_ref<std::string &>() += value;
        if (!value.empty() && !firstBody_) firstBody_ = now;
      } else if (type == "thinking_delta" && blockType == "thinking") {
        const auto value = text(d, "thinking"); b["thinking"].get_ref<std::string &>() += value;
        if (!value.empty()) observed_ = true;
      } else if (type == "signature_delta" && blockType == "thinking") {
        const auto value = text(d, "signature"); b["signature"].get_ref<std::string &>() += value;
        if (!value.empty()) observed_ = true;
      } else if (type == "input_json_delta") { error_ = "暂不支持 Messages 工具调用"; fatal_ = true; }
      else if (type == "text_delta" || type == "thinking_delta" || type == "signature_delta")
        throw std::runtime_error("Messages 增量与内容块类型不一致");
    } else if (kind == "content_block_stop") {
      const int i = index(j);
      if (!open_.erase(i)) throw std::runtime_error("Messages 内容块结束顺序无效");
      if (thinkingStart_) { thinkingMs_ += std::max<int64_t>(0, now - thinkingStart_); thinkingStart_ = 0; }
    } else if (kind == "message_delta") {
      if (!started_ || stopped_ || !open_.empty()) throw std::runtime_error("Messages 结束信息顺序无效");
      stopReason(j.at("delta")); usage(j.value("usage", Json()));
    } else if (kind == "message_stop") {
      if (!started_ || !open_.empty() || reason_.empty()) throw std::runtime_error("Messages 提前结束");
      stopped_ = true;
    }
  }
  bool terminal() const { return started_ && !reason_.empty() && open_.empty(); }
  bool stopped() const { return stopped_; }
  bool fatal() const { return fatal_; }
  bool complete() const { return terminal() && error_.empty(); }
  const std::string &error() const { return error_; }
  void finish(int64_t now) {
    if (thinkingStart_) { thinkingMs_ += std::max<int64_t>(0, now - thinkingStart_); thinkingStart_ = 0; }
    finished_ = true;
  }
  std::string redact(std::string value) const {
    for (const auto &entry : blocks_) for (const auto &key : {"signature", "data"}) {
      const auto it = entry.second.find(key);
      if (it == entry.second.end() || !it->is_string() || it->get<std::string>().empty()) continue;
      const auto secret = it->get<std::string>(); size_t pos = 0;
      while ((pos = value.find(secret, pos)) != std::string::npos) {
        value.replace(pos, secret.size(), "[已隐藏]"); pos += std::string("[已隐藏]").size();
      }
    }
    return value;
  }
  Json snapshot(int64_t now, bool replay) const {
    std::string output, thinking; Json content = Json::array();
    bool replayable = complete();
    for (const auto &entry : blocks_) {
      const auto &b = entry.second; const auto type = b.value("type", std::string());
      if (type == "text") output += b.value("text", std::string());
      else if (type == "thinking") {
        const auto value = b.value("thinking", std::string());
        if (!thinking.empty() && !value.empty()) thinking += "\n";
        thinking += value;
        if (b.value("signature", std::string()).empty()) replayable = false;
      }
      if (type == "text" || type == "thinking" || type == "redacted_thinking") content.push_back(b);
    }
    if (output.empty() && reason_ == "refusal") output = "模型拒绝了本次请求。";
    Json metadata = {{"reasoning_observed", observed_}, {"reasoning_complete", finished_ || firstBody_ > 0 || (observed_ && explicitTiming_ && !thinkingStart_)},
                     {"reasoning_estimated", !explicitTiming_}, {"stop_reason", reason_}};
    if (observed_) metadata["reasoning_duration_ms"] = explicitTiming_ ?
        thinkingMs_ + (thinkingStart_ ? std::max<int64_t>(0, now - thinkingStart_) : 0) :
        std::max<int64_t>(0, (firstBody_ ? firstBody_ : now) - requested_);
    const auto normalized = normalizedAnthropicUsage(usage_);
    if (!normalized.empty()) metadata["usage"] = normalized;
    if (replay && finished_ && replayable) metadata["anthropic_content"] = content;
    return {{"output", output}, {"reasoning", thinking}, {"metadata", metadata}};
  }
};
} // namespace penchat
