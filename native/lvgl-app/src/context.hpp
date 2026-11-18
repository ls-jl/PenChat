#pragma once
#include "database.hpp"
namespace penchat {
// These are local conservative estimates, never provider billing/tokenizer counts.
int contextBudget(const Json &model);
int compressionPercent(const Json &settings);
size_t compressionTrigger(const Json &model);
Json chatContextModel(Json model);
size_t contextInputLimit(const Json &model);
Json publicToolMaterial(Json wire);
Json checkpointPrefix(const Json &checkpoint);
Json activeContextCheckpoint(const Json &rows, Json checkpoint);
Json effectiveContext(const Json &rows, Json checkpoint, const Json &model = Json::object());
std::function<Json(const Json &)> toolContextCompactor(
    Json snapshot, Json compressionModel, Json chatModel, std::atomic<bool> &cancel);
Json compactWindow(const Json &rows, const Json &units, Json old, Json model,
                   const Json &pending, const Json &chatModel, std::atomic<bool> &cancel,
                   std::function<void(int, int)> progress, const Json &originalInput = Json());
void checkContextResources(const Json &input);
size_t estimatedText(const std::string &text);
size_t modelContextEstimate(const Json &input, const Json &model, bool countImages = true);
size_t estimatedContext(const Json &input, const std::string &prompt = "", bool countImages = true);
std::string contextChecksum(const Json &rows, int64_t through);
Json contextInput(const Json &rows, int64_t after, int64_t through,
                  const Json &model = Json::object(), bool states = false);
bool validContext(const Json &checkpoint, const Json &rows);
Json publicContext(const Json &checkpoint);
// Called only by a background worker. No database mutation until every batch succeeds.
Json compressContext(Database &db, const Json &snapshot, Json model,
                     const Json &pending, const Json &chatModel, bool manual,
                     std::atomic<bool> &cancel, std::function<void(int, int)> progress);
} // namespace penchat
