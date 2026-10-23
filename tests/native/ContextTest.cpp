#include "context.hpp"
#include "presets.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace penchat;
int main() {
  assert(contextBudget(Json::object()) == 16384);
  assert(estimatedText("中文") == 4);
  auto image = Json::array(
      {{{"role", "user"},
        {"content", Json::array({{{"type", "input_image"},
                                  {"image_url", std::string(100000, 'a')}}})}}});
  assert(estimatedContext(image) < 2100); // never count Base64 characters as tokens
  auto systemHistory = Json::array({{{"seq", 1},
                                     {"role", "system"},
                                     {"status", "complete"},
                                     {"output_text", "historical constraint"}}});
  auto historicalInput = contextInput(systemHistory, 0, 1);
  assert(historicalInput.size() == 1 && historicalInput[0]["role"] == "user");
  char temp[] = "/tmp/penchat-context-XXXXXX";
  auto root = std::string(mkdtemp(temp));
  Database db(join(root, "penchat.db"));
  db.saveProvider(
      {{"name", "fixture"}, {"baseURL", "http://127.0.0.1:1/v1"}, {"apiKey", "fake"}},
      {"model"});
  auto model = db.activeModel();
  auto session = uid("session");
  std::string answer;
  for (int i = 0; i < 52; ++i) {
    db.append(session, "user", "fact-" + std::to_string(i));
    answer = db.append(session, "assistant", "answer-" + std::to_string(i));
    db.updateMessage(answer, "answer-" + std::to_string(i), "", "complete");
  }
  assert(db.responseInput(session, "").size() == 104);
  db.recordRequest(answer, model);
  auto photo = join(root, "photo.png");
  atomicText(photo, "private photo");
  db.append(session, "user", "", {{"path", photo}, {"width", 1}, {"height", 1}});
  auto last = db.append(session, "assistant", "photo understood");
  db.updateMessage(last, "photo understood", "", "complete");
  auto snapshot = db.contextSnapshot(session, true);
  auto cp = Json{{"version", 1},
                 {"enabled", true},
                 {"summary", "历史事实"},
                 {"through", 100},
                 {"checksum", contextChecksum(snapshot["rows"], 100)},
                 {"revision", 1},
                 {"created_at", nowMs()}};
  assert(validContext(cp, snapshot["rows"]));
  std::atomic<bool> cancel{false};
  auto saved = db.commitCompression(snapshot, cp, cancel);
  assert(!saved.at("archive_id").get<std::string>().empty());
  auto archived = saved.at("archive_id").get<std::string>();
  auto copy = db.contextSnapshot(archived);
  assert(copy["rows"].size() == snapshot["rows"].size());
  assert(copy["checkpoint"].empty());
  assert(copy["rows"][0]["id"] != snapshot["rows"][0]["id"]);
  assert(db.responseInput(session, "").size() == 7);
  auto count = db.summary()["sessions"];
  bool rejected = false;
  try {
    db.commitCompression(snapshot, cp, cancel);
  } catch (...) {
    rejected = true;
  }
  assert(rejected &&
         db.summary()["sessions"] == count); // stale checkpoint must not archive
  auto next = db.contextSnapshot(session);
  cp["revision"] = 2;
  cancel = true;
  rejected = false;
  try {
    db.commitCompression(next, cp, cancel);
  } catch (...) {
    rejected = true;
  }
  assert(rejected && db.summary()["sessions"] == count);
  cancel = false;
  // Force a late transaction error: archive and summary must both roll back.
  db.execute("CREATE TRIGGER context_fail BEFORE UPDATE ON chat_sessions BEGIN SELECT "
             "RAISE(ABORT,'test'); END");
  rejected = false;
  try {
    db.commitCompression(next, cp, cancel);
  } catch (...) {
    rejected = true;
  }
  assert(rejected && db.summary()["sessions"] == count);
  assert(db.contextSnapshot(session)["checkpoint"]["revision"] == 1);
  db.execute("DROP TRIGGER context_fail");
  auto second = db.commitCompression(next, cp, cancel);
  assert(db.summary()["sessions"].get<int>() == count.get<int>() + 1);
  assert(db.contextSnapshot(second["archive_id"])["checkpoint"]["revision"] == 1);
  db.execute("CREATE TRIGGER fail_message BEFORE UPDATE ON chat_messages BEGIN SELECT "
             "RAISE(ABORT,'test'); END");
  db.queueMessage(session, last, 10, 1, "unsaved", "", "complete", Json(),
                  Json::object());
  db.flush();
  rejected = false;
  try {
    db.contextSnapshot(session);
  } catch (...) {
    rejected = true;
  }
  assert(rejected); // Do not compact an older DB value while the latest content is
                    // unsaved.
  db.execute("DROP TRIGGER fail_message");
  db.retrySaves();
  db.flush();
  assert(db.contextSnapshot(session)["rows"].back()["output_text"] == "unsaved");
  auto assets =
      db.query("SELECT stored_path FROM message_assets WHERE session_id=?", {archived});
  assert(!assets.empty());
  auto shared = assets[0]["stored_path"].get<std::string>();
  db.removeSession(session);
  db.releaseTransferSnapshot();
  db.flush();
  assert(access(shared.c_str(), F_OK) == 0);
  assert(db.query("PRAGMA foreign_key_check").empty());
  auto prefs = applyPresetSnapshot({{"compression_model_id", "previous"}},
                                   defaultPreset(), db.models(), model, true, shared);
  assert(prefs["compression_model_id"] == "");
  std::cout << "context snapshots, 106 messages, atomic rollback, shared assets and "
               "preset reset passed\n";
}
