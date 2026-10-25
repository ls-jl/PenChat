#include "network.hpp"
#include "anthropic_stream.hpp"
#include "image_data.h"
#include <cctype>
namespace penchat {
namespace {
Json parseMessageJson(const std::string &text) {
  size_t remaining = 200000;
  return Json::parse(text, [&](int depth, Json::parse_event_t, Json &) {
    if (depth > 64 || !remaining--) throw std::runtime_error("Messages 响应结构过于复杂");
    return true;
  }, false);
}
Json imageSource(std::string url) {
  if (url.find("https://") == 0 || url.find("http://") == 0)
    return {{"type", "url"}, {"url", url}};
  if (url.find("data:image/") != 0) {
    if (url.find("file://") == 0) url.erase(0, 7);
    std::string result, error;
    if (!camera::imageDataUrl(url, &result, &error)) throw std::runtime_error("上下文图片不可用，请检查附件");
    url = std::move(result);
  }
  const auto comma = url.find(',');
  const auto marker = url.find(";base64,");
  if (comma == std::string::npos || marker == std::string::npos || comma != marker + 7)
    throw std::runtime_error("Messages 图片编码无效");
  const auto media = url.substr(5, marker - 5);
  if (media != "image/jpeg" && media != "image/png" && media != "image/webp" && media != "image/gif")
    throw std::runtime_error("Messages 不支持此图片格式");
  return {{"type", "base64"}, {"media_type", media}, {"data", url.substr(comma + 1)}};
}
void sensitive(const Json &value, std::vector<std::string> &out) {
  if (value.is_object()) for (auto i = value.begin(); i != value.end(); ++i) {
    if ((i.key() == "signature" || i.key() == "data") && i->is_string() && !i->get<std::string>().empty()) out.push_back(i->get<std::string>());
    else sensitive(i.value(), out);
  } else if (value.is_array()) for (const auto &entry : value) sensitive(entry, out);
}
}
Json anthropicRequestBody(const Json &model, const Json &input, bool streaming) {
  validateModelImages(model, input);
  const auto caps = reasoningCapabilities(model);
  validateAnthropic(model, caps);
  Json messages = Json::array();
  std::string system = model.value("system_prompt", std::string());
  for (const auto &item : input) {
    if (!item.is_object() || !item.contains("role") || !item["role"].is_string())
      throw std::runtime_error("Messages 历史消息格式无效");
    const auto role = item["role"].get<std::string>();
    if (role != "user" && role != "assistant" && role != "system")
      throw std::runtime_error("Messages 消息角色无效");
    Json content = Json::array();
    const auto &parts = item.at("content");
    if (parts.is_string()) {
      if (!parts.get<std::string>().empty()) content.push_back({{"type", "text"}, {"text", parts}});
    } else if (parts.is_array()) {
      for (const auto &p : parts) {
        const auto kind = p.at("type").get<std::string>();
        if (kind == "input_text" || kind == "output_text" || kind == "text") {
          const auto value = p.at("text").get<std::string>();
          if (!value.empty()) content.push_back({{"type", "text"}, {"text", value}});
        } else if (kind == "input_image" && role == "user") {
          content.push_back({{"type", "image"}, {"source", imageSource(p.at("image_url").get<std::string>())}});
        } else if ((kind == "thinking" || kind == "redacted_thinking") && role == "assistant") {
          if (apiOptions(model).replay) content.push_back(p);
        } else if (kind == "refusal" && role == "assistant")
          content.push_back({{"type", "text"}, {"text", p.at("refusal")}});
        else throw std::runtime_error("Messages 消息包含不支持的内容");
      }
    } else throw std::runtime_error("Messages 消息内容无效");
    if (role == "system") {
      for (const auto &p : content) {
        if (p.at("type") != "text") throw std::runtime_error("系统提示词只能包含文本");
        if (!system.empty()) system += "\n\n";
        system += p.at("text").get<std::string>();
      }
    } else if (!content.empty()) messages.push_back({{"role", role}, {"content", content}});
  }
  if (messages.empty() || messages.back().at("role") != "user")
    throw std::runtime_error("Messages 请求须以用户消息结束，不使用助手预填续写");
  Json body = {{"model", model.value("name", std::string())}, {"max_tokens", anthropicMaxOutput(model)},
               {"messages", messages}, {"stream", streaming}};
  if (!system.empty()) body["system"] = system;
  const auto effort = model.value("effort_value", std::string());
  const auto selected = model.value("effective_effort", std::string());
  if (!effort.empty() && selected != "api_default") {
    if (selected == "none" || effort == "disabled") {
      if (!caps.value("disable", false)) throw std::runtime_error("此模型不支持关闭思考");
      body["thinking"] = {{"type", "disabled"}};
    } else if (caps.value("mode", std::string("off")) == "budget") {
      size_t used = 0; int budget = 0;
      try { budget = std::stoi(effort, &used); } catch (...) { throw std::runtime_error("思考预算无效"); }
      if (used != effort.size() || budget < 1024 || budget >= anthropicMaxOutput(model))
        throw std::runtime_error("思考预算须至少1024且小于最大输出 token");
      body["thinking"] = {{"type", "enabled"}, {"budget_tokens", budget}};
    } else if (caps.value("mode", std::string("off")) == "adaptive") {
      if (effort != "low" && effort != "medium" && effort != "high" && effort != "xhigh" && effort != "max")
        throw std::runtime_error("Messages 自适应强度无效");
      body["thinking"] = {{"type", "adaptive"}};
      body["output_config"] = {{"effort", effort}};
    } else throw std::runtime_error("请先配置此模型的思考模式");
    if (body["thinking"]["type"] != "disabled" && caps.value("display_supported", false))
      body["thinking"]["display"] = apiOptions(model).summary && model.value("reasoning_summary", false) ? "summarized" : "omitted";
  }
  applyModelParameters(body, model);
  return body;
}
void streamAnthropic(const Json &model, Json input, std::atomic<bool> &cancel,
                     Events &events, int generation) {
  const auto options = apiOptions(model);
  AnthropicStream state(ticks());
  int64_t lastPublish = 0; size_t publications = 0; bool dirty = false;
  Json diagnostic = Json::object(), tag = {{"generation", generation}};
  std::vector<std::string> secrets; sensitive(input, secrets);
  auto redact = [&](std::string value) {
    value = state.redact(value);
    for (const auto &secret : secrets) {
      size_t pos = 0;
      while ((pos = value.find(secret, pos)) != std::string::npos) {
        value.replace(pos, secret.size(), "[已隐藏]"); pos += std::string("[已隐藏]").size();
      }
    }
    return redactApiError(value, model);
  };
  auto publish = [&](bool replay) {
    auto snapshot = state.snapshot(ticks(), replay && options.replay);
    snapshot["generation"] = generation; snapshot["metadata"]["replay_identity"] = replayIdentity(model);
    if (!diagnostic.empty()) snapshot["metadata"]["http"] = diagnostic;
    events.pushLatest("response_state", std::move(snapshot));
    lastPublish = ticks(); ++publications; dirty = false;
  };
  try {
    enum class Format { Unknown, Sse, Json };
    Format format = Format::Unknown; std::string pending;
    SseParser parser([&](const std::string &event, const std::string &data) {
      if (state.stopped() || state.fatal()) return;
      const auto j = parseMessageJson(data);
      if (j.is_discarded()) throw std::runtime_error("Messages SSE JSON 无效");
      state.consume(j, event, ticks()); dirty = true;
    });
    const auto result = apiRequest(model, "/messages", "POST", anthropicRequestBody(model, input, options.streaming).dump(),
        cancel, [&](const char *p, size_t n) {
      if (format == Format::Unknown) {
        pending.append(p, n);
        size_t first = pending.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
        while (first < pending.size() && std::isspace(static_cast<unsigned char>(pending[first]))) ++first;
        if (first == pending.size() || (static_cast<unsigned char>(pending[0]) == 0xef && pending.size() < 3)) return;
        format = pending[first] == '{' || pending[first] == '[' ? Format::Json : Format::Sse;
        if (format == Format::Json && options.streaming && !options.compatible)
          throw std::runtime_error("服务器未返回流式响应，请显式启用 API 兼容模式");
        if (format == Format::Sse) { parser.feed(pending.data(), pending.size()); pending.clear(); }
      } else if (format == Format::Sse) parser.feed(p, n);
      else pending.append(p, n);
      if (dirty && (!lastPublish || ticks() - lastPublish >= 100)) publish(false);
    }, [&] {
      if (dirty && (!lastPublish || ticks() - lastPublish >= 100)) publish(false);
      return state.stopped() || state.fatal();
    });
    if (!cancel && result.error.empty()) {
      if (format == Format::Sse) parser.finish();
      else if (format == Format::Json) {
        if (pending.compare(0, 3, "\xef\xbb\xbf") == 0) pending.erase(0, 3);
        const auto j = parseMessageJson(pending);
        if (j.is_discarded()) throw std::runtime_error("Messages JSON 响应无效");
        state.consume(j, "", ticks());
      }
    }
    const bool complete = !cancel && result.error.empty() && state.complete() &&
                          (state.stopped() || (options.compatible && state.terminal()));
    const auto error = redact(!result.error.empty() ? result.error : !state.error().empty() ? state.error() :
                              !complete ? "Messages 流在明确结束前中断，请手动重试" : "");
    diagnostic = {{"status", result.status}, {"request_id", redact(result.requestId)}, {"elapsed_ms", result.elapsedMs},
                  {"outcome", cancel ? "cancelled" : !complete ? "api_or_transport_error" : "response"}};
    state.finish(ticks()); publish(complete);
    tag["http_status"] = result.status; tag["request_id"] = redact(result.requestId);
    tag["elapsed_ms"] = result.elapsedMs; tag["snapshots"] = publications;
    if (cancel) events.push("cancelled", tag);
    else if (!complete) { tag["error"] = error; events.push("network_error", tag); }
    else events.push("done", tag);
  } catch (const std::exception &e) {
    state.finish(ticks()); publish(false);
    if (cancel) events.push("cancelled", tag);
    else { tag["error"] = redact(e.what()); events.push("network_error", tag); }
  }
}
} // namespace penchat
