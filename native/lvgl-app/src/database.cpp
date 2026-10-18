#include "database.hpp"
#include "reasoning.hpp"
#include "schema.hpp"
#include "titles.hpp"
#include "util.h"
#include <algorithm>
#include <sqlite3.h>
#include <stdexcept>
#include <unistd.h>
namespace penchat {
static std::string text(const Json &j) {
  return j.is_string() ? j.get<std::string>() : j.dump();
}
Database::Database(const std::string &path) {
  assetDirectory_ = join(path.substr(0, path.find_last_of('/')), "assets");
  if (sqlite3_open_v2(path.c_str(), &db_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      nullptr) != SQLITE_OK)
    throw std::runtime_error("Cannot open chat database");
  sqlite3_busy_timeout(db_, 3000);
  char *err = nullptr;
  if (sqlite3_exec(db_, Schema, nullptr, nullptr, &err) != SQLITE_OK) {
    std::string message = err ? err : "Schema error";
    sqlite3_free(err);
    throw std::runtime_error(message);
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
          "VALUES('schema_version','1')");
  execute(
      "UPDATE chat_messages SET status='interrupted' WHERE status='streaming'");
}
Json Database::perform(std::function<Json()> task) {
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
  if (db_)
    sqlite3_close(db_);
}
Json Database::query(const std::string &sql, const Json &args) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return perform([this, sql, args] { return queryOnWorker(sql, args); });
}
Json Database::queryOnWorker(const std::string &sql, const Json &args) {
  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
    throw std::runtime_error(sqlite3_errmsg(db_));
  std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(
      raw, sqlite3_finalize);
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
        const char *p =
            reinterpret_cast<const char *>(sqlite3_column_text(raw, i));
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
  Json out = Json::object();
  for (auto &r : query("SELECT key,value_json FROM app_settings")) {
    try {
      auto v = Json::parse(r["value_json"].get<std::string>());
      if (!v.is_null())
        out[r["key"].get<std::string>()] = v;
    } catch (...) {
    }
  }
  return out;
}
void Database::setting(const std::string &key, const Json &value) {
  execute("INSERT OR REPLACE INTO app_settings(key,value_json,updated_at) "
          "VALUES(?,?,?)",
          {key, value.dump(), nowMs()});
}
Json Database::sessions(int offset, bool archived) {
  auto rows = query(std::string("SELECT * FROM chat_sessions s WHERE status ") +
                    (archived ? "=" : "!=") +
                    " 'archived' AND EXISTS (SELECT 1 FROM chat_messages m "
                    "WHERE m.session_id=s.id) ORDER BY updated_at DESC,id");
  if (!archived)
    std::stable_sort(
        rows.begin(), rows.end(), [](const Json &a, const Json &b) {
          return objectField(a, "metadata_json")
                     .value("pinned_at", int64_t(0)) >
                 objectField(b, "metadata_json").value("pinned_at", int64_t(0));
        });
  Json page = Json::array();
  for (int i = std::max(0, offset); i < int(rows.size()) && i < offset + 30;
       ++i)
    page.push_back(rows[i]);
  return page;
}
Json Database::sessionInfo(const std::string &id) {
  auto rows = query("SELECT * FROM chat_sessions WHERE id=?", {id});
  return rows.empty() ? Json::object() : rows[0];
}
void Database::renameSession(const std::string &id, const std::string &title) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
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
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto session = sessionInfo(id);
  if (session.empty())
    return;
  auto metadata = objectField(session, "metadata_json");
  if (pinned)
    metadata["pinned_at"] = nowMs();
  else
    metadata.erase("pinned_at");
  execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?",
          {metadata.dump(), id});
}
void Database::archiveSession(const std::string &id, bool archived) {
  execute("UPDATE chat_sessions SET status=? WHERE id=?",
          {archived ? "archived" : "active", id});
}
Json Database::prepareTitle(const std::string &id, const std::string &assistant,
                            const std::string &model) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto session = sessionInfo(id);
  if (session.empty())
    return Json::object();
  auto metadata = objectField(session, "metadata_json");
  if (metadata.contains("title_state"))
    return Json::object();
  auto first = query("SELECT id,status FROM chat_messages WHERE session_id=? "
                     "AND role='assistant' ORDER BY seq LIMIT 1",
                     {id});
  if (first.empty() || first[0]["id"] != assistant ||
      first[0]["status"] != "complete")
    return Json::object();
  metadata["title_state"] = "pending";
  metadata["title_token"] = uid("title");
  metadata["title_model_id"] = model;
  metadata["title_assistant_id"] = assistant;
  execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?",
          {metadata.dump(), id});
  return {{"session", id},
          {"token", metadata["title_token"]},
          {"model_id", model},
          {"assistant", assistant}};
}
Json Database::pendingTitles() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
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
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto session = sessionInfo(id);
  if (session.empty())
    return false;
  auto metadata = objectField(session, "metadata_json");
  if (metadata.value("title_state", std::string()) != "pending" ||
      metadata.value("title_token", std::string()) != token)
    return false;
  metadata["title_state"] = "running";
  execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?",
          {metadata.dump(), id});
  return true;
}
bool Database::finishTitle(const std::string &id, const std::string &token,
                           const std::string &title) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto session = sessionInfo(id);
  if (session.empty())
    return false;
  auto metadata = objectField(session, "metadata_json");
  if (metadata.value("title_state", std::string()) != "running" ||
      metadata.value("title_token", std::string()) != token)
    return false;
  metadata["title_state"] = title.empty() ? "failed" : "complete";
  execute(
      "UPDATE chat_sessions SET title=?,metadata_json=? WHERE id=?",
      {title.empty() ? session["title"] : Json(title), metadata.dump(), id});
  return !title.empty();
}
Json Database::messages(const std::string &s, int offset, int limit) {
  auto rows = query(
      "SELECT m.*,a.stored_path,a.original_path,a.display_src,a.width,a.height "
      "FROM chat_messages m LEFT "
      "JOIN message_parts p ON p.message_id=m.id AND "
      "p.part_index=0 LEFT JOIN message_assets "
      "a ON a.id=p.asset_id WHERE m.session_id=? ORDER BY m.seq "
      "DESC LIMIT ? OFFSET ?",
      {s, limit, offset});
  std::reverse(rows.begin(), rows.end());
  return rows;
}
std::string Database::createSession() {
  auto id = uid("session");
  execute(
      "INSERT INTO "
      "chat_sessions(id,title,status,created_at,updated_at,last_message_at) "
      "VALUES(?,?,'active',?,?,?)",
      {id, "新对话", nowMs(), nowMs(), nowMs()});
  setting("active_session_id", id);
  return id;
}
void Database::removeSession(const std::string &s) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto scanAssets = query("SELECT stored_path,display_src FROM message_assets "
                          "WHERE session_id=? AND COALESCE(display_src,'')<>''",
                          {s});
  execute("BEGIN IMMEDIATE");
  try {
    for (auto table :
         {"stream_events", "message_parts", "message_assets", "chat_messages"})
      execute(std::string("DELETE FROM ") + table + " WHERE session_id=?", {s});
    execute("DELETE FROM chat_sessions WHERE id=?", {s});
    execute("COMMIT");
  } catch (...) {
    execute("ROLLBACK");
    throw;
  }
  // Only remove app-owned scan files after the SQL transaction commits.
  // Original camera/album paths outside the asset directory are untouched.
  for (auto &asset : scanAssets)
    for (const char *key : {"stored_path", "display_src"}) {
      if (!asset[key].is_string())
        continue;
      auto path = asset[key].get<std::string>();
      auto prefix = assetDirectory_ + "/";
      if (path.find(prefix) == 0 &&
          path.find('/', prefix.size()) == std::string::npos)
        perform([path] {
          unlink(path.c_str());
          return Json();
        });
    }
}
std::string Database::append(const std::string &s, const std::string &role,
                             const std::string &content, const Json &image,
                             const Json &preferences) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto id = uid("message");
  std::vector<std::string> createdAssets;
  execute("BEGIN IMMEDIATE");
  try {
    // A new UI conversation is transient until its first message is committed.
    // Persist its row and active selection in the same transaction as content.
    execute("INSERT OR IGNORE INTO chat_sessions"
            "(id,title,status,created_at,updated_at,last_message_at) "
            "VALUES(?,'新对话','active',?,?,?)",
            {s, nowMs(), nowMs(), nowMs()});
    if (!preferences.empty())
      saveSessionPreferences(s, preferences);
    auto q = query("SELECT COALESCE(MAX(seq),0)+1 AS n FROM chat_messages "
                   "WHERE session_id=?",
                   {s});
    bool hasImage = image.contains("path");
    execute("INSERT INTO "
            "chat_messages(id,session_id,seq,role,status,message_type,created_"
            "at,updated_at,output_"
            "text) VALUES(?,?,?,?,?,?,?,?,?)",
            {id, s, q[0]["n"], role,
             role == "assistant" ? "streaming" : "complete",
             hasImage ? "image" : "text", nowMs(), nowMs(), content});
    if (hasImage) {
      auto asset = uid("asset");
      auto original = image["path"].get<std::string>();
      auto extension = camera::extensionOf(original);
      if (extension.empty())
        extension = "jpg";
      ensureDirectory(assetDirectory_);
      auto stored = join(assetDirectory_, asset + "." + extension);
      createdAssets.push_back(stored);
      perform([original, stored] {
        auto bytes = fileText(original, 16 * 1024 * 1024);
        if (bytes.empty())
          throw std::runtime_error("Cannot read attached image");
        atomicText(stored, bytes);
        return Json();
      });
      std::string preview;
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
      execute("INSERT INTO "
              "message_assets(id,session_id,message_id,kind,source_type,"
              "original_path,stored_path,"
              "width,height,display_src,metadata_json,created_at) "
              "VALUES(?,?,?,'image',?,?,?,?,?,?,?,?)",
              {asset, s, id, image.value("source", std::string("camera")),
               image.value("source", std::string()) == "scan" ? Json(stored)
                                                              : image["path"],
               stored, image.value("width", 0), image.value("height", 0),
               preview, image.value("metadata", Json::object()).dump(),
               nowMs()});
      execute("INSERT INTO "
              "message_parts(id,session_id,message_id,part_index,part_type,"
              "asset_id) "
              "VALUES(?,?,?,0,'image',?)",
              {uid("part"), s, id, asset});
    } else
      execute(
          "INSERT INTO "
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
    execute(
        "UPDATE chat_sessions SET "
        "updated_at=?,last_message_at=?,message_count=message_count+1,"
        "preview_text=?,title="
        "CASE WHEN title='新对话' AND ?='user' THEN ? ELSE title END WHERE "
        "id=?",
        {nowMs(), nowMs(), preview,
         objectField(sessionInfo(s), "metadata_json").contains("title_state")
             ? std::string()
             : role,
         preview, s});
    setting("active_session_id", s);
    execute("COMMIT");
  } catch (...) {
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
  if (!metadata.empty()) {
    auto rows =
        query("SELECT metadata_json FROM chat_messages WHERE id=?", {id});
    if (!rows.empty()) {
      auto merged = objectField(rows[0], "metadata_json");
      merged.update(metadata);
      execute("UPDATE chat_messages SET metadata_json=? WHERE id=?",
              {merged.dump(), id});
    }
  }
  execute("UPDATE chat_messages SET "
          "output_text=?,reasoning_text=?,status=?,error_json=?,updated_at=? "
          "WHERE id=?",
          {t, r, status, error.is_null() ? Json() : Json(error.dump()), nowMs(),
           id});
  execute(
      "UPDATE message_parts SET text=? WHERE message_id=? AND part_type='text'",
      {t, id});
}
Json Database::providers() {
  return query("SELECT * FROM providers WHERE enabled=1 ORDER BY created_at");
}
Json Database::providerForm(const std::string &id) {
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
          {"api", metadata.is_object()
                      ? metadata.value("api", std::string("responses"))
                      : "responses"},
          {"model", names},
          {"default_effort", metadata.value("default_effort", std::string())}};
}
Json Database::models() {
  return query(
      "SELECT m.*,p.name AS "
      "provider_name,p.base_url,p.api_key,p.headers_json,p.metadata_json AS "
      "provider_metadata FROM model_configs m JOIN providers p ON "
      "p.id=m.provider_id WHERE "
      "m.enabled=1 AND p.enabled=1 ORDER BY m.is_default DESC,m.created_at");
}
Json Database::activeModel() {
  auto m = models();
  return m.empty() ? Json::object() : m[0];
}
Json Database::sessionPreferences(const std::string &session) {
  auto rows = query("SELECT * FROM chat_sessions WHERE id=?", {session});
  if (rows.empty())
    return Json::object();
  auto preferences = objectField(rows[0], "metadata_json")
                         .value("chat_preferences", Json::object());
  if (!preferences.is_object())
    preferences = Json::object();
  if (rows[0]["default_model_config_id"].is_string())
    preferences["model_id"] = rows[0]["default_model_config_id"];
  return preferences;
}
void Database::saveSessionPreferences(const std::string &session,
                                      const Json &preferences) {
  auto rows =
      query("SELECT metadata_json FROM chat_sessions WHERE id=?", {session});
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
           model.empty() ? Json() : model[0]["provider_id"], metadata.dump(),
           session});
}
void Database::saveModelCapabilities(const std::string &id,
                                     const Json &reasoning) {
  auto rows =
      query("SELECT capabilities_json FROM model_configs WHERE id=?", {id});
  if (rows.empty())
    throw std::runtime_error("Model no longer exists");
  auto caps = objectField(rows[0], "capabilities_json");
  caps["reasoning"] = reasoning;
  execute(
      "UPDATE model_configs SET capabilities_json=?,updated_at=? WHERE id=?",
      {caps.dump(), nowMs(), id});
}
void Database::recordRequest(const std::string &message, const Json &model) {
  Json parameters = {
      {"model", model.value("name", std::string())},
      {"effort", model.value("effective_effort", std::string())},
      {"effort_value", model.value("effort_value", std::string())},
      {"summary", model.value("reasoning_summary", false)}};
  execute("UPDATE chat_messages SET "
          "provider_id=?,model_config_id=?,request_json=? WHERE id=?",
          {model["provider_id"], model["id"], parameters.dump(), message});
}
void Database::saveProvider(const Json &f,
                            const std::vector<std::string> &names) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (names.empty())
    throw std::runtime_error("At least one model is required");
  const bool editing = f.contains("id") && f["id"].is_string() &&
                       !f["id"].get<std::string>().empty();
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
      auto metadata = Json::parse(old[0]["metadata_json"].get<std::string>(),
                                  nullptr, false);
      if (!metadata.is_object())
        metadata = Json::object();
      metadata["api"] = f.value("api", std::string("responses"));
      metadata["default_effort"] = f.value("default_effort", std::string());
      execute(
          "UPDATE providers SET "
          "name=?,base_url=?,api_key=?,headers_json=?,metadata_json=?,updated_"
          "at=? WHERE id=?",
          {f.value("name", std::string("Provider")),
           f.value("baseURL", std::string()), f.value("apiKey", std::string()),
           f.contains("headers") ? f["headers"].dump()
                                 : old[0]["headers_json"].get<std::string>(),
           metadata.dump(), nowMs(), id});
    } else {
      execute(
          "INSERT INTO "
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
          query("SELECT id FROM model_configs WHERE provider_id=? AND name=?",
                {id, n});
      if (existing.empty()) {
        execute(
            "INSERT INTO "
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
  std::lock_guard<std::recursive_mutex> lock(mutex_);
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
  auto rows = messages(s, 0, 80);
  Json input = Json::array();
  for (const auto &m : rows) {
    if (m["id"] == before)
      continue;
    auto role = m.value("role", std::string());
    if (role != "user" && role != "assistant")
      continue;
    std::string content = m["output_text"].is_string()
                              ? m["output_text"].get<std::string>()
                              : std::string();
    if (!model.empty() &&
        objectField(model, "provider_metadata").value("api", std::string()) !=
            "chat" &&
        m["provider_id"] == model.value("provider_id", std::string()) &&
        m["model_config_id"] == model.value("id", std::string())) {
      auto metadata = objectField(m, "metadata_json");
      for (const auto &item :
           metadata.value("response_reasoning_items", Json::array()))
        if (item.is_object() &&
            item.value("type", std::string()) == "reasoning" &&
            item.value("status", std::string("completed")) == "completed")
          input.push_back(item);
    }
    Json parts = Json::array();
    if (!content.empty())
      parts.push_back(
          {{"type", role == "assistant" ? "output_text" : "input_text"},
           {"text", content}});
    std::string imagePath;
    if (m.contains("stored_path") && m["stored_path"].is_string())
      imagePath = m["stored_path"];
    if (imagePath.empty() && m.contains("original_path") &&
        m["original_path"].is_string())
      imagePath = m["original_path"];
    if (!imagePath.empty())
      parts.push_back({{"type", "input_image"}, {"image_url", imagePath}});
    if (parts.size() == 1 &&
        parts[0].value("type", std::string()) == "input_image")
      parts.insert(parts.begin(), Json{{"type", "input_text"},
                                       {"text", "请根据这张图片回答。"}});
    if (!parts.empty())
      input.push_back({{"role", role}, {"content", parts}});
  }
  return input;
}
Json Database::summary() {
  return {
      {"sessions", query("SELECT count(*) AS n FROM chat_sessions")[0]["n"]},
      {"messages", query("SELECT count(*) AS n FROM chat_messages")[0]["n"]},
      {"assets", query("SELECT count(*) AS n FROM message_assets")[0]["n"]}};
}
} // namespace penchat
