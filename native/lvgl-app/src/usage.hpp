#pragma once
#include "common.hpp"
namespace penchat {
// Whitelist provider-reported counts; missing is distinct from a reported zero.
Json normalizedUsage(const Json &usage);
// Background-only read model. Never returns text, credentials or opaque state.
Json sessionMetrics(Json snapshot, const Json &model, const Json &pending);
} // namespace penchat
