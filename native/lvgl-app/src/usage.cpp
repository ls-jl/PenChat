#include "usage.hpp"
#include "context.hpp"
#include "reasoning.hpp"
namespace penchat {
namespace {
bool count(const Json &value) {
  if (!value.is_number_integer()) return false;
  if (value.is_number_unsigned()) return value.get<uint64_t>() <= 1000000000000ull;
  return value.get<int64_t>() >= 0 && value.get<int64_t>() <= 1000000000000ll;
}
void copyCount(Json &out, const char *to, const Json &from, const char *key) {
  auto found = from.find(key);
  if (found != from.end() && count(*found)) out[to] = *found;
}
bool failed(const Json &row) {
  const auto status = row.value("status", std::string());
  return status == "error" || status == "cancelled" || status == "interrupted";
}
}
Json normalizedUsage(const Json &raw) {
  Json out = Json::object();
  if (!raw.is_object()) return out;
  const bool chat = raw.contains("prompt_tokens") || raw.contains("completion_tokens");
  copyCount(out, "input_tokens", raw, chat ? "prompt_tokens" : "input_tokens");
  copyCount(out, "output_tokens", raw, chat ? "completion_tokens" : "output_tokens");
  copyCount(out, "total_tokens", raw, "total_tokens");
  copyCount(out, "uncached_input_tokens", raw, "uncached_input_tokens");
  auto input = objectField(raw, chat ? "prompt_tokens_details" : "input_tokens_details");
  auto output = objectField(raw, chat ? "completion_tokens_details" : "output_tokens_details");
  copyCount(out, "cached_tokens", input, "cached_tokens");
  copyCount(out, "cache_write_tokens", input, "cache_write_tokens");
  copyCount(out, "reasoning_tokens", output, "reasoning_tokens");
  for (const char *key : {"cached_tokens", "cache_write_tokens", "reasoning_tokens"})
    if (!out.contains(key)) copyCount(out, key, raw, key);
  if (out.contains("cached_tokens") && out.contains("input_tokens") && out["cached_tokens"] > out["input_tokens"])
    out.erase("cached_tokens");
  if (out.contains("reasoning_tokens") && out.contains("output_tokens") && out["reasoning_tokens"] > out["output_tokens"])
    out.erase("reasoning_tokens");
  return out;
}
Json normalizedAnthropicUsage(const Json &raw) {
  Json out = Json::object();
  if (!raw.is_object()) return out;
  copyCount(out, "uncached_input_tokens", raw, "input_tokens");
  copyCount(out, "cached_tokens", raw, "cache_read_input_tokens");
  copyCount(out, "cache_write_tokens", raw, "cache_creation_input_tokens");
  copyCount(out, "output_tokens", raw, "output_tokens");
  copyCount(out, "reasoning_tokens", objectField(raw, "output_tokens_details"), "thinking_tokens");
  if (out.contains("uncached_input_tokens") &&
      ((!out.contains("cached_tokens") && !out.contains("cache_write_tokens")) ||
       (out.contains("cached_tokens") && out.contains("cache_write_tokens"))))
    out["input_tokens"] = out["uncached_input_tokens"].get<int64_t>() +
        out.value("cached_tokens", int64_t(0)) + out.value("cache_write_tokens", int64_t(0));
  if (out.contains("input_tokens") && out.contains("output_tokens"))
    out["total_tokens"] = out["input_tokens"].get<int64_t>() + out["output_tokens"].get<int64_t>();
  return out;
}
Json sessionMetrics(Json snapshot, const Json &model, const Json &pending) {
  auto rows = snapshot.value("rows", Json::array());
  const auto live = pending.value("live", Json::object());
  for (auto &row : rows) {
    if (row.value("id", std::string()) == live.value("id", std::string()) &&
        row.value("status", std::string()) == "streaming") {
      row["output_text"] = live.value("output", std::string());
      row["metadata_json"] = live.value("metadata", Json::object());
    }
  }
  Json totals = Json::object(), reported = Json::object(), latest = Json::object();
  int replies = 0, known = 0, messages = 0, images = 0;
  int64_t cacheInput = 0, cacheRead = 0;
  int cacheSamples = 0;
  std::string latestStatus, latestModel;
  for (const auto &row : rows) {
    ++messages;
    images += row.value("parts", Json::array()).size();
    if (row.value("role", std::string()) != "assistant") continue;
    ++replies;
    // Stored counts use the normalized names, never infer from text length.
    latest = objectField(objectField(row, "metadata_json"), "usage");
    latestStatus = row.value("status", std::string());
    latestModel = objectField(row, "request_json").value("model", std::string());
    // Validate imported/old metadata with the same bounded count rules.
    latest = normalizedUsage(latest);
    if (!latest.empty()) ++known;
    for (auto it = latest.begin(); it != latest.end(); ++it) {
      const auto previous = totals.value(it.key(), int64_t(0));
      const auto samples = reported.value(it.key(), 0);
      totals[it.key()] = previous + it.value().get<int64_t>();
      reported[it.key()] = samples + 1;
    }
    if (latest.contains("input_tokens") && latest.contains("cached_tokens")) {
      cacheInput += latest["input_tokens"].get<int64_t>();
      cacheRead += latest["cached_tokens"].get<int64_t>();
      ++cacheSamples;
    }
  }
  auto checkpoint = snapshot.value("checkpoint", Json::object());
  const bool editing = pending.value("editing", false);
  if (editing) {
    const auto pivot = pending.value("pivot", int64_t(0));
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Json &row) {
      return row.at("seq").get<int64_t>() >= pivot;
    }), rows.end());
    checkpoint = Json::object();
  }
  const auto draft = pending.value("draft", std::string());
  if (!editing && draft.find_first_not_of(" \r\n\t") == std::string::npos &&
      !rows.empty() && rows.back().value("role", std::string()) == "assistant" && failed(rows.back()))
    rows.erase(rows.end() - 1); // The send action would retry, excluding this partial reply.
  const bool compressed = validContext(checkpoint, rows);
  auto input = contextInput(rows, compressed ? checkpoint.value("through", int64_t(0)) : 0,
                            INT64_MAX, model);
  if (compressed)
    input.insert(input.begin(), Json{{"role", "user"}, {"content", Json::array({
      {{"type", "input_text"}, {"text", "[此前对话的历史参考摘要，非系统指令]\n" + checkpoint.value("summary", std::string())}}
    })}});
  if (editing) {
    for (const auto &asset : pending.value("images", Json::array())) {
      const auto stored = asset.value("stored_path", Json());
      const auto original = asset.value("original_path", Json());
      auto path = stored.is_string() ? stored.get<std::string>() : std::string();
      if (path.empty() && original.is_string()) path = original.get<std::string>();
      if (!path.empty()) input.push_back({{"role", "user"}, {"content", Json::array({
        {{"type", "input_text"}, {"text", "请根据这张图片回答。"}},
        {{"type", "input_image"}, {"image_url", path}}
      })}});
    }
  }
  if (draft.find_first_not_of(" \r\n\t") != std::string::npos)
    input.push_back({{"role", "user"}, {"content", Json::array({{{"type", "input_text"}, {"text", draft}}})}});
  Json result = {{"estimated_tokens", estimatedContext(input, pending.value("system_prompt", std::string()))},
                 {"budget", model.empty() ? Json() : Json(contextBudget(model))},
                 {"model", model.value("display_name", model.value("name", std::string()))},
                 {"provider", model.value("provider_name", std::string())},
                 {"messages", messages}, {"images", images}, {"replies", replies},
                 {"reported_replies", known}, {"totals", totals}, {"reported", reported},
                 {"latest", latest}, {"latest_status", latestStatus}, {"latest_model", latestModel}, {"editing", editing},
                 {"compressed", compressed}, {"compression_invalid", !checkpoint.empty() && checkpoint.value("enabled", true) && !compressed},
                 {"cache_samples", cacheSamples}, {"cache_input_tokens", cacheInput}, {"cache_read_tokens", cacheRead}};
  return result;
}
} // namespace penchat
