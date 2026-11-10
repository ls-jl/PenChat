#include "network.hpp"
#include "tool_registry.hpp"
#include "tools.hpp"
#include "chat_instructions.hpp"
#include "context.hpp"
#include <cassert>
using namespace penchat;
int main() {
  const std::string custom="Answer in English. Preserve my task instructions.";
  const auto prompt=chatInstructions(custom,true);
  assert(prompt.find("PenChat_APPLICATION_RULES_V1")==0 && prompt.find(custom)!=std::string::npos);
  assert(chatInstructions(custom,false).find("Page IDs expire")==std::string::npos);
  const std::string raw=u8"Useful text\n<｜｜DSML｜｜ calls>\n<｜｜DSML｜｜ invoke name=\"web_open\">";
  for(size_t n=0;n<=raw.size();++n) {
    const auto visible=checkedToolText(raw.substr(0,n)).text;
    assert(visible.find("DSML")==std::string::npos);
    assert(std::string("Useful text\n").compare(0,visible.size(),visible)==0);
  }
  assert(checkedToolText(raw,true).rejected);
  assert(publicAssistantText(raw).find("格式不兼容")!=std::string::npos);
  for(const auto &normal:{std::string("Example: {\"name\":\"web_search\",\"arguments\":{\"query\":\"x\"}}"),
                          std::string("```json\n{\"hello\": true}\n```"),std::string("a < b"),std::string("<")})
    assert(checkedToolText(normal,true).text==normal&&!checkedToolText(normal,true).rejected);
  assert(webEnabled(Json::object(), Json::object()));
  assert(!webEnabled({{"web_enabled", false}}, {{"new_chat_web", "on"}}));
  assert(webEnabled({{"web_enabled", true}}, {{"new_chat_web", "off"}}));
  assert(!webEnabled(Json::object(), {{"new_chat_web", "recent"}, {"last_web_enabled", false}}));
  assert(webEnabled(Json::object(), {{"new_chat_web", "recent"}}));
  int calls = 0;
  ToolRegistry registry;
  registry.add({{"name", "future_echo"},
                {"parameters",
                 {{"type", "object"},
                  {"required", Json::array({"text"})},
                  {"properties", {{"text", {{"type", "string"}, {"maxLength", 4}}}}}}}},
               [&](const Json &args) {
                 ++calls;
                 return Json({{"text", args.at("text")}});
               });
  assert(registry.execute("future_echo", {{"text", "ok"}}).at("text") == "ok");
  for (const auto &invalid : Json::array({Json::object(),
                                          {{"text", 3}},
                                          {{"text", "12345"}},
                                          {{"text", "ok"}, {"unexpected", true}}})) {
    bool rejected = false;
    try {
      registry.execute("future_echo", invalid);
    } catch (...) {
      rejected = true;
    }
    assert(rejected);
  }
  bool unknown = false;
  try {
    registry.execute("not_registered", {{"text", "ok"}});
  } catch (...) {
    unknown = true;
  }
  assert(unknown && calls == 1);
  Json model = {
      {"name", "fixture"},
      {"provider_metadata", {{"api", "responses"}}},
      {"web_tools", true},
      {"capabilities_json", {{"reasoning", {{"mode", "off"}, {"levels", Json::object()}}}}}};
  auto function = Json{{"type", "function_call"},
                       {"call_id", "c1"},
                       {"name", "web_search"},
                       {"arguments", "{\"query\":\"x\"}"}};
  auto output = Json{{"type", "function_call_output"}, {"call_id", "c1"}, {"output", "reference"}};
  auto responses = requestBody(model, Json::array({function, output}), false);
  assert(!responses.at("store").get<bool>() && responses.at("input")[1].at("call_id") == "c1");
  assert(responses.at("tools").size() == 6 && responses.at("tools")[0].at("strict") == false);
  model["provider_metadata"]["api"] = "chat";
  Json chat =
      Json::array({{{"role", "assistant"},
                    {"content", ""},
                    {"tool_calls",
                     Json::array({{{"id", "c1"},
                                   {"type", "function"},
                                   {"function", {{"name", "web_search"}, {"arguments", "{}"}}}}})}},
                   {{"role", "tool"}, {"tool_call_id", "c1"}, {"content", "reference"}}});
  auto chatBody = requestBody(model, chat, true);
  assert(chatBody.at("messages")[1].at("role") == "tool" &&
         chatBody.at("messages")[1].at("tool_call_id") == "c1");
  assert(chatBody.at("tools")[0].at("function").contains("parameters"));
  model["provider_metadata"]["api"] = "anthropic";
  auto messageBody = requestBody(
      model,
      Json::array(
          {{{"role", "assistant"},
            {"content", Json::array({{{"type", "tool_use"},
                                      {"id", "c1"},
                                      {"name", "web_search"},
                                      {"input", {{"query", "x"}}}}})}},
           {{"role", "user"},
            {"content", Json::array({{{"type", "tool_result"},
                                      {"tool_use_id", "c1"},
                                      {"content", Json::array({{{"type", "text"},
                                                                {"text", "reference"}}})}}})}}}),
      false);
  assert(messageBody.at("messages")[1].at("content")[0].at("tool_use_id") == "c1");
  assert(messageBody.at("tools")[0].contains("input_schema"));
  model["web_tools"] = false;
  assert(!requestBody(model, Json::array({{{"role", "user"}, {"content", "hello"}}}), false)
              .contains("tools"));
  auto exported =
      publicWebMetadata({{"tool_replay_input", Json::array({{{"encrypted_content", "secret"}}})},
                         {"web_images", Json::array({{{"ref", "img_1"},
                                                      {"path", "/private/assets/web-x.img"},
                                                      {"asset_id", "a1"},
                                                      {"data", "Base64-secret"},
                                                      {"source_url", "https://example.org/"}}})}});
  assert(exported.at("web_images")[0].at("asset") == "a1");
  assert(exported.dump().find("secret") == std::string::npos &&
         exported.dump().find("/private/") == std::string::npos);
  model["provider_metadata"]["api"]="responses";
  model["id"]="m";model["provider_id"]="p";model["web_enabled"]=true;
  Json old={{"seq",1},{"role","assistant"},{"status","complete"},
            {"output_text",raw},{"provider_id","p"},{"model_config_id","m"},
            {"metadata_json",{{"replay_identity",replayIdentity(model)},
                              {"response_output_items",Json::array({{{"type","message"},{"status","completed"},
                                  {"role","assistant"},{"content",Json::array({{{"type","output_text"},{"text",raw}}})}}})}}}};
  const auto history=contextInput(Json::array({old}),0,2,model).dump();
  assert(history.find("DSML")==std::string::npos&&history.find("Useful text")!=std::string::npos);
}
