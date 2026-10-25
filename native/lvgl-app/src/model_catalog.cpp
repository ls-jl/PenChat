#include "model_catalog.hpp"
#include "miniz.h"
#include "model_catalog_data.inc"
#include <map>
#include <cctype>
#include <regex>
#include <stdexcept>

namespace penchat {
struct ModelCatalog::Impl {
  Json data;
  struct Rule { Json node; std::regex pattern; };
  std::vector<Rule> rules;
  std::map<std::string, Json> nodes;
};
namespace {
uint32_t word(const unsigned char *p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
std::string normalize(const std::string &name, const Json &data) {
  if (name.size() > 1024) return {};
  std::string ascii;
  const auto &table = data.at("normalization");
  for (size_t i = 0; i < name.size();) {
    unsigned char first = name[i++];
    uint32_t cp = first;
    if (first >= 128) {
      int rest = first >= 0xc2 && first <= 0xdf ? 1 : first <= 0xef && first >= 0xe0 ? 2 : first <= 0xf4 && first >= 0xf0 ? 3 : -1;
      if (rest < 0 || i + rest > name.size()) return {};
      cp = first & ((1u << (6 - rest)) - 1);
      const int n = rest;
      while (rest--) {
        const unsigned char c = name[i++];
        if ((c & 0xc0) != 0x80) return {};
        cp = cp * 64 + (c & 63);
      }
      if ((n == 1 && cp < 128) || (n == 2 && cp < 2048) || (n == 3 && cp < 65536) ||
          cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return {};
    }
    if (cp < 128) {
      if (cp < 32 && cp != '\t' && cp != '\r' && cp != '\n') return {};
      ascii += char(cp >= 'A' && cp <= 'Z' ? cp + 32 : cp == '\t' || cp == '\r' || cp == '\n' ? ' ' : cp);
    } else {
      const auto it = table.find(std::to_string(cp));
      if (it == table.end()) return {};
      ascii += it->get<std::string>();
    }
    if (ascii.size() > 256) return {};
  }
  const auto first = ascii.find_first_not_of(' '), last = ascii.find_last_not_of(' ');
  if (first == std::string::npos) return {};
  ascii = ascii.substr(first, last - first + 1);
  std::string out;
  bool space = false;
  for (char c : ascii) {
    if (c == ' ') { if (!space) out += '-'; space = true; }
    else { out += c == '_' ? '-' : c; space = false; }
  }
  for (const auto &p : data.at("prefixes")) {
    const auto prefix = p.get<std::string>();
    if (out.compare(0, prefix.size(), prefix) == 0) { out.erase(0, prefix.size()); break; }
  }
  return out;
}
std::string host(const std::string &url) {
  const auto p = url.find("://");
  if (p == std::string::npos) return {};
  auto h = url.substr(p + 3, url.find('/', p + 3) - (p + 3));
  // No userinfo, suffix matching or alternate ports for automatic trust.
  std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return std::tolower(c); });
  return h;
}
}
ModelCatalog::ModelCatalog(const unsigned char *data, size_t size) {
  try {
    if (size < 17 || size > 512 * 1024 || std::string(reinterpret_cast<const char *>(data), 4) != "PCMC" || word(data + 4) != 1)
      return;
    const auto length = word(data + 8);
    if (!length || length > 2 * 1024 * 1024) return;
    std::vector<unsigned char> raw(length);
    mz_ulong n = length;
    if (mz_uncompress(raw.data(), &n, data + 16, size - 16) != MZ_OK || n != length ||
        mz_crc32(0, raw.data(), raw.size()) != word(data + 12)) return;
    auto impl = std::make_shared<Impl>();
    impl->data = Json::from_cbor(raw, true, false);
    if (!impl->data.is_object() || impl->data.value("format", 0) != 1 || !impl->data.at("rules").is_array() ||
        impl->data.at("rules").size() > 2048 || impl->data.at("normalization").size() > 8192) return;
    for (const auto &r : impl->data.at("rules")) {
      const auto id = r.at("id").get<std::string>();
      if (!impl->nodes.emplace(id, r).second) return;
      const auto pattern = r.at("pattern").get<std::string>();
      if (pattern.size() > 256) return;
      if (!pattern.empty()) impl->rules.push_back({r, std::regex(pattern, std::regex::ECMAScript | std::regex::optimize)});
    }
    for (const auto &entry : impl->nodes) {
      auto r = entry.second;
      std::vector<std::string> seen;
      while (!r.at("parent").get<std::string>().empty()) {
        const auto id = r.at("id").get<std::string>();
        if (seen.size() >= 16 || std::find(seen.begin(), seen.end(), id) != seen.end()) return;
        seen.push_back(id);
        r = impl->nodes.at(r.at("parent").get<std::string>());
      }
    }
    impl_ = impl;
  } catch (...) { /* Bad catalog must not prevent manual configuration/chat. */ }
}
const ModelCatalog &ModelCatalog::instance() {
  static const ModelCatalog catalog(CatalogBytes, sizeof(CatalogBytes));
  return catalog;
}
Json ModelCatalog::match(const std::string &name) const {
  if (!impl_) return {{"status", "unavailable"}};
  auto input = normalize(name, impl_->data);
  int priority = -1;
  std::vector<Json> hits;
  if (!input.empty()) for (const auto &r : impl_->rules) if (std::regex_match(input, r.pattern)) {
    int rank = r.node.at("priority");
    if (rank > priority) { hits.clear(); priority = rank; }
    if (rank == priority) hits.push_back(r.node);
  }
  if (hits.empty()) return {{"status", "unmatched"}};
  if (hits.size() != 1) return {{"status", "ambiguous"}};
  const auto &hit = hits.front();
  Json facts = Json::object(), profiles = Json::object(), sources = Json::array(), origins = Json::object();
  auto node = hit;
  while (true) {
    for (auto i = node.at("facts").begin(); i != node.at("facts").end(); ++i)
      if (!facts.contains(i.key()) && !i->is_null()) { facts[i.key()] = *i; origins[i.key()] = node.at("id"); }
    for (auto i = node.at("profiles").begin(); i != node.at("profiles").end(); ++i)
      if (!profiles.contains(i.key())) profiles[i.key()] = *i;
    for (const auto &s : node.at("sources")) if (std::find(sources.begin(), sources.end(), s) == sources.end()) sources.push_back(s);
    const auto parent = node.at("parent").get<std::string>();
    if (parent.empty()) break;
    node = impl_->nodes.at(parent);
  }
  return {{"status", "matched"}, {"rule", hit.at("id")}, {"vendor", hit.at("vendor")},
          {"canonical", hit.at("canonical")}, {"revision", impl_->data.at("revision")},
          {"facts", facts}, {"profiles", profiles}, {"origins", origins}, {"sources", sources}};
}
std::string modelAdapter(const Json &model) {
  const auto metadata = objectField(model, "provider_metadata");
  const auto selected = metadata.value("model_adapter", std::string("auto"));
  if (selected != "auto") return selected;
  if (model.value("base_url", std::string()).compare(0, 8, "https://") != 0) return "standard";
  const auto h = host(model.value("base_url", std::string()));
  if (h == "api.deepseek.com") return "deepseek";
  if (h == "api.moonshot.ai" || h == "api.moonshot.cn") return "kimi";
  if (h == "api.minimax.io" || h == "api.minimaxi.com") return "minimax";
  if (h == "api.z.ai" || h == "open.bigmodel.cn") return "glm";
  if (h == "dashscope.aliyuncs.com" || h == "dashscope-intl.aliyuncs.com" || h == "dashscope-us.aliyuncs.com") return "qwen";
  return "standard";
}
Json modelRecommendation(const Json &model) {
  auto match = ModelCatalog::instance().match(model.value("name", std::string()));
  Json out = {{"match", match}, {"fields", Json::object()}};
  if (match.value("status", std::string()) != "matched") return out;
  auto &fields = out["fields"];
  const auto &facts = match.at("facts");
  if (facts.contains("context_tokens")) fields["context_budget"] = facts.at("context_tokens");
  if (facts.contains("input_modalities")) {
    fields["input_modalities"] = facts.at("input_modalities");
    fields["vision"] = std::find(facts.at("input_modalities").begin(), facts.at("input_modalities").end(), "image") != facts.at("input_modalities").end();
  }
  if (facts.contains("max_input_tokens")) fields["max_input_tokens"] = facts.at("max_input_tokens");
  if (facts.contains("reasoning_supported")) fields["reasoning_supported"] = facts.at("reasoning_supported");
  // Retain future facts privately, never create tool/UI actions from them.
  fields["catalog_facts"] = facts;
  const auto api = protocolName(apiProtocol(model));
  const auto &profiles = match.at("profiles");
  if (profiles.contains(api)) {
    auto reasoning = profiles.at(api);
    const auto required = reasoning.value("adapter", std::string("standard"));
    const bool active = required == "standard" || modelAdapter(model) == required;
    if (active && reasoning.contains("max_input_tokens")) fields["max_input_tokens"] = reasoning.at("max_input_tokens");
    if (!active) {
      // Standard effort fields remain valid on Chat/Responses gateways. Modes
      // requiring vendor bodies are not advertised until explicitly enabled.
      if (api == "anthropic" || reasoning.value("mode", std::string()) == "toggle") reasoning["levels"] = Json::object();
      else if (reasoning.value("wire", std::string()) != "standard") reasoning["levels"].erase("none");
      reasoning["adapter_required"] = required;
    }
    fields["reasoning"] = reasoning;
  }
  return out;
}
Json catalogReasoning(const Json &model) {
  const auto capabilities = objectField(model, "capabilities_json");
  const bool hasSaved = capabilities.contains("reasoning") && capabilities.at("reasoning").is_object();
  const auto saved = objectField(capabilities, "reasoning");
  Json out = saved;
  if (!hasSaved) {
    const auto rec = modelRecommendation(model);
    const auto &fields = rec.at("fields");
    out = fields.contains("reasoning") ? fields.at("reasoning") :
        Json{{"mode", anthropicProtocol(model) ? "off" : "effort"}, {"levels", Json::object()},
             {"summary", false}, {"disable", false}};
  }
  const auto required = out.value("adapter", std::string("standard"));
  if (required != "standard" && required != modelAdapter(model)) {
    out["adapter_required"] = required;
    if (anthropicProtocol(model) || out.value("mode", std::string()) == "toggle") {
      out["levels"] = Json::object();
      if (anthropicProtocol(model)) out["mode"] = "off";
    } else if (out.value("wire", std::string()) != "standard") {
      auto &levels = out["levels"];
      for (auto i = levels.begin(); i != levels.end();) {
        if (*i == "disabled" || *i == "enabled") i = levels.erase(i); else ++i;
      }
    }
  }
  return out;
}
Json initialModelCapabilities(const Json &model) {
  const auto rec = modelRecommendation(model);
  auto caps = rec.at("fields");
  caps["catalog"] = {{"rule", rec.at("match").value("rule", std::string())},
                     {"revision", rec.at("match").value("revision", std::string())}, {"overrides", Json::array()}};
  if (!caps.contains("reasoning")) caps["reasoning"] = catalogReasoning(model);
  if (!caps.contains("context_budget")) caps["context_budget"] = 16384;
  if (anthropicProtocol(model)) caps["max_output_tokens"] = 16384;
  return caps;
}
void validateModelImages(const Json &model, const Json &input) {
  const auto caps = objectField(model, "capabilities_json");
  if (caps.value("vision", Json()) != Json(false)) return;
  for (const auto &m : input) {
    const auto content = m.value("content", Json());
    if (content.is_array()) for (const auto &p : content)
      if (p.is_object() && p.value("type", std::string()) == "input_image")
        throw std::runtime_error("此模型已配置为不支持图片，请修改模型或图片；输入已保留");
  }
}
void applyModelParameters(Json &body, const Json &model) {
  const auto caps = objectField(model, "capabilities_json");
  auto reasoning = objectField(caps, "reasoning");
  if (reasoning.empty()) reasoning = catalogReasoning(model);
  const auto required = reasoning.value("adapter", std::string("standard"));
  if (required == "standard" || modelAdapter(model) != required ||
      model.value("effective_effort", std::string()) == "api_default") return;
  const auto effort = model.value("effort_value", std::string());
  const auto wire = reasoning.value("wire", std::string("standard"));
  auto mode = reasoning.value("request_mode", std::string());
  if (!effort.empty()) mode = effort == "disabled" || effort == "none" ? "disabled" : "enabled";
  if (mode != "enabled" && mode != "disabled") return;
  if (mode == "disabled" && !reasoning.value("disable", false))
    throw std::runtime_error("此模型不支持关闭思考，请修改模型设置");
  if (wire == "qwen_toggle") {
    body.erase("reasoning_effort"); body["enable_thinking"] = mode == "enabled";
    if (mode == "enabled" && reasoning.contains("token_budget") && reasoning["token_budget"].is_number_integer()) {
      int64_t budget = reasoning["token_budget"];
      if (budget < 1 || budget > 262144) throw std::runtime_error("思考预算须为1～262144");
      body["thinking_budget"] = budget;
    }
  } else if (wire == "thinking_effort" || wire == "thinking_toggle" || wire == "anthropic_switch") {
    body["thinking"] = {{"type", mode}};
    if (mode == "disabled") { body.erase("reasoning_effort"); body.erase("output_config"); }
    else if (!effort.empty() && wire == "anthropic_switch") body["output_config"] = {{"effort", effort}};
  }
}
} // namespace penchat
