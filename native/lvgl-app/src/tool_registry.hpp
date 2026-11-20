#pragma once
#include "common.hpp"
#include <map>
#include <stdexcept>
namespace penchat {
// Protocol-neutral registration boundary. Future executors can return typed
// public data/assets without entering any API parser or LVGL callback.
class ToolRegistry {
public:
  using Executor = std::function<Json(const Json &)>;

private:
  struct Entry {
    Json definition;
    Executor execute;
  };
  std::map<std::string, Entry> entries_;

public:
  void add(Json definition, Executor execute) {
    const auto name = definition.at("name").get<std::string>();
    if (name.empty() || !execute ||
        !entries_.emplace(name, Entry{std::move(definition), std::move(execute)}).second)
      throw std::runtime_error("Duplicate or invalid tool registration");
  }
  Json definitions() const {
    Json out = Json::array();
    for (const auto &entry : entries_)
      out.push_back(entry.second.definition);
    return out;
  }
  Json execute(const std::string &name, const Json &arguments) const {
    const auto entry = entries_.find(name);
    if (entry == entries_.end())
      throw std::runtime_error("未注册的工具");
    if (!arguments.is_object() || arguments.dump().size() > 16384)
      throw std::runtime_error("工具参数无效或过大");
    const auto &schema = entry->second.definition.at("parameters");
    for (const auto &key : schema.at("required"))
      if (!arguments.contains(key.get<std::string>()))
        throw std::runtime_error("工具缺少必填参数");
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
      const auto &properties = schema.at("properties");
      if (!properties.contains(it.key()))
        throw std::runtime_error("工具包含未知参数");
      const auto &property = properties.at(it.key());
      const auto type = property.at("type").get<std::string>();
      if ((type == "string" &&
           (!it->is_string() || it->get_ref<const std::string &>().size() >
                                    size_t(property.value("maxLength", 4096)))) ||
          (type == "integer" &&
           (!it->is_number_integer() || it->get<double>() < property.value("minimum", 0.0) ||
            it->get<double>() > 1e12)) ||
          (type == "boolean" && !it->is_boolean()))
        throw std::runtime_error("工具参数类型或范围无效");
    }
    return entry->second.execute(arguments);
  }
};
} // namespace penchat
