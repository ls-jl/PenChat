#include "TestFiles.hpp"
#include "chat_time.hpp"
#include "database.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
using namespace penchat;
int main() {
  setenv("TZ","UTC",1);tzset();
  tm date{};date.tm_year=126;date.tm_mon=9;date.tm_mday=24;date.tm_hour=13;date.tm_min=8;
  const int64_t now=int64_t(mktime(&date))*1000;
  assert(historyTime(now,now)=="13:08");
  assert(historyTime(now-86400000,now)=="10月23日 13:08");
  date.tm_year=125;auto old=int64_t(mktime(&date))*1000;
  assert(historyTime(old,now)=="2025年10月24日 13:08");
  assert(historyTime(now,now,true)=="2026年10月24日 13:08");
  assert(historyTime(0,now).empty() && historyTime(1,now).empty());
  assert(chatTimestamp(Json{{"t","not a timestamp"}},"t")==0);
  assert(publicTimeline(Json{{"sent_at",now},{"secret","must not survive"}}).size()==2);
  assert(sessionChatTime(Json{{"last_message_at",now},{"updated_at",now+60000}})==now);
  char dir[]="/tmp/penchat-time-XXXXXX";assert(mkdtemp(dir));
  {
    Database db(join(dir,"penchat.db"));
    db.saveProvider({{"name","fixture"},{"baseURL","http://127.0.0.1:1/v1"},{"apiKey","fake-only"}}, {"fixture"});
    auto model=db.activeModel();assert(!model.empty());
    auto session=uid("session");auto user=db.append(session,"user","question");
    auto reply=db.append(session,"assistant","");
    assert(sessionChatTime(db.sessionInfo(session))==0); // no false append time
    db.setting("history_time_display","off");
    db.recordRequest(reply,model,now,now+3000);
    auto rows=db.query("SELECT metadata_json FROM chat_messages WHERE id=?",{user});
    assert(objectField(rows[0],"metadata_json")["timeline"]["sent_at"]==now);
    assert(sessionChatTime(db.sessionInfo(session))==now);
    const auto completed=now+65000;
    db.queueMessage(session,reply,1,1,"answer","","complete",Json(),
                    {{"timeline",{{"version",1},{"request_started_at",now+3000},{"completed_at",completed}}}});
    db.flush();
    assert(sessionChatTime(db.sessionInfo(session))==completed);
    db.renameSession(session,"renamed");db.pinSession(session,true);db.archiveSession(session,true);
    assert(sessionChatTime(db.sessionInfo(session))==completed);
    db.recordRequest(reply,model,now+120000,now+120001);
    rows=db.query("SELECT metadata_json FROM chat_messages WHERE id=?",{user});
    assert(objectField(rows[0],"metadata_json")["timeline"]["sent_at"]==now); // retry keeps original send
    auto newMessage=db.append(session,"user","pending");
    assert(sessionChatTime(db.sessionInfo(session))==now+120000);
    auto legacy=uid("session");db.execute("INSERT INTO chat_sessions(id,title,status,created_at,updated_at,last_message_at) VALUES(?,'legacy','active',0,0,0)",{legacy});
    assert(sessionChatTime(db.sessionInfo(legacy))==0);
    assert(db.settings()["history_time_display"]=="off");
    (void)newMessage;
  }
  test::removeTree(dir);
  std::cout<<"History time: local calendar formatting, absent legacy times, explicit send/completion, retries, hidden-display recording and metadata-only edits passed.\n";
}
