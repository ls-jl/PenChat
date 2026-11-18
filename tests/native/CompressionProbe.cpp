#include "context.hpp"
#include "network.hpp"
#include "tools.hpp"
#include <iostream>
using namespace penchat;
int main(int argc, char **argv) {
  if (argc != 4) return 2;
  Database db(argv[1]);
  Json model = {{"id", "probe"}, {"name", "gemini-fixture"},
                {"provider_id", "probe"}, {"api_key", "local-fake-only"},
                {"base_url", argv[2]}, {"provider_metadata", {{"api", "chat"}}},
                {"capabilities_json", {{"context_budget", 16384}}}};
  if (std::string(argv[3]) == "compatible")
    model["provider_metadata"]["compatibility"] = {{"enabled", true}};
  Json input = Json::array({{{"role", "user"}, {"content", Json::array({
      {{"type", "input_text"}, {"text", "Summarize local fixture only."}}})}}});
  std::atomic<bool> cancel{false};
  Events events(true);
  streamModelRound(model, input, cancel, events, 1);
  Json out;
  for (const auto &event : events.drain())
    if (event.type == "network_error" || event.type == "done")
      out["direct"] = {{"type", event.type}, {"data", event.data}};
  Json rows = Json::array();
  for (int i = 1; i <= 2; ++i)
    rows.push_back({{"seq", i}, {"role", i == 1 ? "user" : "assistant"},
                    {"status", "complete"}, {"output_text", "Local reference."},
                    {"parts", Json::array()}});
  Json chat = {{"capabilities_json", {{"context_budget", 32768}}}};
  try {
    auto checkpoint = compressContext(db, {{"rows", rows}}, model,
        Json::array(), chat, true, cancel, [](int, int) {});
    out["compression"] = {{"success", true}, {"summary", checkpoint.at("summary")}};
  } catch (const std::exception &error) {
    out["compression"] = {{"success", false}, {"error", error.what()}};
  }
  std::cout << out.dump() << '\n';
}
