#include "app.hpp"
#include "menu_action.hpp"
#include "attachment_layout.hpp"
#include "icons.hpp"
#include "tools.hpp"
#include "effort_slider.hpp"
#include "ui_motion.hpp"
#include "empty_state.hpp"
namespace penchat {
void App::closeAttachments() {
  if(effortPreview_){effortPreview_=false;syncEffortGauge();}
  if (popupKind_ == "attachments") { ++sessionInfoGeneration_; sessionInfoPending_ = false; }
  contextUsageArc_ = contextUsageLabel_ = nullptr;
  auto closingPanel=attachmentPanel_;
  attachmentPanel_ = nullptr;
  attachmentEntries_.clear();
  popupKind_.clear();
  if (!attachments_)
    return;
  auto object = attachments_;
  attachments_ = nullptr;
  motion::dismiss(object,closingPanel);
}
void App::toggleAttachments() {
  if(view_!="chat"||!chatControls_)return;
  if (contextBusy_) {
    contextProgress("正在整理历史，请稍候…");
    return;
  }
  if (attachments_) {
    closeAttachments();
    return;
  }
  resetPull();
  // Finish the composer transition so a click during its animation uses the
  // actual resting capsule, with enough physical room for the 10px overlap.
  if(chatControls_){lv_anim_delete(chatControls_,nullptr);lv_obj_set_y(chatControls_,chatComposerY());}
  lv_obj_update_layout(screenObj_);
  lv_area_t capsule;lv_obj_get_coords(chatControls_,&capsule);
  attachments_ = box(lv_screen_active(),0,0,screen_.width,screen_.height);
  popupKind_ = "attachments";
  lv_obj_set_style_bg_color(attachments_, lv_color_hex(theme::palette().scrim), 0);
  lv_obj_set_style_bg_opa(attachments_, LV_OPA_50, 0);
  lv_obj_set_scrollable(attachments_, false);
  menuAction(attachments_, [this] { closeAttachments(); });
  std::vector<std::string> names{"拍照", "相册", "模型", "推理强度"};
  std::vector<std::string> icons{"camera", "album", "settings", "gauge"};
  std::vector<std::string> ids{"camera", "album", "models", "effort", "compress", "summary"};
  names.push_back("压缩上下文（计算中）");
  icons.push_back("history");
  names.push_back("查看上下文摘要");
  icons.push_back("context-summary");
  if (db_.settings().value("presets_enabled", false)) {
    ids.push_back("presets");
    names.push_back("预设");
    icons.push_back("settings");
  }
  ids.push_back("web");
  names.push_back(webEnabled(sessionPreferences_, db_.settings()) ? "联网搜索 · 已开启"
                                                                  : "联网搜索 · 已关闭");
  icons.push_back("globe");
  ids.push_back("session_info");
  names.push_back("会话信息");
  icons.push_back("info");
  int measuredWidth=0;
  for(const auto &name:names) {
    lv_point_t measured{};lv_text_get_size(&measured,name.c_str(),font(uiMetrics_.fontSize(UiFont::SettingsTitle)),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    measuredWidth=std::max(measuredWidth,int(measured.x)+uiMetrics_.dp(76));
  }
  auto area=attachmentMenuArea(layout(),{capsule.x1,capsule.y1,capsule.x2-capsule.x1+1,capsule.y2-capsule.y1+1},
                                    measuredWidth,int(names.size())*uiMetrics_.dp(44)+uiMetrics_.dp(8));
  if(keyboard_&&officialDocked_) {
    const int top=std::max(layout().safeTop(),uiMetrics_.dp(8));
    area.height=std::min(area.height,std::max(1,display_.topHeight()-top-uiMetrics_.dp(8)));
    area.y=std::max(top,std::min(area.y,display_.topHeight()-uiMetrics_.dp(8)-area.height));
  }
  const int menuWidth=area.width;
  auto menu=box(attachments_,area.x,area.y,area.width,area.height,theme::palette().surface);
  attachmentPanel_=menu;
  lv_obj_set_style_radius(menu,uiMetrics_.dp(16),0);
  lv_obj_set_scroll_dir(menu,LV_DIR_VER);
  lv_obj_set_scroll_elastic(menu,false);lv_obj_set_scroll_momentum(menu,false);
  for (int i = 0; i < int(names.size()); ++i) {
    const auto actionId = ids[i];
    std::function<void()> action = [this, actionId] {
      if (actionId == "session_info") {
        if(!finishChatEditorForNavigation())return;
        openSessionInfo();
        return;
      }
      if (actionId == "web") {
        const bool enabled = webEnabled(sessionPreferences_, db_.settings());
        sessionPreferences_["web_enabled"] = !enabled;
        db_.setting("last_web_enabled", !enabled);
        savePreferences();
        closeAttachments();
        if (generating_)
          modal("已修改联网搜索", "当前回复继续使用发送时的配置，下一次提问生效。");
        return;
      }
      if (actionId == "camera") {
        if(!finishChatEditorForNavigation())return;show("camera");
      }
      else if (actionId == "album") {
        if(!finishChatEditorForNavigation())return;show("album");
      }
      else if (actionId == "models")
        modelPicker();
      else if (actionId == "compress") {
        if(!finishChatEditorForNavigation())return;startContext(true);
      }
      else if (actionId == "summary")
        contextDetails();
      else if (actionId == "presets")
        presetPicker();
      else {
        bool enabled =
            sessionPreferences_.value("gauge", !effortMemory_.empty());
        if (enabled)
          chooseEffort("");
        sessionPreferences_["gauge"] = !enabled;
        savePreferences();
        closeAttachments();
        // Composer visibility is synchronized by the owning chat controller.
        syncEffortGauge();
      }
    };
    auto perform=action;
    action=[this,perform] {
      if(keyboard_&&officialDocked_) {
        if(!officialActionId_.empty())return;
        officialMenuAction_=perform;officialActionId_=uid("official-action");officialActionKind_="menu";
        officialActionSession_=session_;officialActionAt_=ticks();
        if(!bridge_.send("keyboard.commit-action",{{"requestId",keyboardId_},{"actionId",officialActionId_},{"action","menu"},{"uncommitted",officialKeyEventAt_>officialTextEventAt_},{"keyEventTime",officialKeyEventAt_}})) {
          officialActionId_.clear();officialMenuAction_={};
        }
        return;
      }
      perform();
    };
    if (ids[i] == "compress") {
      auto control = button(menu, "", uiMetrics_.dp(4), uiMetrics_.dp(4) + i * uiMetrics_.dp(44), uiMetrics_.dp(44), uiMetrics_.dp(44), action, theme::palette().surface);
      contextUsageArc_ = lv_arc_create(control);
      lv_obj_remove_style_all(contextUsageArc_);
      lv_obj_set_scrollable(contextUsageArc_, false);
      lv_obj_set_clickable(contextUsageArc_, false);
      lv_obj_set_size(contextUsageArc_, uiMetrics_.dp(24), uiMetrics_.dp(24));
      lv_obj_center(contextUsageArc_);
      lv_arc_set_rotation(contextUsageArc_, 270);
      lv_arc_set_bg_angles(contextUsageArc_, 0, 360);
      lv_arc_set_range(contextUsageArc_, 0, 1000);
      lv_arc_set_value(contextUsageArc_, 0);
      lv_obj_set_style_arc_color(contextUsageArc_, lv_color_hex(theme::palette().muted), LV_PART_MAIN);
      lv_obj_set_style_arc_opa(contextUsageArc_, LV_OPA_50, LV_PART_MAIN);
      lv_obj_set_style_arc_width(contextUsageArc_, uiMetrics_.dp(2), LV_PART_MAIN);
      lv_obj_set_style_arc_color(contextUsageArc_, lv_color_hex(theme::palette().link), LV_PART_INDICATOR);
      lv_obj_set_style_arc_opa(contextUsageArc_, LV_OPA_TRANSP, LV_PART_INDICATOR);
      lv_obj_set_style_arc_width(contextUsageArc_, uiMetrics_.dp(2), LV_PART_INDICATOR);
      lv_obj_set_style_arc_rounded(contextUsageArc_, true, LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(contextUsageArc_, LV_OPA_TRANSP, LV_PART_KNOB);
    } else
      icon(menu, icons[i], uiMetrics_.dp(4), uiMetrics_.dp(4) + i * uiMetrics_.dp(44), uiMetrics_.dp(44), uiMetrics_.dp(44), action, theme::palette().surface);
    auto entry = button(menu, names[i], uiMetrics_.dp(48), uiMetrics_.dp(4) + i * uiMetrics_.dp(44), menuWidth - uiMetrics_.dp(52), uiMetrics_.dp(44), action, theme::palette().surface,
                        LV_TEXT_ALIGN_LEFT);
    attachmentEntries_[ids[i]] = entry;
    if (ids[i] == "compress")
      contextUsageLabel_ = lv_obj_get_child(entry, 0);
  }
  ++sessionInfoGeneration_;
  sessionInfoSession_ = session_;
  sessionInfoData_ = Json::object();
  refreshSessionInfo();
  motion::present(menu,1,uiMetrics_);
}
void App::modelPicker(bool forTitles) {
  closeAttachments();
  resetPull();
  const bool portrait=layout().portrait();
  auto models=db_.models();
  const auto providers=db_.providers();
  attachments_ = box(portrait?lv_screen_active():screenObj_,layout().gutter(),0,screen_.width-layout().gutter(),screen_.height);
  popupKind_ = forTitles ? "title_models" : "models";
  lv_obj_set_style_bg_color(attachments_, lv_color_hex(theme::palette().scrim), 0);
  lv_obj_set_style_bg_opa(attachments_, LV_OPA_50, 0);
  lv_obj_set_scrollable(attachments_, false);
  menuAction(attachments_, [this] { closeAttachments(); });
  int naturalHeight=uiMetrics_.dp(8)+(forTitles?uiMetrics_.dp(48):0),naturalWidth=uiMetrics_.dp(264);
  if(portrait) {
    for(const auto &provider:providers) {
      if(models.empty())break;
      naturalHeight+=uiMetrics_.dp(30);
      lv_point_t measured{};
      lv_text_get_size(&measured,provider.value("name",std::string("Provider")).c_str(),font(uiMetrics_.fontSize(UiFont::FormNote)),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
      naturalWidth=std::max(naturalWidth,int(measured.x)+uiMetrics_.dp(40));
    }
    for(const auto &model:models) {
      naturalHeight+=uiMetrics_.dp(46);
      lv_point_t measured{};
      lv_text_get_size(&measured,model.value("display_name",model.value("name",std::string())).c_str(),font(uiMetrics_.fontSize(UiFont::Button)),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
      naturalWidth=std::max(naturalWidth,int(measured.x)+uiMetrics_.dp(76));
    }
    if(models.empty())naturalHeight+=uiMetrics_.dp(48);
    if(forTitles&&!models.empty())naturalHeight+=uiMetrics_.dp(30);
  }
  const LayoutRect anchor=portrait&&view_=="chat"?LayoutRect{layout().safeSide(),chatComposerY(),1,1}:
      LayoutRect{lastTouch_.value("x",screen_.width/2),lastTouch_.value("y",screen_.height/2),1,1};
  const auto area=portrait?placeContextPopup(screen_.width,screen_.height,roundedDisplay_,anchor,
      std::min(uiMetrics_.dp(440),naturalWidth),std::min(uiMetrics_.dp(360),naturalHeight),keepChatEditor()?editorLayout().panelTop:0,uiMetrics_):
      pickerArea(std::min(uiMetrics_.dp(650),screen_.width-uiMetrics_.dp(130)));
  const int panelWidth=area.width;
  auto panel=box(attachments_,area.x,area.y,panelWidth,area.height,theme::palette().popup);
  attachmentPanel_=panel;stylePopupPanel(panel,layout().portrait(),uiMetrics_);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  lv_obj_set_scroll_elastic(panel,false);
  lv_obj_set_scroll_chain_ver(panel,false);
  auto current = currentModel();
  if (forTitles)
    current = {{"id", db_.settings().value("title_model_id", std::string())}};
  int y = uiMetrics_.dp(4);
  if (forTitles) {
    button(
        panel, "跟随当前聊天模型", uiMetrics_.dp(8), y, panelWidth - uiMetrics_.dp(30), uiMetrics_.dp(44),
        [this] {
          db_.setting("title_model_id", "");
          build();
        },
        theme::palette().surface, LV_TEXT_ALIGN_LEFT);
    y += uiMetrics_.dp(48);
  }
  std::sort(models.begin(), models.end(), [](const Json &a, const Json &b) {
    return a.value("name", std::string()) < b.value("name", std::string());
  });
  for (const auto &provider : providers) {
    if(portrait&&models.empty())break;
    auto caption=label(panel, provider.value("name", std::string("Provider")), uiMetrics_.dp(14), y, panelWidth - uiMetrics_.dp(40),
          uiMetrics_.fontSize(UiFont::FormNote), theme::palette().muted);
    lv_label_set_long_mode(caption,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(caption,font(uiMetrics_.fontSize(UiFont::FormNote))->line_height);
    y += uiMetrics_.dp(30);
    for (const auto &model : models)
      if (model["provider_id"] == provider["id"]) {
        auto id = model["id"].get<std::string>();
        auto held = std::make_shared<bool>(false);
        auto item = button(
            panel,
            (portrait?std::string():current.value("id", std::string()) == id ? "● " : "○ ") +
                model.value("display_name",model.value("name",std::string())),
            uiMetrics_.dp(8), y, panelWidth - uiMetrics_.dp(30), uiMetrics_.dp(44),
            [this, id, forTitles, held] {
              if (*held) return;
              if (forTitles)
                db_.setting("title_model_id", id);
              else
                chooseModel(id);
              if(forTitles)build();
              else {closeAttachments();syncEffortGauge();}
            },
            theme::palette().surface, LV_TEXT_ALIGN_LEFT);
        if(portrait){selected(item,current.value("id",std::string())==id);stylePopupRow(item);}
        if (forTitles) {
          auto action = new std::function<void()>([this, id, held] {
            *held = true;
            events_.push("title_model_detail", {{"id", id}});
          });
          lv_obj_add_event_cb(item, [](lv_event_t *e) {
            auto callback = *static_cast<std::function<void()> *>(lv_event_get_user_data(e));
            callback();
          }, LV_EVENT_LONG_PRESSED, action);
          lv_obj_add_event_cb(item, [](lv_event_t *e) {
            delete static_cast<std::function<void()> *>(lv_event_get_user_data(e));
          }, LV_EVENT_DELETE, action);
        }
        y += uiMetrics_.dp(46);
      }
  }
  if (models.empty())
    button(panel, "添加 Provider / 模型", uiMetrics_.dp(10), portrait?y:uiMetrics_.dp(50), panelWidth - uiMetrics_.dp(30), uiMetrics_.dp(48),
           [this] { if(finishChatEditorForNavigation())show("provider"); });
  if (forTitles && !models.empty())
    label(portrait?panel:attachments_, "长按模型查看完整名称", uiMetrics_.dp(portrait?14:24),
          portrait?y+uiMetrics_.dp(4):screen_.height-uiMetrics_.dp(50), panelWidth-uiMetrics_.dp(28), uiMetrics_.fontSize(UiFont::Detail), theme::palette().muted);
  motion::present(panel,1,uiMetrics_);
}
void App::effortPicker() {
  auto model=currentModel();
  if(model.empty()){modal("选择模型","请先选择一个可用模型。");return;}
  const auto options=effortChoices(model);
  closeAttachments();resetPull();
  attachments_=box(keepChatEditor()?lv_screen_active():screenObj_,0,0,screen_.width,screen_.height);
  popupKind_="effort";
  lv_obj_set_style_bg_color(attachments_,lv_color_hex(theme::palette().scrim),0);
  lv_obj_set_style_bg_opa(attachments_,LV_OPA_60,0);
  lv_obj_set_scrollable(attachments_,false);
  menuAction(attachments_,[this]{closeAttachments();});
  const auto safe=popupSafeArea(screen_.width,screen_.height,roundedDisplay_,uiMetrics_);
  const int panelHeight=std::min(uiMetrics_.dp(132),safe.height);
  const int panelY=std::max(safe.y,std::min(safe.y+safe.height-panelHeight,(screen_.height-panelHeight)/2));
  const int side=std::max(safe.x,layout().safety().sideInset(panelY,panelHeight));
  const int panelWidth=std::min(uiMetrics_.dp(520),screen_.width-2*side);
  const LayoutRect area{(screen_.width-panelWidth)/2,panelY,panelWidth,panelHeight};
  auto panel=box(attachments_,area.x,area.y,area.width,area.height,theme::palette().popup);
  attachmentPanel_=panel;lv_obj_set_scrollable(panel,false);
  lv_obj_set_style_bg_opa(panel,LV_OPA_TRANSP,0);
  if(options.empty()){closeAttachments();modal("推理强度","当前模型尚未配置可选强度，请在模型设置中核对能力。");return;}
  const bool explicitEffort=!sessionPreferences_.value("effort",std::string()).empty()&&sessionPreferences_.value("effort",std::string())!="api_default";
  const int initial=std::max(0,effortChoiceIndex(model));
  auto title=label(panel,explicitEffort?effortCaption(options[initial]):std::string("跟随服务商默认 · 推理强度"),uiMetrics_.dp(24),uiMetrics_.dp(16),
                   area.width-uiMetrics_.dp(48),uiMetrics_.fontSize(UiFont::Body),theme::palette(theme::Mode::Dark).richHeading);
  lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
  lv_label_set_long_mode(title,LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(title,font(uiMetrics_.fontSize(UiFont::Body))->line_height);
  auto capsule=box(panel,uiMetrics_.dp(16),uiMetrics_.dp(60),area.width-uiMetrics_.dp(32),uiMetrics_.dp(60),theme::palette().popup);
  lv_obj_set_scrollable(capsule,false);lv_obj_set_style_radius(capsule,LV_RADIUS_CIRCLE,0);
  auto slider=createEffortSlider(capsule,uiMetrics_.dp(8),uiMetrics_.dp(8),area.width-uiMetrics_.dp(48),
      int(options.size()),initial,uiMetrics_,[this,title,options](int index,EffortSliderEvent event){
        lv_label_set_text(title,effortCaption(options[index]).c_str());
        if(event==EffortSliderEvent::Preview) {
          effortPreview_=true;
          const auto placement=effortGaugePlacement(options,index);
          setIcon(composerGauge_,"gauge",placement.isDefault?theme::palette().muted:theme::palette().accent,1.f,uiMetrics_);
          setGaugePosition(composerGauge_,placement.index,placement.count);
        } else {
          effortPreview_=false;
          if(event==EffortSliderEvent::Commit)chooseEffort(options[index]);
          syncEffortGauge();
        }
      });
  lv_obj_set_ext_click_area(slider,uiMetrics_.dp(8));
  motion::present(panel,1,uiMetrics_);
}
void App::historyDrawer() {
  // Only the drawer is replaced when paging, keeping the underlying form/draft.
  if (navigationList_)
    navigationScroll_ = std::max(0, int(lv_obj_get_scroll_y(navigationList_)));
  historyRows_.clear();historyCaptions_.clear();navigationFooter_.clear();
  auto previous = navigation_;
  navigation_ = navigationList_ = navigationActive_ = nullptr;
  if (previous)
    lv_obj_delete(previous);
  navigation_ = box(screenObj_, 0, 0, screen_.width, screen_.height, 0x000000);
  lv_obj_set_scrollable(navigation_, false);
  lv_obj_set_style_bg_opa(navigation_, 120, 0);
  lv_obj_set_clickable(navigation_, true);
  const int panelWidth = screen_.width * 2 / 3;
  const int listLeft=layout().portrait()?layout().navigationX():uiMetrics_.dp(12);
  const int listTop=layout().portrait()?layout().safeTop():uiMetrics_.dp(8);
  const auto footerFont=font(uiMetrics_.fontSize(UiFont::Button));
  lv_point_t newText{},settingsText{};
  lv_text_get_size(&newText,"新聊天",footerFont,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  lv_text_get_size(&settingsText,"设置",footerFont,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  const int newNatural=newText.x+uiMetrics_.dp(56),settingsMinimum=settingsText.x+uiMetrics_.dp(56);
  const int footerHeight=std::max(uiMetrics_.touchTarget(48),int(footerFont->line_height)+uiMetrics_.dp(8));
  const int available=panelWidth-listLeft-uiMetrics_.dp(12),gap=uiMetrics_.dp(8);
  const bool stacked=layout().portrait()&&newNatural+settingsMinimum+footerHeight+2*gap>available;
  const int footerY=screen_.height-footerHeight-(layout().portrait()?layout().safeBottom():uiMetrics_.dp(6));
  const int footerTop=footerY-(stacked?footerHeight+gap:0);
  const int listWidth = panelWidth - listLeft - uiMetrics_.dp(12);
  auto panel =
      box(navigation_, 0, 0, panelWidth, screen_.height, theme::palette().sidebar);
  lv_obj_set_scrollable(panel, false);
  navigationList_ =
      box(panel, listLeft, listTop, listWidth, footerTop-listTop-uiMetrics_.dp(8), theme::palette().sidebar);
  lv_obj_set_scroll_dir(navigationList_, LV_DIR_VER);
  lv_obj_set_scroll_elastic(navigationList_, true);
  lv_obj_set_scroll_momentum(navigationList_, true);
  lv_obj_set_scroll_chain_ver(navigationList_, false);
  lv_obj_set_scroll_chain_hor(navigationList_, false);
  lv_obj_set_scrollbar_mode(navigationList_, LV_SCROLLBAR_MODE_AUTO);
  auto sessions = db_.sessions(historyOffset_);
  if (sessions.empty()) {
    statusState(navigationList_,{StatusKind::Empty,"暂无历史对话","发送文字或图片后会自动保存","history"},
        uiMetrics_,font(uiMetrics_.fontSize(UiFont::Body)),font(uiMetrics_.fontSize(UiFont::FormNote)),
        uiMetrics_.dp(12),uiMetrics_.dp(8),listWidth-uiMetrics_.dp(24),lv_obj_get_height(navigationList_)-uiMetrics_.dp(16));
  }
  int y = 0;
  for (const auto &s : sessions) {
    auto id = s["id"].get<std::string>();
    auto row = historyRow(navigationList_, s, y, listWidth);
    if (id == session_)
      navigationActive_ = row;
    y += lv_obj_get_style_height(row,LV_PART_MAIN)+uiMetrics_.dp(6);
  }
  if (historyOffset_ > 0)
    button(navigationList_, "上一页", 0, y, uiMetrics_.dp(160), uiMetrics_.dp(44), [this] {
      historyOffset_ = std::max(0, historyOffset_ - 30);
      navigationScroll_ = 0;
      historyDrawer();
    });
  if (sessions.size() == 30)
    button(navigationList_, "下一页", uiMetrics_.dp(180), y, uiMetrics_.dp(160), uiMetrics_.dp(44), [this] {
      historyOffset_ += 30;
      navigationScroll_ = 0;
      historyDrawer();
    });
  const int footerWidth=layout().portrait()?std::min(newNatural,available):
      std::min(newNatural,std::max(uiMetrics_.dp(44),panelWidth-listLeft-uiMetrics_.dp(20)-settingsMinimum));
  const int settingsX=panelWidth-uiMetrics_.dp(12)-settingsMinimum;
  const int walletX=stacked?listLeft:(listLeft+footerWidth+settingsX-footerHeight)/2;
  auto footerAction = [&](int x, int y, int width, const char *kind, const char *title,
                          std::function<void()> action) {
    auto control=button(panel,title,x,y,width,footerHeight,std::move(action),theme::palette().sidebar,LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_radius(control,uiMetrics_.dp(24),0);
    auto caption=lv_obj_get_child(control,0);
    lv_obj_set_width(caption,std::max(1,width-uiMetrics_.dp(56)));lv_obj_align(caption,LV_ALIGN_LEFT_MID,uiMetrics_.dp(44),0);
    lv_obj_set_clickable(caption,false);
    auto glyph=icon(control,kind,uiMetrics_.dp(8),(footerHeight-uiMetrics_.dp(32))/2,uiMetrics_.dp(32),uiMetrics_.dp(32),{},theme::palette().sidebar);
    lv_obj_set_style_bg_opa(glyph,LV_OPA_TRANSP,0);lv_obj_set_clickable(glyph,false);
    navigationFooter_[kind]=control;
  };
  footerAction(listLeft, footerTop, footerWidth, "newchat", "新聊天", [this] { newChat(); });
  if(layout().portrait())balanceButton(panel,walletX,footerY,theme::palette().sidebar);
  footerAction(settingsX,footerY,panelWidth-uiMetrics_.dp(12)-settingsX,"settings","设置",[this]{show("settings");});
  lv_obj_update_layout(navigation_);
  // LVGL needs scrollable extent before it will start an elastic gesture.
  // A transparent one-pixel tail also enables bounce for short/empty lists.
  if (lv_obj_get_scroll_bottom(navigationList_) <= 0) {
    auto tail =
        box(navigationList_, 0, lv_obj_get_height(navigationList_), uiMetrics_.dp(1), uiMetrics_.dp(1));
    lv_obj_set_style_bg_opa(tail, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollable(tail, false);
    lv_obj_set_clickable(tail, false);
    lv_obj_update_layout(navigationList_);
  }
  const int end = std::max(0, int(lv_obj_get_scroll_y(navigationList_) +
                                  lv_obj_get_scroll_bottom(navigationList_)));
  lv_obj_scroll_to_y(navigationList_, std::min(navigationScroll_, end),
                     LV_ANIM_OFF);
}

} // namespace penchat
