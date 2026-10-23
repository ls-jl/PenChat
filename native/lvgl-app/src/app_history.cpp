#include "app.hpp"
#include "titles.hpp"
#include <algorithm>
namespace penchat {
int App::titleWidth() const {
  return (screen_.width * 2 / 3 - 24 - 48) * 4 / 5;
}
int App::titleLimit() const {
  lv_point_t size;
  lv_text_get_size(&size, "汉", font(20), 0, 0, LV_COORD_MAX,
                   LV_TEXT_FLAG_NONE);
  return std::max(1, titleWidth() / std::max(1, int(size.x)));
}
std::string App::fitTitle(const std::string &raw) const {
  auto title = plainTitle(raw, titleLimit());
  auto parts = characters(title);
  while (!parts.empty()) {
    lv_point_t size;
    lv_text_get_size(&size, title.c_str(), font(20), 0, 0, LV_COORD_MAX,
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
lv_obj_t *App::historyRow(lv_obj_t *parent, const Json &session, int y,
                          int width) {
  const auto id = session["id"].get<std::string>();
  auto row = button(
      parent, fitTitle(session.value("title", std::string("对话"))), 0, y,
      width, 44,
      [this, id] {
        if (longPressedSession_ == id)
          return;
        if (id != session_) {
          stopNetwork();
          session_ = id;
          sessionPreferences_ = db_.sessionPreferences(id);
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
      id == session_ ? theme::surface : theme::sidebar, LV_TEXT_ALIGN_LEFT);
  auto caption = lv_obj_get_child(row, 0);
  lv_obj_set_width(caption, width - 48);
  lv_obj_align(caption, LV_ALIGN_LEFT_MID, 24, 0);
  if (objectField(session, "metadata_json").value("pinned_at", int64_t(0)) >
      0) {
    icon(row, "pin", width - 34, 8, 28, 28, {},
         id == session_ ? theme::surface : theme::sidebar, theme::muted);
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
void App::closeHistoryContext() {
  auto object = modal_;
  modal_ = nullptr;
  historyContextId_.clear();
  navigationPointerDown_ = navigationBackdrop_ = false;
  if (object)
    lv_obj_delete(object);
}
void App::historyContext(const std::string &id) {
  auto session = db_.sessionInfo(id);
  if (session.empty())
    return;
  closeHistoryContext();
  historyContextId_ = id;
  modal_ = box(screenObj_, 0, 0, screen_.width, screen_.height, 0x000000);
  lv_obj_set_style_bg_opa(modal_, 100, 0);
  lv_obj_set_scrollable(modal_, false);
  lv_obj_set_clickable(modal_, true);
  lv_obj_add_event_cb(
      modal_,
      [](lv_event_t *e) {
        auto app = static_cast<App *>(lv_event_get_user_data(e));
        if (lv_event_get_code(e) == LV_EVENT_DELETE) {
          app->modal_ = nullptr;
          app->historyContextId_.clear();
        } else if (lv_event_get_target(e) == app->modal_)
          app->closeHistoryContext();
      },
      LV_EVENT_CLICKED, this);
  auto panel = box(modal_, 32, 72, 720, 112, theme::surface);
  lv_obj_set_scrollable(panel, false);
  lv_obj_set_style_radius(panel, 16, 0);
  auto title =
      label(panel, session.value("title", std::string()), 16, 10, 688, 21);
  lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(title, 28);
  button(panel, "重命名", 12, 50, 132, 50, [this, id, session] {
    closeHistoryContext();
    edit(
        session.value("title", std::string()), "重命名会话",
        [this, id](const std::string &value) {
          auto title = fitTitle(value);
          if (!title.empty()) {
            if (titleSession_ == id)
              titleCancel_ = true;
            db_.renameSession(id, title);
          }
          toggleNavigation();
        },
        titleLimit(), "ZhCNPreferred", [this] { toggleNavigation(); });
  });
  bool pinned =
      objectField(session, "metadata_json").value("pinned_at", int64_t(0)) > 0;
  button(panel, pinned ? "取消置顶" : "置顶", 152, 50, 132, 50,
         [this, id, pinned] {
           closeHistoryContext();
           db_.pinSession(id, !pinned);
           historyOffset_ = 0;
           navigationScroll_ = 0;
           if (navigationList_)
             lv_obj_scroll_to_y(navigationList_, 0, LV_ANIM_OFF);
           historyDrawer();
         });
  button(panel, "归档", 292, 50, 132, 50, [this, id] {
    closeHistoryContext();
    retireSession(id, true);
  });
  auto remove = button(panel, "删除", 432, 50, 132, 50, [this, id] {
    closeHistoryContext();
    retireSession(id, false);
  });
  lv_obj_set_style_text_color(lv_obj_get_child(remove, 0),
                              lv_color_hex(theme::danger), 0);
  button(panel, "导出", 572, 50, 132, 50, [this, id] {
    events_.push("transfer_open_session", {{"id", id}});
  });
}
void App::retireSession(const std::string &id, bool archive) {
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
  historyRows_.clear();
  auto sessions = db_.sessions(archiveOffset_, true);
  if (sessions.empty())
    emptyState("没有归档会话", "长按历史对话可归档，在这里还原或删除。");
  int y = 6;
  for (const auto &session : sessions) {
    auto id = session["id"].get<std::string>();
    auto title = label(body_, fitTitle(session.value("title", std::string())),
                       10, y + 10, 570, 20);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(title, 26);
    historyRows_.push_back({id, title});
    button(body_, "还原", 590, y, 100, 44, [this, id] {
      db_.archiveSession(id, false);
      if (archiveOffset_ && db_.sessions(archiveOffset_, true).empty())
        archiveOffset_ = std::max(0, archiveOffset_ - 30);
      build();
    });
    auto remove = button(body_, "删除", 710, y, 100, 44,
           [this, id] { retireSession(id, false); });
    lv_obj_set_style_text_color(lv_obj_get_child(remove, 0), lv_color_hex(theme::danger), 0);
    y += 52;
  }
  if (archiveOffset_)
    button(body_, "上一页", 10, y, 180, 44, [this] {
      archiveOffset_ = std::max(0, archiveOffset_ - 30);
      build();
    });
  if (sessions.size() == 30)
    button(body_, "下一页", 220, y, 180, 44, [this] {
      archiveOffset_ += 30;
      build();
    });
}
} // namespace penchat
