#include "context.hpp"
#include "reasoning.hpp"
#include <limits>
#include <stdexcept>
namespace penchat {
Json Database::contextSnapshot(const std::string &session, bool lease, bool allowPending) {
  if (!onWorker())
    return perform([=] { return contextSnapshot(session, lease, allowPending); });
  {
    std::lock_guard<std::mutex> lock(jobsMutex_);
    for (const auto *queue : {&pendingSaves_, &failedSaves_})
      for (const auto &entry : *queue)
        if (!allowPending && entry.second.value("session", std::string()) == session)
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
  snapshot["reply_choices"]=replyChoices(session);
  const auto cut=replyCut(session);
  const auto lastAssistant=query("SELECT coalesce(max(seq),0) n FROM chat_messages WHERE session_id=? AND role='assistant'",{session})[0]["n"].get<int64_t>();
  int64_t seq = -1;
  for (;;) {
    auto page = query("SELECT * FROM chat_messages WHERE session_id=? AND seq>? ORDER "
                      "BY seq LIMIT 100",
                      {session, seq});
    if (page.empty())
      break;
    // Fetch image parts once for this physical page, including rows beyond a
    // reply cut. The effective-message filter below still owns their visibility.
    std::map<std::string, Json> imageParts;
    for (const auto &part : query(
             "SELECT p.message_id,a.stored_path,a.original_path,a.width,a.height,"
             "a.size_bytes FROM chat_messages m JOIN message_parts p ON "
             "p.message_id=m.id LEFT JOIN message_assets a ON a.id=p.asset_id "
             "WHERE m.session_id=? AND m.seq>? AND m.seq<=? AND "
             "p.part_type='image' ORDER BY m.seq,p.part_index",
             {session, seq, page.back()["seq"]})) {
      auto path = part["stored_path"].is_string()
                      ? part["stored_path"].get<std::string>()
                      : "";
      if (path.empty() && part["original_path"].is_string())
        path = part["original_path"];
      imageParts[part["message_id"].get<std::string>()].push_back(
          {{"type", "image"},
           {"path", path},
           {"signature", Json::array({part["width"], part["height"],
                                      part["size_bytes"]}).dump()}});
    }
    for (auto m : page) {
      seq = m["seq"];
      if(seq>cut) {
        if(!(m["role"]=="user"&&m["message_type"]=="image"&&(!m["output_text"].is_string()||m["output_text"].get<std::string>().empty())&&seq>lastAssistant))continue;
      }
      auto parts = imageParts.find(m["id"].get<std::string>());
      m["parts"] = parts == imageParts.end() ? Json::array() : std::move(parts->second);
      snapshot["rows"].push_back(std::move(m));
    }
  }
  snapshot["source_checksum"] = contextChecksum(snapshot["rows"], INT64_MAX);
  snapshot["extra_usage"]=replyUsageExtras(session,snapshot["rows"]);
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
Json Database::commitToolCompression(const std::string &session, const std::string &message,
                                      Json checkpoint, const Json &state, std::atomic<bool> &cancel) {
  flush();
  return perform([&, session, message, checkpoint, state]() mutable {
    execute("BEGIN IMMEDIATE");
    try {
      if (cancel) throw std::runtime_error("压缩已取消");
      auto current = contextSnapshot(session, false, true);
      const auto source = checkpoint.value("source_through", int64_t(0));
      if (!current.contains("session") ||
          checkpoint.value("source_checksum", std::string()) != contextChecksum(current["rows"], source))
        throw std::runtime_error("工具压缩来源已变化");
      auto found = query("SELECT status FROM chat_messages WHERE id=? AND session_id=?", {message, session});
      if (found.empty() || found[0]["status"] != "streaming") throw std::runtime_error("回复已变化");
      const auto saved = query("SELECT metadata_json FROM chat_messages WHERE id=?", {message});
      const auto prior = objectField(saved[0], "metadata_json").value("tool_compaction", Json::object());
      if (prior.value("revision", 0) >= checkpoint.value("revision", 0)) {
        if (prior.value("revision", 0) != checkpoint.value("revision", 0) ||
            prior.value("summary", std::string()) != checkpoint.value("summary", std::string()))
          throw std::runtime_error("工具摘要已更新，拒绝过期结果");
        execute("COMMIT");
        return prior;
      }
      // Map new tool images before taking the archive, inside this transaction.
      updateMessage(message, state.value("output", std::string()), state.value("reasoning", std::string()),
                    "streaming", Json(), state.value("metadata", Json::object()));
      current = contextSnapshot(session, false, true);
      const auto archived = archiveCopy(current, "压缩前副本", "compression_archive");
      checkpoint["archive_id"] = archived;
      auto record = query("SELECT metadata_json FROM chat_messages WHERE id=?", {message});
      auto metadata = objectField(record[0], "metadata_json");
      metadata["tool_compaction"] = checkpoint;
      execute("UPDATE chat_messages SET metadata_json=? WHERE id=?", {metadata.dump(), message});
      saveReplyResult(message);
      if (cancel) throw std::runtime_error("压缩已取消");
      execute("COMMIT");
      return checkpoint;
    } catch (...) { execute("ROLLBACK"); throw; }
  });
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
