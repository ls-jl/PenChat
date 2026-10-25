#include "usage.hpp"
#include "context.hpp"
#include "network.hpp"
#include "reasoning_stream.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
int main() {
  const Json raw = {{"input_tokens", 1000}, {"output_tokens", 100}, {"total_tokens", 1100},
                    {"input_tokens_details", {{"cached_tokens", 500}, {"cache_write_tokens", 200}}},
                    {"output_tokens_details", {{"reasoning_tokens", 30}}}, {"private", "must not persist"}};
  const auto usage = normalizedUsage(raw);
  assert(usage.size() == 6 && usage["cached_tokens"] == 500 && usage["reasoning_tokens"] == 30);
  assert(normalizedUsage(usage) == usage);
  assert(normalizedUsage(Json()).empty());
  assert(normalizedUsage({{"input_tokens", -1}, {"output_tokens", "3"},
                          {"total_tokens", 3.5}, {"reasoning_tokens", UINT64_MAX}}).empty());
  assert(!normalizedUsage({{"input_tokens", 10}, {"cached_tokens", 11}}).contains("cached_tokens"));
  const Json zero = {{"input_tokens", 0}, {"cached_tokens", 0}};
  assert(normalizedUsage(zero).size() == 2);
  ReasoningStream responses(1);
  const Json completed = {{"response", {{"usage", raw}}}};
  responses.consume(completed, "response.completed", 5);
  responses.consume(completed, "response.completed", 6); // cumulative snapshot, never add twice
  responses.consume({{"response", {{"usage", Json()}}}}, "response.created", 7);
  assert(responses.snapshot(10)["metadata"]["usage"] == usage);
  ReasoningStream chat(1);
  chat.consume({{"choices", Json::array()}, {"usage", {{"prompt_tokens", 800},
    {"completion_tokens", 80}, {"total_tokens", 880},
    {"prompt_tokens_details", {{"cached_tokens", 0}}},
    {"completion_tokens_details", {{"reasoning_tokens", 20}}}}}}, "", 5);
  assert(chat.snapshot(10)["metadata"]["usage"]["cached_tokens"] == 0);
  assert(chat.snapshot(10)["metadata"]["usage"]["input_tokens"] == 800);
  ReasoningStream absent(1);
  assert(!absent.snapshot(10)["metadata"].contains("usage"));
  Json model = {{"name", "model"}, {"display_name", "Model"}, {"provider_id", "p"},
                {"id", "m"}, {"provider_metadata", "{\"api\":\"chat\"}"},
                {"capabilities_json", "{\"context_budget\":32768}"}};
  assert(requestBody(model, Json::array(), true)["stream_options"]["include_usage"] == true);
  assert(!requestBody(model, Json::array(), false).contains("stream_options"));
  model["provider_metadata"] = "{}";
  assert(!requestBody(model, Json::array(), true).contains("stream_options"));
  Json rows = Json::array();
  for (int i = 1; i <= 6; ++i) rows.push_back({{"id", std::to_string(i)}, {"seq", i},
    {"role", i % 2 ? "user" : "assistant"}, {"status", "complete"},
    {"output_text", std::string(100, 'x')}, {"parts", Json::array()},
    {"metadata_json", i == 2 ? Json{{"usage", usage}} : Json::object()}});
  Json snapshot = {{"rows", rows}, {"checkpoint", Json::object()}};
  const Json pending = {{"draft", "未发送草稿"}, {"system_prompt", "SYSTEM"}};
  auto plain = sessionMetrics(snapshot, model, pending);
  assert(plain["budget"] == 32768 && plain["reported_replies"] == 1 && plain["replies"] == 3);
  assert(plain["totals"]["total_tokens"] == 1100 && plain["cache_input_tokens"] == 1000);
  assert(plain["latest"].empty()); // latest reply has no usage; never borrow an older one
  const auto empty = sessionMetrics({{"rows", Json::array()}}, Json::object(), Json::object());
  assert(empty["budget"].is_null() && empty["replies"] == 0 && empty["totals"].empty());
  Json checkpoint = {{"version", 1}, {"enabled", true}, {"through", 4},
                     {"summary", "Summary"}, {"checksum", contextChecksum(rows, 4)}};
  snapshot["checkpoint"] = checkpoint;
  auto compressed = sessionMetrics(snapshot, model, pending);
  assert(compressed["compressed"] == true && compressed["estimated_tokens"] < plain["estimated_tokens"]);
  snapshot["checkpoint"]["checksum"] = "stale";
  assert(sessionMetrics(snapshot, model, pending)["compression_invalid"] == true);
  snapshot["checkpoint"] = Json::object();
  auto picture = rows;picture[0]["parts"] = Json::array({{{"type", "image"}, {"path", "/fake.png"}}});
  snapshot["rows"] = picture;
  assert(sessionMetrics(snapshot, model, pending)["estimated_tokens"] > plain["estimated_tokens"]);
  auto live = pending;live["live"] = {{"id", "6"}, {"output", "live"}, {"metadata", {{"usage", usage}}}};
  snapshot["rows"][5]["status"] = "streaming";
  assert(sessionMetrics(snapshot, model, live)["reported_replies"] == 2);
  auto edit = pending;edit["editing"] = true;edit["pivot"] = 3;
  assert(sessionMetrics(snapshot, model, edit)["estimated_tokens"] < sessionMetrics(snapshot, model, pending)["estimated_tokens"]);
  std::cout << "Usage normalization, protocol snapshots, omission/zero, duplicate finals, safe totals, context budget/compression/images/live/edit passed\n";
}
