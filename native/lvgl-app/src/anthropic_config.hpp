#pragma once
#include "api_protocol.hpp"
#include "model_catalog.hpp"
#include <map>
namespace penchat {
inline int anthropicMaxOutput(const Json &model) {
  const auto caps = objectField(model, "capabilities_json");
  const auto value = caps.value("max_output_tokens", Json(16384));
  if (!value.is_number_integer()) throw std::runtime_error("最大输出 token 必须是正整数");
  const auto n = value.get<double>();
  if (n < 1 || n > 1048576)
    throw std::runtime_error("最大输出 token 超出模型上限，请修改模型设置");
  return int(n);
}
inline std::string anthropicBudget(const std::string &key) {
  static const std::map<std::string, std::string> values{
      {"none", "disabled"}, {"low", "1024"}, {"medium", "4096"},
      {"high", "8192"}, {"xhigh", "12288"}, {"max", "14336"}};
  const auto it = values.find(key);
  return it == values.end() ? std::string() : it->second;
}
inline Json anthropicReasoning(const Json &model) {
  return catalogReasoning(model);
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
