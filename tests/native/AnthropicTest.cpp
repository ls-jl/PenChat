#include "network.hpp"
#include "anthropic_stream.hpp"
#include "context.hpp"
#include "database.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
using namespace penchat;
int main() {
  Json m={{"name","claude-sonnet-4-6"},{"provider_id","p"},{"id","m"},{"api_key","fixture-key"},
          {"base_url","https://api.anthropic.com/v1/messages/"},{"provider_metadata",{{"api","anthropic"}}}};
  assert(apiRoot(m)=="https://api.anthropic.com/v1");
  auto h=apiHeaders(m); assert(h["x-api-key"]=="fixture-key" && !h.contains("Authorization"));
  assert(h["anthropic-version"]=="2023-06-01");
  m["provider_metadata"]["anthropic"]={{"auth","bearer"},{"version","2026-10-21"}};
  h=apiHeaders(m);assert(h["Authorization"]=="Bearer fixture-key" && !h.contains("x-api-key"));
  m["headers_json"]={{"X-Api-Key","conflict"}};bool bad=false;
  try {apiHeaders(m);} catch (...) {bad=true;}assert(bad);m.erase("headers_json");
  const auto a=reasoningCapabilities(m);assert(a["mode"]=="adaptive" && !a["levels"].contains("xhigh"));
  auto unknown=m;unknown["name"]="claude-proxy-unknown";assert(reasoningCapabilities(unknown)["levels"].empty());
  auto budget=m;budget["name"]="claude-sonnet-4-5-20250929";
  auto resolved=resolveReasoning(budget,{{"effort","high"}},"");assert(resolved["effort_value"]=="8192");
  auto input=Json::array({{{"role","user"},{"content","question"}}});
  auto body=requestBody(resolved,input,true);assert(body["thinking"]["budget_tokens"]==8192 && body["max_tokens"]==16384);
  resolved=resolveReasoning(m,{{"effort","api_default"}},"high");body=requestBody(resolved,input,true);
  assert(!body.contains("thinking") && !body.contains("output_config"));
  auto invalid=m;invalid["capabilities_json"]={{"max_output_tokens",4096},{"reasoning",{{"mode","budget"},{"levels",{{"high","8192"}}}}}};
  bad=false;try {requestBody(invalid,input,true);}catch(...){bad=true;}assert(bad);
  AnthropicStream stream(1000);
  stream.consume({{"type","message_start"},{"message",{{"content",Json::array()},{"usage",{{"input_tokens",10},{"cache_read_input_tokens",7},{"cache_creation_input_tokens",3}}}}}},"message_start",1100);
  stream.consume({{"type","content_block_start"},{"index",0},{"content_block",{{"type","thinking"},{"thinking",""},{"signature",""}}}},"",1200);
  stream.consume({{"type","content_block_delta"},{"index",0},{"delta",{{"type","thinking_delta"},{"thinking","public"}}}},"",1300);
  stream.consume({{"type","content_block_delta"},{"index",0},{"delta",{{"type","signature_delta"},{"signature","OPAQUE_SECRET"}}}},"",1400);
  assert(!stream.snapshot(1500,true)["metadata"].contains("anthropic_content"));
  stream.consume({{"type","content_block_stop"},{"index",0}},"",2200);
  stream.consume({{"type","content_block_start"},{"index",1},{"content_block",{{"type","text"},{"text","answer"}}}},"",2300);
  stream.consume({{"type","content_block_stop"},{"index",1}},"",2400);
  stream.consume({{"type","message_delta"},{"delta",{{"stop_reason","end_turn"}}},{"usage",{{"output_tokens",9}}}},"",2500);
  assert(stream.terminal() && !stream.stopped());
  stream.consume({{"type","message_stop"}},"",2600);stream.finish(2700);
  auto snapshot=stream.snapshot(2800,true);assert(snapshot["metadata"]["reasoning_duration_ms"]==1000);
  assert(snapshot["output"]=="answer" && snapshot["reasoning"]=="public");
  assert(snapshot["metadata"]["usage"]["input_tokens"]==20 && snapshot["metadata"]["usage"]["total_tokens"]==29);
  assert(stream.redact("OPAQUE_SECRET") == "[已隐藏]");
  snapshot["metadata"]["replay_identity"]=replayIdentity(m);
  Json rows=Json::array({{{"seq",1},{"role","assistant"},{"status","complete"},{"provider_id","p"},{"model_config_id","m"},{"output_text","answer"},{"metadata_json",snapshot["metadata"]}}});
  auto replay=contextInput(rows,0,100,m);assert(replay.size()==1 && replay[0]["content"][0]["signature"]=="OPAQUE_SECRET");
  assert(contextInput(rows,0,100,m,true)[0]["content"][0]["type"]=="output_text");
  for(const auto &variant : {"version","provider","model","endpoint","credential"}) {
    auto changed=m;
    if(std::string(variant)=="version")changed["provider_metadata"]["anthropic"]["version"]="2023-06-01";
    if(std::string(variant)=="provider")changed["provider_id"]="other";
    if(std::string(variant)=="model")changed["name"]="other";
    if(std::string(variant)=="endpoint")changed["base_url"]="https://other.example/v1";
    if(std::string(variant)=="credential")changed["provider_metadata"]["credential_revision"]="new";
    replay=contextInput(rows,0,100,changed);assert(replay[0]["content"][0]["type"]=="output_text");
  }
  auto partial=normalizedAnthropicUsage({{"input_tokens",10},{"cache_read_input_tokens",7}});
  assert(!partial.contains("input_tokens") && !partial.contains("cache_write_tokens"));
  auto unknownUsage=normalizedAnthropicUsage(Json::object());assert(unknownUsage.empty());
  char temp[]="/tmp/penchat-anthropic-XXXXXX";const std::string folder=mkdtemp(temp);
  {
    Database db(join(folder,"penchat.db"));
    Json form={{"name","Claude"},{"baseURL","https://api.anthropic.com/v1"},{"apiKey","fake"},{"api","anthropic"},{"anthropic",{{"auth","x-api-key"},{"version","2023-06-01"}}}};
    db.saveProvider(form,{"claude-sonnet-4-6"});auto model=db.models()[0];auto id=model["id"].get<std::string>();
    const auto identity=replayIdentity(model);assert(!model.contains("api_key") && !model.contains("headers_json"));
    auto edit=db.providerForm(model["provider_id"]);edit["apiKey"]="changed";db.saveProvider(edit,{"claude-sonnet-4-6"});
    auto next=db.requestModel(model);assert(replayIdentity(next)!=identity && db.models()[0]["id"]==id);
    auto oldRows=rows;
    oldRows[0]["provider_id"]=model["provider_id"];oldRows[0]["model_config_id"]=model["id"];
    oldRows[0]["metadata_json"]["replay_identity"]=identity;
    assert(contextInput(oldRows,0,100,model)[0]["content"][0]["type"]=="thinking");
    assert(contextInput(oldRows,0,100,next)[0]["content"][0]["type"]=="output_text");
    const auto session=uid("session");db.append(session,"user","snapshot");
    const auto answer=db.append(session,"assistant","");
    db.recordRequest(answer,next);
    auto parameters=Json::parse(db.query("SELECT request_json FROM chat_messages WHERE id=?",{answer})[0]["request_json"].get<std::string>());
    assert(parameters["thinking_mode"]=="api_default" && parameters["summary"]==false);
    auto cap=anthropicReasoning(next);db.saveModelCapabilities(id,cap,32768,8192);
    assert(anthropicMaxOutput(db.models()[0])==8192);
  }
  unlink(join(folder,"penchat.db").c_str());rmdir(folder.c_str());
  std::cout<<"Messages configuration, budgets, signed replay identity, timing, usage and Provider revision passed\n";
}
