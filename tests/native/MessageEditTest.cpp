#include "database.hpp"
#include "reasoning.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
using namespace penchat;
int main() {
  char dir[]="/tmp/penchat-message-edit-XXXXXX";assert(mkdtemp(dir));
  {
    Database db(join(dir,"penchat.db"));
    auto s=uid("session"),original=join(dir,"system-photo.png");atomicText(original,"image-fixture");
    auto first=db.append(s,"user","prefix");auto a=db.append(s,"assistant","");db.updateMessage(a,"prefix answer","","complete");
    auto image=db.append(s,"user","",{{"path",original},{"source","album"}});
    auto selected=db.append(s,"user","old question");a=db.append(s,"assistant","");db.updateMessage(a,"old answer","public thought","complete");
    auto later=db.append(s,"user","later question");a=db.append(s,"assistant","");db.updateMessage(a,"later answer","","complete");
    auto pending=db.append(s,"user","",{{"path",original},{"source","album"}});
    auto snapshot=db.messageEditSnapshot(s,selected);assert(snapshot["pivot"]==3&&snapshot["images"].size()==1&&snapshot["text"]=="old question");
    assert(db.messages(s).size()==8); // preparation/cancellation never rewrite data
    auto path=snapshot["images"][0]["stored_path"].get<std::string>();
    auto result=db.commitMessageEdit(snapshot,snapshot["images"],"edited question",{{"effort","api_default"}},true,nowMs());
    const auto archived=result["archive"].get<std::string>();assert(!archived.empty());
    assert(db.messages(s).size()==4&&db.messages(archived).size()==8);
    assert(db.messages(s)[0]["id"]==first&&db.messages(s).back()["output_text"]=="edited question");
    assert(db.query("select id from message_assets where stored_path=?",{path}).size()==2);
    assert(fileText(original)=="image-fixture");db.releaseTransferSnapshot();
    assert(objectField(db.sessionInfo(s),"metadata_json").value("context_compression",Json::object()).empty());
    auto newId=db.messages(s).back()["id"].get<std::string>();
    auto retry=db.messageEditSnapshot(s,newId);
    // Failure during replacement rolls the archive and deletion back together.
    db.execute("CREATE TRIGGER fail_edit BEFORE INSERT ON chat_messages WHEN NEW.output_text='fail' BEGIN SELECT RAISE(ABORT,'test failure'); END");
    bool failed=false;try{db.commitMessageEdit(retry,retry["images"],"fail",Json::object(),true,nowMs());}catch(...){failed=true;}
    assert(failed&&db.messages(s).back()["output_text"]=="edited question"&&db.sessions(0,true).size()==1);
    db.execute("DROP TRIGGER fail_edit");
    // A concurrent body change is rejected, never silently overwritten.
    db.updateMessage(newId,"concurrent edit","","complete");
    failed=false;try{db.commitMessageEdit(retry,Json::array(),"replacement",Json::object(),false,nowMs());}catch(...){failed=true;}
    assert(failed);db.releaseTransferSnapshot();
    auto final=db.messageEditSnapshot(s,newId);
    db.commitMessageEdit(final,Json::array(),"without image",Json::object(),false,nowMs());db.releaseTransferSnapshot();
    assert(db.messages(s).size()==3&&db.sessions(0,true).size()==1);
    assert(fileText(path)=="image-fixture"); // archived copy retains the shared file
    db.removeSession(archived);db.flush();assert(fileText(original)=="image-fixture");
    auto consecutive=uid("session");db.append(consecutive,"user","keep this first question");
    auto second=db.append(consecutive,"user","edit only this question");
    auto one=db.messageEditSnapshot(consecutive,second);
    assert(one["pivot"]==2 && one["text"]=="edit only this question");
    db.commitMessageEdit(one,Json::array(),"changed second question",Json::object(),false,nowMs());
    db.releaseTransferSnapshot();
    assert(db.messages(consecutive).size()==2 && db.messages(consecutive)[0]["output_text"]=="keep this first question");
    assert(db.query("PRAGMA foreign_key_check").empty());
    (void)later;(void)pending;(void)image;
  }
  assert(system(("rm -rf "+std::string(dir)).c_str())==0);
  std::cout<<"Message editing: read-only preparation, logical image turn, atomic archive/rewrite, rollback, stale snapshot, shared asset safety, private recycling and ownership passed.\n";
}
