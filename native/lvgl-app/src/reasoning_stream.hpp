#pragma once
#include "common.hpp"
#include "usage.hpp"
#include <algorithm>
#include <map>
namespace penchat {
// Collect protocol items independently: final items replace their streamed
// parts.
class ReasoningStream {
  int64_t requested_, started_ = 0, stopped_ = 0, firstBody_ = 0;
  bool observed_ = false, finished_ = false;
  std::map<std::pair<int, int>, std::string> text_, reasoning_;
  std::map<int, Json> items_, outputItems_;
  Json usage_ = Json::object();
  static std::string
  flatten(const std::map<std::pair<int, int>, std::string> &parts) {
    std::string out;
    for (const auto &part : parts) {
      if (!out.empty())
        out += "\n";
      out += part.second;
    }
    return out;
  }
  void item(const Json &value, int index, bool done, int64_t now) {
    if (!value.is_object()) throw std::runtime_error("Invalid output item");
    const auto type = value.value("type", std::string());
    if (done && (type == "reasoning" || type == "message")) outputItems_[index] = value;
    if (type == "reasoning") {
      if (value.contains("encrypted_content") &&
          value["encrypted_content"].is_string() &&
          !value["encrypted_content"].get<std::string>().empty())
        observed_ = true;
      if (!done && !started_)
        started_ = now;
      if (done) {
        items_[index] = value;
        if (!stopped_)
          stopped_ = now;
      }
      if (value.contains("summary") && value["summary"].is_array()) {
        int n = 0;
        for (const auto &part : value["summary"]) {
          if (!part.is_object()) throw std::runtime_error("Invalid reasoning part");
          if (part.contains("text") && part["text"].is_string()) {
            reasoning_[{index, n}] = part["text"];
            if (!part["text"].get<std::string>().empty())
              observed_ = true;
          }
          ++n;
        }
      }
      if ((!value.contains("summary") || value["summary"].empty()) && value.contains("content") && value["content"].is_array()) {
        int n = 0;
        for (const auto &part : value["content"]) {
          const auto text = part.value("text", std::string());
          reasoning_[{index, n++}] = text;
          if (!text.empty()) observed_ = true;
        }
      }
    } else if (type == "message" && value.contains("content")) {
      if (!value["content"].is_array()) throw std::runtime_error("Invalid message content");
      int n = 0;
      for (const auto &part : value["content"]) {
        const auto kind = part.at("type").get<std::string>();
        const auto key = kind == "refusal" ? "refusal" : "text";
        if ((kind == "output_text" || kind == "refusal") && part.contains(key) && !part[key].is_string())
          throw std::runtime_error("Invalid output text/refusal");
        if ((kind == "output_text" || kind == "refusal") && part.contains(key) && part[key].is_string()) {
          text_[{index, n}] = part[key];
          if (!part[key].get<std::string>().empty() && !firstBody_)
            firstBody_ = now;
        }
        ++n;
      }
    }
  }

public:
  explicit ReasoningStream(int64_t requested) : requested_(requested) {}
  void consume(const Json &j, const std::string &type, int64_t now) {
    if (!j.is_object()) throw std::runtime_error("Invalid response event");
    auto rawUsage = j.value("usage", Json());
    if (j.contains("response") && j["response"].is_object() && j["response"].contains("usage"))
      rawUsage = j["response"]["usage"];
    const auto usage = normalizedUsage(rawUsage);
    for (auto it = usage.begin(); it != usage.end(); ++it) usage_[it.key()] = it.value();
    int index = j.value("output_index", 0);
    if (type == "response.output_item.added" ||
        type == "response.output_item.done") {
      if (j.contains("item") && j["item"].is_object())
        item(j["item"], index, type == "response.output_item.done", now);
    } else if (type == "response.output_text.delta" || type == "response.refusal.delta") {
      const auto delta = j.value("delta", std::string());
      text_[{index, j.value("content_index", 0)}] += delta;
      if (!delta.empty() && !firstBody_)
        firstBody_ = now;
    } else if (type == "response.output_text.done" || type == "response.refusal.done") {
      auto text = j.value(type == "response.refusal.done" ? "refusal" : "text", std::string());
      text_[{index, j.value("content_index", 0)}] = text;
      if (!text.empty() && !firstBody_)
        firstBody_ = now;
    } else if (type == "response.reasoning_summary_text.done" ||
               type == "response.reasoning_text.done") {
      auto text = j.value("text", std::string());
      if (!text.empty()) {
        observed_ = true;
        reasoning_[{index, j.value("summary_index",
                                   j.value("content_index", 0))}] = text;
      }
    } else if (type == "response.reasoning_summary_text.delta" ||
               type == "response.reasoning_text.delta") {
      const auto delta = j.value("delta", std::string());
      if (!delta.empty()) {
        observed_ = true;
        if (!started_)
          started_ = now;
        reasoning_[{index, j.value("summary_index",
                                   j.value("content_index", 0))}] += delta;
      }
    }
    const auto &response = j.contains("response") ? j.at("response") : j;
    if (response.is_object() && response.contains("output")) {
      if (!response["output"].is_array()) throw std::runtime_error("Invalid response output");
      const auto status = response.value("status", std::string());
      const bool done = type == "response.completed" || type == "response.incomplete" ||
                        status == "completed" || status == "incomplete";
      int n = 0;
      for (const auto &value : response["output"]) item(value, n++, done, now);
    }
    if (j.contains("choices") && j["choices"].is_array() &&
        !j["choices"].empty()) {
      const Json *selected = nullptr;
      for (const auto &candidate : j["choices"]) {
        if (!candidate.is_object()) throw std::runtime_error("Invalid choice");
        if (candidate.value("index", 0) == 0) { selected = &candidate; break; }
      }
      if (!selected) return;
      const auto &choice = *selected;
      const bool full = choice.contains("message");
      auto delta = choice.value(full ? "message" : "delta", Json::object());
      if (!delta.is_object() && !delta.is_null()) throw std::runtime_error("Invalid chat message/delta");
      if (delta.is_object()) {
        for (const auto key : {"content", "refusal"})
          if (delta.contains(key) && !delta[key].is_null() && !delta[key].is_string())
            throw std::runtime_error("Invalid chat content/refusal");
        if (delta.contains("content") && delta["content"].is_string()) {
          auto content = delta["content"].get<std::string>();
          if (full)
            text_[{0, 0}] = content;
          else
            text_[{0, 0}] += content;
          if (!content.empty() && !firstBody_)
            firstBody_ = now;
        }
        if (delta.contains("refusal") && delta["refusal"].is_string()) {
          const auto refusal = delta["refusal"].get<std::string>();
          if (full) text_[{0, 1}] = refusal; else text_[{0, 1}] += refusal;
          if (!refusal.empty() && !firstBody_) firstBody_ = now;
        }
        for (const char *key : {"reasoning_content", "reasoning"})
          if (delta.contains(key) && delta[key].is_string() &&
              !delta[key].get<std::string>().empty()) {
            observed_ = true;
            if (!started_)
              started_ = now;
            if (full)
              reasoning_[{0, 0}] = delta[key].get<std::string>();
            else
              reasoning_[{0, 0}] += delta[key].get<std::string>();
            break;
          }
      }
    }
  }
  void finish(int64_t now) {
    finished_ = true;
    if (!stopped_)
      stopped_ = now;
  }
  Json snapshot(int64_t now) const {
    auto end = firstBody_ ? firstBody_ : stopped_ ? stopped_ : now;
    if (stopped_)
      end = std::min(end, stopped_);
    Json metadata = {
        {"reasoning_observed", observed_},
        {"reasoning_complete", finished_ || firstBody_ || stopped_},
        {"reasoning_estimated", started_ == 0}};
    if (observed_)
      metadata["reasoning_duration_ms"] =
          std::max<int64_t>(0, end - (started_ ? started_ : requested_));
    Json items = Json::array();
    for (const auto &entry : items_)
      items.push_back(entry.second);
    metadata["response_reasoning_items"] = items;
    Json output = Json::array();
    for (const auto &entry : outputItems_) output.push_back(entry.second);
    metadata["response_output_items"] = output;
    if (!usage_.empty()) metadata["usage"] = usage_;
    return {{"output", flatten(text_)},
            {"reasoning", flatten(reasoning_)},
            {"metadata", metadata}};
  }
};
} // namespace penchat
