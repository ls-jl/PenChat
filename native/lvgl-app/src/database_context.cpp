#include "context.hpp"
#include "reasoning.hpp"
#include <limits>
#include <stdexcept>
namespace penchat {
Json Database::contextSnapshot(const std::string &session, bool lease) {
  if (!onWorker())
    return perform([=] { return contextSnapshot(session, lease); });
  {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    for (const auto *queue : {&pendingSaves_, &failedSaves_})
      for (const auto &entry : *queue)
        if (entry.second.value("session", std::string()) == session)
          throw std::runtime_error("会话尚有未保存内容，请先重试保存，再发送或压缩");
  }
  auto sessions = query("SELECT * FROM chat_sessions WHERE id=?", {session});
  Json snapshot = {{"rows", Json::array()}, {"checkpoint", Json::object()}};
  if (sessions.empty())
    return snapshot;
  auto total = query("SELECT coalesce(sum(coalesce(length(CAST(output_text AS "
                     "BLOB)),0)+coalesce(length(metadata_json),0)+coalesce(length(CAST("
                     "reasoning_text AS BLOB)),0)+coalesce(length(request_json),0)),0) "
                     "n,count(*) c FROM chat_messages WHERE session_id=?",
                     {session});
  if (total[0]["n"].get<int64_t>() > 32 * 1024 * 1024 ||
      total[0]["c"].get<int64_t>() > 100000)
    throw std::runtime_error(
        "会话超过本地上下文读取上限，请导出备份或新建会话；未截断历史");
  snapshot["session"] = sessions[0];
  snapshot["checkpoint"] = objectField(sessions[0], "metadata_json")
                               .value("context_compression", Json::object());
  int64_t seq = -1;
  for (;;) {
    auto page = query("SELECT * FROM chat_messages WHERE session_id=? AND seq>? ORDER "
                      "BY seq LIMIT 100",
                      {session, seq});
    if (page.empty())
      break;
    for (auto m : page) {
      seq = m["seq"];
      m["parts"] = Json::array();
      for (const auto &part :
           query("SELECT "
                 "p.part_type,a.stored_path,a.original_path,a.width,a.height,a.size_"
                 "bytes FROM message_parts p LEFT JOIN message_assets a ON "
                 "a.id=p.asset_id WHERE p.message_id=? ORDER BY p.part_index",
                 Json::array({m["id"]}))) {
        if (part["part_type"] != "image")
          continue;
        auto path = part["stored_path"].is_string()
                        ? part["stored_path"].get<std::string>()
                        : "";
        if (path.empty() && part["original_path"].is_string())
          path = part["original_path"];
        m["parts"].push_back({{"type", "image"},
                              {"path", path},
                              {"signature", Json::array({part["width"], part["height"],
                                                         part["size_bytes"]})
                                                .dump()}});
      }
      snapshot["rows"].push_back(std::move(m));
    }
  }
  snapshot["source_checksum"] = contextChecksum(snapshot["rows"], INT64_MAX);
  if (lease) {
    ++assetLeases_;
    snapshot["leased"] = true;
  }
  return snapshot;
}
std::string Database::archiveCopy(const Json &snapshot, const std::string &label,
                                  const std::string &key) {
  const auto source = snapshot.at("session").at("id").get<std::string>();
  auto archived = uid("session");
  auto metadata = objectField(snapshot.at("session"), "metadata_json");
  metadata[key] = {
      {"source", source}, {"created_at", nowMs()}, {"label", label}};
  metadata["title_state"] = "manual";
  auto session = snapshot.at("session");
  session["id"] = archived;
  session["status"] = "archived";
  session["pinned_at"] = 0;
  session["title"] = label + " · " + session.value("title", std::string("聊天"));
  session["metadata_json"] = metadata.dump();
  session["updated_at"] = nowMs();
  copyConversationRows(session, snapshot.at("rows"));
  return archived;
}
Json Database::commitCompression(const Json &snapshot, Json checkpoint,
                                 std::atomic<bool> &cancel) {
  return perform([this, snapshot, checkpoint, &cancel]() mutable {
    if (cancel)
      throw std::runtime_error("压缩已取消");
    auto source = snapshot.at("session").at("id").get<std::string>();
    execute("BEGIN IMMEDIATE");
    try {
      auto current = contextSnapshot(source);
      if (!current.contains("session") ||
          current["source_checksum"] != snapshot.at("source_checksum") ||
          current["checkpoint"] != snapshot.at("checkpoint") ||
          !validContext(checkpoint, current["rows"]))
        throw std::runtime_error("会话已变化，未应用过期摘要，请重新压缩");
      auto archived = archiveCopy(snapshot, "压缩前副本", "compression_archive");
      checkpoint["archive_id"] = archived;
      auto liveMeta = objectField(current.at("session"), "metadata_json");
      liveMeta["context_compression"] = checkpoint;
      execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?",
              {liveMeta.dump(), source});
      if (cancel)
        throw std::runtime_error("压缩已取消");
      execute("COMMIT");
      return checkpoint;
    } catch (...) {
      execute("ROLLBACK");
      throw;
    }
  });
}
} // namespace penchat
