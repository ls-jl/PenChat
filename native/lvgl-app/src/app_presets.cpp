#include "app.hpp"
#include "empty_state.hpp"
#include "attachment_layout.hpp"
#include "menu_action.hpp"
#include "ui_motion.hpp"
namespace penchat {
static bool presetHasText(const std::string &text) {
  return text.find_first_not_of(" \t\r\n") != std::string::npos;
}
bool App::emptyForPreset() {
  return !sessionPreferences_.value("has_sent", false) &&
         db_.query("SELECT id FROM chat_messages WHERE session_id=? AND role='assistant' LIMIT 1", {session_}).empty();
}
std::string App::presetSummary(const Json &p) {
  auto id = p.value("model_id", std::string());
  std::string model = id.empty() ? "沿用模型" : "模型不可用";
  for (const auto &m : db_.models()) if (m["id"] == id) model = m.value("display_name",m.value("name", std::string()));
  auto effort = p.value("effort", std::string());
  auto mode = p.value("prompt_mode", std::string("inherit"));
  return model + " · " + (effort.empty() ? "沿用强度" : effort == "api_default" ? "API 默认" : effortLabel(effort)) +
         " · " + (mode == "custom" ? "自定义提示词" : mode == "clear" ? "清空提示词" : "沿用提示词");
}
void App::applyPreset(const Json &preset) {
  std::string warning;
  sessionPreferences_ = applyPresetSnapshot(sessionPreferences_, preset, db_.models(),
                                            currentModel(), emptyForPreset(), warning);
  savePreferences();
  build();
  if (!warning.empty()) modal("预设已部分应用", warning);
}
void App::presetPicker() {
  const int inheritedWidth=attachmentPanel_?lv_obj_get_width(attachmentPanel_):uiMetrics_.dp(240);
  closeAttachments();resetPull();
  if(!chatControls_)return;
  lv_obj_update_layout(chatControls_);
  lv_area_t capsule;lv_obj_get_coords(chatControls_,&capsule);
  const auto presets=presetList(db_.settings());
  int naturalWidth=inheritedWidth;
  for(const auto &preset:presets) {
    lv_point_t measured{};
    lv_text_get_size(&measured,preset.value("name",std::string()).c_str(),
        font(uiMetrics_.fontSize(UiFont::SettingsTitle)),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    naturalWidth=std::max(naturalWidth,int(measured.x)+uiMetrics_.dp(76));
  }
  const int rowHeight=std::max(uiMetrics_.touchTarget(),int(font(uiMetrics_.fontSize(UiFont::SettingsTitle))->line_height)+uiMetrics_.dp(8));
  auto area=attachmentMenuArea(layout(),{capsule.x1,capsule.y1,capsule.x2-capsule.x1+1,capsule.y2-capsule.y1+1},
      naturalWidth,int(presets.size())*rowHeight+uiMetrics_.dp(8));
  fitAttachmentEditor(area);
  attachments_=box(lv_screen_active(),0,0,screen_.width,screen_.height);
  popupKind_="presets";
  lv_obj_set_scrollable(attachments_,false);
  lv_obj_set_style_bg_color(attachments_,lv_color_hex(theme::palette().scrim),0);
  lv_obj_set_style_bg_opa(attachments_,LV_OPA_50,0);
  menuAction(attachments_,[this]{closeAttachments();});
  auto panel=box(attachments_,area.x,area.y,area.width,area.height,theme::palette().surface);
  attachmentPanel_=panel;stylePopupPanel(panel,layout().portrait(),uiMetrics_);
  lv_obj_set_scroll_dir(panel,LV_DIR_VER);lv_obj_set_scroll_elastic(panel,false);
  lv_obj_set_scroll_momentum(panel,false);lv_obj_set_scroll_chain_ver(panel,false);
  lv_obj_t *activeRow=nullptr;int y=uiMetrics_.dp(4);
  for(const auto &preset:presets) {
    const bool active=preset.at("id")==sessionPreferences_.value("preset_id",std::string("default"));
    auto row=button(panel,preset.value("name",std::string()),uiMetrics_.dp(4),y,
        area.width-uiMetrics_.dp(8),rowHeight,[this,preset]{applyPreset(preset);},
        theme::palette().surface,LV_TEXT_ALIGN_LEFT);
    selected(row,active);stylePopupRow(row);
    if(active)activeRow=row;
    y+=rowHeight;
  }
  lv_obj_update_layout(panel);
  if(activeRow)lv_obj_scroll_to_view(activeRow,LV_ANIM_OFF);
  motion::present(panel,1,uiMetrics_);
}
void App::presetsPage() {
  pageHeader("会话预设");
  const int width = layout().contentWidth(), column = (width - uiMetrics_.dp(14)) / 2;
  bool enabled = db_.settings().value("presets_enabled", false);
  button(pageContent_, enabled ? "启用预设入口：开" : "启用预设入口：关", uiMetrics_.dp(10), uiMetrics_.dp(4), column, uiMetrics_.dp(48),
         [this, enabled] { db_.setting("presets_enabled", !enabled); build(); });
  auto add = pageAction("新增预设", uiMetrics_.dp(24), uiMetrics_.dp(130), [this] {
    presetForm_ = defaultPreset();
    presetForm_["id"] = uid("preset");
    presetForm_["name"] = "新预设";
    show("preset_edit");
  });
  primary(add);
  label(pageContent_, "关闭只隐藏入口\n已有会话配置继续生效", uiMetrics_.dp(24) + column, uiMetrics_.dp(7), column, uiMetrics_.fontSize(UiFont::FormHint), theme::palette().muted);
  int y = uiMetrics_.dp(64);
  const auto presets=presetList(db_.settings());
  if(presets.empty())
    statusState(pageContent_,{StatusKind::Empty,"还没有会话预设","点击“新增预设”保存常用提示词、模型或推理强度；开启入口后可从聊天加号选择。","settings"},uiMetrics_,
                font(uiMetrics_.fontSize(UiFont::EmptyTitle)),font(uiMetrics_.fontSize(UiFont::FormNote)),
                uiMetrics_.dp(10),y,width,
                std::max(1,int(lv_obj_get_height(body_))-int(lv_obj_get_y(pageContent_))-y-uiMetrics_.dp(8)));
  const int titleHeight=int(font(uiMetrics_.fontSize(UiFont::Button))->line_height);
  const int detailTop=std::max(uiMetrics_.dp(36),uiMetrics_.dp(6)+titleHeight+uiMetrics_.dp(4));
  const int detailHeight=std::max(uiMetrics_.dp(21),int(font(uiMetrics_.fontSize(UiFont::Detail))->line_height));
  const int rowHeight=std::max(uiMetrics_.dp(64),detailTop+detailHeight+uiMetrics_.dp(7));
  for (const auto &p : presets) {
    bool modified=false;
    const auto defaults=defaultPreset();
    if(p.at("id")=="default")for(auto it=defaults.begin();it!=defaults.end();++it)
      if(it.key()!="id"&&it.key()!="name"&&p.value(it.key(),it.value())!=it.value())modified=true;
    const int modifiedHeight=modified?detailHeight:0;
    auto row = button(pageContent_, p.value("name", std::string()), uiMetrics_.dp(10), y,width,rowHeight+modifiedHeight,
                      [this, p] { presetForm_ = p; show("preset_edit"); }, theme::palette().surface, LV_TEXT_ALIGN_LEFT);
    lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_TOP_LEFT, uiMetrics_.dp(12), uiMetrics_.dp(6));
    if(modified)label(row,"（已修改）",uiMetrics_.dp(12),detailTop,width-uiMetrics_.dp(24),uiMetrics_.fontSize(UiFont::Detail),theme::palette().muted);
    auto detail = label(row, presetSummary(p), uiMetrics_.dp(12),detailTop+modifiedHeight,width-uiMetrics_.dp(24), uiMetrics_.fontSize(UiFont::Detail), theme::palette().muted);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(detail,detailHeight);
    lv_obj_set_clickable(detail, false);
    y+=rowHeight+modifiedHeight+uiMetrics_.dp(8);
  }
}
Json App::presetsDiagnostic() const {
  const bool menu=popupKind_=="presets"&&attachmentPanel_;
  auto parent=menu?attachmentPanel_:view_=="presets"?pageContent_:nullptr;
  if(!parent)return Json();
  auto bounds=[](lv_obj_t *object) {
    lv_area_t a;lv_obj_get_coords(object,&a);return Json::array({a.x1,a.y1,a.x2,a.y2});
  };
  Json labels=Json::array();int row=-1;lv_obj_t *lastRow=nullptr;
  auto append=[&](lv_obj_t *label,const char *role,lv_obj_t *owner) {
    labels.push_back({{"role",role},{"index",row},{"bounds",bounds(label)},
      {"parent_bounds",bounds(lv_obj_get_parent(label))},{"row_bounds",owner?bounds(owner):Json()},
      {"line_height",lv_obj_get_style_text_font(label,LV_PART_MAIN)->line_height},
      {"text_length",editLength(lv_label_get_text(label))}});
  };
  for(unsigned i=0;i<lv_obj_get_child_count(parent);++i) {
    auto object=lv_obj_get_child(parent,i);
    if(!lv_obj_check_type(object,&lv_label_class)&&lv_obj_get_child_count(object)>0&&
        lv_obj_check_type(lv_obj_get_child(object,0),&lv_label_class)) {
      // The settings page also has an enable button before its preset rows.
      if(!menu&&lv_obj_get_child_count(object)<2)continue;
      ++row;lastRow=object;
      for(unsigned j=0;j<lv_obj_get_child_count(object);++j) {
        auto child=lv_obj_get_child(object,j);
        if(lv_obj_check_type(child,&lv_label_class))append(child,j==0?"title":"detail",object);
      }
    } else if(menu&&lv_obj_check_type(object,&lv_label_class)) {
      append(object,row<0?"heading":"detail",lastRow);
    }
  }
  return {{"kind",menu?"menu":"page"},{"bounds",bounds(parent)},{"labels",labels}};
}
void App::presetChoices(const std::string &field) {
  const bool modelField = field == "model_id" || field == "compression_model_id";
  std::vector<std::pair<std::string,std::string>> options;
  auto option=[&](const std::string &key,const std::string &name){options.push_back({key,name});};
  if (field == "prompt_mode") {
    option("inherit", "沿用会话"); option("custom", "自定义"); option("clear", "清空");
  } else if (modelField) {
    option("", field == "compression_model_id" ? "跟随聊天模型" : "沿用会话");
    for (const auto &provider : db_.providers()) {
      for (const auto &m : db_.models()) if (m["provider_id"] == provider["id"])
        option(m["id"], m.value("display_name",m.value("name", std::string()))+" · "+provider.value("name",std::string()));
    }
  } else {
    option("", "沿用会话");
    auto id = presetForm_.value("model_id", std::string());
    Json model;
    for (const auto &m : db_.models()) if (m["id"] == id) model = m;
    std::vector<std::string> choices;
    if (id.empty()) { choices = effortOrder(); choices.insert(choices.begin() + 1, "api_default"); }
    else if (model.is_object()) {choices = effortChoices(model);choices.insert(choices.begin(),"api_default");}
    else choices = {"api_default"};
    for (const auto &key : choices) option(key, key == "api_default" ? "API 默认（不传参数）" : effortLabel(key));
  }
  choiceMenu("preset_"+field,options,presetForm_.value(field,std::string()),
    [this,field](const std::string &key){presetForm_[field]=key;build();});
}
void App::presetEditor() {
  pageHeader("编辑预设");
  const int width = layout().contentWidth(), column = (width - uiMetrics_.dp(14)) / 2, right = uiMetrics_.dp(24) + column;
  auto save = pageAction("保存", uiMetrics_.dp(24), uiMetrics_.dp(130), [this] {
    if (!presetHasText(presetForm_.value("name", std::string()))) { modal("请输入预设名称", "名称不能为空。"); return; }
    if (presetForm_.value("prompt_mode", std::string()) == "custom" && !presetHasText(presetForm_.value("prompt", std::string()))) {
      modal("请输入系统提示词", "若需要移除提示词，请选择“清空”。"); return;
    }
    auto id = presetForm_.value("model_id", std::string());
    if (!id.empty()) {
      Json model;
      for (const auto &m : db_.models()) if (m["id"] == id) model = m;
      if (model.is_null() || !presetSupports(model, presetForm_.value("effort", std::string()))) {
        modal("请检查模型与强度", "模型已不可用或不支持所选强度，请重新选择。"); return;
      }
    }
    auto compressionId = presetForm_.value("compression_model_id", std::string());
    if (!compressionId.empty()) {
      bool available = false;
      for (const auto &m : db_.models()) if (m["id"] == compressionId) available = true;
      if (!available) { modal("压缩模型不可用", "请选择已有模型，或恢复“跟随聊天模型”。"); return; }
    }
    auto presets = presetList(db_.settings());
    bool replaced = false;
    for (auto &p : presets) if (p["id"] == presetForm_["id"]) { p = presetForm_; replaced = true; break; }
    if (!replaced) presets.push_back(presetForm_);
    db_.setting("chat_presets", presets);
    presetForm_ = Json::object();
    show("presets");
  });
  primary(save);
  auto action = [&](const std::string &text, int x, int y, std::function<void()> callback) {
    auto separator = text.find("：");
    if (separator != std::string::npos) {
      auto field=fieldButton(pageContent_, text.substr(0, separator), text.substr(separator + 3), x, y, column, callback);
      pageActions_["preset:"+text.substr(0,separator)]=field;
    } else {
      auto object = button(pageContent_, text, x, y, column, uiMetrics_.dp(48), callback, theme::palette().surface, LV_TEXT_ALIGN_LEFT);
      if (text == "删除预设") lv_obj_set_style_text_color(lv_obj_get_child(object, 0), lv_color_hex(theme::palette().danger), 0);
    }
  };
  action("名称：" + presetForm_.value("name", std::string()), uiMetrics_.dp(10), uiMetrics_.dp(4), [this] {
    if (presetForm_["id"] == "default") { modal("默认预设", "默认预设始终存在，名称固定；其他配置可以修改。"); return; }
    edit(presetForm_.value("name", std::string()), "预设名称", [this](const std::string &v) { presetForm_["name"] = v; build(); }, 32);
  });
  auto mode = presetForm_.value("prompt_mode", std::string("inherit"));
  action(std::string("系统提示词：") + (mode == "custom" ? "自定义" : mode == "clear" ? "清空" : "沿用会话"), right, uiMetrics_.dp(4),
         [this] { presetChoices("prompt_mode"); });
  std::string name = "沿用会话";
  auto id = presetForm_.value("model_id", std::string());
  if (!id.empty()) {
    name = "模型不可用";
    for (const auto &m : db_.models()) if (m["id"] == id) name = m.value("display_name",m.value("name", std::string()));
  }
  action("模型：" + name, uiMetrics_.dp(10), uiMetrics_.dp(62), [this] { presetChoices("model_id"); });
  auto effort = presetForm_.value("effort", std::string());
  action("推理强度：" + (effort.empty() ? std::string("沿用会话") : effort == "api_default" ? "API 默认" : effortLabel(effort)), right, uiMetrics_.dp(62),
         [this] { presetChoices("effort"); });
  auto timing = [&](const std::string &field, int x) {
    bool always = presetForm_.value(field, std::string("empty")) == "always";
    label(pageContent_, "应用时机", x, uiMetrics_.dp(120), column, uiMetrics_.fontSize(UiFont::Detail), theme::palette().muted);
    for (int i = 0; i < 2; ++i) {
      const std::string value = i ? "always" : "empty";
      const int segment = (column - uiMetrics_.dp(8)) / 2;
      auto choice = button(pageContent_, ((always == bool(i)) ? "● " : "○ ") + std::string(i ? "每次切换" : "仅空白会话"),
                           x + i * (segment + uiMetrics_.dp(8)), uiMetrics_.dp(144), segment, uiMetrics_.dp(44),
                           [this, field, value] { presetForm_[field] = value; build(); }, theme::palette().surface);
      if (always == bool(i)) {
        lv_obj_set_style_border_width(choice, uiMetrics_.dp(1), 0);
        lv_obj_set_style_border_color(choice, lv_color_hex(theme::palette().muted), 0);
      }
    }
  };
  if (!id.empty()) timing("model_when", uiMetrics_.dp(10));
  if (!effort.empty()) timing("effort_when", right);
  auto compressionId = presetForm_.value("compression_model_id", std::string());
  std::string compressionName =
      compressionId.empty() ? "跟随聊天模型" : "指定模型不可用";
  for (const auto &m : db_.models())
    if (m["id"] == compressionId)
      compressionName = m.value("display_name",m.value("name", std::string()));
  const int compressionY = (!id.empty() || !effort.empty()) ? uiMetrics_.dp(202) : uiMetrics_.dp(120);
  action("上下文压缩模型：" + compressionName, uiMetrics_.dp(10), compressionY,
         [this] { presetChoices("compression_model_id"); });
  label(pageContent_, "仅影响下一次压缩；不改变聊天模型和已有摘要", right, compressionY + uiMetrics_.dp(8), column, uiMetrics_.fontSize(UiFont::Detail),
        theme::palette().muted);
  if (mode == "custom") {
    fieldButton(
        pageContent_, "系统提示词", presetForm_.value("prompt", std::string("点击编辑")), uiMetrics_.dp(10),
        compressionY + uiMetrics_.dp(64), width, [this] {
          edit(
              presetForm_.value("prompt", std::string()), "系统提示词（可输入多行）",
              [this](const std::string &v) {
                presetForm_["prompt"] = v;
                build();
              },
              8192, "ZhCNPreferred", {}, true);
        });
    label(pageContent_, "应用后替换会话提示词；需要移除时请选择“清空”。", uiMetrics_.dp(10), compressionY + uiMetrics_.dp(122), width, uiMetrics_.fontSize(UiFont::Detail),
          theme::palette().muted);
  }
  if (presetForm_["id"] != "default")
    action("删除预设", right, compressionY + (mode == "custom" ? uiMetrics_.dp(156) : uiMetrics_.dp(64)), [this] {
      modal("删除预设", "已有会话仍保留当前配置。确定删除此预设？");
      button(modal_, "确定删除", uiMetrics_.dp(16), screen_.height - uiMetrics_.dp(66), uiMetrics_.dp(160), uiMetrics_.dp(44), [this] {
        auto presets = presetList(db_.settings());
        Json kept = Json::array();
        for (const auto &p : presets)
          if (p["id"] != presetForm_["id"])
            kept.push_back(p);
        db_.setting("chat_presets", kept);
        presetForm_ = Json::object();
        show("presets");
      });
    });
}
} // namespace penchat
