#pragma once
#include "reasoning.hpp"
namespace penchat {
inline Json defaultPreset() {
  return {{"id", "default"}, {"name", "默认预设"}, {"prompt_mode", "inherit"},
          {"prompt", ""}, {"model_id", ""}, {"model_when", "empty"},
          {"effort", ""}, {"effort_when", "empty"}};
}
inline Json presetList(const Json &settings) {
  Json out = Json::array({defaultPreset()});
  auto saved = settings.value("chat_presets", Json::array());
  if (!saved.is_array()) return out;
  for (auto p : saved) {
    if (!p.is_object() || !p.contains("id") || !p["id"].is_string()) continue;
    if (p["id"] == "default") { p["name"] = "默认预设"; out[0] = p; }
    else out.push_back(p);
  }
  return out;
}
inline bool presetSupports(const Json &model, const std::string &effort) {
  if (effort.empty() || effort == "api_default") return true;
  auto choices = supportedEfforts(model);
  return std::find(choices.begin(), choices.end(), effort) != choices.end();
}
// No global preferences are changed here. Only the returned session snapshot
// is persisted, and only after that session exists.
inline Json applyPresetSnapshot(Json preferences, const Json &preset,
                                const Json &models, const Json &fallback,
                                bool empty, std::string &warning) {
  auto model = fallback;
  for (const auto &m : models)
    if (m.value("id", std::string()) == preferences.value("model_id", std::string())) model = m;
  auto id = preset.value("model_id", std::string());
  if (!id.empty() && (empty || preset.value("model_when", std::string("empty")) == "always")) {
    auto found = std::find_if(models.begin(), models.end(), [&](const Json &m) { return m.value("id", std::string()) == id; });
    if (found != models.end()) { model = *found; preferences["model_id"] = id; }
    else warning = "预设模型不可用，已保留会话模型。请编辑预设重新选择。";
  }
  auto effort = preset.value("effort", std::string());
  if (!effort.empty() && (empty || preset.value("effort_when", std::string("empty")) == "always")) {
    if (presetSupports(model, effort)) { preferences["effort"] = effort; preferences["gauge"] = true; }
    else warning += "当前模型不支持预设强度，已保留会话强度。";
  }
  auto mode = preset.value("prompt_mode", std::string("inherit"));
  if (mode == "custom") preferences["system_prompt"] = preset.value("prompt", std::string());
  else if (mode == "clear") preferences.erase("system_prompt");
  preferences["preset_id"] = preset.value("id", std::string("default"));
  preferences["preset_name"] = preset.value("name", std::string("默认预设"));
  preferences["preset_adjusted"] = false;
  return preferences;
}
} // namespace penchat
