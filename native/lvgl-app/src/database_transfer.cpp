#include "database.hpp"
#include "presets.hpp"
#include "reasoning.hpp"
#include <sqlite3.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace penchat {
void Database::transferSnapshot(const std::string &path) {
  perform([this, path] {
    sqlite3 *copy = nullptr;
    if (sqlite3_open(path.c_str(), &copy) != SQLITE_OK) {
      if (copy)
        sqlite3_close(copy);
      throw std::runtime_error("无法创建导出快照");
    }
    chmod(path.c_str(), 0600);
    auto backup = sqlite3_backup_init(copy, "main", db_, "main");
    int rc = backup ? sqlite3_backup_step(backup, -1) : SQLITE_ERROR;
    if (backup)
      sqlite3_backup_finish(backup);
    sqlite3_close(copy);
    if (rc != SQLITE_DONE) {
      unlink(path.c_str());
      throw std::runtime_error("无法取得一致性快照");
    }
    ++assetLeases_;
    return Json();
  });
}
void Database::releaseTransferSnapshot() {
  perform([this] {
    if (assetLeases_)
      --assetLeases_;
    deferAssetCollection();
    return Json();
  });
}
Json Database::importTransfer(const Json &bundle) {
  return perform([this, bundle] {
    int added = 0, skipped = 0;
    execute("BEGIN IMMEDIATE");
    try {
      if (bundle.at("kind") == "presets") {
        auto presets = presetList(settings());
        for (auto p : bundle.at("items")) {
          if (p.value("skip", false)) {
            ++skipped;
            continue;
          }
          auto origin = p.at("origin");
          bool duplicate = false;
          for (const auto &old : presets)
            if (old.value("transfer_origin", Json()) == origin)
              duplicate = true;
          if (duplicate && !bundle.value("copy", false)) {
            ++skipped;
            continue;
          }
          p.erase("origin");
          p.erase("skip");
          p["transfer_origin"] = origin;
          bool replace = p.value("replace_default", false);
          p.erase("replace_default");
          if (replace) {
            p["id"] = "default";
            p["name"] = "默认预设";
            presets[0] = p;
          } else {
            p["id"] = uid("preset");
            auto name = p.value("name", std::string("导入的预设"));
            auto original = name;
            int n = 1;
            for (;;) {
              bool conflict = false;
              for (const auto &old : presets)
                if (old["name"] == name)
                  conflict = true;
              if (!conflict)
                break;
              name = original + "（导入" + std::to_string(n++) + "）";
            }
            p["name"] = name;
            presets.push_back(p);
          }
          ++added;
        }
        setting("chat_presets", presets);
      } else {
        std::set<std::string> origins;
        for (const auto &old : query("SELECT metadata_json FROM chat_sessions WHERE "
                                     "metadata_json LIKE '%transfer_origin%'"))
          origins.insert(objectField(old, "metadata_json")
                             .value("transfer_origin", Json())
                             .dump());
        for (const auto &c : bundle.at("items")) {
          if (c.value("skip", false)) {
            ++skipped;
            continue;
          }
          auto origin = c.at("origin");
          // Provenance lives in existing metadata; no schema or ID replacement.
          bool duplicate = origins.count(origin.dump()) != 0;
          if (duplicate && !bundle.value("copy", false)) {
            ++skipped;
            continue;
          }
          origins.insert(origin.dump());
          auto sid = uid("session");
          auto meta = c.value("metadata", Json::object());
          meta["transfer_origin"] = origin;
          meta["title_state"] = "manual";
          auto model = c.value("model_id", std::string());
          auto found =
              query("SELECT provider_id FROM model_configs WHERE id=? AND enabled=1",
                    {model});
          Json provider = found.empty() ? Json() : found[0]["provider_id"];
          execute("INSERT INTO "
                  "chat_sessions(id,title,status,created_at,updated_at,last_message_at,"
                  "message_count,preview_text,metadata_json,pinned_at,default_model_"
                  "config_id,default_provider_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
                  {sid, c.at("title"), c.value("status", std::string("active")),
                   c.at("created_at"), c.at("updated_at"),
                   c.value("last_message_at", int64_t(0)), c.at("messages").size(),
                   c.value("preview_text", std::string()), meta.dump(),
                   c.value("pinned_at", int64_t(0)),
                   model.empty() ? Json() : Json(model), provider});
          std::map<std::string, std::string> assets;
          for (const auto &a : c.at("assets")) {
            auto id = uid("asset");
            assets[a.at("id").get<std::string>()] = id;
            execute("INSERT INTO "
                    "message_assets(id,session_id,kind,source_type,original_path,"
                    "stored_path,display_src,mime_type,width,height,size_bytes,created_"
                    "at,metadata_json) VALUES(?,?,'image','import',?,?,?,?,?,?,?,?,?)",
                    {id, sid, a.at("path"), a.at("path"),
                     a.value("preview", std::string()),
                     a.value("mime_type", std::string()), a.value("width", 0),
                     a.value("height", 0), a.value("size_bytes", int64_t(0)), nowMs(),
                     "{}"});
          }
          for (const auto &m : c.at("messages")) {
            auto id = uid("message");
            auto mm = m.value("model_id", std::string());
            auto mf = query(
                "SELECT provider_id FROM model_configs WHERE id=? AND enabled=1", {mm});
            auto metadata = m.value("metadata", Json::object());
            execute(
                "INSERT INTO "
                "chat_messages(id,session_id,seq,role,status,message_type,created_at,"
                "updated_at,provider_id,model_config_id,output_text,reasoning_text,"
                "metadata_json,request_json) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                {id, sid, m.at("seq"), m.at("role"), m.at("status"),
                 m.at("message_type"), m.at("created_at"), m.at("updated_at"),
                 mf.empty() ? Json() : mf[0]["provider_id"],
                 mm.empty() ? Json() : Json(mm), m.at("text"), m.at("reasoning"),
                 metadata.dump(), m.value("request", Json::object()).dump()});
            int index = 0;
            for (const auto &part : m.at("parts")) {
              auto key = part.value("asset", std::string());
              auto a = assets.find(key);
              if (part.at("type") == "image" && a == assets.end())
                continue;
              execute("INSERT INTO "
                      "message_parts(id,session_id,message_id,part_index,part_type,"
                      "text,asset_id,json) VALUES(?,?,?,?,?,?,?,?)",
                      {uid("part"), sid, id, index++, part.at("type"),
                       part.value("text", std::string()),
                       a == assets.end() ? Json() : Json(a->second), "{}"});
              if (a != assets.end())
                execute("UPDATE message_assets SET message_id=coalesce(message_id,?) "
                        "WHERE id=?",
                        {id, a->second});
            }
          }
          ++added;
        }
      }
      execute("COMMIT");
    } catch (...) {
      execute("ROLLBACK");
      throw;
    }
    return Json{{"added", added}, {"skipped", skipped}};
  });
}
} // namespace penchat
