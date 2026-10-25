#pragma once
#include "anthropic_config.hpp"
#include "model_catalog.hpp"
#include <map>
namespace penchat {
inline const std::vector<std::string> &effortOrder() {
  static const std::vector<std::string> values{"none", "minimal", "low",   "medium",
                                               "high", "xhigh", "max"};
  return values;
}
inline std::string effortLabel(const std::string &value) {
  static const std::map<std::string, std::string> labels{
      {"minimal", "极低"}, {"api_default", "默认"}, {"none", "即时"}, {"low", "低"},
      {"medium", "中"},        {"high", "高"},   {"xhigh", "极高"},
      {"max", "最高"}};
  auto i = labels.find(value);
  return i == labels.end() ? "API 默认" : i->second;
}
inline Json reasoningCapabilities(const Json &model) {
  if (anthropicProtocol(model)) return anthropicReasoning(model);
  return catalogReasoning(model);
}
inline std::vector<std::string> supportedEfforts(const Json &model) {
  auto levels = reasoningCapabilities(model).value("levels", Json::object());
  std::vector<std::string> out;
  for (const auto &key : effortOrder())
    if (levels.contains(key) && levels[key].is_string() &&
        !levels[key].get<std::string>().empty())
      out.push_back(key);
  return out;
}
inline std::vector<std::string> effortChoices(const Json &model) {
  auto options = supportedEfforts(model);
  auto position = std::find(options.begin(), options.end(), "none");
  options.insert(position == options.end() ? options.begin() : position + 1,
                 "api_default");
  return options;
}
inline Json resolveReasoning(Json model, const Json &preferences,
                             const std::string &memory) {
  auto caps = reasoningCapabilities(model);
  auto levels = caps.value("levels", Json::object());
  auto provider = objectField(model, "provider_metadata");
  model["effective_effort"] = "";
  model["effort_value"] = "";
  for (const auto &key : {preferences.value("effort", std::string()), memory,
                          provider.value("default_effort", std::string())}) {
    if (key == "api_default") {
      model["effective_effort"] = key;
      break;
    }
    if (levels.contains(key) && levels[key].is_string() &&
        !levels[key].get<std::string>().empty()) {
      model["effective_effort"] = key;
      model["effort_value"] = levels[key];
      break;
    }
  }
  model["reasoning_summary"] = caps.value("summary", false);
  return model;
}
} // namespace penchat
