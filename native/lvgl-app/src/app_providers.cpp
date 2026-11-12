#include "api_config.hpp"
#include "app.hpp"
#include "chat_text.hpp"
#include "icons.hpp"
#include "reasoning.hpp"
#include <cctype>
namespace penchat {
namespace {
std::string protocolName(const Json &form) {
  const auto api = form.value("api", std::string("responses"));
  return api == "anthropic" ? "Anthropic Messages"
         : api == "chat"    ? "Chat Completions"
                            : "Responses";
}
std::vector<std::string> modelIds(std::string text) {
  std::replace(text.begin(), text.end(), '\n', ',');
  std::vector<std::string> values;
  size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find(',', start);
    auto value = text.substr(start, end == std::string::npos ? end : end - start);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
      value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
      value.pop_back();
    if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end())
      values.push_back(value);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return values;
}
std::string folded(std::string text) {
  for (auto &c : text)
    c = std::tolower(static_cast<unsigned char>(c));
  return text;
}
} // namespace
void App::providerPage() {
  const int width = layout().contentWidth();
  pageHeader(providerForm_.empty()          ? "模型管理"
             : providerForm_.contains("id") ? "编辑服务商"
                                            : "添加服务商",
             8);
  if (providerForm_.empty()) {
    pageAction("添加 Provider", 17, 168, [this] {
      providerForm_ = {{"name", "OpenAI"},
                       {"baseURL", "https://api.openai.com/v1"},
                       {"apiKey", ""},
                       {"api", "responses"}};
      providerAdvanced_ = false;
      modelNames_.clear();
      ++modelGeneration_;
      modelCancel_ = true;
      build();
    });
    int y = 5;
    const auto models = db_.models(), providers = db_.providers();
    const auto selected = currentModel().value("id", std::string());
    for (const auto &provider : providers) {
      const auto id = provider.at("id").get<std::string>();
      int count = 0;
      for (const auto &model : models)
        if (model.at("provider_id") == id)
          ++count;
      const bool narrow = width < 420;
      const int captionWidth = narrow ? width - 20 : width - 253, controlY = y + (narrow ? 52 : 4);
      auto caption = label(pageContent_, provider.value("name", std::string("服务商")), 10, y,
                           captionWidth, 21);
      lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_height(caption, font(21)->line_height);
      label(pageContent_, std::to_string(count) + " 个模型", 10, y + 27, captionWidth, 16,
            theme::muted);
      pageActions_["provider-add:" + id] =
          button(pageContent_, "添加模型", narrow ? 10 : width - 234, controlY,
                 narrow ? (width - 32) / 2 : 104, 44, [this, id] { openProviderModels(id); });
      pageActions_["provider-edit:" + id] =
          button(pageContent_, "连接设置", narrow ? 22 + (width - 32) / 2 : width - 118, controlY,
                 narrow ? (width - 32) / 2 : 108, 44, [this, id] { editProvider(id); });
      y += narrow ? 108 : 60;
      for (const auto &model : models) {
        if (model.at("provider_id") != id)
          continue;
        const auto modelId = model.at("id").get<std::string>();
        const auto name = model.value("display_name", model.value("name", std::string()));
        auto choose = button(
            pageContent_, std::string(modelId == selected ? "● " : "○ ") + name, 10, y,
            narrow ? width - 20 : width - 185, 48,
            [this, modelId] {
              chooseModel(modelId);
              build();
            },
            theme::surface, LV_TEXT_ALIGN_LEFT);
        pageActions_["model-use:" + modelId] = choose;
        pageActions_["model-delete:" + modelId] =
            icon(pageContent_, "trash", narrow ? width - 54 : width - 165, y + (narrow ? 54 : 2),
                 44, 44, [this, model] { removeModel(model); });
        pageActions_["model-edit:" + modelId] =
            button(pageContent_, "编辑", narrow ? 10 : width - 105, y + (narrow ? 54 : 2), 95, 44,
                   [this, model] { editModel(model); });
        y += narrow ? 106 : 56;
      }
      if (!count) {
        auto hint = readableLabel(pageContent_, "尚未添加模型，连接设置已保存。", 10, y, width - 20,
                                  18, theme::muted);
        lv_obj_update_layout(hint);
        y += lv_obj_get_height(hint) + 12;
      }
      y += 16;
    }
    if (!providers.empty())
      readableLabel(pageContent_, "点击模型名称，切换当前聊天并设为新聊天默认。", 10, y, width - 20,
                    16, theme::muted);
    if (providers.empty())
      emptyState("还没有服务商", "先保存服务地址与密钥，再选择或填写模型。可随时修改连接设置。");
    return;
  }
  primary(pageAction("保存", 24, 130, [this] { saveProvider(); }));
  const int column = (width - 30) / 2, right = 20 + column;
  auto field = [&](const std::string &key, const std::string &name, int x, int y, int span,
                   bool secret = false) {
    const auto value = providerForm_.value(key, std::string());
    auto object = fieldButton(pageContent_, name,
                              value.empty() ? "点击填写"
                              : secret      ? "••••••••"
                                            : value,
                              x, y, span, [this, key, name] {
                                edit(
                                    providerForm_.value(key, std::string()), name,
                                    [this, key](const std::string &text) {
                                      providerForm_[key] = text;
                                      build();
                                    },
                                    4096,
                                    key == "name"     ? "ZhCNPreferred"
                                    : key == "apiKey" ? "EnUSOnly"
                                                      : "EnUSPreferred",
                                    {}, false, key == "apiKey");
                              });
    pageActions_["field:" + key] = object;
  };
  field("name", "名称", 10, 4, column);
  auto api = button(
      pageContent_, "接口：" + protocolName(providerForm_), right, 58, column, 48,
      [this] { protocolPicker(); }, theme::surface, LV_TEXT_ALIGN_LEFT);
  pageActions_["provider-protocol"] = api;
  field("baseURL", "Base URL", right, 4, column);
  field("apiKey", "API Key", 10, 58, column, true);
  settingsRow("balance_vendor", "余额查询厂商",
              balanceVendorName(providerForm_.value("balance_vendor", std::string())),
              10, 122, width - 20, [this] { balanceVendorPicker(); });
  settingsRow("provider_advanced", "更多连接选项",
              providerAdvanced_ ? "已展开 · 返回此页后仍需保存"
                                : "默认推理强度、兼容模式与厂商适配",
              10, 186, width - 20, [this] {
                providerAdvanced_ = !providerAdvanced_;
                build();
              });
  int y = 250;
  if (providerAdvanced_) {
    settingsRow("provider_effort", "默认推理强度",
                effortLabel(providerForm_.value("default_effort", std::string())), 10, y,
                width - 20, [this] {
                  const auto current = providerForm_.value("default_effort", std::string());
                  modal("默认推理强度", "");
                  lv_obj_update_layout(modalScroll_);
                  const int span = lv_obj_get_width(modalScroll_) - 16;
                  int at = 0;
                  std::vector<std::string> values{""};
                  values.insert(values.end(), effortOrder().begin(), effortOrder().end());
                  for (const auto &value : values) {
                    button(
                        modalScroll_,
                        std::string(value == current ? "● " : "○ ") + effortLabel(value), 4, at,
                        span, 44,
                        [this, value] {
                          providerForm_["default_effort"] = value;
                          build();
                        },
                        theme::surface, LV_TEXT_ALIGN_LEFT);
                    at += 48;
                  }
                });
    y += 64;
    settingsRow("api_compatibility", "API 兼容模式",
                objectField(providerForm_, "compatibility").value("enabled", false)
                    ? "已启用 · 按需调整连接与扩展"
                    : "标准模式 · 不自动重发请求",
                10, y, width - 20, [this] {
                  providerScroll_ = lv_obj_get_scroll_y(body_);
                  show("api_compatibility");
                });
    y += 64;
    const bool anthropic = providerForm_.value("api", std::string()) == "anthropic";
    if (anthropic) {
      settingsRow("anthropic_connection", "Anthropic 连接设置", "鉴权、API 版本与自定义请求头", 10,
                  y, width - 20, [this] {
                    providerScroll_ = lv_obj_get_scroll_y(body_);
                    show("anthropic_connection");
                  });
      y += 64;
    }
    settingsRow("model_adapter", "厂商参数适配",
                providerForm_.value("model_adapter", std::string("auto")), 10, y, width - 20,
                [this] { modelAdapterPicker(); });
    y += 64;
    if (anthropic) {
      const auto maximum = providerForm_.value("max_output_tokens", 16384);
      settingsRow("provider_output", "新模型输出额度",
                  std::to_string(maximum) + " token · 仅用于之后添加的模型", 10, y, width - 20,
                  [this, maximum] {
                    edit(
                        std::to_string(maximum), "新模型输出额度（必填）",
                        [this](const std::string &text) {
                          try {
                            size_t used = 0;
                            const int n = std::stoi(text, &used);
                            if (used != text.size() || n < 1 || n > 1048576)
                              throw std::runtime_error("range");
                            providerForm_["max_output_tokens"] = n;
                            build();
                          } catch (...) {
                            modal("输出额度未修改", "请输入1～1048576之间的整数，不能留空。");
                          }
                        },
                        7, "EnUSOnly", {}, false);
                  });
      y += 64;
    }
  }
  readableLabel(pageContent_,
                providerForm_.contains("id") ? "保存连接设置不会添加、删除或重新选择模型。"
                                             : "可以先保存服务商，稍后再添加模型。",
                10, y, width - 20, 17, theme::muted);
}
void App::editProvider(const std::string &id) {
  ++modelGeneration_;
  modelCancel_ = true;
  providerForm_ = db_.providerForm(id);
  providerAdvanced_ = false;
  modelNames_.clear();
  notice_.clear();
  build();
}
void App::saveProvider() {
  const auto name = providerForm_.value("name", std::string()),
             base = providerForm_.value("baseURL", std::string()),
             key = providerForm_.value("apiKey", std::string());
  if (!hasChatText(name) || !hasChatText(base) || !hasChatText(key)) {
    modal("请检查连接配置", "请填写名称、Base URL 和 API Key。");
    return;
  }
  try {
    Json model = {{"base_url", base},
                  {"api_key", key},
                  {"headers_json", providerForm_.value("headers", Json::object())},
                  {"provider_metadata",
                   {{"api", providerForm_.value("api", std::string("responses"))},
                    {"anthropic", objectField(providerForm_, "anthropic")}}}};
    apiRoot(model);
    apiHeaders(model);
    const bool fresh = !providerForm_.contains("id");
    const auto id = db_.saveProvider(providerForm_, {}, false);
    ++modelGeneration_;
    modelCancel_ = true;
    providerForm_ = Json::object();
    if (fresh)
      openProviderModels(id);
    else
      show("provider");
  } catch (const std::exception &error) {
    modal("服务商未保存", error.what());
  }
}
void App::openProviderModels(const std::string &id) {
  ++modelGeneration_;
  modelCancel_ = true;
  providerModelsId_ = id;
  providerForm_ = db_.providerForm(id);
  providerManualModels_.clear();
  providerModelFilter_.clear();
  providerModelSelection_.clear();
  providerModelPage_ = 0;
  modelNames_.clear();
  show("provider_models");
}
void App::providerModelsPage() {
  pageHeader("添加模型");
  auto add = pageAction("添加选中", 24, 140, [this] { addProviderModels(); });
  primary(add);
  if (providerModelSelection_.empty())
    lv_obj_add_state(add, LV_STATE_DISABLED);
  const int left = layout().portrait() ? layout().safeSide() : 10,
            width = layout().contentWidth() - 2 * left;
  auto title = readableLabel(pageContent_,
                             providerForm_.value("name", std::string()) + " · " +
                                 std::to_string(providerModelSelection_.size()) + " 个待添加",
                             left, 4, width, 19, theme::muted);
  lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(title, font(19)->line_height);
  auto manual = fieldButton(
      pageContent_, "手动模型 ID",
      providerManualModels_.empty() ? "可填写多个，逗号或换行分隔" : providerManualModels_, left,
      36, width, [this] {
        edit(
            providerManualModels_, "填写模型 ID",
            [this](const std::string &value) {
              const auto previous = modelIds(providerManualModels_);
              const auto ids = modelIds(value);
              providerManualModels_ = value;
              for (const auto &id : previous)
                if (std::find(ids.begin(), ids.end(), id) == ids.end())
                  providerModelSelection_.erase(id);
              std::vector<std::string> existing;
              for (const auto &model : db_.models())
                if (model.at("provider_id") == providerModelsId_)
                  existing.push_back(model.at("name").get<std::string>());
              for (const auto &id : ids) {
                if (std::find(modelNames_.begin(), modelNames_.end(), id) == modelNames_.end())
                  modelNames_.push_back(id);
                if (std::find(existing.begin(), existing.end(), id) == existing.end())
                  providerModelSelection_.insert(id);
              }
              providerModelPage_ = 0;
              build();
            },
            4096, "EnUSPreferred", {}, true);
      });
  pageActions_["models-manual"] = manual;
  const int fetchWidth = std::min(180, width);
  auto fetch = button(pageContent_, modelsBusy_ ? "正在获取…" : "获取模型列表", left, 94,
                      fetchWidth, 48, [this] { fetchModels(); });
  pageActions_["models-fetch"] = fetch;
  if (modelsBusy_)
    lv_obj_add_state(fetch, LV_STATE_DISABLED);
  auto filter =
      fieldButton(pageContent_, "筛选",
                  providerModelFilter_.empty() ? "输入模型 ID 关键词" : providerModelFilter_,
                  left + fetchWidth + 12, 94, width - fetchWidth - 12, [this] {
                    edit(
                        providerModelFilter_, "筛选模型",
                        [this](const std::string &value) {
                          providerModelFilter_ = value;
                          providerModelPage_ = 0;
                          build();
                        },
                        256, "EnUSPreferred");
                  });
  pageActions_["models-filter"] = filter;
  auto status = readableLabel(
      pageContent_, notice_.empty() ? "获取列表后勾选要添加的模型，不会自动导入全部。" : notice_,
      left, 154, width, 17, theme::muted);
  lv_obj_update_layout(status);
  int y = 154 + lv_obj_get_height(status) + 12;
  std::vector<std::string> active;
  for (const auto &model : db_.models())
    if (model.at("provider_id") == providerModelsId_)
      active.push_back(model.at("name").get<std::string>());
  std::vector<std::string> names;
  const auto needle = folded(providerModelFilter_);
  for (const auto &name : modelNames_)
    if (needle.empty() || folded(name).find(needle) != std::string::npos)
      names.push_back(name);
  // Put genuinely new options before already-added entries in the short reader.
  std::stable_partition(names.begin(), names.end(), [&](const std::string &name) {
    return std::find(active.begin(), active.end(), name) == active.end();
  });
  constexpr size_t pageSize = 30;
  if (providerModelPage_ >= names.size())
    providerModelPage_ = names.empty() ? 0 : ((names.size() - 1) / pageSize) * pageSize;
  if (names.size() > pageSize) {
    readableLabel(pageContent_,
                  "第 " + std::to_string(providerModelPage_ / pageSize + 1) + " / " +
                      std::to_string((names.size() + pageSize - 1) / pageSize) + " 页 · " +
                      std::to_string(names.size()) + " 个匹配",
                  left, y, width, 17, theme::muted);
    y += 30;
    if (providerModelPage_)
      pageActions_["models-prev"] = button(pageContent_, "上一页", left, y, 120, 44, [this] {
        providerModelPage_ -= 30;
        build();
      });
    if (providerModelPage_ + pageSize < names.size())
      pageActions_["models-next"] = button(pageContent_, "下一页", left + 132, y, 120, 44, [this] {
        providerModelPage_ += 30;
        build();
      });
    y += 54;
  }
  for (size_t i = providerModelPage_; i < std::min(names.size(), providerModelPage_ + pageSize);
       ++i) {
    const auto name = names[i];
    const bool exists = std::find(active.begin(), active.end(), name) != active.end(),
               selected = providerModelSelection_.count(name);
    auto row = button(
        pageContent_, "", left, y, width, 56,
        [this, name, exists] {
          if (exists)
            return;
          if (providerModelSelection_.count(name))
            providerModelSelection_.erase(name);
          else
            providerModelSelection_.insert(name);
          build();
        },
        theme::surface);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_style_radius(row, 10, 0);
    auto caption = label(row, name, 12, 4, width - 68, 20, exists ? theme::muted : theme::text);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(caption, font(20)->line_height);
    lv_obj_set_clickable(caption, false);
    auto detail = label(row,
                        exists     ? "已添加"
                        : selected ? "已选择"
                                   : "点击选择",
                        12, 31, width - 68, 16, theme::muted);
    lv_obj_set_clickable(detail, false);
    auto mark = box(row, width - 42, 17, 24, 24, selected ? theme::accent : theme::surface);
    lv_obj_set_clickable(mark, false);
    lv_obj_set_scrollable(mark, false);
    lv_obj_set_style_radius(mark, 5, 0);
    lv_obj_set_style_border_width(mark, 1, 0);
    lv_obj_set_style_border_color(mark, lv_color_hex(theme::muted), 0);
    if (exists || selected)
      attachIcon(mark, "check", exists ? theme::muted : theme::text);
    pageActions_["model-option:" + name] = row;
    y += 64;
  }
  if (modelNames_.empty())
    readableLabel(pageContent_, "也可以直接手动填写模型 ID，无需服务商支持模型列表接口。", left, y,
                  width, 18, theme::muted);
  else if (names.empty())
    readableLabel(pageContent_, "没有匹配的模型，换个关键词或手动填写。", left, y, width, 18,
                  theme::muted);
}
void App::addProviderModels() {
  try {
    std::vector<std::string> names(providerModelSelection_.begin(), providerModelSelection_.end());
    db_.addProviderModels(providerModelsId_, names);
    ++modelGeneration_;
    modelCancel_ = true;
    providerForm_ = Json::object();
    providerModelsId_.clear();
    modelNames_.clear();
    providerModelSelection_.clear();
    show("provider");
  } catch (const std::exception &error) {
    modal("模型未添加", error.what());
  }
}
void App::fetchModels() {
  if (modelsBusy_.exchange(true))
    return;
  if (modelWorker_.joinable())
    modelWorker_.join();
  Json model = {
      {"provider_id",providerModelsId_},
      {"base_url", providerForm_.value("baseURL", std::string())},
      {"api_key", providerForm_.value("apiKey", std::string())},
      {"headers_json", providerForm_.value("headers", Json::object()).dump()},
      {"provider_metadata", Json({{"api", providerForm_.value("api", std::string("responses"))},
                                  {"credential_revision",providerForm_.value("credential_revision",std::string())},
                                  {"compatibility", objectField(providerForm_, "compatibility")},
                                  {"anthropic", objectField(providerForm_, "anthropic")}})
                                .dump()}};
  notice_ = "正在拉取模型…";
  build();
  modelCancel_ = false;
  const int generation = ++modelGeneration_;
  modelWorker_ = std::thread([this, model, generation] {
    Json data = {{"generation", generation}};
    try {
      data["body"] = fetchModelList(model, modelCancel_);
      data["status"] = 200;
    } catch (const std::exception &error) {
      data["error"] = redactApiError(error.what(), model);
      data["status"] = 0;
    }
    modelsBusy_ = false;
    events_.push("models", data);
  });
}
void App::providerModelsResult(const Event &event) {
  if (view_ != "provider_models")
    return;
  if (event.data.value("generation", -1) != modelGeneration_) {
    if (!modelsBusy_)
      build();
    return;
  }
  modelNames_.clear();
  const auto body = event.data.value("body", Json());
  if (event.data.value("status", 0) >= 200 && event.data.value("status", 0) < 300 &&
      body.is_object() && body.contains("data") && body.at("data").is_array()) {
    for (const auto &model : body.at("data"))
      if (model.is_object() && model.contains("id") && model.at("id").is_string())
        modelNames_.push_back(model.at("id").get<std::string>());
    notice_ = "已获取 " + std::to_string(modelNames_.size()) + " 个模型，请勾选需要的项。";
  } else
    notice_ = "获取失败：" + event.data.value("error", std::string("未返回有效的模型列表"));
  const auto manual = modelIds(providerManualModels_);
  modelNames_.insert(modelNames_.end(), manual.begin(), manual.end());
  modelNames_.insert(modelNames_.end(), providerModelSelection_.begin(),
                     providerModelSelection_.end());
  std::sort(modelNames_.begin(), modelNames_.end());
  modelNames_.erase(std::unique(modelNames_.begin(), modelNames_.end()), modelNames_.end());
  build();
}
void App::editModel(const Json &model) {
  modelForm_ = model;
  const auto saved = objectField(model, "capabilities_json");
  modelForm_["reasoning"] = saved.contains("reasoning") && saved.at("reasoning").is_object()
                                ? saved.at("reasoning")
                                : reasoningCapabilities(model);
  modelAdvanced_ = false;
  show("model");
}
int App::modelIdentityFields() {
  const int width = layout().contentWidth(), column = (width - 30) / 2;
  settingsRow("model_identity", "模型 ID", modelForm_.value("name", std::string()), 10, 0, column,
              [this] {
                edit(
                    modelForm_.value("name", std::string()), "模型 ID（用于 API 请求）",
                    [this](const std::string &value) {
                      if (!hasChatText(value) || value.find_first_of("\r\n") != std::string::npos) {
                        modal("模型 ID 未修改", "模型 ID 不能为空，也不能包含换行。");
                        return;
                      }
                      const auto old = modelForm_.value("name", std::string()),
                                 display = modelForm_.value("display_name", old);
                      modelForm_["name"] = value;
                      if (display == old)
                        modelForm_["display_name"] = value;
                      build();
                    },
                    512, "EnUSPreferred", {}, false);
              });
  settingsRow("model_display_name", "显示名称",
              modelForm_.value("display_name", modelForm_.value("name", std::string())),
              20 + column, 0, column, [this] {
                edit(
                    modelForm_.value("display_name", std::string()), "显示名称（不影响 API 请求）",
                    [this](const std::string &value) {
                      const auto display =
                          hasChatText(value) ? value : modelForm_.value("name", std::string());
                      modelForm_["display_name"] = display;
                      build();
                    },
                    256, "ZhCNPreferred", {}, false);
              });
  const std::string label = modelAdvanced_ ? "档位选择" : "参数映射";
  auto toggle = [this] {
    modelAdvanced_ = !modelAdvanced_;
    build();
  };
  if (layout().compact()) {
    pageActions_[label] = button(pageContent_, label, 10, 64, width - 20, 44, toggle,
                                 theme::surface, LV_TEXT_ALIGN_LEFT);
    return 116;
  }
  pageAction(label, 170, 140, toggle);
  return 64;
}
void App::removeModel(const Json &model) {
  const auto id = model.at("id").get<std::string>();
  modal("删除模型", model.value("display_name", model.value("name", std::string())) +
                        "\n\n只从可用模型列表移除，聊天记录保留。使用此模型的会话需要重新选择；之后"
                        "再次添加相同 ID 可恢复其原设置。");
  lv_obj_update_layout(modal_);
  const int width = lv_obj_get_width(modal_);
  auto cancel = lv_obj_get_child(modal_, -1);
  lv_obj_set_pos(cancel, width - 112, screen_.height - 66);
  lv_label_set_text(lv_obj_get_child(cancel, 0), "取消");
  auto confirm = button(modal_, "删除", 12, screen_.height - 66, 100, 44, [this, id] {
    try {
      db_.removeModel(id);
      build();
    } catch (const std::exception &error) {
      modal("模型未删除", error.what());
    }
  });
  lv_obj_set_style_bg_color(confirm, lv_color_hex(theme::danger), 0);
}
} // namespace penchat
