#include "network.hpp"
#include "context.hpp"
#include "images.hpp"
#include "reasoning_stream.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
int main() {
  Json model = {{"name", "gpt-5.4"}, {"provider_id", "p"}, {"id", "m"},
                {"base_url", "https://api.openai.com/v1/responses"},
                {"provider_metadata", "{\"api\":\"responses\"}"},
                {"effort_value", "high"}, {"reasoning_summary", true}, {"system_prompt", "once"}};
  assert(apiRoot(model) == "https://api.openai.com/v1");
  auto body = requestBody(model, Json::array(), true);
  assert(body["store"] == false && body["instructions"] == "once");
  assert(body["reasoning"]["effort"] == "high" && body["reasoning"]["summary"] == "auto");
  model["provider_metadata"] = {{"api", "chat"}, {"compatibility", {{"enabled", true}, {"usage", false}, {"summary", false}, {"replay", false}, {"streaming", false}, {"timeout", 45}, {"http1", true}}}};
  bool mismatch = false;
  try { apiRoot(model); } catch (...) { mismatch = true; }
  assert(mismatch);
  model["base_url"] = "http://localhost/v1/chat/completions/";
  auto options = apiOptions(model);
  assert(!options.streaming && !options.usage && !options.summary && !options.replay && options.http1 && options.timeout == 45);
  Json input = Json::array({{{"role", "user"}, {"content", Json::array({{{"type", "input_text"}, {"text", "hi"}}})}}});
  body = requestBody(model, input, false);
  assert(body["messages"][0]["role"] == "developer" && body["messages"][0]["content"] == "once");
  assert(body["messages"][1]["content"] == "hi" && !body.contains("stream_options") && body["reasoning_effort"] == "high");
  model["headers_json"] = {{"authorization", "Custom fake"}, {"X-Test", "value"}};
  auto headers = apiHeaders(model);
  assert(!headers.contains("Authorization") && headers["authorization"] == "Custom fake");
  std::vector<std::pair<std::string, std::string>> parsed;
  SseParser parser([&](const std::string &event, const std::string &data) {parsed.push_back({event,data});});
  const std::string wire = "\xef\xbb\xbf:hi\r\nevent: text\rdata: 中\rdata: 文\r\rdata: tail\n\n";
  for (const auto c : wire) parser.feed(&c, 1);
  parser.finish();
  assert(parsed.size() == 2 && parsed[0].first == "text" && parsed[0].second == "中\n文" && parsed[1].second == "tail");
  bool oversized = false;
  SseParser limited([](const std::string &,const std::string &){});
  try { const auto part = "data: " + std::string(2 * 1024 * 1024, 'a') + "\n"; limited.feed(part.data(),part.size()); limited.feed(part.data(),part.size()); } catch (...) { oversized = true; }
  assert(oversized);
  ReasoningStream stream(1000);
  stream.consume({{"output_index",0},{"item",{{"type","reasoning"},{"encrypted_content","partial"}}}},"response.output_item.added",1100);
  assert(stream.snapshot(1200)["metadata"]["response_reasoning_items"].empty());
  Json reason={{"type","reasoning"},{"id","r"},{"encrypted_content","opaque"},{"summary",Json::array({{{"type","summary_text"},{"text","summary"}}})}};
  Json answer={{"type","message"},{"role","assistant"},{"phase","final_answer"},{"status","completed"},{"content",Json::array({{{"type","output_text"},{"text","answer"}}})}};
  stream.consume({{"response",{{"status","completed"},{"output",Json::array({reason,answer})}}}},"response.completed",2100);
  auto snapshot=stream.snapshot(2200);
  assert(snapshot["output"] == "answer" && snapshot["reasoning"] == "summary");
  assert(snapshot["metadata"]["response_output_items"][1]["phase"] == "final_answer");
  model["provider_metadata"] = "{\"api\":\"responses\"}";
  snapshot["metadata"]["replay_identity"]=replayIdentity(model);
  Json rows=Json::array({{{"seq",1},{"role","assistant"},{"provider_id","p"},{"model_config_id","m"},{"output_text","answer"},{"metadata_json",snapshot["metadata"]}}});
  auto replay=contextInput(rows,0,10,model);
  assert(replay.size()==2 && replay[1]["phase"]=="final_answer");
  model["base_url"]="https://other.example/v1";
  replay=contextInput(rows,0,10,model);
  assert(replay.size()==1 && !replay[0].contains("phase"));
  ReasoningStream refusal(1000);
  refusal.consume({{"choices",Json::array({{{"index",1},{"delta",{{"content","wrong"}}}},{{"index",0},{"delta",{{"refusal","cannot comply"}}}}})}},"",1100);
  assert(refusal.snapshot(1200)["output"]=="cannot comply");
  bool bad = false;
  try { refusal.consume({{"output_index","wrong"}},"response.output_text.delta",1200); } catch (...) { bad = true; }
  assert(bad);
  {
    Events imageEvents(true); Images images(imageEvents);
    const auto first=images.request("/nonexistent/penchat-api-fixture-first",44,44);
    for (int i=0; i<200 && !images.failed(first); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(images.failed(first));
    for (int i=0;i<1000;++i)
      images.request("/nonexistent/penchat-api-fixture-"+std::to_string(i),44,44);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(!images.failed(first));
  }
  std::cout << "API configuration, bounded SSE, refusal and endpoint-bound ordered replay passed\n";
}
