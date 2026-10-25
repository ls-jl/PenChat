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
  std::map<int, Json> items_;
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
    const auto type = value.value("type", std::string());
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
          if (part.contains("text") && part["text"].is_string()) {
            reasoning_[{index, n}] = part["text"];
            if (!part["text"].get<std::string>().empty())
              observed_ = true;
          }
          ++n;
        }
      }
    } else if (type == "message" && value.contains("content")) {
      int n = 0;
      for (const auto &part : value["content"]) {
        if (part.value("type", std::string()) == "output_text" &&
            part.contains("text") && part["text"].is_string()) {
          text_[{index, n}] = part["text"];
          if (!part["text"].get<std::string>().empty() && !firstBody_)
            firstBody_ = now;
        }
        ++n;
      }
    }
  }

public:
  explicit ReasoningStream(int64_t requested) : requested_(requested) {}
  void consume(const Json &j, const std::string &type, int64_t now) {
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
    } else if (type == "response.output_text.delta") {
      const auto delta = j.value("delta", std::string());
      text_[{index, j.value("content_index", 0)}] += delta;
      if (!delta.empty() && !firstBody_)
        firstBody_ = now;
    } else if (type == "response.output_text.done") {
      auto text = j.value("text", std::string());
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
    if (j.contains("response") && j["response"].is_object() &&
        j["response"].contains("output") &&
        j["response"]["output"].is_array()) {
      int n = 0;
      for (const auto &value : j["response"]["output"])
        item(value, n++, true, now);
    }
    if (j.contains("choices") && j["choices"].is_array() &&
        !j["choices"].empty()) {
      const auto &choice = j["choices"][0];
      const bool full = choice.contains("message");
      auto delta = choice.value(full ? "message" : "delta", Json::object());
      if (delta.is_object()) {
        if (delta.contains("content") && delta["content"].is_string()) {
          auto content = delta["content"].get<std::string>();
          if (full)
            text_[{0, 0}] = content;
          else
            text_[{0, 0}] += content;
          if (!content.empty() && !firstBody_)
            firstBody_ = now;
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
    if (!usage_.empty()) metadata["usage"] = usage_;
    return {{"output", flatten(text_)},
            {"reasoning", flatten(reasoning_)},
            {"metadata", metadata}};
  }
};
} // namespace penchat
