#include "database.hpp"
#include "chat_time.hpp"
#include "context.hpp"
#include "reasoning.hpp"
#include "schema.hpp"
#include "titles.hpp"
#include "util.h"
#include <algorithm>
#include <dirent.h>
#include <sqlite3.h>
#include <stdexcept>
#include <unistd.h>
namespace penchat {
static std::string text(const Json &j) {
  return j.is_string() ? j.get<std::string>() : j.dump();
}
Database::Database(const std::string &path) {
  const auto slash = path.find_last_of('/');
  assetDirectory_ = join(
      slash == std::string::npos ? std::string(".") : path.substr(0, slash), "assets");
  if (sqlite3_open_v2(path.c_str(), &db_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK) {
    if (db_)
      sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error("Cannot open chat database");
  }
  sqlite3_busy_timeout(db_, 3000);
  char *err = nullptr;
  if (sqlite3_exec(db_, Schema, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string message = err ? err : "Schema error";
    sqlite3_free(err);
    sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error(message);
  }
  try {
    migrate(path);
  } catch (...) {
    for (auto &s : statements_)
      sqlite3_finalize(s.second);
    statements_.clear();
    sqlite3_close(db_);
    db_ = nullptr;
    throw;
  }
  worker_ = std::thread([this] {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(jobsMutex_);
        jobsReady_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (jobs_.empty() && stopping_)
          return;
        task = std::move(jobs_.front());
        jobs_.pop_front();
      }
      task();
    }
  });
  execute("INSERT OR IGNORE INTO schema_meta(key,value) "
          "VALUES('schema_version','2')");
  execute("UPDATE chat_messages SET status='interrupted' WHERE status='streaming'");
  {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    jobs_.push_back([this] {
      try {
        collectAssets(true);
      } catch (...) {
        saveEvents_.push("maintenance_error");
      }
    });
  }
  jobsReady_.notify_one();
}
Json Database::perform(std::function<Json()> task) {
  if (onWorker())
    return task();
  auto job = std::make_shared<std::packaged_task<Json()>>(std::move(task));
  auto result = job->get_future();
  {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    jobs_.push_back([job] { (*job)(); });
  }
  jobsReady_.notify_one();
  return result.get();
}
Database::~Database() {
  {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    stopping_ = true;
  }
  jobsReady_.notify_one();
  if (worker_.joinable())
    worker_.join();
  if (gcDirectory_)
    closedir(static_cast<DIR *>(gcDirectory_));
  for (auto &s : statements_)
    sqlite3_finalize(s.second);
  if (db_)
    sqlite3_close(db_);
}
Json Database::query(const std::string &sql, const Json &args) {
  return perform([this, sql, args] { return queryOnWorker(sql, args); });
}
Json Database::queryOnWorker(const std::string &sql, const Json &args) {
  if (!args.is_array())
    throw std::invalid_argument("SQL parameters must be an array");
  ++queryCount_;
  if (sql.find("BEGIN") == 0)
    ++transactionCount_;
  sqlite3_stmt *raw = nullptr;
  auto cached = statements_.find(sql);
  if (cached != statements_.end())
    raw = cached->second;
  else {
    if (statements_.size() >= 64) {
      auto i = statements_.begin();
      sqlite3_finalize(i->second);
      statements_.erase(i);
    }
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
      throw std::runtime_error(sqlite3_errmsg(db_));
    statements_[sql] = raw;
  }
  sqlite3_reset(raw);
  sqlite3_clear_bindings(raw);
  auto reset = [](sqlite3_stmt *s) {
    sqlite3_reset(s);
    sqlite3_clear_bindings(s);
  };
  std::unique_ptr<sqlite3_stmt, decltype(reset)> stmt(raw, reset);
  if (sql == "ROLLBACK") {
    settingsCache_ = modelsCache_ = Json();
    sessionCursors_.clear();
    messageCursors_.clear();
  }
  if (sqlite3_stmt_readonly(raw) == 0) {
    if (sql.find("app_settings") != std::string::npos)
      settingsCache_ = Json();
    if (sql.find("providers") != std::string::npos ||
        sql.find("model_configs") != std::string::npos)
      modelsCache_ = Json();
    if (sql.find("chat_sessions") != std::string::npos)
      sessionCursors_.clear();
    if (sql.find("DELETE FROM chat_messages") != std::string::npos)
      messageCursors_.clear();
  }
  for (size_t i = 0; i < args.size(); ++i) {
    const auto &a = args[i];
    if (a.is_null())
      sqlite3_bind_null(raw, i + 1);
    else if (a.is_number_integer())
      sqlite3_bind_int64(raw, i + 1, a.get<int64_t>());
    else if (a.is_boolean())
      sqlite3_bind_int(raw, i + 1, a.get<bool>());
    else {
      auto v = text(a);
      sqlite3_bind_text(raw, i + 1, v.data(), v.size(), SQLITE_TRANSIENT);
    }
  }
  Json rows = Json::array();
  int code;
  while ((code = sqlite3_step(raw)) == SQLITE_ROW) {
    Json row = Json::object();
    for (int i = 0; i < sqlite3_column_count(raw); ++i) {
      auto name = sqlite3_column_name(raw, i);
      switch (sqlite3_column_type(raw, i)) {
      case SQLITE_NULL:
        row[name] = nullptr;
        break;
      case SQLITE_INTEGER:
        row[name] = sqlite3_column_int64(raw, i);
        break;
      case SQLITE_FLOAT:
        row[name] = sqlite3_column_double(raw, i);
        break;
      default: {
        const char *p = reinterpret_cast<const char *>(sqlite3_column_text(raw, i));
        row[name] = std::string(p ? p : "", sqlite3_column_bytes(raw, i));
      }
      }
    }
    rows.push_back(std::move(row));
  }
  if (code != SQLITE_DONE)
    throw std::runtime_error(sqlite3_errmsg(db_));
  return rows;
}
void Database::execute(const std::string &sql, const Json &a) { query(sql, a); }
Json Database::settings() {
  if (!onWorker()) {
    return perform([=] { return settings(); });
  }
  if (!settingsCache_.is_null())
    return settingsCache_;
  Json out = Json::object();
  for (auto &r : query("SELECT key,value_json FROM app_settings")) {
    try {
      auto v = Json::parse(r["value_json"].get<std::string>());
      if (!v.is_null())
        out[r["key"].get<std::string>()] = v;
    } catch (...) {
    }
  }
  settingsCache_ = out;
  return out;
}
void Database::setting(const std::string &key, const Json &value) {
  if (!onWorker()) {
    perform([=] {
      setting(key, value);
      return Json();
    });
    return;
  }
  execute("INSERT OR REPLACE INTO app_settings(key,value_json,updated_at) "
          "VALUES(?,?,?)",
          {key, value.dump(), nowMs()});
}
void Database::queueSetting(const std::string &key, const Json &value) {
  {
    std::lock_guard<std::mutex> guard(jobsMutex_);
    if(stopping_||jobs_.size()>=512) {saveEvents_.push("setting_error",{{"key",key}});return;}
    jobs_.push_back([this,key,value] {
      try {setting(key,value);}catch(...){saveEvents_.push("setting_error",{{"key",key}});}
    });
  }
  jobsReady_.notify_one();
}
Json Database::sessions(int offset, bool archived) {
  if (!onWorker()) {
    return perform([=] { return sessions(offset, archived); });
  }
  const std::string key = archived ? "archive" : "active";
  auto &cursors = sessionCursors_[key];
  if (offset <= 0)
    cursors.clear();
  cursors[0] = Json();
  const int page = std::max(0, offset) / 30;
  auto nearest = cursors.upper_bound(page);
  --nearest;
  int position = nearest->first;
  Json cursor = nearest->second;
  while (position < page) {
    auto rows = sessionsAfter(cursor, archived);
    if (rows.size() < 30)
      return Json::array();
    const auto &last = rows.back();
    cursor = {{"pinned", last["pinned_at"]},
              {"time", last["updated_at"]},
              {"id", last["id"]}};
    cursors[++position] = cursor;
  }
  while (cursors.size() > 128) {
    auto low = std::next(cursors.begin()), high = std::prev(cursors.end());
    cursors.erase(page - low->first > high->first - page ? low : high);
  }
  return sessionsAfter(cursor, archived);
}
Json Database::sessionInfo(const std::string &id) {
  if (!onWorker()) {
    return perform([=] { return sessionInfo(id); });
  }
  auto rows = query("SELECT * FROM chat_sessions WHERE id=?", {id});
  return rows.empty() ? Json::object() : rows[0];
}
void Database::renameSession(const std::string &id, const std::string &title) {
  if (!onWorker()) {
    perform([=] {
      renameSession(id, title);
      return Json();
    });
    return;
  }
  auto session = sessionInfo(id);
  if (session.empty() || title.empty())
    return;
  auto metadata = objectField(session, "metadata_json");
  metadata["title_state"] = "manual";
  metadata.erase("title_token");
  execute("UPDATE chat_sessions SET title=?,metadata_json=? WHERE id=?",
          {title, metadata.dump(), id});
}
void Database::pinSession(const std::string &id, bool pinned) {
  if (!onWorker()) {
    perform([=] {
      pinSession(id, pinned);
      return Json();
    });
    return;
  }
  auto session = sessionInfo(id);
  if (session.empty())
    return;
  auto metadata = objectField(session, "metadata_json");
  if (pinned)
    metadata["pinned_at"] = nowMs();
  else
    metadata.erase("pinned_at");
  execute("UPDATE chat_sessions SET metadata_json=?,pinned_at=? WHERE id=?",
          {metadata.dump(), metadata.value("pinned_at", int64_t(0)), id});
}
void Database::archiveSession(const std::string &id, bool archived) {
  if (!onWorker()) {
    perform([=] {
      archiveSession(id, archived);
      return Json();
    });
    return;
  }
  execute("UPDATE chat_sessions SET status=? WHERE id=?",
          {archived ? "archived" : "active", id});
}
Json Database::prepareTitle(const std::string &id, const std::string &assistant,
                            const std::string &model) {
  if (!onWorker()) {
    return perform([=] { return prepareTitle(id, assistant, model); });
  }
  auto session = sessionInfo(id);
  if (session.empty())
    return Json::object();
  auto metadata = objectField(session, "metadata_json");
  if (metadata.contains("title_state"))
    return Json::object();
  auto first = query("SELECT id,status FROM chat_messages WHERE session_id=? "
                     "AND role='assistant' ORDER BY seq LIMIT 1",
                     {id});
  if (first.empty() || first[0]["id"] != assistant || first[0]["status"] != "complete")
    return Json::object();
  metadata["title_state"] = "pending";
  metadata["title_token"] = uid("title");
  metadata["title_model_id"] = model;
  metadata["title_assistant_id"] = assistant;
  execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?", {metadata.dump(), id});
  return {{"session", id},
          {"token", metadata["title_token"]},
          {"model_id", model},
          {"assistant", assistant}};
}
Json Database::pendingTitles() {
  if (!onWorker()) {
    return perform([=] { return pendingTitles(); });
  }
  Json jobs = Json::array();
  for (auto &session : query("SELECT id,metadata_json FROM chat_sessions")) {
    auto metadata = objectField(session, "metadata_json");
    auto state = metadata.value("title_state", std::string());
    if (state == "running") {
      // A request interrupted by process exit has an unknown outcome. Do not
      // automatically bill another request on restart.
      metadata["title_state"] = "failed";
      execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?",
              {metadata.dump(), session["id"]});
    } else if (state == "pending")
      jobs.push_back(
          {{"session", session["id"]},
           {"token", metadata.value("title_token", std::string())},
           {"model_id", metadata.value("title_model_id", std::string())},
           {"assistant", metadata.value("title_assistant_id", std::string())}});
  }
  return jobs;
}
bool Database::startTitle(const std::string &id, const std::string &token) {
  if (!onWorker()) {
    return perform([=] { return Json(startTitle(id, token)); }).get<bool>();
  }
  auto session = sessionInfo(id);
  if (session.empty())
    return false;
  auto metadata = objectField(session, "metadata_json");
  if (metadata.value("title_state", std::string()) != "pending" ||
      metadata.value("title_token", std::string()) != token)
    return false;
  metadata["title_state"] = "running";
  execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?", {metadata.dump(), id});
  return true;
}
bool Database::finishTitle(const std::string &id, const std::string &token,
                           const std::string &title) {
  if (!onWorker()) {
    return perform([=] { return Json(finishTitle(id, token, title)); }).get<bool>();
  }
  auto session = sessionInfo(id);
  if (session.empty())
    return false;
  auto metadata = objectField(session, "metadata_json");
  if (metadata.value("title_state", std::string()) != "running" ||
      metadata.value("title_token", std::string()) != token)
    return false;
  metadata["title_state"] = title.empty() ? "failed" : "complete";
  execute("UPDATE chat_sessions SET title=?,metadata_json=? WHERE id=?",
          {title.empty() ? session["title"] : Json(title), metadata.dump(), id});
  return !title.empty();
}
Json Database::messages(const std::string &s, int offset, int limit) {
  if (!onWorker()) {
    return perform([=] { return messages(s, offset, limit); });
  }
  limit = std::max(1, std::min(100, limit));
  const auto key = s + ":" + std::to_string(limit);
  if (messageCursors_.size() >= 8 && !messageCursors_.count(key))
    messageCursors_.erase(messageCursors_.begin());
  auto &cursors = messageCursors_[key];
  if (offset <= 0)
    cursors.clear();
  cursors[0] = INT64_MAX;
  const int page = std::max(0, offset) / limit;
  auto nearest = cursors.upper_bound(page);
  --nearest;
  int position = nearest->first;
  int64_t cursor = nearest->second;
  while (position < page) {
    auto rows = query("SELECT seq FROM chat_messages WHERE session_id=? AND seq<? "
                      "ORDER BY seq DESC LIMIT ?",
                      {s, cursor, limit});
    if (int(rows.size()) < limit)
      return Json::array();
    cursor = rows.back()["seq"];
    cursors[++position] = cursor;
  }
  while (cursors.size() > 128) {
    auto low = std::next(cursors.begin()), high = std::prev(cursors.end());
    cursors.erase(page - low->first > high->first - page ? low : high);
  }
  return messagesBefore(s, cursor, limit);
}
std::string Database::createSession() {
  if (!onWorker()) {
    return perform([=] { return Json(createSession()); }).get<std::string>();
  }
  auto id = uid("session");
  execute("INSERT INTO "
          "chat_sessions(id,title,status,created_at,updated_at,last_message_at) "
          "VALUES(?,?,'active',?,?,?)",
          {id, "新对话", nowMs(), nowMs(), 0});
  setting("active_session_id", id);
  return id;
}
void Database::removeSession(const std::string &s) {
  if (!onWorker()) {
    perform([=] {
      removeSession(s);
      return Json();
    });
    return;
  }
  auto scanAssets = query("SELECT stored_path,display_src FROM message_assets "
                          "WHERE session_id=?",
                          {s});
  execute("BEGIN IMMEDIATE");
  try {
    enqueueAssets(scanAssets);
    for (auto table :
         {"stream_events", "message_parts", "message_assets", "chat_messages"}) {
      if (std::string(table) == "message_assets")
        detachSharedAssets("session_id", s);
      execute(std::string("DELETE FROM ") + table + " WHERE session_id=?", {s});
    }
    execute("DELETE FROM chat_sessions WHERE id=?", {s});
    execute("COMMIT");
  } catch (...) {
    execute("ROLLBACK");
    throw;
  }
  gcCursor_.clear();
  deferAssetCollection();
}
bool Database::pendingImage(const std::string &session, const std::string &message) {
  if (!onWorker()) {
    return perform([=] { return Json(pendingImage(session, message)); }).get<bool>();
  }
  return !query("SELECT id FROM chat_messages WHERE id=? AND session_id=? "
                "AND role='user' AND message_type='image' AND "
                "coalesce(output_text,'')='' "
                "AND seq>(SELECT coalesce(max(seq),0) FROM chat_messages WHERE "
                "session_id=? AND role='assistant')",
                {message, session, session})
              .empty();
}
bool Database::removePendingImage(const std::string &session,
                                  const std::string &message) {
  if (!onWorker()) {
    return perform([=] { return Json(removePendingImage(session, message)); })
        .get<bool>();
  }
  execute("BEGIN IMMEDIATE");
  Json assets;
  try {
    if (!pendingImage(session, message)) {
      execute("ROLLBACK");
      return false;
    }
    assets = query("SELECT stored_path,display_src FROM message_assets WHERE "
                   "message_id=? AND session_id=?",
                   {message, session});
    enqueueAssets(assets);
    execute("DELETE FROM message_parts WHERE message_id=? AND session_id=?",
            {message, session});
    detachSharedAssets("message_id", message);
    execute("DELETE FROM message_assets WHERE message_id=? AND session_id=?",
            {message, session});
    execute("DELETE FROM chat_messages WHERE id=? AND session_id=?",
            {message, session});
    auto latest = query("SELECT output_text,message_type,created_at FROM chat_messages "
                        "WHERE session_id=? ORDER BY seq DESC LIMIT 1",
                        {session});
    if (latest.empty()) {
      execute("DELETE FROM chat_sessions WHERE id=?", {session});
      execute("DELETE FROM app_settings WHERE key='active_session_id' AND "
              "value_json=?",
              {Json(session).dump()});
    } else {
      std::string preview;
      if (latest[0]["message_type"] == "image")
        preview = "[图片]";
      else {
        int n = 0;
        for (auto &c : characters(latest[0].value("output_text", std::string()))) {
          if (++n > 40)
            break;
          preview += c;
        }
      }
      execute("UPDATE chat_sessions SET message_count=(SELECT count(*) FROM "
              "chat_messages WHERE "
              "session_id=?),preview_text=?,updated_at=? "
              "WHERE id=?",
              {session, preview, nowMs(), session});
    }
    execute("COMMIT");
  } catch (...) {
    execute("ROLLBACK");
    throw;
  }
  gcCursor_.clear();
  deferAssetCollection();
  return true;
}
std::string Database::append(const std::string &s, const std::string &role,
                             const std::string &content, const Json &image,
                             const Json &preferences) {
  if (!onWorker()) {
    return perform([=] { return Json(append(s, role, content, image, preferences)); })
        .get<std::string>();
  }
  auto id = uid("message");
  std::vector<std::string> createdAssets;
  const bool hasImage = image.contains("path");
  const auto asset = uid("asset");
  const std::string original =
      hasImage ? image["path"].get<std::string>() : std::string();
  std::string stored, preview;
  bool transaction = false;
  try {
    if (hasImage) {

      auto extension = camera::extensionOf(original);
      if (extension.empty())
        extension = "jpg";
      ensureDirectory(assetDirectory_);
      stored = join(assetDirectory_, asset + "." + extension);
      createdAssets.push_back(stored);
      perform([original, stored] {
        auto bytes = fileText(original, 16 * 1024 * 1024);
        if (bytes.empty())
          throw std::runtime_error("Cannot read attached image");
        atomicText(stored, bytes);
        return Json();
      });

      if (image.contains("previewPath")) {
        preview = join(assetDirectory_, asset + ".preview.png");
        createdAssets.push_back(preview);
        auto source = image["previewPath"].get<std::string>();
        perform([source, preview] {
          auto bytes = fileText(source, 1024 * 1024);
          if (bytes.empty())
            throw std::runtime_error("Cannot read scan preview");
          atomicText(preview, bytes);
          return Json();
        });
      }
    }
    execute("BEGIN IMMEDIATE");
    transaction = true;
    // A new UI conversation is transient until its first message is committed.
    // Persist its row and active selection in the same transaction as content.
    execute("INSERT OR IGNORE INTO chat_sessions"
            "(id,title,status,created_at,updated_at,last_message_at) "
            "VALUES(?,'新对话','active',?,?,?)",
            {s, nowMs(), nowMs(), 0});
    if (!preferences.empty())
      saveSessionPreferences(s, preferences);
    auto q = query("SELECT COALESCE(MAX(seq),0)+1 AS n FROM chat_messages "
                   "WHERE session_id=?",
                   {s});
    execute("INSERT INTO "
            "chat_messages(id,session_id,seq,role,status,message_type,created_"
            "at,updated_at,output_"
            "text,metadata_json) VALUES(?,?,?,?,?,?,?,?,?,?)",
            {id, s, q[0]["n"], role, role == "assistant" ? "streaming" : "complete",
             hasImage ? "image" : "text", nowMs(), nowMs(), content,
             Json{{"timeline",{{"version",1}}}}.dump()});
    if (hasImage) {
      execute("INSERT INTO "
              "message_assets(id,session_id,message_id,kind,source_type,"
              "original_path,stored_path,"
              "width,height,display_src,metadata_json,created_at) "
              "VALUES(?,?,?,'image',?,?,?,?,?,?,?,?)",
              {asset, s, id, image.value("source", std::string("camera")),
               (image.value("source", std::string()) == "scan" ||
                image.value("ephemeral", false))
                   ? Json(stored)
                   : image["path"],
               stored, image.value("width", 0), image.value("height", 0), preview,
               image.value("metadata", Json::object()).dump(), nowMs()});
      execute("INSERT INTO "
              "message_parts(id,session_id,message_id,part_index,part_type,"
              "asset_id) "
              "VALUES(?,?,?,0,'image',?)",
              {uid("part"), s, id, asset});
    } else
      execute("INSERT INTO "
              "message_parts(id,session_id,message_id,part_index,part_type,text) "
              "VALUES(?,?,?,0,'text',?)",
              {uid("part"), s, id, content});
    std::string preview;
    if (hasImage)
      preview = "[图片]";
    else {
      int n = 0;
      for (auto &character : characters(content)) {
        if (++n > 40)
          break;
        preview += character;
      }
    }
    execute("UPDATE chat_sessions SET "
            "updated_at=?,message_count=message_count+1,"
            "preview_text=?,title="
            "CASE WHEN title='新对话' AND ?='user' THEN ? ELSE title END WHERE "
            "id=?",
            {nowMs(), preview,
             objectField(sessionInfo(s), "metadata_json").contains("title_state")
                 ? std::string()
                 : role,
             preview, s});
    setting("active_session_id", s);
    execute("COMMIT");
  } catch (...) {
    if (transaction)
      execute("ROLLBACK");
    for (auto &path : createdAssets) {
      unlink(path.c_str());
      unlink((path + ".part").c_str());
    }
    throw;
  }
  return id;
}
void Database::updateMessage(const std::string &id, const std::string &t,
                             const std::string &r, const std::string &status,
                             const Json &error, const Json &metadata) {
  if (!onWorker()) {
    perform([=] {
      updateMessage(id, t, r, status, error, metadata);
      return Json();
    });
    return;
  }
  execute("BEGIN IMMEDIATE");
  try {
    auto rows = query("SELECT session_id,role,metadata_json FROM chat_messages WHERE id=?", {id});
    if (!rows.empty()) {
        auto merged = objectField(rows[0], "metadata_json");
        if(metadata.is_object())merged.update(metadata);
        auto timeline=merged.value("timeline",Json::object());
        if(status=="complete" && rows[0]["role"]=="assistant" && timeline.is_object() && timeline.value("version",0)==1) {
          auto completed=chatTimestamp(timeline,"completed_at");
          if(!completed)completed=nowMs();
          timeline["completed_at"]=completed;merged["timeline"]=timeline;
          recordActivity(rows[0]["session_id"],completed);
        }
        execute("UPDATE chat_messages SET metadata_json=? WHERE id=?",
                {merged.dump(), id});
    }
    execute("UPDATE chat_messages SET "
            "output_text=?,reasoning_text=?,status=?,error_json=?,updated_at=? "
            "WHERE id=?",
            {t, r, status, error.is_null() ? Json() : Json(error.dump()), nowMs(), id});
    execute("UPDATE message_parts SET text=? WHERE message_id=? AND part_type='text'",
            {t, id});
    execute("COMMIT");
  } catch (...) {
    execute("ROLLBACK");
    throw;
  }
}
Json Database::providers() {
  if (!onWorker()) {
    return perform([=] { return providers(); });
  }
  return query("SELECT * FROM providers WHERE enabled=1 ORDER BY created_at");
}
Json Database::providerForm(const std::string &id) {
  if (!onWorker()) {
    return perform([=] { return providerForm(id); });
  }
  auto rows = query("SELECT * FROM providers WHERE id=? AND enabled=1", {id});
  if (rows.empty())
    throw std::runtime_error("Provider no longer exists");
  const auto &p = rows[0];
  auto headers = objectField(p, "headers_json");
  auto metadata = objectField(p, "metadata_json");
  std::string names;
  for (const auto &m : query("SELECT name FROM model_configs WHERE "
                             "provider_id=? AND enabled=1 ORDER BY created_at",
                             {id})) {
    if (!names.empty())
      names += ", ";
    names += m["name"].get<std::string>();
  }
  return {{"id", id},
          {"name", p["name"]},
          {"baseURL", p["base_url"]},
          {"apiKey", p["api_key"].is_string() ? p["api_key"] : Json("")},
          {"headers", headers.is_object() ? headers : Json::object()},
          {"api", metadata.is_object() ? metadata.value("api", std::string("responses"))
                                       : "responses"},
          {"model", names},
          {"default_effort", metadata.value("default_effort", std::string())}};
}
Json Database::models() {
  if (!onWorker()) {
    return perform([=] { return models(); });
  }
  if (!modelsCache_.is_null())
    return modelsCache_;
  modelsCache_ =
      query("SELECT m.*,p.name AS "
            "provider_name,p.base_url,p.metadata_json AS "
            "provider_metadata FROM model_configs m JOIN providers p ON "
            "p.id=m.provider_id WHERE "
            "m.enabled=1 AND p.enabled=1 ORDER BY m.is_default DESC,m.created_at");
  return modelsCache_;
}
Json Database::activeModel() {
  if (!onWorker()) {
    return perform([=] { return activeModel(); });
  }
  auto m = models();
  return m.empty() ? Json::object() : m[0];
}
Json Database::sessionPreferences(const std::string &session) {
  if (!onWorker()) {
    return perform([=] { return sessionPreferences(session); });
  }
  auto rows = query("SELECT * FROM chat_sessions WHERE id=?", {session});
  if (rows.empty())
    return Json::object();
  auto preferences =
      objectField(rows[0], "metadata_json").value("chat_preferences", Json::object());
  if (!preferences.is_object())
    preferences = Json::object();
  if (rows[0]["default_model_config_id"].is_string())
    preferences["model_id"] = rows[0]["default_model_config_id"];
  return preferences;
}
void Database::saveSessionPreferences(const std::string &session,
                                      const Json &preferences) {
  if (!onWorker()) {
    perform([=] {
      saveSessionPreferences(session, preferences);
      return Json();
    });
    return;
  }
  auto rows = query("SELECT metadata_json FROM chat_sessions WHERE id=?", {session});
  if (rows.empty())
    return;
  auto metadata = objectField(rows[0], "metadata_json");
  metadata["chat_preferences"] = preferences;
  auto id = preferences.value("model_id", std::string());
  auto model = query("SELECT provider_id FROM model_configs WHERE id=?", {id});
  execute("UPDATE chat_sessions SET "
          "default_model_config_id=?,default_provider_id=?,metadata_json=? "
          "WHERE id=?",
          {id.empty() ? Json() : Json(id),
           model.empty() ? Json() : model[0]["provider_id"], metadata.dump(), session});
}
void Database::saveModelCapabilities(const std::string &id, const Json &reasoning,
                                     int budget) {
  if (!onWorker()) {
    perform([=] {
      saveModelCapabilities(id, reasoning, budget);
      return Json();
    });
    return;
  }
  auto rows = query("SELECT capabilities_json FROM model_configs WHERE id=?", {id});
  if (rows.empty())
    throw std::runtime_error("Model no longer exists");
  auto caps = objectField(rows[0], "capabilities_json");
  caps["reasoning"] = reasoning;
  if (budget > 0)
    caps["context_budget"] = std::max(1024, std::min(2097152, budget));
  execute("UPDATE model_configs SET capabilities_json=?,updated_at=? WHERE id=?",
          {caps.dump(), nowMs(), id});
}
void Database::recordActivity(const std::string &session,int64_t stamp) {
  auto row=sessionInfo(session);if(row.empty())return;
  auto meta=objectField(row,"metadata_json");
  meta["timeline"]={{"version",1},{"last_activity_at",stamp}};
  execute("UPDATE chat_sessions SET last_message_at=?,metadata_json=? WHERE id=?",{stamp,meta.dump(),session});
}
void Database::recordRequest(const std::string &message, const Json &model,int64_t sentAt,int64_t startedAt) {
  if(!sentAt)sentAt=nowMs();
  if(!startedAt)startedAt=nowMs();
  if (!onWorker()) {
    perform([=] {
      recordRequest(message, model,sentAt,startedAt);
      return Json();
    });
    return;
  }
  Json parameters = {{"model", model.value("name", std::string())},
                     {"effort", model.value("effective_effort", std::string())},
                     {"effort_value", model.value("effort_value", std::string())},
                     {"summary", model.value("reasoning_summary", false)},
                     {"system_prompt", model.value("system_prompt", std::string())}};
  execute("BEGIN IMMEDIATE");
  try {
    auto rows=query("SELECT session_id,seq,metadata_json FROM chat_messages WHERE id=?",{message});
    if(!rows.empty()) {
      auto session=rows[0]["session_id"];auto seq=rows[0]["seq"];
      auto users=query("SELECT id,metadata_json FROM chat_messages WHERE session_id=? AND role='user' AND seq<? AND seq>(SELECT coalesce(max(seq),0) FROM chat_messages WHERE session_id=? AND role='assistant' AND seq<?)",{session,seq,session,seq});
      for(auto &user:users) {
        auto meta=objectField(user,"metadata_json");auto time=meta.value("timeline",Json::object());
        // Only new tracked messages acquire their original send time; a retry
        // must never invent or replace the original time of an old message.
        if(time.is_object() && time.value("version",0)==1 && !chatTimestamp(time,"sent_at")) {
          time["sent_at"]=sentAt;meta["timeline"]=time;
          execute("UPDATE chat_messages SET metadata_json=? WHERE id=?",{meta.dump(),user["id"]});
        }
      }
      auto meta=objectField(rows[0],"metadata_json");meta["timeline"]={{"version",1},{"request_started_at",startedAt}};
      execute("UPDATE chat_messages SET provider_id=?,model_config_id=?,request_json=?,metadata_json=? WHERE id=?",
              {model["provider_id"],model["id"],parameters.dump(),meta.dump(),message});
      recordActivity(session,sentAt);
    }
    execute("COMMIT");
  } catch(...) {execute("ROLLBACK");throw;}
}
void Database::saveProvider(const Json &f, const std::vector<std::string> &names) {
  if (!onWorker()) {
    perform([=] {
      saveProvider(f, names);
      return Json();
    });
    return;
  }
  if (names.empty())
    throw std::runtime_error("At least one model is required");
  const bool editing =
      f.contains("id") && f["id"].is_string() && !f["id"].get<std::string>().empty();
  const auto id = editing ? f["id"].get<std::string>() : uid("provider");
  bool first = models().empty();
  execute("BEGIN IMMEDIATE");
  try {
    if (editing) {
      auto old = query("SELECT metadata_json,headers_json FROM providers WHERE "
                       "id=? AND enabled=1",
                       {id});
      if (old.empty())
        throw std::runtime_error("Provider no longer exists");
      auto metadata =
          Json::parse(old[0]["metadata_json"].get<std::string>(), nullptr, false);
      if (!metadata.is_object())
        metadata = Json::object();
      metadata["api"] = f.value("api", std::string("responses"));
      metadata["default_effort"] = f.value("default_effort", std::string());
      execute("UPDATE providers SET "
              "name=?,base_url=?,api_key=?,headers_json=?,metadata_json=?,updated_"
              "at=? WHERE id=?",
              {f.value("name", std::string("Provider")),
               f.value("baseURL", std::string()), f.value("apiKey", std::string()),
               f.contains("headers") ? f["headers"].dump()
                                     : old[0]["headers_json"].get<std::string>(),
               metadata.dump(), nowMs(), id});
    } else {
      execute("INSERT INTO "
              "providers(id,name,type,base_url,api_key,headers_json,metadata_"
              "json,created_at,updated_at,is_default) "
              "VALUES(?,?,'openai-compatible',?,?,?,?,?,?,?)",
              {id, f.value("name", std::string("Provider")),
               f.value("baseURL", std::string()), f.value("apiKey", std::string()),
               f.value("headers", Json::object()).dump(),
               Json({{"api", f.value("api", std::string("responses"))},
                     {"default_effort", f.value("default_effort", std::string())}})
                   .dump(),
               nowMs(), nowMs(), first ? 1 : 0});
    }
    for (const auto &m : query("SELECT id,name FROM model_configs WHERE "
                               "provider_id=? AND enabled=1",
                               {id}))
      if (std::find(names.begin(), names.end(), m["name"].get<std::string>()) ==
          names.end())
        execute("UPDATE model_configs SET enabled=0,is_default=0,updated_at=? "
                "WHERE id=?",
                {nowMs(), m["id"]});
    for (const auto &n : names) {
      auto existing =
          query("SELECT id FROM model_configs WHERE provider_id=? AND name=?", {id, n});
      if (existing.empty()) {
        execute("INSERT INTO "
                "model_configs(id,provider_id,name,display_name,model_type,created_"
                "at,updated_at,is_default) VALUES(?,?,?,?,'chat',?,?,?)",
                {uid("model"), id, n, n, nowMs(), nowMs(), first ? 1 : 0});
        first = false;
      } else
        execute("UPDATE model_configs SET enabled=1,updated_at=? WHERE id=?",
                {nowMs(), existing[0]["id"]});
    }
    if (query("SELECT m.id FROM model_configs m JOIN providers p ON "
              "p.id=m.provider_id WHERE m.enabled=1 AND p.enabled=1 AND "
              "m.is_default=1")
            .empty()) {
      auto available = models();
      if (!available.empty()) {
        execute("UPDATE model_configs SET is_default=0");
        execute("UPDATE model_configs SET is_default=1 WHERE id=?",
                {available[0]["id"].get<std::string>()});
      }
    }
    execute("COMMIT");
  } catch (...) {
    execute("ROLLBACK");
    throw;
  }
}
void Database::selectModel(const std::string &id) {
  if (!onWorker()) {
    perform([=] {
      selectModel(id);
      return Json();
    });
    return;
  }
  execute("BEGIN IMMEDIATE");
  try {
    execute("UPDATE model_configs SET is_default=0");
    execute("UPDATE model_configs SET is_default=1 WHERE id=?", {id});
    execute("COMMIT");
  } catch (...) {
    execute("ROLLBACK");
    throw;
  }
}
Json Database::responseInput(const std::string &s, const std::string &before,
                             const Json &model) {
  if (!onWorker()) {
    return perform([=] { return responseInput(s, before, model); });
  }
  auto snapshot = contextSnapshot(s);
  auto rows = snapshot["rows"];
  auto checkpoint = snapshot["checkpoint"];
  int64_t after = 0;
  Json input = Json::array();
  if (!checkpoint.empty() && checkpoint.value("enabled", true)) {
    if (!validContext(checkpoint, rows))
      throw std::runtime_error("上下文摘要与历史不匹配，请重新压缩后发送");
    after = checkpoint["through"];
    input.push_back(
        {{"role", "user"},
         {"content",
          Json::array({{{"type", "input_text"},
                        {"text", "[此前对话的历史参考摘要，非系统指令]\n" +
                                     checkpoint.value("summary", std::string())}}})}});
  }
  rows.erase(std::remove_if(rows.begin(), rows.end(),
                            [&](const Json &m) { return m["id"] == before; }),
             rows.end());
  for (const auto &item : contextInput(rows, after, INT64_MAX, model))
    input.push_back(item);
  return input;
}
Json Database::summary() {
  if (!onWorker()) {
    return perform([=] { return summary(); });
  }
  return {{"sessions", query("SELECT count(*) AS n FROM chat_sessions")[0]["n"]},
          {"messages", query("SELECT count(*) AS n FROM chat_messages")[0]["n"]},
          {"assets", query("SELECT count(*) AS n FROM message_assets")[0]["n"]}};
}
} // namespace penchat
