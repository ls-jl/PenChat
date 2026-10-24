#include "database.hpp"
#include "reasoning.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
using namespace penchat;
int main() {
  char dir[]="/tmp/penchat-reply-actions-XXXXXX";assert(mkdtemp(dir));
  {
    Database db(join(dir,"penchat.db"));std::atomic<bool> cancel{false};
    auto source=uid("session"),photo=join(dir,"system.png");atomicText(photo,"image fixture");
    auto first=db.append(source,"user","prefix question");auto target=db.append(source,"assistant","");
    db.updateMessage(target,"answer **markdown**","public thought","complete");
    auto laterImage=db.append(source,"user","",{{"path",photo},{"source","album"}});
    db.append(source,"user","discarded later question");auto last=db.append(source,"assistant","");db.updateMessage(last,"later answer","","complete");
    auto pending=db.append(source,"user","",{{"path",photo},{"source","album"}});
    db.renameSession(source,"测试");db.pinSession(source,true);
    auto original=db.contextSnapshot(source,true);
    auto branch=db.createBranch(original,target,{{"system_prompt","frozen prompt"},{"effort","api_default"}},cancel);
    db.releaseTransferSnapshot();auto id=branch["id"].get<std::string>();
    assert(branch["title"]=="测试 · 分支");auto rows=db.messages(id);
    assert(rows.size()==3&&rows[0]["output_text"]=="prefix question"&&rows[1]["output_text"]=="answer **markdown**");
    assert(rows[2]["message_type"]=="image"&&rows[0]["id"]!=first&&rows[1]["id"]!=target);
    assert(db.messages(source).size()==5&&db.query("SELECT id FROM chat_messages WHERE id=?",{pending}).empty());
    assert(db.sessionPreferences(id)["system_prompt"]=="frozen prompt");
    assert(db.sessionInfo(id)["pinned_at"]==0&&!objectField(db.sessionInfo(id),"metadata_json").contains("pinned_at"));
    auto b2=db.createBranch(db.contextSnapshot(id),rows[1]["id"],Json::object(),cancel);assert(b2["title"]=="测试 · 分支2");
    db.archiveSession(b2["id"],true);
    auto b3=db.createBranch(db.contextSnapshot(source),target,Json::object(),cancel);assert(b3["title"]=="测试 · 分支3");
    assert(db.query("PRAGMA foreign_key_check").empty());
    // Failure rolls back the full copy and the pending-image move.
    auto pending2=db.append(source,"user","",{{"path",photo},{"source","album"}});
    auto before=db.contextSnapshot(source);
    db.execute("CREATE TRIGGER block_branch BEFORE INSERT ON chat_sessions WHEN NEW.title LIKE '%分支4' BEGIN SELECT RAISE(ABORT,'fixture'); END");
    bool failed=false;try{db.createBranch(before,target,Json::object(),cancel);}catch(...){failed=true;}
    assert(failed&&db.contextSnapshot(source)["rows"]==before["rows"]);db.execute("DROP TRIGGER block_branch");
    db.execute("CREATE TRIGGER block_pending_move BEFORE DELETE ON chat_messages WHEN OLD.id='"+pending2+"' BEGIN SELECT RAISE(ABORT,'fixture late failure'); END");
    failed=false;try{db.createBranch(before,target,Json::object(),cancel);}catch(...){failed=true;}
    assert(failed&&db.contextSnapshot(source)["rows"]==before["rows"]);db.execute("DROP TRIGGER block_pending_move");
    db.execute("CREATE TRIGGER block_regen BEFORE INSERT ON chat_messages WHEN NEW.id='"+target+"' BEGIN SELECT RAISE(ABORT,'fixture retry failure'); END");
    failed=false;try{db.commitRegeneration(before,target,true,Json::object(),cancel);}catch(...){failed=true;}
    assert(failed&&db.contextSnapshot(source)["rows"]==before["rows"]);db.execute("DROP TRIGGER block_regen");
    cancel=true;failed=false;try{db.createBranch(before,target,Json::object(),cancel);}catch(...){failed=true;}assert(failed);cancel=false;
    // Regeneration archives the complete original and keeps pending photographs.
    auto snapshot=db.contextSnapshot(source,true);
    auto regenerated=db.commitRegeneration(snapshot,target,true,Json::object(),cancel);db.releaseTransferSnapshot();
    const auto archive=regenerated["archive"].get<std::string>();
    assert(db.messages(archive).size()==before["rows"].size());
    auto live=db.messages(source);assert(live.size()==3&&live[0]["id"]==first&&live[1]["id"]==target&&live[2]["id"]==pending2);
    assert(live[1]["status"]=="interrupted"&&live[1]["output_text"]==""&&live[2]["seq"]==3);
    assert(db.pendingImage(source,pending2));
    assert(db.query("SELECT id FROM chat_messages WHERE id=?",{laterImage}).empty());
    assert(fileText(photo)=="image fixture");
    // Stale snapshots cannot overwrite new content; no-question references cannot retry.
    failed=false;try{db.commitRegeneration(snapshot,target,false,Json::object(),cancel);}catch(...){failed=true;}assert(failed);
    auto reference=uid("session");auto ref=db.append(reference,"assistant","");db.updateMessage(ref,"reference","","complete");
    failed=false;try{db.commitRegeneration(db.contextSnapshot(reference),ref,false,Json::object(),cancel);}catch(...){failed=true;}assert(failed);
    auto longSource=uid("session");db.append(longSource,"user","q");auto longReply=db.append(longSource,"assistant","");db.updateMessage(longReply,"a","","complete");
    db.renameSession(longSource,std::string(256,'x'));
    auto longBranch=db.createBranch(db.contextSnapshot(longSource),longReply,Json::object(),cancel);
    auto title=longBranch["title"].get<std::string>();assert(characters(title).size()==256&&title.substr(title.size()-std::string(" · 分支").size())==" · 分支");
    db.renameSession(reference,"Title: 原样");
    auto literal=db.createBranch(db.contextSnapshot(reference),ref,Json::object(),cancel);
    assert(literal["title"]=="Title: 原样 · 分支");
    assert(db.query("PRAGMA foreign_key_check").empty());
    db.removeSession(id);assert(fileText(photo)=="image fixture");
  }
  assert(system(("rm -rf "+std::string(dir)).c_str())==0);
  std::cout<<"Reply actions: prefix cloning, shared assets, pending move, numbering, archive/truncate, rollback, cancellation, stale data, soft settings and ownership passed.\n";
}
