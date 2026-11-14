#include "app.hpp"
#include "chat_time.hpp"
#include "titles.hpp"
#include "tools.hpp"
#include <algorithm>
namespace penchat {
void App::refreshChatTitle() {
  if (!chatTitle_) return;
  const auto info = db_.sessionInfo(session_);
  const auto title = info.value("title", std::string());
  lv_label_set_text(chatTitle_, title.empty()?"新聊天":title.c_str());
}
int App::titleWidth() const {
  return (screen_.width * 2 / 3 - 24 - 48) * 4 / 5;
}
int App::titleLimit() const {
  lv_point_t size;
  lv_text_get_size(&size, "汉", font(uiMetrics_.fontSize(UiFont::SettingsTitle)), 0, 0, LV_COORD_MAX,
                   LV_TEXT_FLAG_NONE);
  return std::max(1, titleWidth() / std::max(1, int(size.x)));
}
std::string App::fitTitle(const std::string &raw) const {
  auto title = plainTitle(raw, titleLimit());
  auto parts = characters(title);
  while (!parts.empty()) {
    lv_point_t size;
    lv_text_get_size(&size, title.c_str(), font(uiMetrics_.fontSize(UiFont::SettingsTitle)), 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    if (size.x <= titleWidth())
      break;
    parts.pop_back();
    title.clear();
    for (const auto &part : parts)
      title += part;
  }
  return title;
}
void App::scheduleTitle(const std::string &session, const std::string &assistant) {
  auto settings = db_.settings();
  if (!settings.value("auto_title_enabled", true))
    return;
  auto rows =
      db_.query("SELECT model_config_id FROM chat_messages WHERE id=?", {assistant});
  if (rows.empty())
    return;
  auto modelId = settings.value("title_model_id", std::string());
  if (modelId.empty() && rows[0]["model_config_id"].is_string())
    modelId = rows[0]["model_config_id"];
  auto job = db_.prepareTitle(session, assistant, modelId);
  if (!job.empty())
    titleJobs_.push_back(job);
}
void App::tickTitles() {
  const int day=chatDayKey();
  if(day!=historyDayKey_) {historyDayKey_=day;if(navigationOpen_ || view_=="archives")historyDirty_=true;}
  if (historyDirty_ && !touchDown_ && !navigationPointerDown_ && !modal_ &&
      !keyboard_) {
    if (navigationOpen_)
      historyDrawer();
    else if (view_ == "archives") {
      int scroll = lv_obj_get_scroll_y(body_);
      build();
      lv_obj_scroll_to_y(body_, scroll, LV_ANIM_OFF);
    }
    historyDirty_ = false;
  }
  if (titleBusy_ || titleJobs_.empty())
    return;
  if (titleWorker_.joinable())
    titleWorker_.join();
  auto job = titleJobs_.front();
  titleJobs_.pop_front();
  auto id = job["session"].get<std::string>(),
       token = job["token"].get<std::string>();
  if (!db_.startTitle(id, token))
    return;
  Json model = Json::object();
  for (const auto &candidate : db_.models())
    if (candidate["id"] == job["model_id"]) {
      model = candidate;
      break;
    }
  auto rows = db_.query("SELECT output_text,seq FROM chat_messages WHERE "
                        "session_id=? AND id=? AND status='complete'",
                        {id, job["assistant"]});
  if (model.empty() || rows.empty() ||
      !db_.settings().value("auto_title_enabled", true)) {
    db_.finishTitle(id, token, "");
    return;
  }
  std::string question;
  for (const auto &row :
       db_.query("SELECT output_text,message_type FROM chat_messages WHERE "
                 "session_id=? AND role='user' AND seq<? ORDER BY seq",
                 {id, rows[0]["seq"]})) {
    if (row["message_type"] == "image")
      question += "[图片提问] ";
    if (row["output_text"].is_string())
      question += row["output_text"].get<std::string>();
    question += '\n';
  }
  const auto answer = rows[0]["output_text"].is_string()
                          ? rows[0]["output_text"].get<std::string>()
                          : std::string();
  if (answer.empty()) {
    db_.finishTitle(id, token, "");
    return;
  }
  titleSession_ = id;
  titleBusy_ = true;
  titleCancel_ = false;
  const int limit = titleLimit();
  titleWorker_ = std::thread([this, job, model, question, answer, limit] {
    std::string title;
    try {
      title =
          generateTitle(db_.requestModel(model), question, answer, limit, titleCancel_);
    } catch (...) {
    }
    events_.push("title_result", {{"session", job["session"]},
                                  {"token", job["token"]},
                                  {"title", title}});
    titleBusy_ = false;
  });
}
void App::historyTimePicker() {
  choiceMenu("history_time", {{"time","时间显示优先"},{"title","会话标题优先"},{"off","关闭时间显示"}},
    db_.settings().value("history_time_display",std::string("time")),
    [this](const std::string &key){events_.push("history_time_display",{{"mode",key}});});
}
void App::historyCaption(lv_obj_t *parent,const Json &session,int x,int y,int width) {
  const auto text=session.value("title",std::string("对话"));
  const auto mode=db_.settings().value("history_time_display",std::string("time"));
  const auto stamp=mode=="off"?std::string():historyTime(sessionChatTime(session));
  lv_point_t titleSize{},timeSize{};
  lv_text_get_size(&titleSize,text.c_str(),font(uiMetrics_.fontSize(UiFont::SettingsTitle)),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  lv_text_get_size(&timeSize,stamp.c_str(),font(uiMetrics_.fontSize(UiFont::Detail)),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  const bool show=!stamp.empty() && (mode!="title" || titleSize.x+12+timeSize.x<=width);
  const int titleWidth=std::max(1,width-(show?timeSize.x+uiMetrics_.dp(12):0));
  auto caption=label(parent,text,x,y,titleWidth,uiMetrics_.fontSize(UiFont::SettingsTitle));
  lv_obj_set_height(caption,uiMetrics_.dp(26));
  lv_label_set_long_mode(caption,mode=="time"?LV_LABEL_LONG_MODE_SCROLL_CIRCULAR:LV_LABEL_LONG_MODE_DOTS);
  lv_obj_t *time=nullptr;
  if(show) {time=label(parent,stamp,x+width-timeSize.x,y+2,timeSize.x,uiMetrics_.fontSize(UiFont::Detail),theme::palette().muted);lv_obj_set_height(time,uiMetrics_.dp(23));}
  historyCaptions_[session["id"].get<std::string>()]={caption,time};
  auto owner=new std::pair<App *,std::string>(this,session["id"].get<std::string>());
  lv_obj_add_event_cb(caption,[](lv_event_t *e) {
    auto owner=static_cast<std::pair<App *,std::string> *>(lv_event_get_user_data(e));
    owner->first->historyCaptions_.erase(owner->second);delete owner;
  },LV_EVENT_DELETE,owner);
}
lv_obj_t *App::historyRow(lv_obj_t *parent, const Json &session, int y,
                          int width) {
  const auto id = session["id"].get<std::string>();
  auto row = button(
      parent, "", 0, y,
      width, uiMetrics_.dp(44),
      [this, id] {
        if(replyBusy_||contextRegenerating_)return;
        if (longPressedSession_ == id)
          return;
        if (id != session_) {
          if(!switchDraft(id))return;
          stopNetwork();
          session_ = id;
          sessionPreferences_ = db_.sessionPreferences(id);
          sessionPreferences_["web_enabled"] = webEnabled(sessionPreferences_, db_.settings());
          expandedReasoning_.clear();
          reasoningOverrides_.clear();
          tableScrolls_.clear();
          pullNeedsPause_ = pullTrackActivity_ = false;
          db_.setting("active_session_id", id);
          messageOffset_ = 0;
          chatFollowing_ = true;
          chatScroll_ = 0;
        }
        show("chat");
      },
      id == session_ ? theme::palette().surface : theme::palette().sidebar, LV_TEXT_ALIGN_LEFT);
  auto caption = lv_obj_get_child(row, 0);
  if(id==session_)lv_obj_add_state(row,LV_STATE_CHECKED);
  lv_obj_set_hidden(caption,true);
  const bool pinned=objectField(session,"metadata_json").value("pinned_at",int64_t(0))>0;
  historyCaption(row,session,24,9,width-48-(pinned?28:0));
  if (pinned) {
    auto pin=icon(row, "pin", width - uiMetrics_.dp(34), uiMetrics_.dp(8), uiMetrics_.dp(28), uiMetrics_.dp(28), {},
         id == session_ ? theme::palette().surface : theme::palette().sidebar, theme::palette().muted);
    lv_obj_set_style_bg_opa(pin,LV_OPA_TRANSP,0);
  }
  historyRows_.push_back({id, row});
  struct Press {
    App *app;
    std::string id;
    lv_point_t start{};
    int distance = 0;
  };
  auto data = new Press{this, id};
  lv_obj_add_event_cb(
      row,
      [](lv_event_t *event) {
        auto data = static_cast<Press *>(lv_event_get_user_data(event));
        const auto code = lv_event_get_code(event);
        if (code == LV_EVENT_DELETE) {
          delete data;
          return;
        }
        if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
            code != LV_EVENT_LONG_PRESSED)
          return;
        auto indev = lv_indev_active();
        if (!indev)
          return;
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        if (code == LV_EVENT_PRESSED) {
          data->start = point;
          data->distance = 0;
          data->app->longPressedSession_.clear();
        }
        int dx = point.x - data->start.x, dy = point.y - data->start.y;
        data->distance = std::max(data->distance, dx * dx + dy * dy);
        if (code == LV_EVENT_LONG_PRESSED && data->distance <= 144) {
          data->app->longPressedSession_ = data->id;
          data->app->events_.push("history_context", {{"id", data->id}});
        }
      },
      LV_EVENT_ALL, data);
  return row;
}
void App::refreshHistoryHighlight() {
  if(!navigationOpen_)return;
  const auto &selected=historyContextId_.empty()?session_:historyContextId_;
  for(const auto &entry:historyRows_) {
    const bool active=entry.first==selected;
    lv_obj_set_style_bg_color(entry.second,lv_color_hex(active?theme::palette().surface:theme::palette().sidebar),0);
    if(active)lv_obj_add_state(entry.second,LV_STATE_CHECKED);
    else lv_obj_remove_state(entry.second,LV_STATE_CHECKED);
  }
}
void App::closeHistoryContext() {
  auto object = modal_;
  modal_ = nullptr;
  historyActions_.clear();
  historyContextId_.clear();
  refreshHistoryHighlight();
  navigationPointerDown_ = navigationBackdrop_ = false;
  if (object)
    lv_obj_delete(object);
}
void App::historyContext(const std::string &id) {
  auto session = db_.sessionInfo(id);
  if (session.empty())
    return;
  closeHistoryContext();
  auto actionRail=contextMenu("history",5);
  historyContextId_=id;
  refreshHistoryHighlight();
  const int actionWidth=lv_obj_get_width(actionRail);
  const int rowHeight=layout().portrait()?uiMetrics_.popup().portraitRow:uiMetrics_.popup().row;
  historyActions_["rename"]=button(actionRail, "重命名", 0, 0, actionWidth, rowHeight-uiMetrics_.dp(4), [this, id, session] {
    closeHistoryContext();
    if(layout().portrait()) {
      // Editing is a separate form, not a tall operation menu. Keep its field
      // above the docked keyboard even when the source row was near the bottom.
      modal("重命名会话", "");
      auto field=fieldButton(modalScroll_,"会话标题",session.value("title",std::string()),
                             uiMetrics_.dp(4),0,lv_obj_get_width(modalScroll_)-uiMetrics_.dp(8),[]{});
      inputTarget(field,"history-title:"+id,1);
      inputSource_=field;inputSourcePoint_={10,10};
    }
    edit(
        session.value("title", std::string()), "重命名会话",
        [this, id](const std::string &value) {
          if(layout().portrait())closeHistoryContext();
          auto title = plainTitle(value,256);
          if (!title.empty()) {
            if (titleSession_ == id)
              titleCancel_ = true;
            db_.renameSession(id, title);
            if (id == session_) refreshChatTitle();
          }
          toggleNavigation();
        },
        256, "ZhCNPreferred", [this] { if(layout().portrait())closeHistoryContext();toggleNavigation(); });
  });
  bool pinned =
      objectField(session, "metadata_json").value("pinned_at", int64_t(0)) > 0;
  historyActions_["pin"]=button(actionRail, pinned ? "取消置顶" : "置顶", 0, rowHeight, actionWidth, rowHeight-uiMetrics_.dp(4),
         [this, id, pinned] {
           closeHistoryContext();
           db_.pinSession(id, !pinned);
           historyOffset_ = 0;
           navigationScroll_ = 0;
           if (navigationList_)
             lv_obj_scroll_to_y(navigationList_, 0, LV_ANIM_OFF);
           historyDrawer();
         });
  historyActions_["archive"]=button(actionRail, "归档", 0, 2*rowHeight, actionWidth, rowHeight-uiMetrics_.dp(4), [this, id] {
    closeHistoryContext();
    retireSession(id, true);
  });
  auto remove = button(actionRail, "删除", 0, 3*rowHeight, actionWidth, rowHeight-uiMetrics_.dp(4), [this, id] {
    closeHistoryContext();
    retireSession(id, false);
  });
  historyActions_["delete"]=remove;
  lv_obj_set_style_text_color(lv_obj_get_child(remove, 0),
                              lv_color_hex(theme::palette().danger), 0);
  historyActions_["export"]=button(actionRail, "导出", 0, 4*rowHeight, actionWidth, rowHeight-uiMetrics_.dp(4), [this, id] {
    events_.push("transfer_open_session", {{"id", id}});
  });
  for(const auto &entry:historyActions_)stylePopupRow(entry.second);
}
void App::retireSession(const std::string &id, bool archive) {
  if(!archive)sessionDrafts_.erase(id);
  if (id == session_)
    stopNetwork();
  if (archive)
    db_.archiveSession(id, true);
  else {
    if (titleSession_ == id)
      titleCancel_ = true;
    db_.removeSession(id);
  }
  if (id == session_) {
    session_ = uid("session");
    sessionPreferences_ = Json::object();
    expandedReasoning_.clear();
    reasoningOverrides_.clear();
    tableScrolls_.clear();
    draft_.clear();
    db_.setting("active_session_id", nullptr);
    pullNeedsPause_ = pullTrackActivity_ = false;
    messageOffset_ = 0;
    chatFollowing_ = true;
    chatScroll_ = 0;
  }
  if (view_ == "archives") {
    if (archiveOffset_ && db_.sessions(archiveOffset_, true).empty())
      archiveOffset_ = std::max(0, archiveOffset_ - 30);
  } else if (historyOffset_ && db_.sessions(historyOffset_).empty())
    historyOffset_ = std::max(0, historyOffset_ - 30);
  build();
}
void App::archivesPage() {
  pageHeader("归档会话");
  historyRows_.clear();historyCaptions_.clear();archiveViewButtons_.clear();
  auto sessions = db_.sessions(archiveOffset_, true);
  if (sessions.empty())
    emptyState("没有归档会话", "长按历史对话可归档，在这里还原或删除。");
  // Keep archive titles above their actions on short landscape as well.
  const bool portrait=layout().category()!=LayoutClass::WideLandscape;
  int y = uiMetrics_.dp(6);
  for (const auto &session : sessions) {
    auto id = session["id"].get<std::string>();
    const int width=layout().contentWidth();
    auto holder=box(pageContent_,uiMetrics_.dp(10),y,portrait?width:width-uiMetrics_.dp(350),uiMetrics_.dp(44));
    lv_obj_set_scrollable(holder,false);lv_obj_set_clickable(holder,true);
    historyCaption(holder,session,0,9,portrait?width:width-350);
    auto title=historyCaptions_[id].title;
    struct Press {App *app;std::string id;lv_point_t start{};int distance=0;bool held=false;};
    auto data=new Press{this,id};
    lv_obj_add_event_cb(holder,[](lv_event_t *e) {
      auto p=static_cast<Press *>(lv_event_get_user_data(e));const auto code=lv_event_get_code(e);
      if(code==LV_EVENT_DELETE){delete p;return;}
      if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_LONG_PRESSED&&code!=LV_EVENT_CLICKED)return;
      auto indev=lv_indev_active();if(!indev)return;lv_point_t point;lv_indev_get_point(indev,&point);
      if(code==LV_EVENT_PRESSED){p->start=point;p->distance=0;p->held=false;}
      const int dx=point.x-p->start.x,dy=point.y-p->start.y;p->distance=std::max(p->distance,dx*dx+dy*dy);
      if(code==LV_EVENT_LONG_PRESSED&&!p->held&&p->distance<=144){p->held=true;p->app->events_.push("history_details",{{"id",p->id}});}
      if(code==LV_EVENT_CLICKED&&!p->held&&p->distance<=144)p->app->events_.push("archive_open",{{"id",p->id}});
    },LV_EVENT_ALL,data);
    archiveViewButtons_[id]=button(pageContent_,"查看",portrait?uiMetrics_.dp(10):width-uiMetrics_.dp(330),portrait?y+uiMetrics_.dp(48):y,portrait?(width-uiMetrics_.dp(16))/3:uiMetrics_.dp(100),uiMetrics_.dp(44),[this,id]{events_.push("archive_open",{{"id",id}});});
    if (id == archiveHighlight_)
      lv_obj_set_style_text_color(title, lv_color_hex(theme::palette().link), 0);
    historyRows_.push_back({id, title});
    button(pageContent_, "还原", portrait?uiMetrics_.dp(18)+(width-uiMetrics_.dp(16))/3:width-uiMetrics_.dp(220), portrait?y+uiMetrics_.dp(48):y, portrait?(width-uiMetrics_.dp(16))/3:uiMetrics_.dp(100), uiMetrics_.dp(44), [this, id] {
      db_.archiveSession(id, false);
      if (archiveOffset_ && db_.sessions(archiveOffset_, true).empty())
        archiveOffset_ = std::max(0, archiveOffset_ - 30);
      build();
    });
    auto remove = button(pageContent_, "删除", portrait?uiMetrics_.dp(26)+2*((width-uiMetrics_.dp(16))/3):width-uiMetrics_.dp(110), portrait?y+uiMetrics_.dp(48):y, portrait?(width-uiMetrics_.dp(16))/3:uiMetrics_.dp(100), uiMetrics_.dp(44),
           [this, id] { retireSession(id, false); });
    lv_obj_set_style_text_color(lv_obj_get_child(remove, 0), lv_color_hex(theme::palette().danger), 0);
    y += portrait?106:52;
  }
  if (archiveOffset_)
    button(pageContent_, "上一页", uiMetrics_.dp(10), y, uiMetrics_.dp(180), uiMetrics_.dp(44), [this] {
      archiveOffset_ = std::max(0, archiveOffset_ - 30);
      build();
    });
  if (sessions.size() == 30)
    button(pageContent_, "下一页", uiMetrics_.dp(220), y, uiMetrics_.dp(180), uiMetrics_.dp(44), [this] {
      archiveOffset_ += 30;
      build();
    });
}
void App::openArchiveReader(const std::string &id) {
  auto session=db_.sessionInfo(id);
  if(session.empty()||session.value("status",std::string())!="archived") {
    error("归档会话已不存在，请刷新列表。");return;
  }
  archiveListScroll_=body_?std::max(0,int(lv_obj_get_scroll_y(body_))):0;
  archiveReaderId_=id;archiveReaderOffset_=0;archiveReaderMessages_.clear();archiveReasoningOverrides_.clear();
  show("archive_chat");
}
void App::archiveReaderPage() {
  auto session=db_.sessionInfo(archiveReaderId_);
  pageHeader(session.empty()?"归档会话":session.value("title",std::string("归档会话")));
  reasoningHeaders_.clear();reasoningControls_.clear();expandedReasoning_.clear();archiveReaderMessages_.clear();
  lv_obj_set_flex_flow(pageContent_,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(pageContent_,uiMetrics_.dp(8),0);
  lv_obj_set_style_pad_row(pageContent_,uiMetrics_.dp(8),0);
  const int width=layout().portrait()?layout().contentWidth()-uiMetrics_.dp(16):screen_.width-uiMetrics_.dp(112);
  label(pageContent_,"已归档 · 仅浏览",0,0,width,uiMetrics_.fontSize(UiFont::FormHint),theme::palette().muted);
  if(session.empty()||session.value("status",std::string())!="archived") {
    label(pageContent_,"归档会话已不存在",0,0,width,uiMetrics_.fontSize(UiFont::Body));return;
  }
  const auto messages=db_.messages(archiveReaderId_,archiveReaderOffset_,30);
  for(const auto &m:messages)archiveReaderMessages_.push_back(m["id"].get<std::string>());
  reasoningDisplay_=db_.settings().value("reasoning_display",std::string("collapsed"));
  if(!messages.empty()&&!db_.query("SELECT id FROM chat_messages WHERE session_id=? AND seq<? LIMIT 1",{archiveReaderId_,messages.front()["seq"]}).empty())
    pageActions_["加载更早消息"]=button(pageContent_,"加载更早消息",0,0,width,uiMetrics_.dp(44),[this]{events_.push("archive_page",{{"id",archiveReaderId_},{"offset",archiveReaderOffset_+30}});});
  if(messages.empty())label(pageContent_,"没有聊天内容",0,0,width,uiMetrics_.fontSize(UiFont::Body),theme::palette().muted);
  renderMessageRows(pageContent_,messages,archiveReaderId_,width,true,0,INT64_MAX);
  if(archiveReaderOffset_>0)
    pageActions_["加载较新消息"]=button(pageContent_,"加载较新消息",0,0,width,uiMetrics_.dp(44),[this]{events_.push("archive_page",{{"id",archiveReaderId_},{"offset",std::max(0,archiveReaderOffset_-30)}});});
  lv_obj_add_event_cb(body_,[](lv_event_t *e){static_cast<App *>(lv_event_get_user_data(e))->paintVisible();},LV_EVENT_SCROLL,this);
  lv_obj_update_layout(body_);paintVisible();
}
} // namespace penchat
