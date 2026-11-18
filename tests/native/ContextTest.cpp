#include "context.hpp"
#include "presets.hpp"
#include "api_config.hpp"
#include "transfer.hpp"
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
static Json transferJob(Transfer &transfer, Events &events, const std::string &op, Json args = Json::object()) {
  assert(transfer.start(op, args));
  const auto deadline = ticks() + 10000;
  while (ticks() < deadline) {
    for (const auto &event : events.drain()) {
      if (event.type == "transfer_progress") continue;
      if (event.type != "transfer_result") std::cerr << event.data.dump() << std::endl;
      assert(event.type == "transfer_result");
      return event.data.at("data");
    }
    usleep(1000);
  }
  assert(false);
  return Json();
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
  {
  // Durable drafts do not create history, and a successful send invalidates old writers.
  auto fresh=db.draft("new");fresh["revision"]=1;
  db.queueDraft(fresh,"给我解释");db.flush();
  const auto shared=db.draft("new");assert(shared["text"]=="给我解释");
  auto personal=db.draft(session);personal["revision"]=1;
  db.queueDraft(personal,"会话草稿");db.flush();
  const auto photo=join(root,"draft.png");
  {std::ofstream file(photo);file<<"local-image-fixture";}
  const auto removable=db.addDraftImage(shared,{{"path",photo},{"source","album"}});
  assert(db.removeDraftImage(shared,removable.at("id")));
  assert(!fileText(photo,1024).empty());
  const auto protectedImage=db.addDraftImage(shared,{{"path",photo},{"source","album"}});
  db.execute("UPDATE draft_attachments SET image_json=? WHERE id=?",{Json{{"path",photo}}.dump(),protectedImage.at("id")});
  assert(db.removeDraftImage(shared,protectedImage.at("id")));
  assert(!fileText(photo,1024).empty());
  db.addDraftImage(shared,{{"path",photo},{"source","album"}});
  const auto withImage=db.draft("new");assert(withImage["images"].size()==1);
  const auto other=uid("session");
  db.append(other,"user","existing");
  auto otherDraft=db.draft(other);otherDraft["revision"]=1;
  db.queueDraft(otherDraft,"send now");db.flush();
  db.execute("CREATE TRIGGER draft_fail BEFORE INSERT ON chat_messages BEGIN SELECT RAISE(ABORT,'fixture');END");
  try{db.commitDraftSend(db.draft(other),other,"send now",Json::object(),model,1,Json::object(),Json::object());assert(false);}catch(const std::exception&){}
  assert(db.draft("new")["images"].size()==1&&db.draft(other)["text"]=="send now");
  db.execute("DROP TRIGGER draft_fail");
  db.commitDraftSend(db.draft(other),other,"send now",Json::object(),model,1,Json::object(),Json::object());
  auto late=withImage;late["revision"]=999;db.queueDraft(late,"stale resurrection");db.flush();
  assert(db.draft("new")["text"]==""&&db.draft("new")["images"].empty());
  assert(db.draft(session)["text"]=="会话草稿");
  auto restartDraft=db.draft("new");restartDraft["revision"]=1;
  db.queueDraft(restartDraft,"restart");db.flush();
  {Database reopened(join(root,"penchat.db"));assert(reopened.draft("new")["text"]=="restart");}
  auto imageDraft=db.draft("new");db.addDraftImage(imageDraft,{{"path",photo},{"source","album"}});
  imageDraft=db.draft("new");const auto imageSession=uid("session");
  assert(db.sessionInfo(imageSession).empty());
  db.commitDraftSend(imageDraft,imageSession,"restart",Json::object(),model,1,Json::object(),Json::object());
  assert(db.query("SELECT id FROM chat_messages WHERE session_id=? AND message_type='image'",{imageSession}).size()==1);
  assert(db.draft("new")["images"].empty());
  }
  {
    const auto sid = uid("session");
    db.append(sid, "user", "original tool question");
    const auto assistant = db.append(sid, "assistant", "");
    auto snapshot = db.contextSnapshot(sid);
    auto source = snapshot["rows"]; source.erase(source.end() - 1);
    Json cp = {{"version", 2}, {"enabled", true}, {"summary", "tool checkpoint"},
               {"through", 0}, {"tool_through", 1}, {"retained", Json::array()},
               {"source_through", 1}, {"source_checksum", contextChecksum(source, 1)},
               {"checksum", contextChecksum(source, 0)}, {"revision", 1}};
    Json wire = Json::array();
    for (auto name : {"covered", "remaining"})
      wire.push_back(Json::array({{{"type", "function_call"}, {"call_id", name},
                                   {"name", "web_search"}, {"arguments", "{}"}},
                                  {{"type", "function_call_output"}, {"call_id", name}, {"output", name}}}));
    auto replayModel = model; replayModel["web_enabled"] = true;
    Json publicBatches = Json::array();
    for (const auto &batch : wire) publicBatches.push_back(publicToolMaterial(batch));
    Json metadata = {{"tool_exchanges", wire}, {"public_tool_exchanges", publicBatches},
                     {"replay_identity", replayIdentity(replayModel)}};
    Json state = {{"metadata", metadata}, {"output", "partial body"}};
    const auto archives = db.summary()["sessions"].get<int>();
    cp = db.commitToolCompression(sid, assistant, cp, state, cancel);
    assert(db.summary()["sessions"].get<int>() == archives + 1);
    const auto again = db.commitToolCompression(sid, assistant, cp, state, cancel);
    assert(again["archive_id"] == cp["archive_id"]);
    assert(db.summary()["sessions"].get<int>() == archives + 1);
    auto future = cp; future["revision"] = 2; future["summary"] = "must roll back";
    db.execute("CREATE TRIGGER tool_fail BEFORE UPDATE ON chat_messages BEGIN SELECT RAISE(ABORT,'fixture');END");
    try { db.commitToolCompression(sid, assistant, future, state, cancel); assert(false); }
    catch (const std::exception &) {}
    db.execute("DROP TRIGGER tool_fail");
    assert(db.summary()["sessions"].get<int>() == archives + 1);
    metadata["tool_compaction"] = cp;
    db.updateMessage(assistant, "final body", "", "complete", Json(), metadata);
    const auto input = db.responseInput(sid, "", replayModel).dump();
    assert(input.find("covered") == std::string::npos);
    assert(input.find("remaining") != std::string::npos);
    assert(input.find("tool checkpoint") != std::string::npos);
    auto offline = replayModel; offline["web_enabled"] = false;
    const auto offlineInput = db.responseInput(sid, "", offline).dump();
    assert(offlineInput.find("remaining") != std::string::npos);
    assert(offlineInput.find("covered") == std::string::npos);
    auto different = replayModel; different["name"] = "different";
    const auto portable = db.responseInput(sid, "", different).dump();
    assert(portable.find("covered") == std::string::npos);
    assert(portable.find("remaining") != std::string::npos);
    assert(portable.find("\"type\":\"function_call\"") == std::string::npos);
    { Database reopened(join(root, "penchat.db"));
      assert(reopened.responseInput(sid, "", replayModel).dump() == input); }
    db.renameSession(sid, "Tool checkpoint transfer fixture");
    const auto transferRoot = join(root, "transfer-tools");
    setenv("PENCHAT_TRANSFER_ROOT", transferRoot.c_str(), 1);
    Events transferEvents;
    Transfer transfer(db, transferEvents, root);
    transferJob(transfer, transferEvents, "init");
    const auto package = transferJob(transfer, transferEvents, "export", {{"kind", "chats"}, {"ids", Json::array({sid})}}).at("path").get<std::string>();
    const auto filename = package.substr(package.find_last_of('/') + 1);
    { std::ifstream source(package, std::ios::binary);
      std::ofstream target(join(join(transferRoot, "导入/聊天"), filename), std::ios::binary);
      target << source.rdbuf(); }
    const auto preview = transferJob(transfer, transferEvents, "preview", {{"kind", "chats"}, {"name", filename}});
    assert(preview["items"][0]["warning"] == "");
    assert(transferJob(transfer, transferEvents, "import", {{"indices", Json::array({0})}, {"copy", true}})["added"] == 1);
    const auto copied = db.query("SELECT id FROM chat_sessions WHERE title=? AND id<>? AND status='active'",
                                  {"Tool checkpoint transfer fixture", sid});
    assert(copied.size() == 1);
    const auto imported = db.responseInput(copied[0]["id"], "", different).dump();
    assert(imported.find("covered") == std::string::npos);
    assert(imported.find("remaining") != std::string::npos);
    assert(imported.find("tool checkpoint") != std::string::npos);
  }
  std::cout << "context snapshots, 106 messages, atomic rollback, shared assets and "
               "preset reset passed\n";
}
