#include "database.hpp"
#include "chat_time.hpp"
#include "context.hpp"
#include "reasoning.hpp"
#include "titles.hpp"
#include <cctype>
#include <limits>
#include <stdexcept>
namespace penchat {
namespace {
std::string fieldText(const Json &row, const char *key) {
  auto i=row.find(key);return i!=row.end()&&i->is_string()?i->get<std::string>():std::string();
}
Json pendingPhotos(const Json &rows) {
  int64_t last=0;for(const auto &m:rows)if(m["role"]=="assistant")last=m["seq"];
  Json pending=Json::array();
  for(const auto &m:rows)if(m["role"]=="user"&&m["seq"].get<int64_t>()>last&&
      m["message_type"]=="image"&&fieldText(m,"output_text").empty())pending.push_back(m);
  return pending;
}
int64_t assistantSequence(const Json &rows,const std::string &id) {
  for(const auto &m:rows)if(m["id"]==id&&m["role"]=="assistant"&&m["status"]!="streaming")return m["seq"];
  throw std::runtime_error("回复已变化，请重新选择");
}
void cancelled(std::atomic<bool> *cancel) {
  if(cancel&&*cancel)throw std::runtime_error("操作已取消，原会话保持完整");
}
}
void Database::copyConversationRows(const Json &session,const Json &rows,
                                     std::atomic<bool> *cancel) {
  auto copy=[&](const std::string &table,const Json &row) {
    cancelled(cancel);
    std::string columns,placeholders;Json args=Json::array();
    for(auto i=row.begin();i!=row.end();++i) {
      if(!columns.empty()){columns+=',';placeholders+=',';}
      columns+=i.key();placeholders+='?';args.push_back(i.value());
    }
    execute("INSERT INTO "+table+"("+columns+") VALUES("+placeholders+")",args);
  };
  auto target=session.at("id").get<std::string>();
  copy("chat_sessions",session);
  std::map<std::string,std::string> messages,assets;
  for(auto row:rows) {
    auto old=row.at("id").get<std::string>();messages[old]=uid("message");
    row.erase("parts");row["id"]=messages[old];row["session_id"]=target;
    copy("chat_messages",row);
  }
  for(const auto &m:rows) {
    const auto old=m.at("id").get<std::string>(),message=messages.at(old);
    for(auto part:query("SELECT * FROM message_parts WHERE message_id=? ORDER BY part_index",{old})) {
      if(part["asset_id"].is_string()) {
        auto aid=part["asset_id"].get<std::string>();
        if(!assets.count(aid)) {
          auto found=query("SELECT * FROM message_assets WHERE id=?",{aid});
          if(found.empty())throw std::runtime_error("图片关联缺失，未复制会话");
          auto asset=found[0];assets[aid]=uid("asset");
          asset["id"]=assets[aid];asset["session_id"]=target;
          const auto owner=fieldText(asset,"message_id");
          asset["message_id"]=messages.count(owner)?messages.at(owner):message;
          auto metadata=objectField(asset,"metadata_json");metadata.erase("_gc_detached");asset["metadata_json"]=metadata.dump();
          copy("message_assets",asset);
        }
        part["asset_id"]=assets.at(aid);
      }
      part["id"]=uid("part");part["session_id"]=target;part["message_id"]=message;
      copy("message_parts",part);
    }
    for(auto event:query("SELECT * FROM stream_events WHERE message_id=? ORDER BY seq",{old})) {
      event["id"]=uid("event");event["session_id"]=target;event["message_id"]=message;copy("stream_events",event);
    }
  }
}
void Database::deleteConversationRows(const std::string &session,const Json &rows) {
  for(const auto &m:rows) {
    const auto id=m.at("id").get<std::string>();
    enqueueAssets(query("SELECT stored_path,display_src FROM message_assets WHERE message_id=? AND session_id=?",{id,session}));
    execute("DELETE FROM stream_events WHERE message_id=? AND session_id=?",{id,session});
    execute("DELETE FROM message_parts WHERE message_id=? AND session_id=?",{id,session});
    detachSharedAssets("message_id",id);
    execute("DELETE FROM message_assets WHERE message_id=? AND session_id=?",{id,session});
    execute("DELETE FROM chat_messages WHERE id=? AND session_id=?",{id,session});
  }
}
void Database::refreshConversationCount(const std::string &session) {
  auto last=query("SELECT output_text,message_type FROM chat_messages WHERE session_id=? ORDER BY seq DESC LIMIT 1",{session});
  std::string preview;
  if(!last.empty())preview=last[0]["message_type"]=="image"?"[图片]":titleExcerpt(fieldText(last[0],"output_text"),40);
  execute("UPDATE chat_sessions SET message_count=(SELECT count(*) FROM chat_messages WHERE session_id=?),preview_text=? WHERE id=?",{session,preview,session});
}
Json Database::commitRegeneration(const Json &snapshot,const std::string &message,
                                  bool archive,Json checkpoint,std::atomic<bool> &cancel) {
  return perform([&,snapshot,message,archive,checkpoint]() mutable {
    cancelled(&cancel);const auto source=snapshot.at("session").at("id").get<std::string>();
    execute("BEGIN IMMEDIATE");
    try {
      auto current=contextSnapshot(source);
      if(!current.contains("session")||current["rows"]!=snapshot.at("rows")||current["checkpoint"]!=snapshot.at("checkpoint"))
        throw std::runtime_error("会话已变化，未应用过期重试");
      const auto seq=assistantSequence(current["rows"],message);
      Json prefix=Json::array();bool question=false;
      for(const auto &m:current["rows"])if(m["seq"].get<int64_t>()<seq) {
        prefix.push_back(m);if(m["role"]=="user"&&(!fieldText(m,"output_text").empty()||!m["parts"].empty()))question=true;
      }
      if(!question)throw std::runtime_error("没有对应的用户问题，无法重新生成");
      if(!checkpoint.empty()&&!validContext(checkpoint,prefix))throw std::runtime_error("重试上下文摘要无效，原会话保持完整");
      auto saved=(archive||!checkpoint.empty())?archiveCopy(current,archive?"重试前副本":"压缩前副本",archive?"retry_archive":"compression_archive"):std::string();
      auto pending=pendingPhotos(current["rows"]);std::set<std::string> keep;
      for(const auto &m:pending)keep.insert(m.at("id").get<std::string>());
      Json removed=Json::array();
      for(const auto &m:current["rows"])if(m["seq"].get<int64_t>()>=seq&&!keep.count(m.at("id").get<std::string>()))removed.push_back(m);
      deleteConversationRows(source,removed);
      execute("INSERT INTO chat_messages(id,session_id,seq,role,status,message_type,created_at,updated_at,output_text,metadata_json) VALUES(?,?,?,'assistant','interrupted','text',?,?,'',?)",
              {message,source,seq,nowMs(),nowMs(),Json{{"timeline",{{"version",1}}}}.dump()});
      auto next=seq+1;
      for(const auto &m:pending)execute("UPDATE chat_messages SET seq=? WHERE id=? AND session_id=?",{next++,m["id"],source});
      auto meta=objectField(current["session"],"metadata_json");meta.erase("context_compression");meta.erase("title_token");meta["title_state"]="manual";
      if(!checkpoint.empty()){checkpoint["archive_id"]=saved;meta["context_compression"]=checkpoint;}
      execute("UPDATE chat_sessions SET metadata_json=? WHERE id=?",{meta.dump(),source});refreshConversationCount(source);
      cancelled(&cancel);execute("COMMIT");gcCursor_.clear();deferAssetCollection();
      return Json{{"message",message},{"archive",saved}};
    }catch(...){execute("ROLLBACK");throw;}
  });
}
Json Database::createBranch(const Json &snapshot,const std::string &message,
                            const Json &preferences,std::atomic<bool> &cancel) {
  return perform([&,snapshot,message,preferences] {
    cancelled(&cancel);const auto source=snapshot.at("session").at("id").get<std::string>();
    execute("BEGIN IMMEDIATE");
    try {
      auto current=contextSnapshot(source);
      if(!current.contains("session")||current["rows"]!=snapshot.at("rows"))throw std::runtime_error("会话已变化，未创建过期分支");
      const auto through=assistantSequence(current["rows"],message);
      auto session=current["session"],meta=objectField(session,"metadata_json");
      const auto oldBranch=meta.value("branch",Json::object());
      std::string root=source,base=fieldText(session,"title");
      if(oldBranch.is_object()&&oldBranch.value("title",std::string())==base) {
        root=oldBranch.value("root_id",source);base=oldBranch.value("base_title",base);
      }
      int64_t number=0;std::string cursor;
      if(!oldBranch.is_object()||oldBranch.value("title",std::string())!=fieldText(session,"title")) {
        const auto suffix=base.rfind(" · 分支");
        if(suffix!=std::string::npos) {
          const auto tail=base.substr(suffix+std::string(" · 分支").size());
          if(tail.empty()||(tail.size()<18&&std::all_of(tail.begin(),tail.end(),[](unsigned char c){return c>='0'&&c<='9';}))) {
            number=tail.empty()?1:std::stoll(tail);base.resize(suffix);
          }
        }
      }
      for(;;) {
        auto page=query("SELECT id,title,metadata_json FROM chat_sessions WHERE id>? ORDER BY id LIMIT 100",{cursor});
        if(page.empty())break;
        for(const auto &s:page) {
          cursor=s["id"].get<std::string>();auto b=objectField(s,"metadata_json").value("branch",Json::object());
          if(b.is_object()&&b.value("root_id",std::string())==root&&b.value("ordinal",Json()).is_number_integer())number=std::max(number,b["ordinal"].get<int64_t>());
          const auto title=fieldText(s,"title"),prefix=base+" · 分支";
          if(title.compare(0,prefix.size(),prefix)==0) {
            const auto tail=title.substr(prefix.size());
            if(tail.empty())number=std::max(number,int64_t(1));
            else if(tail.size()<18&&std::all_of(tail.begin(),tail.end(),[](unsigned char c){return c>='0'&&c<='9';}))number=std::max(number,int64_t(std::stoll(tail)));
          }
        }
        cancelled(&cancel);
      }
      std::string title;
      do {
        if(number>=INT64_MAX-1)throw std::runtime_error("分支编号超出范围");
        ++number;const auto suffix=" · 分支"+(number==1?std::string():std::to_string(number));
        title=titleExcerpt(base,256-characters(suffix).size())+suffix;cancelled(&cancel);
      }while(!query("SELECT id FROM chat_sessions WHERE title=? LIMIT 1",{title}).empty());
      Json rows=Json::array();int64_t lastTime=0;
      for(const auto &m:current["rows"])if(m["seq"].get<int64_t>()<=through) {
        rows.push_back(m);auto time=objectField(m,"metadata_json").value("timeline",Json::object());
        lastTime=std::max(lastTime,std::max(chatTimestamp(time,"sent_at"),chatTimestamp(time,"completed_at")));
      }
      auto pending=pendingPhotos(current["rows"]);auto seq=through+1;
      for(auto m:pending){m["seq"]=seq++;rows.push_back(std::move(m));}
      for(const auto key:{"pinned_at","context_compression","compression_archive","edit_archive","retry_archive","title_token","title_model_id","title_assistant_id"})meta.erase(key);
      meta["title_state"]="manual";meta["chat_preferences"]=preferences;
      const auto id=uid("session");meta["branch"]={{"root_id",root},{"base_title",base},{"title",title},{"ordinal",number},{"source_session",source},{"source_message",message},{"created_at",nowMs()}};
      meta.erase("timeline");if(lastTime)meta["timeline"]={{"version",1},{"last_activity_at",lastTime}};
      session["id"]=id;session["title"]=title;session["status"]="active";session["pinned_at"]=0;
      session["created_at"]=session["updated_at"]=nowMs();session["last_message_at"]=lastTime;
      session["message_count"]=rows.size();session["metadata_json"]=meta.dump();
      copyConversationRows(session,rows,&cancel);saveSessionPreferences(id,preferences);refreshConversationCount(id);
      deleteConversationRows(source,pending);refreshConversationCount(source);setting("active_session_id",id);
      cancelled(&cancel);execute("COMMIT");gcCursor_.clear();deferAssetCollection();
      return Json{{"id",id},{"title",title},{"pending",pending.size()}};
    }catch(...){execute("ROLLBACK");throw;}
  });
}
} // namespace penchat
