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
    // One workspace commit owns connection changes and every model mutation.
    auto workspace=db.providerForm(provider);
    auto drafts=Json::array();
    for(const auto &m:db.models())if(m["provider_id"]==provider)drafts.push_back(m);
    const auto defaultId=db.activeModel()["id"];
    const auto stable=drafts[0]["id"].get<std::string>();
    drafts[0]["name"]="workspace-renamed";
    auto custom=objectField(drafts[0],"capabilities_json");custom["workspace_test"]=true;
    drafts[0]["capabilities_json"]=custom;
    drafts.push_back({{"id","workspace-new-id"},{"name","workspace-added"},{"display_name","Added"},
      {"workspace_new",true},{"capabilities_json",Json::object()}});
    workspace["baseURL"]="https://workspace.invalid/v1";
    std::cerr << "workspace commit\n";
    auto saved=db.saveProviderWorkspace(workspace,drafts);
    assert(saved["provider_id"]==provider && db.providerForm(provider)["baseURL"]==workspace["baseURL"]);
    assert(db.query("SELECT name FROM model_configs WHERE id=?",{stable})[0]["name"]=="workspace-renamed");
    assert(db.activeModel()["id"]==defaultId); // No model-default option in this UI.
    assert(db.query("SELECT default_model_config_id FROM chat_sessions WHERE id='history'")[0]["default_model_config_id"]==bid);
    drafts=Json::array();for(const auto &m:db.models())if(m["provider_id"]==provider)drafts.push_back(m);
    for(auto &m:drafts)if(m["id"]=="workspace-new-id")m["workspace_deleted"]=true;
    std::cerr << "workspace deletion\n";
    db.saveProviderWorkspace(db.providerForm(provider),drafts);
    assert(db.query("SELECT enabled FROM model_configs WHERE id='workspace-new-id'")[0]["enabled"]==0);
    drafts=Json::array();for(const auto &m:db.models())if(m["provider_id"]==provider)drafts.push_back(m);
    drafts.push_back({{"id","replacement-draft"},{"name","workspace-added"},{"display_name","Restored"},
      {"workspace_new",true},{"capabilities_json",custom}});
    std::cerr << "workspace restoration\n";
    saved=db.saveProviderWorkspace(db.providerForm(provider),drafts);
    assert(saved["model_ids"]["replacement-draft"]=="workspace-new-id");
    assert(objectField(db.query("SELECT capabilities_json FROM model_configs WHERE id='workspace-new-id'")[0],"capabilities_json")==custom);
    auto beforeProviders=db.query("SELECT * FROM providers ORDER BY id");
    auto beforeModels=db.query("SELECT * FROM model_configs ORDER BY id");
    workspace=db.providerForm(provider);workspace["apiKey"]="not-committed-fake";
    drafts=Json::array();for(const auto &m:db.models())if(m["provider_id"]==provider)drafts.push_back(m);
    drafts[0]["display_name"]="also-not-committed";
    drafts.push_back({{"id","faulted-id"},{"name","fail"},{"workspace_new",true},{"capabilities_json",Json::object()}});
    failed=false;try{db.saveProviderWorkspace(workspace,drafts);}catch(...){failed=true;}
    assert(failed && db.query("SELECT * FROM providers ORDER BY id")==beforeProviders && db.query("SELECT * FROM model_configs ORDER BY id")==beforeModels);
    drafts.erase(drafts.end()-1);drafts[1]["name"]=drafts[0]["name"];
    failed=false;try{db.saveProviderWorkspace(workspace,drafts);}catch(...){failed=true;}
    assert(failed && db.query("SELECT * FROM providers ORDER BY id")==beforeProviders);
    drafts=Json::array();for(const auto &m:db.models())if(m["provider_id"]==other)drafts.push_back(m);
    failed=false;try{db.saveProviderWorkspace(workspace,drafts);}catch(...){failed=true;}
    assert(failed && db.query("SELECT * FROM model_configs ORDER BY id")==beforeModels);
    Json fresh={{"name","Unsaved new provider"},{"baseURL","https://fresh.invalid/v1"},{"apiKey","fake"},{"api","chat"}};
    std::cerr << "workspace new provider\n";
    auto created=db.saveProviderWorkspace(fresh,Json::array({{{"id","fresh-model-id"},{"name","fresh-model"},
      {"workspace_new",true},{"capabilities_json",Json::object()}}}));
    assert(db.query("SELECT provider_id FROM model_configs WHERE id='fresh-model-id'")[0]["provider_id"]==created["provider_id"]);

  }
  std::remove((std::string(directory) + "/penchat.db").c_str());
  rmdir(directory);
  std::cout << "Provider connections, additive models, rename, soft removal, restoration and "
               "rollback passed\n";
}
