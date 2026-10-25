#pragma once
#include "api_protocol.hpp"
namespace penchat {
// Immutable process-wide data; no credentials, network or database access.
class ModelCatalog {
  struct Impl;
  std::shared_ptr<const Impl> impl_;
public:
  ModelCatalog(const unsigned char *data, size_t size);
  static const ModelCatalog &instance();
  bool available() const { return bool(impl_); }
  Json match(const std::string &name) const;
};
std::string modelAdapter(const Json &model);
Json modelRecommendation(const Json &model);
Json catalogReasoning(const Json &model);
Json initialModelCapabilities(const Json &model);
void validateModelImages(const Json &model, const Json &input);
// Applies only explicitly selected settings. API default emits no thinking
// overrides. Adapter selection is frozen with the Provider at request start.
void applyModelParameters(Json &body, const Json &model);
} // namespace penchat
