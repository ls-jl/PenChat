#include "api_protocol.hpp"
#include "database.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
using namespace penchat;
int main() {
  char path[] = "/tmp/penchat-provider-settings-XXXXXX";
  auto directory = mkdtemp(path);
  assert(directory);
  {
    Database db(std::string(directory) + "/penchat.db");
    Json form = {{"name", "Fixture"},
                 {"baseURL", "https://example.invalid/v1"},
                 {"apiKey", "fake-only"},
                 {"api", "chat"}};
    const auto provider = db.saveProvider(form, {}, false);
    assert(db.models().empty() && db.providers().size() == 1);
    db.addProviderModels(provider, {"a", "b"});
    auto models = db.models();
    assert(models.size() == 2);
    auto a = models[0], b = models[1];
    const auto aid = a["id"].get<std::string>(), bid = b["id"].get<std::string>();
    auto caps = objectField(b, "capabilities_json");
    caps["custom"] = true;
    db.saveModelSettings(bid, caps);
    db.selectModel(bid);
    db.execute("INSERT INTO "
               "chat_sessions(id,title,status,created_at,updated_at,last_message_at,default_"
               "provider_id,default_model_"
               "config_id) VALUES('history','History','active',1,1,1,?,?)",
               {provider, bid});
    db.execute("INSERT INTO "
               "chat_messages(id,session_id,seq,role,status,message_type,output_text,created_at,"
               "updated_at) VALUES('message','history',1,'user','complete','text','fixture',1,1)");
    auto edit = db.providerForm(provider);
    edit["baseURL"] = "https://changed.invalid/v1";
    edit["apiKey"] = "changed-fake-only";
    db.saveProvider(edit, {}, false);
    assert(db.models().size() == 2 && db.activeModel()["id"] == bid);
    assert(objectField(db.query("SELECT capabilities_json FROM model_configs WHERE id=?", {bid})[0],
                       "capabilities_json") == caps);
    assert(db.requestModel(db.activeModel())["base_url"] == "https://changed.invalid/v1");
    db.addProviderModels(provider, {"c", "b", "c"});
    assert(db.models().size() == 3);
    const auto cid = db.query("SELECT id FROM model_configs WHERE provider_id=? AND name='c'",
                              {provider})[0]["id"]
                         .get<std::string>();
    db.saveModelSettings(bid, caps, "b-new", "My model");
    assert(db.activeModel()["id"] == bid && db.activeModel()["name"] == "b-new");
    assert(db.query("SELECT default_model_config_id FROM chat_sessions WHERE id='history'")
               [0]["default_model_config_id"] == bid);
    bool conflict = false;
    try {
      db.saveModelSettings(cid, Json::object(), "a", "Collision");
    } catch (...) {
      conflict = true;
    }
    assert(conflict &&
           db.query("SELECT name FROM model_configs WHERE id=?", {cid})[0]["name"] == "c");
    db.removeModel(bid);
    assert(db.models().size() == 2 && db.activeModel()["id"] != bid);
    assert(db.query("SELECT COUNT(*) AS n FROM chat_messages WHERE session_id='history'")[0]["n"] ==
           1);
    assert(db.query("SELECT default_model_config_id FROM chat_sessions WHERE id='history'")
               [0]["default_model_config_id"] == bid);
    db.addProviderModels(provider, {"b-new"});
    auto restored = db.query(
        "SELECT id,capabilities_json,display_name FROM model_configs WHERE name='b-new'")[0];
    assert(restored["id"] == bid && restored["display_name"] == "My model" &&
           objectField(restored, "capabilities_json") == caps);
    db.execute("CREATE TRIGGER fail_model BEFORE INSERT ON model_configs WHEN NEW.name='fail' "
               "BEGIN SELECT RAISE(ABORT,'fixture'); END");
    bool failed = false;
    try {
      db.addProviderModels(provider, {"not-committed", "fail"});
    } catch (...) {
      failed = true;
    }
    assert(failed && db.query("SELECT id FROM model_configs WHERE name='not-committed'").empty());
    db.removeModel(aid);
    db.removeModel(bid);
    db.removeModel(cid);
    assert(db.models().empty());
    db.saveProvider(db.providerForm(provider), {}, false);
    assert(db.providers().size() == 1);
    db.addProviderModels(provider, {"new-default"});
    assert(db.activeModel()["name"] == "new-default");
    auto claude = form;
    claude["name"] = "Messages";
    claude["api"] = "anthropic";
    claude["max_output_tokens"] = 8192;
    const auto other = db.saveProvider(claude, {}, false);
    assert(db.providerForm(other)["max_output_tokens"] == 8192);
    db.addProviderModels(other, {"unknown-model"});
    auto added =
        db.query("SELECT capabilities_json FROM model_configs WHERE provider_id=?", {other})[0];
    assert(objectField(added, "capabilities_json")["max_output_tokens"] == 8192);
  }
  std::remove((std::string(directory) + "/penchat.db").c_str());
  rmdir(directory);
  std::cout << "Provider connections, additive models, rename, soft removal, restoration and "
               "rollback passed\n";
}
