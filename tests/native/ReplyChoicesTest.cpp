#include "TestFiles.hpp"
#include "database.hpp"
#include "reasoning.hpp"
#include "usage.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
#include <sqlite3.h>
using namespace penchat;
static std::string body(Database &db,const std::string &id){return db.query("SELECT output_text FROM chat_messages WHERE id=?",{id})[0]["output_text"];}
int main() {
  char dir[]="/tmp/penchat-reply-choice-XXXXXX";assert(mkdtemp(dir));
  const auto path=join(dir,"penchat.db");std::string session,a1,a2,pending;
  std::atomic<bool> cancel{false};
  {
    Database db(path);assert(db.query("SELECT value FROM schema_meta WHERE key='schema_version'")[0]["value"]=="3");
    session=uid("session");db.append(session,"user","first question");a1=db.append(session,"assistant","");
    ensureDirectory(join(dir,"assets"));const auto web=join(join(dir,"assets"),"web-"+std::string(64,'a')+".png");atomicText(web,"private web fixture");
    db.updateMessage(a1,"first answer","thought one","complete",Json(),{{"usage",{{"input_tokens",10},{"output_tokens",4}}},{"web_images",Json::array({{{"ref","img_1"},{"path",web},{"mime","image/png"},{"width",32},{"height",32}}})}});
    db.append(session,"user","later question");a2=db.append(session,"assistant","");db.updateMessage(a2,"later answer","","complete");
    const auto photo=join(dir,"original.png");atomicText(photo,"original fixture");pending=db.append(session,"user","",{{"path",photo},{"source","album"}});
    const auto before=db.contextSnapshot(session);
    assert(db.replyHasLater(session,a1)&&!db.replyHasLater(session,a2));
    db.beginReplyResult(before,a1,true,Json::object(),cancel);db.updateMessage(a1,"second answer","thought two","complete",Json(),{{"usage",{{"input_tokens",20},{"output_tokens",6}}}});
    assert(db.messages(session).size()==3&&db.query("SELECT id FROM chat_messages WHERE id=?",{a2}).size()==1);
    assert(db.query("SELECT id FROM chat_sessions WHERE status='archived'").empty());
    assert(db.replyChoices(session)[a1]["ordinal"]==2&&db.replyChoices(session)[a1]["total"]==2);
    db.selectReplyResult(session,a1,1);assert(body(db,a1)=="first answer"&&db.messages(session).size()==5);
    assert(db.query("SELECT id FROM message_parts WHERE message_id=? AND part_type='image'",{a1}).size()==1&&fileText(web)=="private web fixture");
    db.beginReplyResult(db.contextSnapshot(session),a2,Json(),Json::object(),cancel);db.updateMessage(a2,"nested result","","complete");
    db.beginReplyResult(db.contextSnapshot(session),a1,Json(),Json::object(),cancel);db.updateMessage(a1,"third answer","","complete");
    assert(db.messages(session).size()==3);
    db.selectReplyResult(session,a1,1);assert(db.messages(session).size()==5&&body(db,a2)=="nested result");
    db.selectReplyResult(session,a1,2);assert(body(db,a1)=="second answer"&&db.messages(session).size()==3);
    auto snap=db.contextSnapshot(session);auto estimate=sessionMetrics(snap,Json::object(),Json::object());
    assert(estimate["totals"]["input_tokens"]==30);
    const auto exported=join(dir,"export.db");db.transferSnapshot(exported);db.releaseTransferSnapshot();
    sqlite3 *copy=nullptr;assert(sqlite3_open_v2(exported.c_str(),&copy,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);sqlite3_stmt *statement=nullptr;
    assert(sqlite3_prepare_v2(copy,"SELECT count(*) FROM chat_messages WHERE output_text='later question' OR output_text='nested result'",-1,&statement,nullptr)==SQLITE_OK);
    assert(sqlite3_step(statement)==SQLITE_ROW&&sqlite3_column_int(statement,0)==0);sqlite3_finalize(statement);
    assert(sqlite3_prepare_v2(copy,"SELECT count(*) FROM reply_groups",-1,&statement,nullptr)==SQLITE_OK);assert(sqlite3_step(statement)==SQLITE_ROW&&sqlite3_column_int(statement,0)==0);sqlite3_finalize(statement);sqlite3_close(copy);
    assert(db.replyChoices(session).size()==2);
    db.execute("CREATE TRIGGER fail_selection BEFORE UPDATE OF output_text ON chat_messages WHEN NEW.output_text='first answer' BEGIN SELECT RAISE(ABORT,'fixture'); END");
    bool failed=false;try{db.selectReplyResult(session,a1,1);}catch(...){failed=true;}
    assert(failed&&body(db,a1)=="second answer"&&db.replyChoices(session)[a1]["ordinal"]==2);db.execute("DROP TRIGGER fail_selection");
    db.execute("CREATE TRIGGER fail_continue BEFORE INSERT ON chat_messages WHEN NEW.output_text='new question' BEGIN SELECT RAISE(ABORT,'fixture'); END");
    failed=false;try{db.commitReplyContinuation(snap,"new question",Json::object(),Json::object());}catch(...){failed=true;}
    assert(failed&&db.replyChoices(session)[a1]["total"]==3&&db.query("SELECT id FROM chat_sessions WHERE status='archived'").empty());db.execute("DROP TRIGGER fail_continue");
    auto requestSnapshot=db.contextSnapshot(session);requestSnapshot["_record_model"]={{"id","fixture"},{"provider_id","fixture"},{"name","fixture"}};
    db.execute("CREATE TRIGGER fail_record BEFORE UPDATE OF request_json ON chat_messages WHEN NEW.request_json IS NOT NULL BEGIN SELECT RAISE(ABORT,'fixture request failure'); END");
    failed=false;try{db.commitReplyContinuation(requestSnapshot,"new question",Json::object(),Json::object());}catch(...){failed=true;}
    assert(failed&&db.replyChoices(session)[a1]["total"]==3&&db.query("SELECT id FROM chat_sessions WHERE status='archived'").empty());db.execute("DROP TRIGGER fail_record");
    auto next=db.commitReplyContinuation(db.contextSnapshot(session),"new question",Json::object(),Json::object());
    assert(db.replyChoices(session).empty()&&db.messages(session).size()==5);
    assert(db.pendingImage(session,pending)==false&&db.query("SELECT id FROM chat_messages WHERE id=?",{a2}).empty());
    const auto archived=db.query("SELECT id FROM chat_sessions WHERE status='archived'");assert(archived.size()==1);
    auto old=db.messages(archived[0]["id"]);assert(old.size()==5&&old[1]["output_text"]=="first answer"&&old[3]["output_text"]=="later answer");
    assert(fileText(photo)=="original fixture"&&db.query("PRAGMA foreign_key_check").empty());
    assert(fileText(web)=="private web fixture"); // retained by the delayed archive
    db.updateMessage(next["message"],"new reply","","complete");
    db.beginReplyResult(db.contextSnapshot(session),next["message"],Json(),Json::object(),cancel);
    db.updateMessage(next["message"],"partial","","streaming");
  }
  {
    Database db(path);auto groups=db.replyChoices(session);assert(groups.size()==1);
    const auto id=groups.begin().key();assert(db.query("SELECT status FROM chat_messages WHERE id=?",{id})[0]["status"]=="interrupted");
    db.selectReplyResult(session,id,1);assert(body(db,id)=="new reply");
    db.selectReplyResult(session,id,2);assert(body(db,id)=="partial");
    auto baseline=db.contextSnapshot(session);cancel=true;bool failed=false;
    try{db.beginReplyResult(baseline,id,Json(),Json::object(),cancel);}catch(...){failed=true;}cancel=false;
    assert(failed&&db.replyChoices(session)[id]["total"]==2);
    auto prepared=db.beginReplyResult(baseline,id,Json(),Json::object(),cancel);db.discardPreparedReply(id,prepared["result"]);
    assert(db.replyChoices(session)[id]["total"]==2&&body(db,id)=="partial");
    db.selectReplyResult(session,id,1);db.commitReplyContinuation(db.contextSnapshot(session),"keep original",Json::object(),Json::object());
    assert(db.query("SELECT id FROM chat_sessions WHERE status='archived'").size()==1);
    assert(db.replyChoices(session).empty());
  }
  ensureDirectory(join(dir,"migration"));const auto migration=join(join(dir,"migration"),"penchat.db");
  {
    Database db(migration);db.append("migrate","user","preserved");
    db.execute("DROP TABLE reply_asset_refs");db.execute("DROP TABLE reply_results");db.execute("DROP TABLE reply_groups");db.execute("UPDATE schema_meta SET value='2' WHERE key='schema_version'");
  }
  {
    Database db(migration);assert(db.query("SELECT value FROM schema_meta WHERE key='schema_version'")[0]["value"]=="3");assert(db.messages("migrate")[0]["output_text"]=="preserved");
    db.execute("DROP TABLE reply_asset_refs");db.execute("DROP TABLE reply_results");db.execute("DROP TABLE reply_groups");db.execute("CREATE TABLE reply_results(conflict TEXT)");db.execute("UPDATE schema_meta SET value='2' WHERE key='schema_version'");
  }
  bool rejected=false;try{Database bad(migration);}catch(...){rejected=true;}assert(rejected);
  sqlite3 *raw=nullptr;assert(sqlite3_open_v2(migration.c_str(),&raw,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);sqlite3_stmt *check=nullptr;
  assert(sqlite3_prepare_v2(raw,"SELECT value FROM schema_meta WHERE key='schema_version'",-1,&check,nullptr)==SQLITE_OK);assert(sqlite3_step(check)==SQLITE_ROW&&std::string(reinterpret_cast<const char *>(sqlite3_column_text(check,0)))=="2");sqlite3_finalize(check);
  assert(sqlite3_prepare_v2(raw,"SELECT count(*) FROM sqlite_master WHERE name='reply_groups'",-1,&check,nullptr)==SQLITE_OK);assert(sqlite3_step(check)==SQLITE_ROW&&sqlite3_column_int(check,0)==0);sqlite3_finalize(check);sqlite3_close(raw);
  ensureDirectory(join(dir,"full"));
  {
    Database db(join(join(dir,"full"),"penchat.db"));db.append("full","user","question");const auto id=db.append("full","assistant","");db.updateMessage(id,std::string(512*1024,'x'),"","complete");
    const auto before=db.contextSnapshot("full");const auto pages=db.query("PRAGMA page_count")[0]["page_count"].get<int64_t>();
    db.execute("PRAGMA max_page_count="+std::to_string(pages));bool full=false;
    try{db.beginReplyResult(before,id,Json(),Json::object(),cancel);}catch(...){full=true;}
    assert(full&&db.replyChoices("full").empty()&&db.contextSnapshot("full")["rows"]==before["rows"]);
  }
  std::cout<<"Reply choices: schema, nested restore, checkpoint/usage, pending images, rollback, delayed archive, atomic continuation and restart passed\n";
  test::removeTree(dir);
}
