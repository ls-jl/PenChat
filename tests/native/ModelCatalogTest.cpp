#include "model_catalog.hpp"
#include "reasoning.hpp"
#include "network.hpp"
#include "database.hpp"
#include "context.hpp"
#include "miniz.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
#include <unistd.h>
using namespace penchat;
namespace {
std::vector<unsigned char> pack(const Json &data) {
  auto raw = Json::to_cbor(data);
  mz_ulong n = mz_compressBound(raw.size());
  std::vector<unsigned char> blob(16 + n);
  std::copy_n("PCMC", 4, blob.begin());
  auto word = [&](int at, uint32_t value) { for (int i = 0; i < 4; ++i) blob[at + i] = value >> (8 * i); };
  word(4, 1); word(8, raw.size()); word(12, mz_crc32(0, raw.data(), raw.size()));
  assert(mz_compress(blob.data() + 16, &n, raw.data(), raw.size()) == MZ_OK);
  blob.resize(16 + n); return blob;
}
Json model(const std::string &name, const std::string &api = "responses", const std::string &url = "https://api.openai.com/v1") {
  return {{"name", name}, {"base_url", url}, {"provider_metadata", {{"api", api}}}};
}
}
int main(int argc, char **argv) {
  assert(argc == 2);
  auto reviewed = Json::parse(fileText(argv[1]));
  const auto &catalog = ModelCatalog::instance();
  assert(catalog.available());
  for (const auto &t : reviewed.at("tests")) {
    auto hit = catalog.match(t.at("input"));
    assert(hit.value("rule", Json()) == t.at("rule"));
    if (!t.at("context").is_null()) assert(hit.at("facts").at("context_tokens") == t.at("context"));
  }
  for (const auto &name : {"proxy/gpt-5.2", "gpt-5.2-turbo", "gpt-5.2-fake", "claude-5-unknown", "deepseek-v4.99", "gpt-5.2/evil", "qwen3.7-max-2099-12-12", ""})
    assert(catalog.match(name).at("status") == "unmatched");
  assert(catalog.match("  ＯｐｅｎＡＩ／ＧＰＴ－５．２  ").at("rule") == "openai:gpt-5:gpt-5.2");
  assert(catalog.match(std::string("gpt-5.2\xc0\x80")).at("status") == "unmatched");
  assert(catalog.match(std::string(10000, 'a')).at("status") == "unmatched");
  assert(!catalog.match("deepseek-v4").at("facts").contains("input_modalities"));
  assert(catalog.match("qwen3.7-max-2026-06-08").at("facts").at("input_modalities").size() == 3);
  assert(catalog.match("qwen3.7-max").at("facts").at("input_modalities") == Json::array({"text"}));
  for (const auto &r : reviewed.at("rules")) {
    auto facts = r.at("facts");
    assert(!facts.contains("max_output_tokens") && !facts.contains("default_output_tokens"));
  }
  auto blob = pack({{"format", 1}, {"rules", Json::array()}, {"prefixes", Json::array()}, {"normalization", Json::object()}, {"revision", "test"}});
  ModelCatalog empty(blob.data(), blob.size()); assert(empty.available());
  blob.back() ^= 7; ModelCatalog corrupt(blob.data(), blob.size()); assert(!corrupt.available());
  ModelCatalog truncated(blob.data(), 8); assert(!truncated.available());
  auto data = Json{{"format", 1}, {"rules", Json::array()}, {"prefixes", Json::array()}, {"normalization", Json::object()}, {"revision", "test"}};
  Json r = {{"id", "one"}, {"parent", ""}, {"vendor", "test"}, {"pattern", "^m$"}, {"priority", 300}, {"canonical", "m"}, {"facts", Json::object()}, {"profiles", Json::object()}, {"sources", Json::array()}};
  data["rules"].push_back(r); r["id"] = "two"; data["rules"].push_back(r);
  blob = pack(data); ModelCatalog ambiguous(blob.data(), blob.size()); assert(ambiguous.match("m").at("status") == "ambiguous");
  data["rules"][0]["parent"] = "two"; data["rules"][1]["parent"] = "one";
  blob = pack(data); ModelCatalog cycle(blob.data(), blob.size()); assert(!cycle.available());
  auto gpt = model("gpt-5"); gpt["capabilities_json"] = initialModelCapabilities(gpt);
  auto selected = resolveReasoning(gpt, {{"effort", "minimal"}}, "");
  auto input = Json::array({{{"role", "user"}, {"content", Json::array({{{"type", "input_text"}, {"text", "question"}}})}}});
  auto body = requestBody(selected, input, true);
  assert(body.at("reasoning").at("effort") == "minimal"); assert(!body.contains("max_output_tokens"));
  gpt["provider_metadata"]["api"] = "chat";
  body = requestBody(resolveReasoning(gpt, {{"effort", "minimal"}}, ""), input, true);
  assert(body.at("reasoning_effort") == "minimal" && !body.contains("max_completion_tokens"));
  for (const auto &api : {"chat", "responses", "anthropic"}) {
    auto deep = model("deepseek-flash", api, "https://api.deepseek.com");
    deep["capabilities_json"] = initialModelCapabilities(deep);
    selected = resolveReasoning(deep, {{"effort", "high"}}, ""); body = requestBody(selected, input, true);
    if (std::string(api) == "chat") assert(body.at("thinking").at("type") == "enabled" && body.at("reasoning_effort") == "high");
    if (std::string(api) == "responses") assert(body.at("reasoning").at("effort") == "high");
    if (std::string(api) == "anthropic") assert(body.at("thinking").at("type") == "enabled" && body.at("output_config").at("effort") == "high");
    body = requestBody(resolveReasoning(deep, {{"effort", "api_default"}}, "high"), input, true);
    assert(!body.contains("thinking") && !body.contains("reasoning_effort") && !body.contains("output_config"));
  }
  auto qwen = model("qwen3.8-max", "chat", "https://dashscope.aliyuncs.com/compatible-mode/v1");
  auto caps = initialModelCapabilities(qwen); caps["reasoning"]["request_mode"] = "enabled"; caps["reasoning"]["token_budget"] = 2048;
  qwen["capabilities_json"] = caps; body = requestBody(qwen, input, true);
  assert(body["enable_thinking"] == true && body["thinking_budget"] == 2048 && !body.contains("reasoning_effort"));
  qwen["base_url"] = "https://dashscope.aliyuncs.com.evil.test/v1";
  body = requestBody(qwen, input, true); assert(!body.contains("enable_thinking"));
  qwen["provider_metadata"]["model_adapter"] = "qwen"; body = requestBody(qwen, input, true); assert(body.at("enable_thinking") == true);
  auto detached = model("deepseek-flash", "chat", "https://api.deepseek.com");
  detached["capabilities_json"] = initialModelCapabilities(detached);
  detached["provider_metadata"]["model_adapter"] = "standard";
  body = requestBody(resolveReasoning(detached, {{"effort", "none"}}, ""), input, true);
  assert(!body.contains("thinking") && !body.contains("reasoning_effort"));
  qwen["provider_metadata"]["api"] = "responses"; qwen["capabilities_json"] = initialModelCapabilities(qwen);
  body = requestBody(resolveReasoning(qwen, {{"effort", "xhigh"}}, ""), input, true);
  assert(body.at("reasoning").at("effort") == "xhigh" && !body.contains("thinking_budget"));
  assert(objectField(qwen, "capabilities_json").at("max_input_tokens") == 800000);
  auto claude = model("claude-opus-5-5", "anthropic", "https://api.anthropic.com/v1");
  claude["capabilities_json"] = initialModelCapabilities(claude);
  assert(requestBody(claude, input, true).at("max_tokens") == 16384);
  auto text = model("deepseek-v4-pro"); text["capabilities_json"] = initialModelCapabilities(text);
  auto photos = input; photos[0]["content"].push_back({{"type", "input_image"}, {"image_url", "https://image.test/p.png"}});
  bool bad = false; try { requestBody(text, photos, true); } catch (...) { bad = true; } assert(bad);
  auto context = model("custom"); context["capabilities_json"] = {{"context_budget", 10000}};
  assert(compressionTrigger(context) == 7500 && compressionTarget(context) == 5000);
  context["context_compression_percent"] = 10;
  assert(compressionTrigger(context) == 1000 && compressionTarget(context) == 666);
  context["context_compression_percent"] = 95;
  assert(compressionTrigger(context) == 9500 && compressionTarget(context) == 5000);
  char path[] = "/tmp/penchat-catalog-XXXXXX"; auto directory = std::string(mkdtemp(path));
  {
    Database db(join(directory, "penchat.db"));
    Json form = {{"name", "fixture"}, {"baseURL", "https://api.openai.com/v1"}, {"apiKey", "fake"}};
    db.saveProvider(form, {"gpt-5"}); auto m = db.activeModel();
    assert(contextBudget(m) == 400000 && supportedEfforts(m).size() == 4);
    auto manual = objectField(m, "capabilities_json"); manual["context_budget"] = 22000;
    db.saveModelSettings(m.at("id"), manual);
    form = db.providerForm(m.at("provider_id")); db.saveProvider(form, {"gpt-5"});
    assert(db.activeModel().at("id") == m.at("id") && contextBudget(db.activeModel()) == 22000);
    form["api"] = "anthropic"; form["max_output_tokens"] = "";
    bad = false; try { db.saveProvider(form, {"claude-opus-5-5"}); } catch (...) { bad = true; } assert(bad);
  }
  auto started = ticks();
  for (int i = 0; i < 10000; ++i) assert(catalog.match(i % 2 ? "gpt-5.2" : "not-a-model").is_object());
  struct rusage usage {}; getrusage(RUSAGE_SELF, &usage);
  std::cout << "Catalog 10000 matches: " << ticks() - started << "ms, peak RSS: " << usage.ru_maxrss << " KiB\n";
  std::cout << "Reviewed model catalog, safe fill, protocols and thresholds passed\n";
}
