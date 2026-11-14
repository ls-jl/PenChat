#include "context.hpp"
#include "presets.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace penchat;
// Independent per-message reader preserves the former lookup/order semantics.
static Json referenceRows(Database &db, const std::string &session) {
  const auto cut = db.replyCut(session);
  const auto last = db.query("SELECT coalesce(max(seq),0) n FROM chat_messages WHERE "
                             "session_id=? AND role='assistant'", {session})[0]["n"].get<int64_t>();
  auto rows = Json::array();
  for (auto row : db.query("SELECT * FROM chat_messages WHERE session_id=? ORDER BY seq", {session})) {
    const auto seq = row["seq"].get<int64_t>();
    if (seq > cut && !(row["role"] == "user" && row["message_type"] == "image" &&
        (!row["output_text"].is_string() || row["output_text"].get<std::string>().empty()) && seq > last))
      continue;
    row["parts"] = Json::array();
    for (const auto &part : db.query(
             "SELECT p.part_type,a.stored_path,a.original_path,a.width,a.height,a.size_bytes "
             "FROM message_parts p LEFT JOIN message_assets a ON a.id=p.asset_id "
             "WHERE p.message_id=? ORDER BY p.part_index", Json::array({row["id"]}))) {
      if (part["part_type"] != "image") continue;
      auto path = part["stored_path"].is_string() ? part["stored_path"].get<std::string>() : "";
      if (path.empty() && part["original_path"].is_string()) path = part["original_path"];
      row["parts"].push_back({{"type", "image"}, {"path", path},
          {"signature", Json::array({part["width"], part["height"], part["size_bytes"]}).dump()}});
    }
    rows.push_back(std::move(row));
  }
  return rows;
}
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
  // Multiple parts on both sides of the 100-row page boundary, inserted out of
  // order. The second asset exercises fallback to the system-original path.
  auto asset = db.query("SELECT id FROM message_assets WHERE session_id=?", {session})[0]["id"];
  const auto fallback = uid("asset");
  db.execute("INSERT INTO message_assets(id,session_id,kind,source_type,original_path,stored_path,"
             "width,height,size_bytes,created_at) VALUES(?,?,'image','album',?,'',3,4,13,1)",
             {fallback, session, photo});
  for (int seq : {100, 101}) {
    auto id = db.query("SELECT id FROM chat_messages WHERE session_id=? AND seq=?", {session, seq})[0]["id"];
    db.execute("INSERT INTO message_parts(id,session_id,message_id,part_index,part_type,asset_id) "
               "VALUES(?,?,?,2,'image',?)", {uid("part"), session, id, fallback});
    db.execute("INSERT INTO message_parts(id,session_id,message_id,part_index,part_type,asset_id) "
               "VALUES(?,?,?,1,'image',?)", {uid("part"), session, id, asset});
  }
  db.flush();
  const auto beforeQueries = db.storageStats()["queries"].get<uint64_t>();
  auto snapshot = db.contextSnapshot(session, true);
  const auto snapshotQueries = db.storageStats()["queries"].get<uint64_t>() - beforeQueries;
  assert(snapshotQueries == 13); // 2 bounded parts batches, rather than 106 individual lookups.
  const auto reference = referenceRows(db, session);
  assert(snapshot["rows"] == reference);
  assert(snapshot["source_checksum"] == contextChecksum(reference, INT64_MAX));
  for (int index : {99, 100}) {
    assert(snapshot["rows"][index]["parts"].size() == 2);
    assert(snapshot["rows"][index]["parts"][1]["path"] == photo);
    assert(snapshot["rows"][index]["parts"][1]["signature"] == "[3,4,13]");
  }
  std::cout << "106-message context snapshot: " << snapshotQueries << " SQL queries\n";
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
      db.query("SELECT stored_path FROM message_assets WHERE session_id=? AND stored_path<>''", {archived});
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
  // Reply alternatives hide a physical tail, but keep a later unsent image.
  const auto choiceSession = uid("session");
  db.append(choiceSession, "user", "first");
  auto firstReply = db.append(choiceSession, "assistant", "original");
  db.updateMessage(firstReply, "original", "", "complete");
  db.append(choiceSession, "user", "hidden later question");
  auto laterReply = db.append(choiceSession, "assistant", "hidden answer");
  db.updateMessage(laterReply, "hidden answer", "", "complete");
  auto pending = db.append(choiceSession, "user", "", {{"path", photo}});
  db.beginReplyResult(db.contextSnapshot(choiceSession), firstReply, Json(), Json::object(), cancel);
  db.updateMessage(firstReply, "alternative", "", "complete");
  auto chosen = db.contextSnapshot(choiceSession);
  assert(chosen["rows"] == referenceRows(db, choiceSession));
  assert(chosen["rows"].size() == 3 && chosen["rows"].back()["id"] == pending);
  assert(chosen["rows"].back()["parts"].size() == 1);
  db.selectReplyResult(choiceSession, firstReply, 1);
  auto restored = db.contextSnapshot(choiceSession);
  assert(restored["rows"].size() == 5 && restored["rows"] == referenceRows(db, choiceSession));
  std::cout << "context snapshots, 106 messages, atomic rollback, shared assets and "
               "preset reset passed\n";
}
