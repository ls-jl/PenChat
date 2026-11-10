#include "network.hpp"
#include "tool_registry.hpp"
#include "tools.hpp"
#include <cassert>
using namespace penchat;
int main() {
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
}
