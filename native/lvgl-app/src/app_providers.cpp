#include "api_config.hpp"
#include "app.hpp"
#include "chat_text.hpp"
#include "icons.hpp"
#include "reasoning.hpp"
#include "model_catalog.hpp"
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
             uiMetrics_.dp(8));
  if (providerForm_.empty()) {
    pageAction("添加 Provider",uiMetrics_.dp(17),uiMetrics_.dp(168),[this]{beginProvider();});
    const auto providers=db_.providers();
    int y=uiMetrics_.dp(4);
    for(const auto &provider:providers) {
      const auto id=provider.at("id").get<std::string>();
      const auto meta=objectField(provider,"metadata_json");
      settingsRow("provider-open:"+id,provider.value("name",std::string("Provider")),protocolName(meta),
                  uiMetrics_.dp(10),y,width-uiMetrics_.dp(20),[this,id]{editProvider(id);});
      pageActions_["provider-open:"+id]=settingsControls_.at("provider-open:"+id).row;
      y+=uiMetrics_.form().settingsPitch;
    }
    if(providers.empty())emptyState("还没有服务商","填写连接信息并添加模型，最后统一保存。");
    return;
  }
  primary(pageAction("保存", uiMetrics_.dp(24), uiMetrics_.dp(130), [this] { saveProvider(); }));
  const int column = (width - uiMetrics_.dp(30)) / 2, right = uiMetrics_.dp(20) + column;
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
  const int connectionHeight = uiMetrics_.identity()?uiMetrics_.dp(48):fieldMetrics(uiMetrics_).height;
  const int secondY = uiMetrics_.dp(4)+connectionHeight+uiMetrics_.dp(6);
  const int optionsY = secondY+connectionHeight+uiMetrics_.dp(16);
  field("name", "名称", uiMetrics_.dp(10), uiMetrics_.dp(4), column);
  auto api = button(
      pageContent_, "接口：" + protocolName(providerForm_), right, secondY, column, connectionHeight,
      [this] { protocolPicker(); }, theme::surface, LV_TEXT_ALIGN_LEFT);
  pageActions_["provider-protocol"] = api;
  field("baseURL", "Base URL", right, uiMetrics_.dp(4), column);
  field("apiKey", "API Key", uiMetrics_.dp(10), secondY, column, true);
  settingsRow("balance_vendor", "余额查询厂商",
              balanceVendorName(providerForm_.value("balance_vendor", std::string())),
              uiMetrics_.dp(10), optionsY, width - uiMetrics_.dp(20), [this] { balanceVendorPicker(); });
  settingsRow("provider_advanced", "更多连接选项",
              providerAdvanced_ ? "已展开 · 返回此页后仍需保存"
                                : "默认推理强度、兼容模式与厂商适配",
              uiMetrics_.dp(10), optionsY+uiMetrics_.form().settingsPitch, width - uiMetrics_.dp(20), [this] {
                providerAdvanced_ = !providerAdvanced_;
                build();
              });
  int y = optionsY+2*uiMetrics_.form().settingsPitch;
  if (providerAdvanced_) {
    settingsRow("provider_effort", "默认推理强度",
                effortLabel(providerForm_.value("default_effort", std::string())), uiMetrics_.dp(10), y,
                width - uiMetrics_.dp(20), [this] {
                  const auto current = providerForm_.value("default_effort", std::string());
                  std::vector<std::pair<std::string,std::string>> options;
                  options.push_back({"",effortLabel("")});
                  for(const auto &key:effortOrder())options.push_back({key,effortLabel(key)});
                  choiceMenu("provider_effort",options,current,[this](const std::string &key) {
                    providerForm_["default_effort"]=key;build();
                  });
                });
    y += uiMetrics_.form().settingsPitch;
    settingsRow("api_compatibility", "API 兼容模式",
                objectField(providerForm_, "compatibility").value("enabled", false)
                    ? "已启用 · 按需调整连接与扩展"
                    : "标准模式 · 不自动重发请求",
                uiMetrics_.dp(10), y, width - uiMetrics_.dp(20), [this] {
                  providerScroll_ = lv_obj_get_scroll_y(body_);
                  show("api_compatibility");
                });
    y += uiMetrics_.form().settingsPitch;
    const bool anthropic = providerForm_.value("api", std::string()) == "anthropic";
    if (anthropic) {
      settingsRow("anthropic_connection", "Anthropic 连接设置", "鉴权、API 版本与自定义请求头", uiMetrics_.dp(10),
                  y, width - uiMetrics_.dp(20), [this] {
                    providerScroll_ = lv_obj_get_scroll_y(body_);
                    show("anthropic_connection");
                  });
      y += uiMetrics_.form().settingsPitch;
    }
    settingsRow("model_adapter", "厂商参数适配",
                providerForm_.value("model_adapter", std::string("auto")), uiMetrics_.dp(10), y, width - uiMetrics_.dp(20),
                [this] { modelAdapterPicker(); });
    y += uiMetrics_.form().settingsPitch;
    if (anthropic) {
      const auto maximum = providerForm_.value("max_output_tokens", 16384);
      settingsRow("provider_output", "新模型输出额度",
                  std::to_string(maximum) + " token · 仅用于之后添加的模型", uiMetrics_.dp(10), y, width - uiMetrics_.dp(20),
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
      y += uiMetrics_.form().settingsPitch;
    }
  }
  auto hint=readableLabel(pageContent_,"连接与模型修改均在点击本页保存后生效。",uiMetrics_.dp(10),y,width-uiMetrics_.dp(20),uiMetrics_.fontSize(UiFont::FormHint),theme::muted);
  lv_obj_update_layout(hint);
  providerModelsSection(y+lv_obj_get_height(hint)+uiMetrics_.dp(16),width);
}
bool App::providerDirty() const {
  return !providerForm_.empty() && (providerForm_!=providerOriginal_.value("form",Json::object()) ||
    providerDraftModels_!=providerOriginal_.value("models",Json::array()));
}
void App::beginProvider(const std::string &id) {
  ++modelGeneration_;modelCancel_=true;
  providerWorkspaceId_=uid("provider-workspace");
  providerDraftModels_=Json::array();
  providerForm_=id.empty()?Json({{"name","OpenAI"},{"baseURL","https://api.openai.com/v1"},{"apiKey",""},{"api","responses"}}):db_.providerForm(id);
  if(!id.empty())for(const auto &model:db_.models())if(model.at("provider_id")==id)providerDraftModels_.push_back(model);
  providerOriginal_={{"form",providerForm_},{"models",providerDraftModels_}};
  providerAdvanced_=false;providerScroll_=0;modelNames_.clear();notice_.clear();
  build();lv_obj_scroll_to_y(body_,0,LV_ANIM_OFF);
}
void App::editProvider(const std::string &id) { beginProvider(id); }
void App::leaveProvider() {
  providerForm_=Json::object();providerDraftModels_=Json::array();providerOriginal_=Json::object();
  providerWorkspaceId_.clear();modelNames_.clear();providerModelsId_.clear();
  ++modelGeneration_;modelCancel_=true;build();
}
void App::returnProvider() {
  show("provider");lv_obj_scroll_to_y(body_,providerScroll_,LV_ANIM_OFF);
}
Json App::draftModel(const Json &source) const {
  auto model=source;
  model["base_url"]=providerForm_.value("baseURL",std::string());
  model["provider_metadata"]={{"api",providerForm_.value("api",std::string("responses"))},
    {"model_adapter",providerForm_.value("model_adapter",std::string("auto"))}};
  return model;
}
void App::providerModelsSection(int y,int width) {
  auto list=box(pageContent_,uiMetrics_.dp(10),y,width,LV_SIZE_CONTENT);
  lv_obj_set_scrollable(list,false);
  auto title=label(list,"模型",uiMetrics_.dp(10),uiMetrics_.dp(10),width-uiMetrics_.dp(180),uiMetrics_.fontSize(UiFont::SettingsTitle));
  lv_obj_set_height(title,font(uiMetrics_.fontSize(UiFont::SettingsTitle))->line_height);
  pageActions_["provider-add-models"]=button(list,"添加模型",width-uiMetrics_.dp(160),0,uiMetrics_.dp(140),uiMetrics_.dp(44),[this]{openProviderModels(providerForm_.value("id",providerWorkspaceId_));});
  int top=uiMetrics_.dp(56);
  for(const auto &model:providerDraftModels_) {
    const auto id=model.at("id").get<std::string>();const bool deleted=model.value("workspace_deleted",false);
    const int nameWidth=width-uiMetrics_.dp(80);
    auto edit=button(list,model.value("display_name",model.value("name",std::string())),uiMetrics_.dp(10),top,nameWidth,uiMetrics_.dp(48),
      [this,model]{editModel(model);},theme::surface,LV_TEXT_ALIGN_LEFT);
    pageActions_["model-edit:"+id]=edit;if(deleted)lv_obj_add_state(edit,LV_STATE_DISABLED);
    lv_obj_t *remove;
    if(deleted)remove=icon(list,"retry",width-uiMetrics_.dp(64),top+2,uiMetrics_.dp(44),uiMetrics_.dp(44),[this,id]{for(auto &m:providerDraftModels_)if(m.at("id")==id)m["workspace_deleted"]=false;build();});
    else remove=icon(list,"trash",width-uiMetrics_.dp(64),top+2,uiMetrics_.dp(44),uiMetrics_.dp(44),[this,model]{removeModel(model);});
    pageActions_["model-delete:"+id]=remove;
    const auto state=deleted?"待删除 · 点击回转箭头撤销":model.value("workspace_new",false)?"新增待保存":"模型配置随本页统一保存";
    auto status=label(list,state,uiMetrics_.dp(12),top+uiMetrics_.dp(50),nameWidth-uiMetrics_.dp(12),uiMetrics_.fontSize(UiFont::Detail),deleted?theme::danger:theme::muted);
    lv_label_set_long_mode(status,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_height(status,font(uiMetrics_.fontSize(UiFont::Detail))->line_height);
    top+=uiMetrics_.dp(80);
  }
  if(providerDraftModels_.empty()) {
    auto hint=label(list,"尚未添加模型。可先获取列表或手动填写，最后统一保存。",uiMetrics_.dp(10),top,width-uiMetrics_.dp(20),uiMetrics_.fontSize(UiFont::FormHint),theme::muted);
    lv_obj_update_layout(hint);top+=lv_obj_get_height(hint)+uiMetrics_.dp(16);
  }
  auto save=button(list,"保存",uiMetrics_.dp(10),top,width-uiMetrics_.dp(20),uiMetrics_.dp(48),[this]{saveProvider();});primary(save);
  pageActions_["provider-save-bottom"]=save;
}
void App::saveProvider(bool returnToList) {
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
    const int scroll=lv_obj_get_scroll_y(body_);const bool expanded=providerAdvanced_;
    const auto result=db_.saveProviderWorkspace(providerForm_,providerDraftModels_);
    if(returnToList){leaveProvider();return;}
    editProvider(result.at("provider_id"));
    if(expanded){providerAdvanced_=true;build();}
    lv_obj_scroll_to_y(body_,scroll,LV_ANIM_OFF);
  } catch (const std::exception &error) {
    modal("服务商未保存", error.what());
  }
}
void App::openProviderModels(const std::string &id) {
  providerScroll_=lv_obj_get_scroll_y(body_);
  ++modelGeneration_;modelCancel_=true;providerModelsId_=id;
  providerManualModels_.clear();providerModelFilter_.clear();providerModelSelection_.clear();providerModelPage_=0;
  modelNames_.clear();show("provider_models");
}
void App::providerModelsPage() {
  pageHeader("添加模型");
  auto add = pageAction("加入草稿", uiMetrics_.dp(24), uiMetrics_.dp(140), [this] { addProviderModels(); });
  primary(add);
  if (providerModelSelection_.empty())
    lv_obj_add_state(add, LV_STATE_DISABLED);
  const int left = layout().portrait() ? layout().safeSide() : uiMetrics_.dp(10),
            width = layout().contentWidth() - 2 * left;
  auto title = readableLabel(pageContent_,
                             providerForm_.value("name", std::string()) + " · " +
                                 std::to_string(providerModelSelection_.size()) + " 个待添加",
                             left, uiMetrics_.dp(4), width, uiMetrics_.fontSize(UiFont::FormCaption), theme::muted);
  lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(title, font(uiMetrics_.fontSize(UiFont::FormCaption))->line_height);
  auto manual = fieldButton(
      pageContent_, "手动模型 ID",
      providerManualModels_.empty() ? "可填写多个，逗号或换行分隔" : providerManualModels_, left,
      uiMetrics_.dp(36), width, [this] {
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
              for (const auto &model : providerDraftModels_)
                if (!model.value("workspace_deleted",false))
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
  const int fieldHeight = fieldMetrics(uiMetrics_).height;
  const int fetchY = uiMetrics_.identity()?uiMetrics_.dp(94):uiMetrics_.dp(36)+fieldHeight+uiMetrics_.dp(10);
  const int statusY = uiMetrics_.identity()?uiMetrics_.dp(154):fetchY+fieldHeight+uiMetrics_.dp(12);
  const int fetchWidth = std::min(uiMetrics_.dp(180), width);
  auto fetch = button(pageContent_, modelsBusy_ ? "正在获取…" : "获取模型列表", left, fetchY,
                      fetchWidth, uiMetrics_.identity()?uiMetrics_.dp(48):fieldHeight, [this] { fetchModels(); });
  pageActions_["models-fetch"] = fetch;
  if (modelsBusy_)
    lv_obj_add_state(fetch, LV_STATE_DISABLED);
  auto filter =
      fieldButton(pageContent_, "筛选",
                  providerModelFilter_.empty() ? "输入模型 ID 关键词" : providerModelFilter_,
                  left + fetchWidth + uiMetrics_.dp(12), fetchY, width - fetchWidth - uiMetrics_.dp(12), [this] {
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
      left, statusY, width, uiMetrics_.fontSize(UiFont::FormHint), theme::muted);
  lv_obj_update_layout(status);
  int y = statusY + lv_obj_get_height(status) + uiMetrics_.dp(12);
  std::vector<std::string> active;
  for (const auto &model : providerDraftModels_)
    if (!model.value("workspace_deleted",false))
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
                  left, y, width, uiMetrics_.fontSize(UiFont::FormHint), theme::muted);
    y += uiMetrics_.dp(30);
    if (providerModelPage_)
      pageActions_["models-prev"] = button(pageContent_, "上一页", left, y, uiMetrics_.dp(120), uiMetrics_.dp(44), [this] {
        providerModelPage_ -= 30;
        build();
      });
    if (providerModelPage_ + pageSize < names.size())
      pageActions_["models-next"] = button(pageContent_, "下一页", left + uiMetrics_.dp(132), y, uiMetrics_.dp(120), uiMetrics_.dp(44), [this] {
        providerModelPage_ += 30;
        build();
      });
    y += uiMetrics_.dp(54);
  }
  for (size_t i = providerModelPage_; i < std::min(names.size(), providerModelPage_ + pageSize);
       ++i) {
    const auto name = names[i];
    const bool exists = std::find(active.begin(), active.end(), name) != active.end(),
               selected = providerModelSelection_.count(name);
    auto row = button(
        pageContent_, "", left, y, width, uiMetrics_.dp(56),
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
    lv_obj_set_style_radius(row, uiMetrics_.dp(10), 0);
    auto caption = label(row, name, uiMetrics_.dp(12), uiMetrics_.dp(4), width - uiMetrics_.dp(68), uiMetrics_.fontSize(UiFont::SettingsTitle), exists ? theme::muted : theme::text);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(caption, font(uiMetrics_.fontSize(UiFont::SettingsTitle))->line_height);
    lv_obj_set_clickable(caption, false);
    auto detail = label(row,
                        exists     ? "已添加"
                        : selected ? "已选择"
                                   : "点击选择",
                        uiMetrics_.dp(12), uiMetrics_.dp(31), width - uiMetrics_.dp(68), uiMetrics_.fontSize(UiFont::Detail), theme::muted);
    lv_obj_set_clickable(detail, false);
    auto mark = box(row, width - uiMetrics_.dp(42), uiMetrics_.dp(17), uiMetrics_.dp(24), uiMetrics_.dp(24), selected ? theme::accent : theme::surface);
    lv_obj_set_clickable(mark, false);
    lv_obj_set_scrollable(mark, false);
    lv_obj_set_style_radius(mark, uiMetrics_.dp(5), 0);
    lv_obj_set_style_border_width(mark, uiMetrics_.dp(1), 0);
    lv_obj_set_style_border_color(mark, lv_color_hex(theme::muted), 0);
    if (exists || selected)
      attachIcon(mark, "check", exists ? theme::muted : theme::text);
    pageActions_["model-option:" + name] = row;
    y += uiMetrics_.form().settingsPitch;
  }
  if (modelNames_.empty())
    readableLabel(pageContent_, "也可以直接手动填写模型 ID，无需服务商支持模型列表接口。", left, y,
                  width, uiMetrics_.fontSize(UiFont::FormNote), theme::muted);
  else if (names.empty())
    readableLabel(pageContent_, "没有匹配的模型，换个关键词或手动填写。", left, y, width, uiMetrics_.fontSize(UiFont::FormNote),
                  theme::muted);
}
void App::addProviderModels() {
  try {
    for(const auto &name:providerModelSelection_) {
      bool found=false;
      for(auto &model:providerDraftModels_)if(model.at("name")==name){model["workspace_deleted"]=false;found=true;break;}
      if(found)continue;
      Json model;
      const auto provider=providerForm_.value("id",std::string());
      if(!provider.empty()) {
        const auto old=db_.query("SELECT * FROM model_configs WHERE provider_id=? AND name=? AND enabled=0",{provider,name});
        if(!old.empty())model=old[0];
      }
      if(model.is_null()) {
        model={{"id",uid("model")},{"provider_id",provider.empty()?providerWorkspaceId_:provider},{"name",name},{"display_name",name},{"workspace_new",true}};
        auto caps=initialModelCapabilities(draftModel(model));
        if(providerForm_.value("api",std::string())=="anthropic")caps["max_output_tokens"]=providerForm_.value("max_output_tokens",16384);
        model["capabilities_json"]=caps;
      }
      model["workspace_deleted"]=false;providerDraftModels_.push_back(model);
    }
    ++modelGeneration_;modelCancel_=true;modelNames_.clear();providerModelSelection_.clear();
    returnProvider();
  } catch(const std::exception &e){modal("模型未加入草稿",e.what());}
}
void App::fetchModels() {
  if(!hasChatText(providerForm_.value("baseURL",std::string())) || !hasChatText(providerForm_.value("apiKey",std::string()))) {
    modal("请填写连接信息","填写服务地址与 API Key 后即可获取模型，无需先保存。");return;
  }
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
  providerFetchIdentity_=providerForm_.dump();
  notice_ = "正在拉取模型…";
  build();
  modelCancel_ = false;
  const int generation = ++modelGeneration_;
  const auto workspace=providerWorkspaceId_;
  modelWorker_ = std::thread([this, model, generation, workspace] {
    Json data = {{"generation", generation},{"workspace",workspace}};
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
  if (view_ != "provider_models" || event.data.value("workspace",std::string())!=providerWorkspaceId_ ||
      providerFetchIdentity_!=providerForm_.dump())return;
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
  providerScroll_=lv_obj_get_scroll_y(body_);
  modelForm_ = draftModel(model);
  const auto saved = objectField(model, "capabilities_json");
  modelForm_["reasoning"] = saved.contains("reasoning") && saved.at("reasoning").is_object()
                                ? saved.at("reasoning")
                                : reasoningCapabilities(modelForm_);
  modelAdvanced_ = false;modelEditOriginal_=modelForm_;
  show("model");
}
int App::modelIdentityFields() {
  const int width = layout().contentWidth(), column = (width - uiMetrics_.dp(30)) / 2;
  settingsRow("model_identity", "模型 ID", modelForm_.value("name", std::string()), uiMetrics_.dp(10), 0, column,
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
              uiMetrics_.dp(20) + column, 0, column, [this] {
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
  const auto geometry = layout();
  const bool crowdedHeader = !uiMetrics_.identity() && geometry.portrait() &&
      geometry.bodyWidth()-uiMetrics_.dp(170)-uiMetrics_.dp(140)-geometry.navigationX() <
          geometry.navigationX()+uiMetrics_.page().navigationGap+uiMetrics_.page().minimumTitle;
  if (geometry.compact() || crowdedHeader) {
    pageActions_[label] = button(pageContent_, label, uiMetrics_.dp(10), uiMetrics_.dp(64), width - uiMetrics_.dp(20), uiMetrics_.dp(44), toggle,
                                 theme::surface, LV_TEXT_ALIGN_LEFT);
    return uiMetrics_.dp(116);
  }
  pageAction(label, uiMetrics_.dp(170), uiMetrics_.dp(140), toggle);
  return uiMetrics_.dp(64);
}
void App::removeModel(const Json &model) {
  const auto id=model.at("id").get<std::string>();
  choiceMenu("model_delete",{{"cancel","取消"},{"delete","标记删除，保存后生效"}},"cancel",[this,id](const std::string &key){
    if(key!="delete")return;
    for(auto &m:providerDraftModels_)if(m.at("id")==id)m["workspace_deleted"]=true;
    build();
  });
}
} // namespace penchat
