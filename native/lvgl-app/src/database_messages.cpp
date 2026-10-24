#include "database.hpp"
#include "context.hpp"
#include "reasoning.hpp"
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace penchat {
namespace {
std::string assetText(const Json &a,const std::string &k){auto i=a.find(k);return i!=a.end()&&i->is_string()?i->get<std::string>():std::string();}
}

Json Database::messageEditSnapshot(const std::string &session,
                                   const std::string &message) {
  return perform([=] {
    auto snapshot = contextSnapshot(session);
    int64_t previous = 0, end = INT64_MAX, chosen = -1;
    for (const auto &row : snapshot["rows"])
      if (row["id"] == message && row["role"] == "user") chosen = row["seq"];
    if (chosen < 0) throw std::runtime_error("消息已变化，请重新选择");
    // Image rows precede their text question. A prior user text is also a
    // turn boundary (e.g. imported consecutive user messages or a failed send
    // without an assistant row); never merge it into the selected prompt.
    for (const auto &row : snapshot["rows"]) {
      const auto seq = row["seq"].get<int64_t>();
      const bool assistant = row["role"] == "assistant";
      const bool userText = row["role"] == "user" && !assetText(row,"output_text").empty();
      if (seq < chosen && (assistant || userText)) previous = seq;
      if (seq >= chosen && (assistant || userText)) {
        end = assistant ? seq : seq + 1;
        break;
      }
    }
    snapshot["images"] = Json::array();
    std::string text;
    int64_t pivot = 0;
    for (const auto &row : snapshot["rows"]) {
      const auto seq = row["seq"].get<int64_t>();
      if (seq <= previous || seq >= end) continue;
      if (!pivot) pivot = seq;
      const auto content = assetText(row,"output_text");
      if (!content.empty()) { if (!text.empty()) text += '\n'; text += content; }
      for (auto asset : query("SELECT a.* FROM message_parts p JOIN message_assets a "
                              "ON a.id=p.asset_id WHERE p.message_id=? AND "
                              "p.part_type='image' ORDER BY p.part_index", Json::array({row["id"]}))) {
        asset["edit_id"] = uid("edit-image");
        snapshot["images"].push_back(std::move(asset));
      }
    }
    snapshot["pivot"] = pivot; snapshot["text"] = text;
    ++assetLeases_; snapshot["leased"] = true;
    return snapshot;
  });
}
Json Database::commitMessageEdit(const Json &snapshot, const Json &images,
                                 const std::string &text, const Json &preferences,
                                 bool archive, int64_t sentAt) {
  return perform([=] {
    const auto session = snapshot.at("session").at("id").get<std::string>();
    const auto pivot = snapshot.at("pivot").get<int64_t>();
    if (text.find_first_not_of(" \r\n\t") == std::string::npos && images.empty())
      throw std::runtime_error("请输入文字或附加图片");
    Json prepared = Json::array();
    std::vector<std::string> created;
    bool transaction = false;
    try {
      // Prepare new image files before opening the transaction. Existing images
      // share their immutable private files; the snapshot lease protects both.
      for (auto asset : images) {
        if (asset.value("edit_new", false)) {
          for (auto column : {"stored_path", "display_src"}) {
            auto path = assetText(asset,column);
            if (path.empty()) continue;
            struct stat st{};
            if (lstat(path.c_str(), &st) || !S_ISREG(st.st_mode))
              throw std::runtime_error("待发送图片不可读");
            const auto stored = join(assetDirectory_, uid("asset") + ".png");
            ensureDirectory(assetDirectory_); created.push_back(stored);
            auto bytes = fileText(path, 16 * 1024 * 1024);
            if (bytes.empty()) throw std::runtime_error("图片准备失败");
            atomicText(stored, bytes); asset[column] = stored;
          }
          asset["original_path"] = asset["stored_path"];
        }
        prepared.push_back(std::move(asset));
      }
      execute("BEGIN IMMEDIATE"); transaction = true;
      const auto current = contextSnapshot(session);
      if (!current.contains("session") || current["source_checksum"] != snapshot.at("source_checksum") ||
          current["checkpoint"] != snapshot.at("checkpoint") || current["rows"] != snapshot.at("rows"))
        throw std::runtime_error("会话已变化，未覆盖原记录，请取消后重新编辑");
      auto archived = archive ? archiveCopy(current, "编辑前副本", "edit_archive") : std::string();
      auto oldAssets = query("SELECT stored_path,display_src FROM message_assets WHERE message_id IN "
                             "(SELECT id FROM chat_messages WHERE session_id=? AND seq>=?)", {session,pivot});
      enqueueAssets(oldAssets);
      auto suffix = query("SELECT id FROM chat_messages WHERE session_id=? AND seq>=?", {session,pivot});
      for (const auto &row : suffix) {
        execute("DELETE FROM stream_events WHERE message_id=?", Json::array({row["id"]}));
        execute("DELETE FROM message_parts WHERE message_id=?", Json::array({row["id"]}));
        detachSharedAssets("message_id", row["id"]);
        execute("DELETE FROM message_assets WHERE message_id=?", Json::array({row["id"]}));
        execute("DELETE FROM chat_messages WHERE id=?", Json::array({row["id"]}));
      }
      int64_t seq = pivot;
      auto appendUser = [&](const std::string &content, const Json &asset) {
        const auto id = uid("message");
        execute("INSERT INTO chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text,metadata_json) "
                "VALUES(?,?,?,'user','complete',?,?,?,?,?)", {id,session,seq++,asset.empty()?"text":"image",sentAt,sentAt,content,
                Json{{"timeline",{{"version",1},{"sent_at",sentAt}}}}.dump()});
        if (asset.empty())
          execute("INSERT INTO message_parts(id,session_id,message_id,part_index,part_type,text) VALUES(?,?,?,0,'text',?)",
                  {uid("part"),session,id,content});
        else {
          const auto aid = uid("asset");
          execute("INSERT INTO message_assets(id,session_id,message_id,kind,source_type,original_path,stored_path,display_src,width,height,size_bytes,metadata_json,created_at) "
                  "VALUES(?,?,?,'image',?,?,?,?,?,?,?,?,?)", {aid,session,id,asset.value("source_type",std::string("camera")),
                  assetText(asset,"original_path"),assetText(asset,"stored_path"),assetText(asset,"display_src"),
                  asset.value("width",0),asset.value("height",0),asset.value("size_bytes",int64_t(0)),asset.value("metadata_json",std::string("{}")),sentAt});
          execute("INSERT INTO message_parts(id,session_id,message_id,part_index,part_type,asset_id) VALUES(?,?,?,0,'image',?)",
                  {uid("part"),session,id,aid});
        }
      };
      for (const auto &asset : prepared) appendUser("",asset);
      if (!text.empty()) appendUser(text,Json::object());
      auto meta = objectField(current.at("session"),"metadata_json");
      meta.erase("context_compression"); meta.erase("title_token"); meta["title_state"]="manual";
      std::string preview;
      int count=0;for(const auto &c:characters(text)){if(count++>=40)break;preview+=c;}
      execute("UPDATE chat_sessions SET metadata_json=?,updated_at=?,message_count=(SELECT count(*) FROM chat_messages WHERE session_id=?),preview_text=? WHERE id=?",
              {meta.dump(),sentAt,session,text.empty()?std::string("[图片]"):preview,session});
      saveSessionPreferences(session,preferences); recordActivity(session,sentAt);
      setting("active_session_id",session);
      execute("COMMIT"); transaction=false;
      gcCursor_.clear(); deferAssetCollection();
      return Json{{"archive",archived},{"session",session}};
    } catch (...) {
      if (transaction) execute("ROLLBACK");
      for (const auto &path : created) {unlink(path.c_str());unlink((path+".part").c_str());}
      throw;
    }
  });
}
} // namespace penchat
