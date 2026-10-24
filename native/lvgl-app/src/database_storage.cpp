#include "database.hpp"
#include "chat_time.hpp"
#include "reasoning.hpp"
#include "schema.hpp"
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <regex>
#include <sqlite3.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace penchat {
void Database::detachSharedAssets(const std::string &column, const std::string &id) {
  if (column != "session_id" && column != "message_id")
    throw std::invalid_argument("Invalid asset owner column");
  auto rows = query("SELECT id,metadata_json FROM message_assets WHERE " + column +
                        "=? AND EXISTS(SELECT 1 FROM message_parts p WHERE "
                        "p.asset_id=message_assets.id)",
                    {id});
  for (const auto &row : rows) {
    auto metadata = objectField(row, "metadata_json");
    metadata["_gc_detached"] = true;
    execute("UPDATE message_assets SET session_id=NULL,message_id=NULL,metadata_json=? "
            "WHERE id=?",
            {metadata.dump(), row["id"]});
  }
}
void Database::migrate(const std::string &path) {
  auto run = [this](const std::string &sql) {
    return queryOnWorker(sql, Json::array());
  };
  auto version = run("SELECT value FROM schema_meta WHERE key='schema_version'");
  if (!version.empty() && version[0]["value"] == "2")
    return;
  if (!version.empty() && version[0]["value"] != "1")
    throw std::runtime_error("Unsupported database schema version");
  auto integrity = run("PRAGMA quick_check");
  if (integrity.size() != 1 || integrity[0].begin().value() != "ok")
    throw std::runtime_error("Database integrity check failed; upgrade cancelled");
  const std::vector<std::pair<std::string, std::string>> checks = {
      {"message sequence duplicates",
       "SELECT count(*) AS n FROM (SELECT 1 FROM chat_messages GROUP BY session_id,seq "
       "HAVING count(*)>1)"},
      {"message part duplicates",
       "SELECT count(*) AS n FROM (SELECT 1 FROM message_parts GROUP BY "
       "message_id,part_index HAVING count(*)>1)"},
      {"model name duplicates",
       "SELECT count(*) AS n FROM (SELECT 1 FROM model_configs GROUP BY "
       "provider_id,name HAVING count(*)>1)"},
      {"orphan messages",
       "SELECT count(*) AS n FROM chat_messages m WHERE NOT EXISTS(SELECT 1 FROM "
       "chat_sessions s WHERE s.id=m.session_id)"},
      {"orphan parts",
       "SELECT count(*) AS n FROM message_parts p WHERE NOT EXISTS(SELECT 1 FROM "
       "chat_messages m WHERE m.id=p.message_id AND m.session_id=p.session_id) OR "
       "(p.asset_id IS NOT NULL AND NOT EXISTS(SELECT 1 FROM message_assets a WHERE "
       "a.id=p.asset_id))"},
      {"orphan assets",
       "SELECT count(*) AS n FROM message_assets a WHERE (a.session_id IS NOT NULL AND "
       "NOT EXISTS(SELECT 1 FROM chat_sessions s WHERE s.id=a.session_id)) OR "
       "(a.message_id IS NOT NULL AND NOT EXISTS(SELECT 1 FROM chat_messages m WHERE "
       "m.id=a.message_id AND (a.session_id IS NULL OR a.session_id=m.session_id)))"},
      {"orphan stream events",
       "SELECT count(*) AS n FROM stream_events e WHERE NOT EXISTS(SELECT 1 FROM "
       "chat_messages m WHERE m.id=e.message_id AND m.session_id=e.session_id)"},
      {"orphan models", "SELECT count(*) AS n FROM model_configs m WHERE NOT "
                        "EXISTS(SELECT 1 FROM providers p WHERE p.id=m.provider_id)"}};
  for (const auto &check : checks) {
    auto n = run(check.second)[0]["n"].get<int64_t>();
    if (n)
      throw std::runtime_error("Database upgrade blocked: " + check.first + " (" +
                               std::to_string(n) + ")");
  }
  // Back up established databases with SQLite's consistent snapshot API.
  if (!version.empty() ||
      run("SELECT count(*) AS n FROM chat_sessions")[0]["n"].get<int64_t>()) {
    auto backupPath = path + ".backup-v1";
    if (access(backupPath.c_str(), F_OK) == 0)
      backupPath += "." + uid("retry");
    const auto temporary = backupPath + ".tmp";
    {
      sqlite3 *backup = nullptr;
      if (sqlite3_open(temporary.c_str(), &backup) != SQLITE_OK) {
        if (backup)
          sqlite3_close(backup);
        throw std::runtime_error("Cannot create migration backup");
      }
      auto copy = sqlite3_backup_init(backup, "main", db_, "main");
      int rc = copy ? sqlite3_backup_step(copy, -1) : SQLITE_ERROR;
      if (copy) {
        const int finished = sqlite3_backup_finish(copy);
        if (finished != SQLITE_OK)
          rc = finished;
      }
      sqlite3_close(backup);
      if (rc != SQLITE_DONE) {
        unlink(temporary.c_str());
        throw std::runtime_error("Cannot complete migration backup");
      }
      chmod(temporary.c_str(), 0600);
      int fd = open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
      const bool synced = fd >= 0 && fsync(fd) == 0;
      if (fd >= 0)
        close(fd);
      if (!synced)
        throw std::runtime_error("Cannot synchronize migration backup");
      if (rename(temporary.c_str(), backupPath.c_str()))
        throw std::runtime_error("Cannot publish migration backup");
      const auto slash = path.find_last_of('/');
      const auto directory =
          slash == std::string::npos ? std::string(".") : path.substr(0, slash);
      fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
      const bool published = fd >= 0 && fsync(fd) == 0;
      if (fd >= 0)
        close(fd);
      if (!published)
        throw std::runtime_error("Cannot synchronize migration backup directory");
    }
  }
  run("PRAGMA foreign_keys=OFF");
  run("BEGIN IMMEDIATE");
  try {
    run("ALTER TABLE chat_sessions ADD COLUMN pinned_at INTEGER NOT NULL DEFAULT 0");
    for (auto &row : run("SELECT id,metadata_json FROM chat_sessions")) {
      int64_t pinned = 0;
      try {
        auto metadata = Json::parse(row["metadata_json"].get<std::string>());
        if (metadata.contains("pinned_at") && metadata["pinned_at"].is_number_integer())
          pinned = std::max<int64_t>(0, metadata["pinned_at"]);
      } catch (...) {
      }
      queryOnWorker("UPDATE chat_sessions SET pinned_at=? WHERE id=?",
                    {pinned, row["id"]});
    }
    const std::vector<std::pair<std::string, std::string>> tables = {
        {"chat_messages", ",UNIQUE(session_id,seq),FOREIGN KEY(session_id) REFERENCES "
                          "chat_sessions(id) ON DELETE CASCADE"},
        {"message_parts",
         ",UNIQUE(message_id,part_index),FOREIGN KEY(session_id) REFERENCES "
         "chat_sessions(id) ON DELETE CASCADE,FOREIGN KEY(message_id) REFERENCES "
         "chat_messages(id) ON DELETE CASCADE,FOREIGN KEY(asset_id) REFERENCES "
         "message_assets(id)"},
        {"message_assets", ",FOREIGN KEY(session_id) REFERENCES chat_sessions(id) ON "
                           "DELETE CASCADE,FOREIGN KEY(message_id) REFERENCES "
                           "chat_messages(id) ON DELETE CASCADE"},
        {"stream_events", ",FOREIGN KEY(session_id) REFERENCES chat_sessions(id) ON "
                          "DELETE CASCADE,FOREIGN KEY(message_id) REFERENCES "
                          "chat_messages(id) ON DELETE CASCADE"},
        {"model_configs", ",UNIQUE(provider_id,name),FOREIGN KEY(provider_id) "
                          "REFERENCES providers(id)"}};
    for (const auto &entry : tables) {
      auto sql =
          queryOnWorker("SELECT sql FROM sqlite_master WHERE type='table' AND name=?",
                        {entry.first})[0]["sql"]
              .get<std::string>();
      const auto open = sql.find('('), close = sql.rfind(')');
      run("CREATE TABLE " + entry.first + "_v2 " + sql.substr(open, close - open) +
          entry.second + ")");
      run("INSERT INTO " + entry.first + "_v2 SELECT * FROM " + entry.first);
      run("DROP TABLE " + entry.first);
      run("ALTER TABLE " + entry.first + "_v2 RENAME TO " + entry.first);
#ifndef __aarch64__
      if (entry.first == "chat_messages" && std::getenv("PENCHAT_TEST_PAUSE_MIGRATION"))
        raise(SIGSTOP);
#endif
    }
    char *error = nullptr;
    if (sqlite3_exec(db_, Schema, nullptr, nullptr, &error) != SQLITE_OK) {
      std::string message = error ? error : "Migration index error";
      sqlite3_free(error);
      throw std::runtime_error(message);
    }
    run("CREATE INDEX idx_sessions_active_page ON chat_sessions(pinned_at "
        "DESC,updated_at DESC,id) WHERE status!='archived'");
    run("CREATE INDEX idx_sessions_archive_page ON chat_sessions(updated_at DESC,id) "
        "WHERE status='archived'");
    run("CREATE INDEX idx_parts_session ON message_parts(session_id)");
    run("CREATE INDEX idx_stream_session ON stream_events(session_id)");
    run("CREATE INDEX idx_assets_message ON message_assets(message_id)");
    run("CREATE INDEX idx_parts_asset ON message_parts(asset_id)");
    run("CREATE INDEX idx_assets_stored ON message_assets(stored_path)");
    run("CREATE INDEX idx_assets_display ON message_assets(display_src)");
    run("CREATE INDEX idx_assets_original ON message_assets(original_path)");
    run("CREATE TABLE asset_gc(path TEXT PRIMARY KEY NOT NULL,created_at INTEGER NOT "
        "NULL)");
    if (!run("PRAGMA foreign_key_check").empty())
      throw std::runtime_error("Database upgrade failed ownership validation");
    run("INSERT OR REPLACE INTO schema_meta(key,value) VALUES('schema_version','2')");
    run("COMMIT");
  } catch (...) {
    run("ROLLBACK");
    run("PRAGMA foreign_keys=ON");
    throw;
  }
  run("PRAGMA foreign_keys=ON");
  // DDL invalidates prepared statements; start with a clean bounded cache.
  for (auto &entry : statements_)
    sqlite3_finalize(entry.second);
  statements_.clear();
}
void Database::enqueueAssets(const Json &assets) {
  for (const auto &a : assets)
    for (const char *key : {"stored_path", "display_src"})
      if (a.contains(key) && a[key].is_string() && !a[key].get<std::string>().empty())
        execute("INSERT OR IGNORE INTO asset_gc(path,created_at) VALUES(?,?)",
                {a[key], nowMs()});
}
void Database::deferAssetCollection() {
  if (gcScheduled_)
    return;
  gcScheduled_ = true;
  std::lock_guard<std::mutex> lock(jobsMutex_);
  jobs_.push_back([this] {
    gcScheduled_ = false;
    try {
      collectAssets();
    } catch (...) {
      saveEvents_.push("maintenance_error");
    }
  });
  jobsReady_.notify_one();
}
void Database::collectAssets(bool discover) {
  if (assetLeases_) return;
  // Only immediate, generated children of the private assets directory qualify.
  const std::regex owned("asset_[0-9]+_[0-9]+\\.[A-Za-z0-9.]+(\\.part)?");
  const auto prefix = assetDirectory_ + "/";
  auto safe = [&](const std::string &path) {
    if (path.compare(0, prefix.size(), prefix) != 0)
      return false;
    auto leaf = path.substr(prefix.size());
    if (leaf.empty() || leaf == "." || leaf == ".." ||
        leaf.find('/') != std::string::npos)
      return false;
    struct stat info {};
    if (lstat(assetDirectory_.c_str(), &info) || !S_ISDIR(info.st_mode) ||
        S_ISLNK(info.st_mode))
      return false;
    return lstat(path.c_str(), &info) == 0
               ? S_ISREG(info.st_mode) && !S_ISLNK(info.st_mode)
               : errno == ENOENT;
  };
  auto referenced = [&](const std::string &path) {
    return !query("SELECT 1 FROM message_assets WHERE stored_path=? OR display_src=? "
                  "OR original_path=? LIMIT 1",
                  {path, path, path})
                .empty();
  };
  if (discover && !gcDirectory_)
    gcDirectory_ = opendir(assetDirectory_.c_str());
  if (gcDirectory_) {
    auto directory = static_cast<DIR *>(gcDirectory_);
    execute("BEGIN IMMEDIATE");
    try {
      for (int i = 0; i < 128; ++i) {
        auto entry = readdir(directory);
        if (!entry) {
          closedir(directory);
          gcDirectory_ = nullptr;
          break;
        }
        auto path = prefix + entry->d_name;
        if (std::regex_match(std::string(entry->d_name), owned) && safe(path) &&
            !referenced(path))
          execute("INSERT OR IGNORE INTO asset_gc VALUES(?,?)", {path, nowMs()});
      }
      execute("COMMIT");
    } catch (...) {
      execute("ROLLBACK");
      throw;
    }
  }
  auto detached = query(
      "SELECT id,stored_path,display_src FROM message_assets a WHERE session_id "
      "IS NULL AND message_id IS NULL AND metadata_json LIKE '%\"_gc_detached\":true%' "
      "AND NOT EXISTS(SELECT 1 FROM message_parts "
      "p WHERE p.asset_id=a.id) LIMIT 128");
  if (!detached.empty()) {
    execute("BEGIN IMMEDIATE");
    try {
      enqueueAssets(detached);
      for (const auto &a : detached)
        execute("DELETE FROM message_assets WHERE id=?", Json::array({a["id"]}));
      execute("COMMIT");
    } catch (...) {
      execute("ROLLBACK");
      throw;
    }
  }
  const auto batch = query(
      "SELECT path FROM asset_gc WHERE path>? ORDER BY path LIMIT 128", {gcCursor_});
  for (const auto &row : batch) {
    auto path = row["path"].get<std::string>();
    if (!safe(path) || referenced(path)) {
      execute("DELETE FROM asset_gc WHERE path=?", {path});
      continue;
    }
    if (unlink(path.c_str()) == 0 || errno == ENOENT)
      execute("DELETE FROM asset_gc WHERE path=?", {path});
  }
  if (!batch.empty())
    gcCursor_ = batch.back()["path"];
  // readdir order is not lexical: newly discovered paths can precede this page.
  if (gcDirectory_)
    gcCursor_.clear();
  if ((gcDirectory_ || batch.size() == 128 || detached.size() == 128) &&
      !gcScheduled_) {
    gcScheduled_ = true;
    std::lock_guard<std::mutex> lock(jobsMutex_);
    jobs_.push_back([this] {
      gcScheduled_ = false;
      try {
        collectAssets();
      } catch (...) {
        saveEvents_.push("maintenance_error");
      }
    });
    jobsReady_.notify_one();
  } else if (!gcDirectory_ && batch.size() < 128)
    gcCursor_.clear();
}
void Database::queueMessage(const std::string &session, const std::string &id,
                            int generation, uint64_t version, const std::string &text,
                            const std::string &reasoning, const std::string &status,
                            const Json &error, const Json &metadata) {
  std::lock_guard<std::mutex> lock(jobsMutex_);
  if (generation < latestSaveGeneration_)
    return;
  latestSaveGeneration_ = generation;
  const bool terminal = status != "streaming";
  auto found = saveVersions_.find(id);
  if (found != saveVersions_.end()) {
    auto old = found->second;
    if (generation < old["generation"].get<int>() ||
        (generation == old["generation"].get<int>() &&
         (version <= old["version"].get<uint64_t>() || old["terminal"].get<bool>())))
      return;
  }
  if (!scheduledSaves_.count(id) && !failedSaves_.count(id) &&
      scheduledSaves_.size() + failedSaves_.size() >= 16) {
    saveEvents_.push(
        "save_error",
        {{"message", id}, {"session", session}, {"error", "保存队列繁忙，请稍后重试"}});
    return;
  }
  while (saveVersions_.size() >= 64 && !saveVersions_.count(id)) {
    auto i = saveVersions_.begin();
    while (i != saveVersions_.end() && scheduledSaves_.count(i->first))
      ++i;
    if (i == saveVersions_.end())
      break;
    saveVersions_.erase(i);
  }
  saveVersions_[id] = {
      {"generation", generation}, {"version", version}, {"terminal", terminal}};
  auto savedMetadata=metadata.is_object()?metadata:Json::object();
  if(status=="complete") {
    auto time=savedMetadata.value("timeline",Json::object());
    if(!chatTimestamp(time,"completed_at"))time["completed_at"]=nowMs();
    time["version"]=1;savedMetadata["timeline"]=time;
  }
  pendingSaves_[id] = {
      {"session", session}, {"message", id},  {"generation", generation},
      {"version", version}, {"text", text},   {"reasoning", reasoning},
      {"status", status},   {"error", error}, {"metadata", savedMetadata}};
  if (scheduledSaves_.insert(id).second)
    jobs_.push_back([this, id] { savePending(id); });
  jobsReady_.notify_one();
}
void Database::savePending(const std::string &id) {
  Json snapshot;
  {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    snapshot = std::move(pendingSaves_[id]);
    pendingSaves_.erase(id);
  }
  try {
    updateMessage(id, snapshot["text"], snapshot["reasoning"], snapshot["status"],
                  snapshot["error"], snapshot["metadata"]);
    {
      std::lock_guard<std::mutex> lock(jobsMutex_);
      failedSaves_.erase(id);
    }
    if (snapshot["status"] != "streaming")
      saveEvents_.push("message_saved", {{"session", snapshot["session"]},
                                         {"message", id},
                                         {"generation", snapshot["generation"]},
                                         {"status", snapshot["status"]}});
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock(jobsMutex_);
      failedSaves_[id] = snapshot;
    }
    saveEvents_.push("save_error",
                     {{"session", snapshot["session"]},
                      {"message", id},
                      {"error", "保存失败，最新内容仍在内存中，请检查空间后重试保存"}});
  }
  std::lock_guard<std::mutex> lock(jobsMutex_);
  if (pendingSaves_.count(id))
    jobs_.push_back([this, id] { savePending(id); });
  else
    scheduledSaves_.erase(id);
  jobsReady_.notify_one();
}
void Database::retrySaves() {
  std::lock_guard<std::mutex> lock(jobsMutex_);
  for (const auto &entry : failedSaves_)
    if (!scheduledSaves_.count(entry.first)) {
      pendingSaves_[entry.first] = entry.second;
      scheduledSaves_.insert(entry.first);
      jobs_.push_back([this, id = entry.first] { savePending(id); });
    }
  jobsReady_.notify_one();
}
void Database::flush() {
  for (;;) {
    perform([] { return Json(); });
    std::lock_guard<std::mutex> lock(jobsMutex_);
    if (scheduledSaves_.empty())
      break;
  }
}
Json Database::storageStats() {
  return perform([this] {
    return Json{{"queries", queryCount_},
                {"transactions", transactionCount_},
                {"statements", statements_.size()},
                {"unsaved", pendingSaves_.size() + failedSaves_.size()}};
  });
}
Json Database::requestModel(const Json &model) {
  return perform([this, model] {
    Json result = model;
    auto rows = query(
        "SELECT api_key,base_url,headers_json FROM providers WHERE id=? AND enabled=1",
        {model.value("provider_id", std::string())});
    if (rows.empty())
      return Json::object();
    result["api_key"] = rows[0]["api_key"];
    result["base_url"] = rows[0]["base_url"];
    result["headers_json"] = rows[0]["headers_json"];
    return result;
  });
}
} // namespace penchat
namespace penchat {
Json Database::sessionsAfter(const Json &cursor, bool archived) {
  if (!onWorker())
    return perform([=] { return sessionsAfter(cursor, archived); });
  std::string sql =
      "SELECT * FROM chat_sessions s WHERE status " +
      std::string(archived ? "=" : "!=") +
      " 'archived' AND EXISTS(SELECT 1 FROM chat_messages m WHERE m.session_id=s.id)";
  Json args = Json::array();
  if (!cursor.is_null()) {
    if (archived) {
      sql += " AND (updated_at<? OR (updated_at=? AND id>?))";
      args = {cursor["time"], cursor["time"], cursor["id"]};
    } else {
      sql += " AND (pinned_at,updated_at)<=(?,?) AND (pinned_at<? OR (pinned_at=? AND "
             "updated_at<?) OR (pinned_at=? AND updated_at=? AND id>?))";
      args = {cursor["pinned"], cursor["time"],   cursor["pinned"], cursor["pinned"],
              cursor["time"],   cursor["pinned"], cursor["time"],   cursor["id"]};
    }
  }
  sql += archived ? " ORDER BY updated_at DESC,id LIMIT 30"
                  : " ORDER BY pinned_at DESC,updated_at DESC,id LIMIT 30";
  return query(sql, args);
}
Json Database::messagesBefore(const std::string &session, int64_t before, int limit) {
  if (!onWorker())
    return perform([=] { return messagesBefore(session, before, limit); });
  auto rows =
      query("SELECT m.*,a.stored_path,a.original_path,a.display_src,a.width,a.height "
            "FROM chat_messages m LEFT JOIN message_parts p ON p.message_id=m.id AND "
            "p.part_index=0 LEFT JOIN message_assets a ON a.id=p.asset_id WHERE "
            "m.session_id=? AND m.seq<? ORDER BY m.seq DESC LIMIT ?",
            {session, before, std::max(1, std::min(100, limit))});
  std::reverse(rows.begin(), rows.end());
  return rows;
}
} // namespace penchat
