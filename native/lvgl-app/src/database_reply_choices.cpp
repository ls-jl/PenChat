#include "database.hpp"
#include "context.hpp"
#include "usage.hpp"
#include "reasoning.hpp"
#include <sqlite3.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdexcept>
namespace penchat {
namespace {
void insertRow(Database &db,const std::string &table,Json row) {
  std::string columns,values;Json args=Json::array();
  for(auto i=row.begin();i!=row.end();++i) {
    if(i.key().empty()||i.key()[0]=='_'||i.key()=="parts")continue;
    if(!columns.empty()){columns+=',';values+=',';}
    columns+=i.key();values+='?';args.push_back(i.value());
  }
  db.execute("INSERT INTO "+table+"("+columns+") VALUES("+values+")",args);
}
void sameView(const Json &a,const Json &b) {
  if(!a.contains("session")||a["rows"]!=b.at("rows")||a["checkpoint"]!=b.at("checkpoint")||
     a.value("reply_choices",Json::object())!=b.value("reply_choices",Json::object()))
    throw std::runtime_error("会话已变化，未应用过期操作");
}
void addSpent(Json &spent,const Json &usage) {
  const auto normalized=normalizedUsage(usage);
  const auto replies=spent.value("replies",int64_t(0)),known=spent.value("known",int64_t(0));
  spent["replies"]=replies+1;spent["known"]=known+(normalized.empty()?0:1);
  auto totals=spent.value("totals",Json::object()),reported=spent.value("reported",Json::object());
  for(auto it=normalized.begin();it!=normalized.end();++it) {
    const auto prior=totals.value(it.key(),int64_t(0)),samples=reported.value(it.key(),int64_t(0));
    if(it.value().get<int64_t>()>INT64_MAX-prior)throw std::runtime_error("用量累计超出范围");
    totals[it.key()]=prior+it.value().get<int64_t>();reported[it.key()]=samples+1;
  }
  spent["totals"]=totals;spent["reported"]=reported;
  if(normalized.contains("input_tokens")&&normalized.contains("cached_tokens")) {
    const auto input=spent.value("cache_input",int64_t(0)),read=spent.value("cache_read",int64_t(0)),samples=spent.value("cache_samples",int64_t(0));
    spent["cache_input"]=input+normalized["input_tokens"].get<int64_t>();spent["cache_read"]=read+normalized["cached_tokens"].get<int64_t>();spent["cache_samples"]=samples+1;
  }
}
}
void Database::migrateReplyChoices(const std::string &path) {
  auto version=queryOnWorker("SELECT value FROM schema_meta WHERE key='schema_version'",Json::array());
  if(!version.empty()&&version[0]["value"]=="3") {
    auto bad=queryOnWorker("SELECT count(*) n FROM reply_groups g LEFT JOIN chat_messages m ON m.id=g.message_id WHERE m.id IS NULL OR m.session_id!=g.session_id OR m.role!='assistant' OR NOT EXISTS(SELECT 1 FROM reply_results r WHERE r.id=g.selected_id AND r.group_id=g.id)",Json::array())[0]["n"].get<int64_t>();
    bad+=queryOnWorker("SELECT count(*) n FROM reply_asset_refs a WHERE a.owner_id!=a.group_id AND NOT EXISTS(SELECT 1 FROM reply_results r WHERE r.id=a.owner_id AND r.group_id=a.group_id)",Json::array())[0]["n"].get<int64_t>();
    if(bad||!queryOnWorker("PRAGMA foreign_key_check",Json::array()).empty())throw std::runtime_error("重试结果所有权异常，未修改数据库");
    return;
  }
  if(version.empty()||version[0]["value"]!="2")throw std::runtime_error("Unsupported reply schema version");
  if(!queryOnWorker("PRAGMA foreign_key_check",Json::array()).empty())throw std::runtime_error("Reply migration ownership check failed");
  // Publish the durable pre-v3 snapshot before creating any new table.
  const auto backupPath=path+".backup-v2-"+uid("retry"),temporary=backupPath+".tmp";
  sqlite3 *copy=nullptr;
  if(sqlite3_open(temporary.c_str(),&copy)!=SQLITE_OK){if(copy)sqlite3_close(copy);throw std::runtime_error("Cannot create reply migration backup");}
  auto backup=sqlite3_backup_init(copy,"main",db_,"main");
  int rc=backup?sqlite3_backup_step(backup,-1):SQLITE_ERROR;
  if(backup&&sqlite3_backup_finish(backup)!=SQLITE_OK)rc=SQLITE_ERROR;
  sqlite3_close(copy);chmod(temporary.c_str(),0600);
  int fd=open(temporary.c_str(),O_RDONLY|O_CLOEXEC);bool ok=fd>=0&&fsync(fd)==0;if(fd>=0)close(fd);
  if(rc!=SQLITE_DONE||!ok||rename(temporary.c_str(),backupPath.c_str())){unlink(temporary.c_str());throw std::runtime_error("Cannot publish reply migration backup");}
  const auto slash=path.find_last_of('/');const auto dir=slash==std::string::npos?std::string("."):path.substr(0,slash);
  fd=open(dir.c_str(),O_DIRECTORY|O_RDONLY|O_CLOEXEC);ok=fd>=0&&fsync(fd)==0;if(fd>=0)close(fd);
  if(!ok)throw std::runtime_error("Cannot synchronize reply migration backup");
  auto run=[this](const std::string &sql){return queryOnWorker(sql,Json::array());};
  run("BEGIN IMMEDIATE");
  try {
    run("CREATE TABLE reply_groups(id TEXT PRIMARY KEY NOT NULL,session_id TEXT NOT NULL REFERENCES chat_sessions(id) ON DELETE CASCADE,message_id TEXT NOT NULL UNIQUE REFERENCES chat_messages(id) ON DELETE CASCADE,selected_id TEXT NOT NULL,archive INTEGER NOT NULL DEFAULT 0,baseline_json TEXT NOT NULL,created_at INTEGER NOT NULL)");
    run("CREATE TABLE reply_results(id TEXT PRIMARY KEY NOT NULL,group_id TEXT NOT NULL REFERENCES reply_groups(id) ON DELETE CASCADE,ordinal INTEGER NOT NULL CHECK(ordinal>=1),payload_json TEXT NOT NULL,usage_json TEXT NOT NULL DEFAULT '{}',UNIQUE(group_id,ordinal))");
    run("CREATE TABLE reply_asset_refs(group_id TEXT NOT NULL REFERENCES reply_groups(id) ON DELETE CASCADE,owner_id TEXT NOT NULL,asset_id TEXT NOT NULL REFERENCES message_assets(id),PRIMARY KEY(owner_id,asset_id))");
    run("CREATE INDEX idx_reply_groups_session ON reply_groups(session_id)");
    run("CREATE INDEX idx_reply_assets_asset ON reply_asset_refs(asset_id)");
    run("UPDATE schema_meta SET value='3' WHERE key='schema_version'");run("COMMIT");
  }catch(...){run("ROLLBACK");throw;}
  for(auto &entry:statements_)sqlite3_finalize(entry.second);
  statements_.clear();
}
Json Database::replyChoices(const std::string &session) {
  return perform([=] {
    Json result=Json::object();
    for(const auto &row:query("SELECT g.id,g.message_id,g.selected_id,g.archive,r.ordinal,m.seq,(SELECT count(*) FROM reply_results a WHERE a.group_id=g.id) total FROM reply_groups g JOIN reply_results r ON r.id=g.selected_id JOIN chat_messages m ON m.id=g.message_id WHERE g.session_id=? ORDER BY m.seq",{session}))
      result[row["message_id"].get<std::string>()]=row;
    return result;
  });
}
int64_t Database::replyCut(const std::string &session) {
  return perform([=] {
    auto rows=query("SELECT min(m.seq) cut FROM reply_groups g JOIN reply_results r ON r.id=g.selected_id JOIN chat_messages m ON m.id=g.message_id WHERE g.session_id=? AND r.ordinal>1",{session});
    return Json(rows[0]["cut"].is_null()?INT64_MAX:rows[0]["cut"].get<int64_t>());
  }).get<int64_t>();
}
bool Database::replyHasLater(const std::string &session,const std::string &message) {
  return perform([=] {
    return Json(!query("SELECT id FROM chat_messages WHERE session_id=? AND seq>(SELECT seq FROM chat_messages WHERE id=?) AND seq<=? AND NOT(role='user' AND message_type='image' AND coalesce(output_text,'')='' AND seq>(SELECT coalesce(max(seq),0) FROM chat_messages WHERE session_id=? AND role='assistant')) LIMIT 1",{session,message,replyCut(session),session}).empty());
  }).get<bool>();
}
Json Database::replyMessageSnapshot(const std::string &message) {
  auto rows=query("SELECT * FROM chat_messages WHERE id=?",{message});
  if(rows.empty())throw std::runtime_error("回复已不存在");
  auto row=rows[0];
  row["_parts"]=query("SELECT * FROM message_parts WHERE message_id=? ORDER BY part_index",{message});
  row["_events"]=query("SELECT * FROM stream_events WHERE message_id=? ORDER BY seq",{message});
  row["_checkpoint"]=objectField(sessionInfo(row["session_id"]),"metadata_json").value("context_compression",Json::object());
  return row;
}
void Database::pinReplyAssets(const std::string &group,const std::string &owner,const Json &rows) {
  execute("DELETE FROM reply_asset_refs WHERE owner_id=?",{owner});
  for(const auto &row:rows)for(const auto &part:row.value("_parts",Json::array()))if(part["asset_id"].is_string())
    execute("INSERT OR IGNORE INTO reply_asset_refs VALUES(?,?,?)",{group,owner,part["asset_id"]});
}
void Database::saveReplyResult(const std::string &message) {
  auto groups=query("SELECT id,selected_id FROM reply_groups WHERE message_id=?",{message});
  if(groups.empty())return;
  const auto payload=replyMessageSnapshot(message);
  execute("UPDATE reply_results SET payload_json=?,usage_json=? WHERE id=?",{payload.dump(),objectField(payload,"metadata_json").value("usage",Json::object()).dump(),groups[0]["selected_id"]});
  pinReplyAssets(groups[0]["id"],groups[0]["selected_id"],Json::array({payload}));
}
void Database::restoreReplyResult(const Json &payload) {
  const auto id=payload.at("id").get<std::string>(),session=payload.at("session_id").get<std::string>();
  execute("DELETE FROM message_parts WHERE message_id=?",{id});execute("DELETE FROM stream_events WHERE message_id=?",{id});
  enqueueAssets(query("SELECT stored_path,display_src FROM message_assets WHERE message_id=?",{id}));
  detachSharedAssets("message_id",id);execute("DELETE FROM message_assets WHERE message_id=?",{id});
  std::string sql;Json args=Json::array();
  for(auto it=payload.begin();it!=payload.end();++it) {
    if(it.key()=="id"||it.key()=="parts"||it.key()[0]=='_')continue;
    if(!sql.empty())sql+=',';
    sql+=it.key()+"=?";args.push_back(it.value());
  }
  args.push_back(id);execute("UPDATE chat_messages SET "+sql+" WHERE id=?",args);
  for(auto part:payload.value("_parts",Json::array())){part["id"]=uid("part");part["message_id"]=id;part["session_id"]=session;insertRow(*this,"message_parts",part);}
  for(auto event:payload.value("_events",Json::array())){event["id"]=uid("event");event["message_id"]=id;insertRow(*this,"stream_events",event);}
  auto meta=objectField(sessionInfo(session),"metadata_json");meta.erase("context_compression");
  const auto checkpoint=payload.value("_checkpoint",Json::object());if(!checkpoint.empty())meta["context_compression"]=checkpoint;
  execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?",{meta.dump(),session});
}
Json Database::beginReplyResult(const Json &snapshot,const std::string &message,const Json &archive,const Json &checkpoint,std::atomic<bool> &cancel) {
  return perform([&,snapshot,message,archive,checkpoint] {
    if(cancel)throw std::runtime_error("重试已取消");
    const auto session=snapshot.at("session").at("id").get<std::string>();
    execute("BEGIN IMMEDIATE");
    try {
      sameView(contextSnapshot(session),snapshot);
      bool found=false,question=false;
      for(const auto &row:snapshot.at("rows")) {
        if(row["id"]==message){found=row["role"]=="assistant"&&row["status"]!="streaming";break;}
        if(row["role"]=="user"&&((row["output_text"].is_string()&&!row["output_text"].get<std::string>().empty())||!row["parts"].empty()))question=true;
      }
      if(!found||!question)throw std::runtime_error("回复已变化或没有对应问题，无法重试");
      auto groups=query("SELECT * FROM reply_groups WHERE message_id=? AND session_id=?",{message,session});
      std::string group;
      if(groups.empty()) {
        group=uid("reply-group");const auto original=replyMessageSnapshot(message);const auto first=uid("reply-result");
        auto baseline=snapshot;
        for(auto &row:baseline["rows"]){auto full=replyMessageSnapshot(row["id"]);row["_parts"]=full["_parts"];row["_events"]=full["_events"];}
        execute("INSERT INTO reply_groups VALUES(?,?,?,?,?,?,?)",{group,session,message,first,archive.is_boolean()&&archive.get<bool>()?1:0,baseline.dump(),nowMs()});
        execute("INSERT INTO reply_results(id,group_id,ordinal,payload_json,usage_json) VALUES(?,?,1,?,?)",{first,group,original.dump(),objectField(original,"metadata_json").value("usage",Json::object()).dump()});
        pinReplyAssets(group,group,baseline["rows"]);pinReplyAssets(group,first,Json::array({original}));
      } else {group=groups[0]["id"];saveReplyResult(message);if(archive.is_boolean())execute("UPDATE reply_groups SET archive=? WHERE id=?",{archive.get<bool>()?1:0,group});}
      const auto maximum=query("SELECT max(ordinal) n FROM reply_results WHERE group_id=?",{group})[0]["n"].get<int64_t>();
      if(maximum>=INT64_MAX-1)throw std::runtime_error("重试结果编号超出范围");
      const int64_t ordinal=maximum+1;
      const auto previous=query("SELECT selected_id FROM reply_groups WHERE id=?",{group})[0]["selected_id"];
      const auto result=uid("reply-result");auto blank=replyMessageSnapshot(message);
      blank["output_text"]="";blank["reasoning_text"]="";blank["status"]="streaming";blank["error_json"]=nullptr;blank["request_json"]=nullptr;blank["response_id"]=nullptr;
      blank["metadata_json"]=Json{{"timeline",{{"version",1}}}}.dump();blank["created_at"]=nowMs();blank["updated_at"]=nowMs();
      blank["_parts"]=Json::array({{{"id",uid("part")},{"session_id",session},{"message_id",message},{"part_index",0},{"part_type","text"},{"text",""},{"asset_id",nullptr},{"json","{}"}}});
      auto preparedCheckpoint=checkpoint;
      if(!preparedCheckpoint.empty())preparedCheckpoint["archive_id"]=archiveCopy(snapshot,"压缩前副本","compression_archive");
      blank["_events"]=Json::array();blank["_checkpoint"]=preparedCheckpoint;
      blank["_previous_result"]=previous;blank["_new_group"]=groups.empty();
      execute("INSERT INTO reply_results(id,group_id,ordinal,payload_json) VALUES(?,?,?,?)",{result,group,ordinal,blank.dump()});
      execute("UPDATE reply_groups SET selected_id=? WHERE id=?",{result,group});
      restoreReplyResult(blank);messageCursors_.clear();
      if(cancel)throw std::runtime_error("重试已取消");
      execute("COMMIT");deferAssetCollection();
      return Json{{"result",result},{"ordinal",ordinal}};
    }catch(...){execute("ROLLBACK");throw;}
  });
}
void Database::discardPreparedReply(const std::string &message,const std::string &result) {
  perform([=] {
    execute("BEGIN IMMEDIATE");
    try {
      auto rows=query("SELECT r.payload_json,g.id FROM reply_results r JOIN reply_groups g ON g.id=r.group_id JOIN chat_messages m ON m.id=g.message_id WHERE r.id=? AND g.message_id=? AND g.selected_id=r.id AND m.request_json IS NULL",{result,message});
      if(!rows.empty()) {
        const auto payload=Json::parse(rows[0]["payload_json"].get<std::string>());
        const auto previous=payload.value("_previous_result",std::string());
        auto old=query("SELECT payload_json FROM reply_results WHERE id=? AND group_id=?",{previous,rows[0]["id"]});
        if(!old.empty()) {
          execute("UPDATE reply_groups SET selected_id=? WHERE id=?",{previous,rows[0]["id"]});restoreReplyResult(Json::parse(old[0]["payload_json"].get<std::string>()));
          execute("DELETE FROM reply_asset_refs WHERE owner_id=?",{result});execute("DELETE FROM reply_results WHERE id=?",{result});
          if(payload.value("_new_group",false))execute("DELETE FROM reply_groups WHERE id=?",Json::array({rows[0]["id"]}));
          messageCursors_.clear();
        }
      }
      execute("COMMIT");deferAssetCollection();
    }catch(...){execute("ROLLBACK");throw;}
    return Json();
  });
}
void Database::selectReplyResult(const std::string &session,const std::string &message,int ordinal) {
  perform([=] {
    execute("BEGIN IMMEDIATE");
    try {
      auto current=contextSnapshot(session);
      auto rows=query("SELECT g.id group_id,g.selected_id,r.id,r.payload_json FROM reply_groups g JOIN reply_results r ON r.group_id=g.id WHERE g.session_id=? AND g.message_id=? AND r.ordinal=?",{session,message,ordinal});
      if(rows.empty())throw std::runtime_error("结果已不存在");
      if(rows[0]["id"]==rows[0]["selected_id"]){execute("COMMIT");return Json();}
      auto selected=Json::parse(rows[0]["payload_json"].get<std::string>());
      if(selected["status"]=="streaming")throw std::runtime_error("请等待回复保存完成");
      saveReplyResult(message);
      execute("UPDATE reply_groups SET selected_id=? WHERE id=?",{rows[0]["id"],rows[0]["group_id"]});
      restoreReplyResult(selected);messageCursors_.clear();
      execute("COMMIT");deferAssetCollection();
    }catch(...){execute("ROLLBACK");throw;}
    return Json();
  });
}
void Database::clearReplyChoices(const std::string &session,int64_t removedAfter) {
  // Preserve billed attempts even when their content is discarded. Each cleared
  // group is accounted once in this same transaction; no body/private state here.
  auto meta=objectField(sessionInfo(session),"metadata_json");
  Json visible=query("SELECT id FROM chat_messages WHERE session_id=? AND seq<=?",{session,removedAfter});
  for(const auto &group:query("SELECT id,selected_id,message_id FROM reply_groups WHERE session_id=?",{session})) {
    saveReplyResult(group["message_id"]);
  }
  meta["discarded_retry_usage"]=replyUsageExtras(session,visible);execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?",{meta.dump(),session});
  execute("DELETE FROM reply_groups WHERE session_id=?",{session});
}
Json Database::replyUsageExtras(const std::string &session,const Json &visible) {
  auto spent=objectField(sessionInfo(session),"metadata_json").value("discarded_retry_usage",Json::object());
  std::set<std::string> ids;for(const auto &row:visible)ids.insert(row["id"]);
  for(const auto &row:query("SELECT id,metadata_json FROM chat_messages WHERE session_id=? AND role='assistant'",{session}))
    if(!ids.count(row["id"].get<std::string>()))addSpent(spent,objectField(row,"metadata_json").value("usage",Json::object()));
  for(const auto &row:query("SELECT r.usage_json FROM reply_results r JOIN reply_groups g ON g.id=r.group_id WHERE g.session_id=? AND r.id!=g.selected_id",{session}))
    addSpent(spent,objectField(row,"usage_json"));
  return spent;
}
Json Database::commitReplyContinuation(const Json &snapshot,const std::string &text,const Json &preferences,const Json &checkpoint,const Json &images,bool ownTransaction) {
  return perform([=] {
    const auto session=snapshot.at("session").at("id").get<std::string>();
    if(ownTransaction)execute("BEGIN IMMEDIATE");
    try {
      const auto current=contextSnapshot(session);sameView(current,snapshot);
      const auto cut=replyCut(session);
      const auto group=query("SELECT g.* FROM reply_groups g JOIN chat_messages m ON m.id=g.message_id WHERE g.session_id=? AND m.seq=?",{session,cut});
      if(!group.empty()&&group[0]["archive"].get<int>())archiveCopy(Json::parse(group[0]["baseline_json"].get<std::string>()),"重试前副本","retry_archive");
      auto meta=objectField(sessionInfo(session),"metadata_json");
      if(!checkpoint.empty()) {
        const auto archived=archiveCopy(current,"压缩前副本","compression_archive");auto saved=checkpoint;saved["archive_id"]=archived;meta["context_compression"]=saved;
        execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?",{meta.dump(),session});
      }
      clearReplyChoices(session,cut);
      if(cut!=INT64_MAX) {
        auto pending=query("SELECT * FROM chat_messages WHERE session_id=? AND role='user' AND message_type='image' AND coalesce(output_text,'')='' AND seq>(SELECT max(seq) FROM chat_messages WHERE session_id=? AND role='assistant') ORDER BY seq",{session,session});
        std::set<std::string> keep;for(const auto &row:pending)keep.insert(row["id"]);
        Json removed=Json::array();for(const auto &row:query("SELECT * FROM chat_messages WHERE session_id=? AND seq>? ORDER BY seq",{session,cut}))if(!keep.count(row["id"].get<std::string>()))removed.push_back(row);
        deleteConversationRows(session,removed);int64_t next=cut+1;
        for(const auto &row:pending)execute("UPDATE chat_messages SET seq=? WHERE id=?",{next++,row["id"]});
      }
      saveSessionPreferences(session,preferences);
      for(const auto &image:images)append(session,"user","",image,preferences);
      if(!text.empty())append(session,"user",text,Json::object(),preferences);
      const auto assistant=append(session,"assistant","",Json::object(),preferences);
      const auto recordModel=snapshot.value("_record_model",Json::object());
      if(!recordModel.empty())recordRequest(assistant,recordModel,snapshot.value("_sent_at",int64_t(0)),nowMs());
      if(ownTransaction)execute("COMMIT");
      messageCursors_.clear();gcCursor_.clear();deferAssetCollection();
      return Json{{"message",assistant}};
    }catch(...){if(ownTransaction)execute("ROLLBACK");throw;}
  });
}
} // namespace penchat
