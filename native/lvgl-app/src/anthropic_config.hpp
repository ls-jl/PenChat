#pragma once
#include "api_protocol.hpp"
#include <map>
namespace penchat {
// Exact families/date aliases only. Unknown proxy names are never guessed.
inline bool anthropicFamily(const std::string &name, const std::string &family) {
  return name == family || name == family + "-latest" ||
         (name.size() == family.size() + 9 && name.compare(0, family.size() + 1, family + "-") == 0 &&
          std::all_of(name.begin() + family.size() + 1, name.end(), [](char c) { return c >= '0' && c <= '9'; }));
}
inline int anthropicOutputLimit(const Json &model) {
  const auto name = model.value("name", std::string());
  if (anthropicFamily(name, "claude-3-haiku-20240307") || anthropicFamily(name, "claude-3-opus-20240229")) return 4096;
  if (anthropicFamily(name, "claude-3-5-sonnet") || anthropicFamily(name, "claude-3-5-haiku")) return 8192;
  for (const auto &family : {"claude-3-7-sonnet", "claude-sonnet-4", "claude-sonnet-4-5",
                             "claude-haiku-4-5", "claude-opus-4", "claude-opus-4-1", "claude-opus-4-5"})
    if (anthropicFamily(name, family)) return family == std::string("claude-opus-4") ||
        family == std::string("claude-opus-4-1") ? 32000 : 64000;
  for (const auto &family : {"claude-sonnet-4-6", "claude-opus-4-6", "claude-opus-4-7",
                             "claude-opus-4-8", "claude-opus-5", "claude-opus-5-5",
                             "claude-sonnet-5", "claude-sonnet-5-5", "claude-haiku-5-5",
                             "claude-fable-5", "claude-fable-5-1", "claude-mythos-5", "claude-mythos-5-1"})
    if (anthropicFamily(name, family)) return 128000;
  return 0;
}
inline int anthropicMaxOutput(const Json &model) {
  const auto caps = objectField(model, "capabilities_json");
  const auto value = caps.value("max_output_tokens", Json(16384));
  const auto limit = anthropicOutputLimit(model);
  if (!value.is_number_integer()) throw std::runtime_error("最大输出 token 必须是正整数");
  const auto n = value.get<double>();
  if (n < 1 || n > 1048576 || (limit && n > limit && caps.contains("max_output_tokens")))
    throw std::runtime_error("最大输出 token 超出模型上限，请修改模型设置");
  return std::min(int(n), limit ? limit : 1048576);
}
inline std::string anthropicBudget(const std::string &key) {
  static const std::map<std::string, std::string> values{
      {"none", "disabled"}, {"low", "1024"}, {"medium", "4096"},
      {"high", "8192"}, {"xhigh", "12288"}, {"max", "14336"}};
  const auto it = values.find(key);
  return it == values.end() ? std::string() : it->second;
}
inline Json anthropicReasoning(const Json &model) {
  const auto caps = objectField(model, "capabilities_json");
  if (caps.contains("reasoning") && caps["reasoning"].is_object()) return caps["reasoning"];
  const auto name = model.value("name", std::string());
  Json out = {{"mode", "off"}, {"levels", Json::object()}, {"summary", false}, {"disable", false}};
  for (const auto &family : {"claude-3-7-sonnet", "claude-sonnet-4", "claude-sonnet-4-5",
                             "claude-haiku-4-5", "claude-opus-4", "claude-opus-4-1", "claude-opus-4-5"})
    if (anthropicFamily(name, family)) {
      out = {{"mode", "budget"}, {"levels", {{"none", "disabled"}, {"medium", "4096"}, {"high", "8192"}}},
             {"summary", true}, {"disable", true}, {"display_supported", false}};
      return out;
    }
  for (const auto &family : {"claude-sonnet-4-6", "claude-opus-4-6", "claude-opus-4-7",
                             "claude-opus-4-8", "claude-opus-5", "claude-opus-5-5",
                             "claude-sonnet-5", "claude-sonnet-5-5", "claude-haiku-5-5",
                             "claude-fable-5", "claude-fable-5-1", "claude-mythos-5", "claude-mythos-5-1"})
    if (anthropicFamily(name, family)) {
      const bool modern = !anthropicFamily(name, "claude-sonnet-4-6") && !anthropicFamily(name, "claude-opus-4-6");
      const bool always = name.find("claude-fable-") == 0 || name.find("claude-mythos-") == 0 ||
                          anthropicFamily(name, "claude-opus-5-5");
      const bool between = anthropicFamily(name, "claude-sonnet-5-5");
      out = {{"mode", "adaptive"}, {"levels", {{"low", "low"}, {"medium", "medium"}, {"high", "high"}, {"max", "max"}}},
             {"summary", true}, {"disable", !always && !between}, {"display_supported", true}};
      if (modern) out["levels"]["xhigh"] = "xhigh";
      if (!always && !between) out["levels"]["none"] = "disabled";
      return out;
    }
  return out;
}
inline void validateAnthropic(const Json &model, const Json &reasoning) {
  const auto maximum = anthropicMaxOutput(model);
  const auto mode = reasoning.value("mode", std::string("off"));
  if (mode != "off" && mode != "adaptive" && mode != "budget") throw std::runtime_error("思考模式无效");
  const auto levels = reasoning.value("levels", Json::object());
  if (!levels.is_object()) throw std::runtime_error("思考档位无效");
  if (mode == "off" && !levels.empty()) throw std::runtime_error("请先选择思考模式，再配置档位");
  for (auto i = levels.begin(); i != levels.end(); ++i) {
    if (!i->is_string()) throw std::runtime_error("思考参数必须为文本");
    const auto value = i->get<std::string>();
    if (i.key() == "none") {
      if (!reasoning.value("disable", false) || value != "disabled")
        throw std::runtime_error("此模型未配置关闭思考能力，不能启用即时");
    } else if (mode == "budget") {
      size_t used = 0; long n = 0;
      try { n = std::stol(value, &used); } catch (...) { throw std::runtime_error("思考预算必须为整数"); }
      if (used != value.size() || n < 1024 || n >= maximum)
        throw std::runtime_error("每档思考预算须至少1024且小于最大输出 token，请修改模型设置");
    } else if (mode == "adaptive") {
      if (value != "low" && value != "medium" && value != "high" && value != "xhigh" && value != "max")
        throw std::runtime_error("自适应强度须为 low/medium/high/xhigh/max");
    }
  }
}
} // namespace penchat
