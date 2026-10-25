#pragma once
#include "reasoning.hpp"
#include <stdexcept>
namespace penchat {
struct ApiOptions {
  bool compatible = false, streaming = true, usage = true;
  bool summary = true, replay = true, http1 = false;
  int timeout = 600;
};
inline ApiOptions apiOptions(const Json &model) {
  ApiOptions out;
  const auto config = objectField(objectField(model, "provider_metadata"), "compatibility");
  if (config.contains("enabled") && config["enabled"].is_boolean())
    out.compatible = config["enabled"].get<bool>();
  if (config.contains("timeout") && config["timeout"].is_number_integer())
    out.timeout = int(std::max(30.0, std::min(3600.0, config["timeout"].get<double>())));
  if (out.compatible) {
    auto flag = [&](const char *key, bool fallback) {
      const auto it = config.find(key);
      return it != config.end() && it->is_boolean() ? it->get<bool>() : fallback;
    };
    out.streaming = flag("streaming", true); out.usage = flag("usage", true);
    out.summary = flag("summary", true); out.replay = flag("replay", true);
    out.http1 = flag("http1", false);
  }
  return out;
}
// Deliberately excludes credentials; changing an endpoint/model invalidates replay.
inline Json replayIdentity(const Json &model) {
  Json identity = {{"provider", model.value("provider_id", std::string())},
          {"model", model.value("name", std::string())},
          {"endpoint", model.value("base_url", std::string())},
          {"protocol", protocolName(apiProtocol(model))}};
  if (anthropicProtocol(model)) {
    identity["endpoint"] = protocolRoot(model);
    identity["version"] = anthropicVersion(model);
    identity["credential_revision"] = objectField(model, "provider_metadata").value("credential_revision", std::string());
  }
  return identity;
}
} // namespace penchat
