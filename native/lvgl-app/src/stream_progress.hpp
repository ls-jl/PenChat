#pragma once
#include "common.hpp"
namespace penchat {
// Inference queue time is governed by the existing overall request timeout.
// Once actual output starts, keep-alive comments/empty chunks must not disguise
// a stalled generation. No automatic retry or replacement request is allowed.
class StreamProgress {
  int64_t last_ = 0;
  bool observed_ = false;
public:
  static constexpr int64_t WaitingMs = 15000, StalledMs = 120000;
  void observe(int64_t now) { last_ = now; observed_ = true; }
  bool observed() const { return observed_; }
  int64_t quiet(int64_t now) const { return observed_ ? std::max<int64_t>(0, now - last_) : 0; }
  bool waiting(int64_t now) const { return observed_ && quiet(now) >= WaitingMs; }
  bool stalled(int64_t now) const { return observed_ && quiet(now) >= StalledMs; }
};
inline bool meaningfulStreamChunk(const Json &value, unsigned depth = 0) {
  if (!value.is_object() || depth > 16) return false;
  for (const char *key : {"delta", "text", "content", "reasoning_content", "reasoning", "refusal", "arguments"}) {
    const auto at = value.find(key);
    if (at == value.end()) continue;
    if (at->is_string() && !at->get_ref<const std::string &>().empty()) return true;
    if (at->is_object() && meaningfulStreamChunk(*at, depth + 1)) return true;
  }
  for (const char *key : {"choices", "tool_calls"}) {
    const auto at = value.find(key);
    if (at != value.end() && at->is_array())
      for (const auto &item : *at) if (meaningfulStreamChunk(item, depth + 1)) return true;
  }
  const auto function = value.find("function");
  return function != value.end() && function->is_object() && meaningfulStreamChunk(*function, depth + 1);
}
} // namespace penchat
