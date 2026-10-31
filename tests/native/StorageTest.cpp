#include "database.hpp"
#include "schema.hpp"
#include <cassert>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <set>
#include <signal.h>
#include <sqlite3.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace penchat;
static void sql(sqlite3 *db, const std::string &s) {
  char *error = nullptr;
  if (sqlite3_exec(db, s.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
    std::string m = error ? error : "SQL failure";
    sqlite3_free(error);
    throw std::runtime_error(m);
  }
}
static void seed(const std::string &path, int sessions = 1, int messages = 1) {
  sqlite3 *db = nullptr;
  assert(sqlite3_open(path.c_str(), &db) == SQLITE_OK);
  sql(db, Schema);
  sql(db, "INSERT INTO schema_meta VALUES('schema_version','1')");
  sql(db, "BEGIN");
  for (int s = 0; s < sessions; ++s) {
    auto id = "s" + std::to_string(s);
    sql(db, "INSERT INTO "
            "chat_sessions(id,title,status,created_at,updated_at,last_message_at,"
            "metadata_json) VALUES('" +
                id + "','fixture','active',1," + std::to_string(s) +
                ",1,'{\"pinned_at\":" + std::to_string(s == 2 ? 100 : 0) + "}')");
    for (int m = 0; m < messages; ++m)
      sql(db, "INSERT INTO "
              "chat_messages(id,session_id,seq,role,status,message_type,created_at,"
              "updated_at,output_text) VALUES('" +
                  id + "m" + std::to_string(m) + "','" + id + "'," +
                  std::to_string(m + 1) + ",'user','complete','text',1,1,'fixture')");
  }
  sql(db, "COMMIT");
  sqlite3_close(db);
}
static int entries(const char *path) {
  auto *directory = opendir(path);
  assert(directory);
  int count = 0;
  while (readdir(directory)) ++count;
  closedir(directory);
  return count;
}
static void startupFailures(const std::string &root) {
  for (const auto &table : {"schema_meta", "chat_messages"}) {
    const auto path = root + "/startup-" + table + ".db";
    {
      Database db(path);
      const auto session = db.createSession();
      db.append(session, "assistant", "partial");
      db.execute("UPDATE chat_messages SET status='streaming'");
    }
    sqlite3 *raw = nullptr;
    assert(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
    sql(raw, std::string("CREATE TRIGGER fail_start BEFORE ") +
        (std::string(table) == "schema_meta" ? "INSERT ON " : "UPDATE ON ") +
        table + " BEGIN SELECT RAISE(ABORT,'fixture startup failure'); END");
    sqlite3_close(raw);
    const auto child = fork();
    assert(child >= 0);
    if (child == 0) {
      const int threads = entries("/proc/self/task"), fds = entries("/proc/self/fd");
      for (int attempt = 0; attempt < 20; ++attempt) {
        bool caught = false;
        try { Database invalid(path); }
        catch (const std::exception &e) {
          caught = std::string(e.what()) == "fixture startup failure";
        }
        assert(caught);
        assert(entries("/proc/self/task") == threads);
        assert(entries("/proc/self/fd") == fds);
      }
      _exit(0);
    }
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    assert(sqlite3_open(path.c_str(), &raw) == SQLITE_OK);
    sql(raw, "DROP TRIGGER fail_start");
    sqlite3_close(raw);
    Database recovered(path);
    assert(recovered.query("SELECT status FROM chat_messages")[0]["status"] == "interrupted");
  }
}
int main() {
  char tmp[] = "/tmp/penchat-storage-XXXXXX";
  auto root = std::string(mkdtemp(tmp));
  startupFailures(root);
  auto path = root + "/penchat.db";
  seed(path, 1000, 100);
  auto begin = ticks();
  {
    Database db(path);
    assert(
        db.query(
            "SELECT value FROM schema_meta WHERE key='schema_version'")[0]["value"] ==
        "2");
    assert(access((path + ".backup-v1").c_str(), F_OK) == 0);
    assert(db.query("PRAGMA foreign_key_check").empty());
    auto migrated = ticks() - begin;
    begin = ticks();
    for (int sample = 0; sample < 10; ++sample) {
      auto old = db.query(
          "SELECT * FROM chat_sessions s WHERE status!='archived' AND EXISTS(SELECT 1 "
          "FROM chat_messages m WHERE m.session_id=s.id) ORDER BY updated_at DESC,id");
      std::stable_sort(old.begin(), old.end(), [](const Json &a, const Json &b) {
        return Json::parse(a["metadata_json"].get<std::string>())
                   .value("pinned_at", int64_t(0)) >
               Json::parse(b["metadata_json"].get<std::string>())
                   .value("pinned_at", int64_t(0));
      });
    }
    const auto baseline = ticks() - begin;
    begin = ticks();
    for (int sample = 0; sample < 10; ++sample)
      db.sessions();
    const auto optimized = ticks() - begin;
    begin = ticks();
    auto first = db.sessions();
    assert(first.size() == 30 && first[0]["id"] == "s2");
    std::set<std::string> ids;
    for (int offset = 0; offset < 1000; offset += 30)
      for (const auto &s : db.sessions(offset))
        assert(ids.insert(s["id"]).second);
    assert(ids.size() == 1000);
    auto pages = ticks() - begin;
    auto page1 = db.messages("s0"), page2 = db.messages("s0", 30);
    assert(page1.front()["seq"] == 71 && page2.back()["seq"] == 70);
    assert(db.messages("s0", 0).back()["seq"] == 100);
    db.pinSession("s3", true);
    assert(db.sessions()[0]["id"] == "s3");
    auto id = db.append("s0", "assistant", "");
    const auto beforeTransactions = db.storageStats()["transactions"].get<uint64_t>();
    begin = ticks();
    for (int i = 1; i <= 200; ++i)
      db.queueMessage("s0", id, 1, i, std::string(4096, 'a'), "reason", "streaming",
                      Json(), {{"n", i}});
    auto submit = ticks() - begin;
    db.queueMessage("s0", id, 1, 201, "final", "reason", "complete", Json(),
                    {{"n", 201}});
    db.queueMessage("s0", id, 1, 202, "stale", "", "streaming", Json(), Json::object());
    db.flush();
    const auto streamTransactions =
        db.storageStats()["transactions"].get<uint64_t>() - beforeTransactions;
    assert(db.messages("s0", 0, 1)[0]["output_text"] == "final");
    assert(db.query("SELECT text FROM message_parts WHERE message_id=?",
                    {id})[0]["text"] == "final");
    db.queueMessage("s0", id, 2, 1, "retry", "", "complete", Json(), Json::object());
    db.flush();
    assert(db.messages("s0", 0, 1)[0]["output_text"] == "retry");
    db.saveEvents();
    db.execute("CREATE TRIGGER fail_save BEFORE UPDATE ON chat_messages BEGIN SELECT "
               "RAISE(FAIL,'fixture'); END");
    db.queueMessage("s0", id, 3, 1, "retained", "", "complete", Json(), Json::object());
    db.flush();
    bool failure = false;
    for (auto &event : db.saveEvents())
      failure |= event.type == "save_error";
    assert(failure);
    assert(db.messages("s0", 0, 1)[0]["output_text"] == "retry");
    db.execute("DROP TRIGGER fail_save");
    db.retrySaves();
    db.flush();
    assert(db.messages("s0", 0, 1)[0]["output_text"] == "retained");
    for (int i = 0; i < 100; ++i)
      db.query("SELECT " + std::to_string(i));
    assert(db.storageStats()["statements"].get<int>() <= 64);
    auto original = root + "/original.png";
    atomicText(original, "fixture image");
    auto image =
        db.append("images", "user", "", {{"path", original}, {"source", "album"}});
    auto copy = db.messages("images")[0]["stored_path"].get<std::string>();
    assert(access(copy.c_str(), F_OK) == 0);
    db.removeSession("images");
    db.flush();
    assert(access(copy.c_str(), F_OK) != 0 && access(original.c_str(), F_OK) == 0);
    auto shared = db.append("one", "user", "", {{"path", original}});
    copy = db.messages("one")[0]["stored_path"];
    auto other = db.append("two", "user", "");
    auto asset = db.query("SELECT asset_id FROM message_parts WHERE message_id=?",
                          {shared})[0]["asset_id"];
    db.execute(
        "UPDATE message_parts SET part_type='image',asset_id=? WHERE message_id=?",
        {asset, other});
    db.removeSession("one");
    db.flush();
    assert(access(copy.c_str(), F_OK) == 0);
    db.removeSession("two");
    db.flush();
    assert(access(copy.c_str(), F_OK) != 0);
    auto stats = db.storageStats();
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::cout << Json{{"sessions", 1000},
                      {"messages", 100000},
                      {"migration_ms", migrated},
                      {"old_first_page_10_runs_ms", baseline},
                      {"new_first_page_10_runs_ms", optimized},
                      {"all_history_pages_ms", pages},
                      {"submit_200_snapshots_ms", submit},
                      {"stream_transactions", streamTransactions},
                      {"peak_rss_kib", usage.ru_maxrss},
                      {"storage", stats}}
                     .dump()
              << std::endl;
  }
  {
    Database db(path);
    assert(db.query("PRAGMA foreign_key_check").empty());
  }
  {
    atomicText(root + "/assets/asset_1_1.png", "orphan");
    atomicText(root + "/assets/unknown.png", "retain");
    {
      Database db(path);
      db.execute(
          "INSERT INTO message_assets(id,kind,source_type,stored_path,created_at) "
          "VALUES('unknown-library','image','library',?,1)",
          {root + "/assets/unknown.png"});
    }
    assert(symlink((root + "/original.png").c_str(),
                   (root + "/assets/asset_1_2.png").c_str()) == 0);
    Database db(path);
    db.flush();
    assert(access((root + "/assets/asset_1_1.png").c_str(), F_OK) != 0);
    assert(access((root + "/assets/unknown.png").c_str(), F_OK) == 0);
    assert(
        db.query("SELECT id FROM message_assets WHERE id='unknown-library'").size() ==
        1);
    assert(access((root + "/assets/asset_1_2.png").c_str(), F_OK) == 0);
  }
  {
    auto interrupted = root + "/interrupted.db";
    seed(interrupted, 2, 10);
    auto child = fork();
    assert(child >= 0);
    if (child == 0) {
      setenv("PENCHAT_TEST_PAUSE_MIGRATION", "1", 1);
      Database db(interrupted);
      _exit(1);
    }
    int status = 0;
    assert(waitpid(child, &status, WUNTRACED) == child && WIFSTOPPED(status));
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
    Database recovered(interrupted);
    assert(recovered.query("SELECT count(*) AS n FROM chat_messages")[0]["n"] == 20);
    assert(recovered.query("PRAGMA foreign_key_check").empty());
  }
  {
    auto retry = root + "/retry.db";
    seed(retry);
    atomicText(retry + ".backup-v1", "preserve existing backup");
    Database upgraded(retry);
    assert(fileText(retry + ".backup-v1") == "preserve existing backup");
  }
  {
    for (int i = 300; i > 0; --i)
      atomicText(root + "/assets/asset_2_" + std::to_string(i) + ".png", "orphan");
    { Database collected(path); }
    for (int i = 1; i <= 300; ++i)
      assert(access((root + "/assets/asset_2_" + std::to_string(i) + ".png").c_str(),
                    F_OK) != 0);
  }
  for (const auto &kind : {"duplicate", "orphan"}) {
    auto bad = root + "/" + kind + ".db";
    seed(bad);
    sqlite3 *db = nullptr;
    sqlite3_open(bad.c_str(), &db);
    if (std::string(kind) == "duplicate")
      sql(db, "INSERT INTO chat_messages SELECT "
              "'copy',session_id,seq,role,status,message_type,created_at,updated_at,"
              "provider_id,model_config_id,request_json,output_text,reasoning_text,"
              "error_json,response_id,metadata_json FROM chat_messages");
    else
      sql(db, "DELETE FROM chat_sessions");
    sqlite3_close(db);
    bool rejected = false;
    try {
      Database invalid(bad);
    } catch (const std::exception &e) {
      rejected = std::string(e.what()).find("upgrade blocked") != std::string::npos;
    }
    assert(rejected);
    sqlite3_open(bad.c_str(), &db);
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db, "SELECT value FROM schema_meta WHERE key='schema_version'",
                       -1, &stmt, nullptr);
    assert(sqlite3_step(stmt) == SQLITE_ROW &&
           std::string(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))) ==
               "1");
    sqlite3_finalize(stmt);
    sqlite3_close(db);
  }
  std::cout << "Storage migration, constraints, keyset pages, async barriers, retry, "
               "and shared-asset GC passed"
            << std::endl;
}
