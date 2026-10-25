#pragma once
#include "common.hpp"
#include <stdexcept>
namespace penchat {
inline Json objectField(const Json &j, const char *key) {
  auto i = j.find(key);
  if (i == j.end()) return Json::object();
  auto value = i->is_string() ? Json::parse(i->get<std::string>(), nullptr, false) : *i;
  return value.is_object() ? value : Json::object();
}
enum class ApiProtocol { Responses, Chat, Anthropic };
inline ApiProtocol apiProtocol(const Json &model) {
  const auto api = objectField(model, "provider_metadata").value("api", std::string("responses"));
  if (api.empty() || api == "responses") return ApiProtocol::Responses;
  if (api == "chat") return ApiProtocol::Chat;
  if (api == "anthropic") return ApiProtocol::Anthropic;
  throw std::runtime_error("不支持的接口类型，请重新选择 Provider 接口");
}
inline std::string protocolName(ApiProtocol protocol) {
  return protocol == ApiProtocol::Chat ? "chat" :
         protocol == ApiProtocol::Anthropic ? "anthropic" : "responses";
}
inline bool chatProtocol(const Json &model) { return apiProtocol(model) == ApiProtocol::Chat; }
inline bool anthropicProtocol(const Json &model) { return apiProtocol(model) == ApiProtocol::Anthropic; }
inline Json anthropicConnection(const Json &model) {
  return objectField(objectField(model, "provider_metadata"), "anthropic");
}
inline std::string anthropicVersion(const Json &model) {
  return anthropicConnection(model).value("version", std::string("2023-06-01"));
}
inline std::string protocolRoot(const Json &model) {
  const auto protocol = apiProtocol(model);
  auto root = model.value("base_url", std::string());
  if (root.empty()) root = protocol == ApiProtocol::Anthropic ?
      "https://api.anthropic.com/v1" : "https://api.openai.com/v1";
  while (!root.empty() && root.back() == '/') root.pop_back();
  if (root.find_first_of("\r\n?#") != std::string::npos ||
      (root.find("https://") != 0 && root.find("http://") != 0))
    throw std::runtime_error("Base URL 需为 HTTP(S) API 地址，不含查询或片段");
  for (const auto &entry : {std::make_pair(std::string("/responses"), ApiProtocol::Responses),
                            std::make_pair(std::string("/chat/completions"), ApiProtocol::Chat),
                            std::make_pair(std::string("/messages"), ApiProtocol::Anthropic)}) {
    const auto &suffix = entry.first;
    if (root.size() >= suffix.size() && root.compare(root.size() - suffix.size(), suffix.size(), suffix) == 0) {
      if (protocol != entry.second) throw std::runtime_error("Base URL 后缀与所选接口不一致");
      root.resize(root.size() - suffix.size()); break;
    }
  }
  if (protocol == ApiProtocol::Anthropic && root == "https://api.anthropic.com") root += "/v1";
  return root;
}
} // namespace penchat
