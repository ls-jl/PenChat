#include "transfer.hpp"
#include "presets.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace penchat;
static Json run(Transfer &t, Events &events, const std::string &op,
                Json args = Json::object(), bool error = false) {
  assert(t.start(op, args));
  auto end = ticks() + 20000;
  while (ticks() < end) {
    for (const auto &e : events.drain()) {
      if (e.type == "transfer_progress")
        continue;
      assert(e.data.at("op") == op);
      if (error) {
        assert(e.type == "transfer_error");
        return e.data;
      }
      if (e.type == "transfer_error")
        std::cerr << e.data.dump() << std::endl;
      assert(e.type == "transfer_result");
      return e.data.at("data");
    }
    usleep(1000);
  }
  assert(false);
  return {};
}
static void cp(const std::string &from, const std::string &to) {
  std::ifstream i(from, std::ios::binary);
  std::ofstream o(to, std::ios::binary);
  o << i.rdbuf();
  assert(o.good());
}
int main(int argc, char **argv) {
  if (argc == 3) {
    std::string data = argv[1];
    ensureDirectory(data);
    setenv("PENCHAT_TRANSFER_ROOT", argv[2], 1);
    Database db(join(data, "penchat.db"));
    Events events;
    Transfer t(db, events, data);
    std::string line;
    while (std::getline(std::cin, line)) {
      auto args = Json::parse(line);
      assert(t.start(args.at("op"), args.value("args", Json::object())));
      bool done = false;
      while (!done) {
        for (const auto &e : events.drain())
          if (e.type != "transfer_progress") {
            std::cout << Json{{"type", e.type}, {"data", e.data}}.dump() << std::endl;
            done = true;
          }
        if (!done)
          usleep(1000);
      }
    }
    return 0;
  }

  char temp[] = "/tmp/penchat-transfer-XXXXXX";
  auto root = std::string(mkdtemp(temp));
  auto data = join(root, "data"), folder = join(root, "Favorite/PenChat");
  ensureDirectory(data);
  setenv("PENCHAT_TRANSFER_ROOT", folder.c_str(), 1);
  Database db(join(data, "penchat.db"));
  Events events;
  Transfer t(db, events, data);
  run(t, events, "init");
  db.saveProvider(
      {{"name", "Provider"},
       {"baseURL", "https://user:pass@example.test/v1?api_key=DO_NOT_EXPORT"},
       {"apiKey", "KEY_DO_NOT_EXPORT"},
       {"api", "responses"}},
      {"test-model"});
  auto model = db.models()[0];
  auto mid = model.at("id").get<std::string>();
  auto preferences =
      Json{{"model_id", mid}, {"system_prompt", "系统提示词"}, {"effort", "high"}};
  auto s = uid("session");
  db.append(s, "user", "第一个问题", Json::object(), preferences);
  auto aid = db.append(s, "assistant", "# 标题\n\n|A|B|\n|-|-|\n|1|2|\n\n公式 $x^2$。");
  db.updateMessage(aid, "# 标题\n\n|A|B|\n|-|-|\n|1|2|\n\n公式 $x^2$。", "公开思考",
                   "complete", Json(),
                   {{"reasoning_observed", true},
                    {"reasoning_duration_ms", 4500},
                    {"usage", {{"input_tokens", 1000}, {"output_tokens", 100}, {"total_tokens", 1100}, {"cached_tokens", 700}, {"reasoning_tokens", 30}, {"private", "MUST_NOT_EXPORT_USAGE"}}},
                    {"reasoning_complete", true},
                    {"response_reasoning_items",
                     Json::array({{{"encrypted_content", "OPAQUE_DO_NOT_EXPORT"}}})}});
  db.recordRequest(aid, model);
  auto image = join(root, "source.png");
  atomicText(image, "fixture-image-content");
  db.append(s, "user", "",
            {{"path", image}, {"width", 100}, {"height", 60}, {"source", "album"}});
  auto photoReply = db.append(s, "assistant", "图片收到");
  db.updateMessage(photoReply, "图片收到", "", "complete");
  db.pinSession(s, true);
  db.archiveSession(s, true);
  auto empty = uid("session");
  db.createSession();
  auto pending = uid("session");
  db.append(pending, "user", "", {{"path", image}, {"source", "album"}});
  auto p = defaultPreset();
  p["prompt_mode"] = "custom";
  p["prompt"] = "默认系统提示";
  p["model_id"] = mid;
  p["effort"] = "high";
  auto ordinary = p;
  ordinary["id"] = "p2";
  ordinary["name"] = "代码预设";
  db.setting("chat_presets", Json::array({p, ordinary}));
  auto result = run(t, events, "export", {{"kind", "chats"}, {"all", true}});
  assert(result["count"] == 1);
  auto package = result.at("path").get<std::string>();
  auto dest = join(folder, "导入/聊天/roundtrip.penchat.zip");
  cp(package, dest);
  auto preview =
      run(t, events, "preview", {{"kind", "chats"}, {"name", "roundtrip.penchat.zip"}});
  assert(preview["items"].size() == 1 && preview["pictures"] == 1);
  assert(preview["models"][0]["descriptor"]["endpoint"] == "https://example.test/v1");
  assert(preview["models"][0]["id"] == mid);
  auto n = db.summary()["sessions"].get<int>();
  auto imported = run(t, events, "import", {{"indices", Json::array({0})}});
  assert(imported["added"] == 1);
  assert(db.summary()["sessions"] == n + 1);
  auto sessions =
      db.query("SELECT * FROM chat_sessions WHERE id!=? AND status='archived'", {s});
  assert(sessions.size() == 1);
  auto other = sessions[0].at("id").get<std::string>();
  auto rows =
      db.query("SELECT * FROM chat_messages WHERE session_id=? ORDER BY seq", {other});
  assert(rows.size() == 4);
  assert(rows[1]["reasoning_text"] == "公开思考");
  const auto importedUsage = objectField(rows[1], "metadata_json")["usage"];
  assert(importedUsage["cached_tokens"] == 700 && importedUsage["total_tokens"] == 1100);
  assert(!importedUsage.contains("private"));
  assert(objectField(rows[1], "metadata_json")["reasoning_duration_ms"] == 4500);
  assert(!objectField(rows[1], "metadata_json").contains("response_reasoning_items"));
  assert(db.sessionPreferences(other)["system_prompt"] == "系统提示词");
  assert(db.sessionPreferences(other)["model_id"] == mid);
  assert(sessions[0]["pinned_at"].get<int64_t>() > 0);
  assert(db.query("pragma foreign_key_check").empty());
  preview =
      run(t, events, "preview", {{"kind", "chats"}, {"name", "roundtrip.penchat.zip"}});
  assert(preview["items"][0]["duplicate"] == true);
  run(t, events, "import", {{"indices", Json::array({0})}}, true);
  assert(db.summary()["sessions"] == n + 1);
  imported = run(t, events, "import", {{"indices", Json::array({0})}, {"copy", true}});
  assert(imported["added"] == 1);
  auto presets = run(t, events, "export", {{"kind", "presets"}, {"all", true}});
  auto presetPath = presets.at("path").get<std::string>();
  auto content = fileText(presetPath);
  assert(content.find("KEY_DO_NOT_EXPORT") == std::string::npos &&
         content.find("DO_NOT_EXPORT") == std::string::npos &&
         content.find("user:pass") == std::string::npos);
  cp(presetPath, join(folder, "导入/预设/test.penchat-presets.json"));
  preview = run(t, events, "preview",
                {{"kind", "presets"}, {"name", "test.penchat-presets.json"}});
  assert(preview.contains("default_diff"));
  run(t, events, "import", {{"indices", Json::array({0, 1})}}, true);
  auto old = presetList(db.settings());
  assert(old.size() == 2);
  imported = run(t, events, "import",
                 {{"indices", Json::array({0, 1})}, {"default_confirm", true}});
  assert(imported["added"] == 2);
  assert(presetList(db.settings()).size() == 3);
  assert(!db.settings().value("presets_enabled", false));
  atomicText(join(folder, "导入/聊天/reference.md"),
             "# 中文参考\n\n正文\n\n![示例](images/photo.png)\n");
  ensureDirectory(join(folder, "导入/聊天/images"));
  cp(image, join(folder, "导入/聊天/images/photo.png"));
  preview = run(t, events, "preview", {{"kind", "chats"}, {"name", "reference.md"}});
  assert(preview["pictures"] == 1);
  imported = run(t, events, "import", {{"indices", Json::array({0})}});
  assert(imported["added"] == 1);
  auto ref = db.query(
      "SELECT * FROM chat_messages WHERE metadata_json LIKE '%imported_reference%'");
  assert(ref.size() == 1 && ref[0]["role"] == "assistant");
  assert(ref[0]["output_text"].get<std::string>().find(join(data, "assets/")) !=
         std::string::npos);
  auto imports = run(t, events, "list", {{"kind", "chats"}});
  assert(imports["items"].size() == 2);
  assert(access(image.c_str(), F_OK) == 0);
  // Invalid selection and file traversal must never mutate the database.
  run(t, events, "preview", {{"kind", "chats"}, {"name", "../outside.md"}}, true);
  assert(db.query("pragma integrity_check")[0].begin().value() == "ok");
  auto policyRoot = join(root, "policy-files");
  setenv("PENCHAT_TRANSFER_ROOT", policyRoot.c_str(), 1);
  {
    Transfer policy(db, events, join(root, "policy-data"));
    run(policy, events, "init");
    auto exported = join(policyRoot, "导出/聊天/export.txt");
    auto incoming = join(policyRoot, "导入/聊天/keep.md");
    atomicText(exported, "export");
    atomicText(incoming, "keep");
    assert(policy.finish(true));
    assert(access(exported.c_str(), F_OK) == 0);
    assert(policy.finish(false));
    assert(access(join(policyRoot, "导出").c_str(), F_OK) != 0);
    assert(access(incoming.c_str(), F_OK) == 0);
    run(policy, events, "init");
    assert(access(join(policyRoot, "导出/聊天").c_str(), F_OK) == 0);
    rmdir(join(policyRoot, "导出/聊天").c_str());
    rmdir(join(policyRoot, "导出/预设").c_str());
    rmdir(join(policyRoot, "导出").c_str());
    assert(symlink(root.c_str(), join(policyRoot, "导出").c_str()) == 0);
    assert(!policy.finish(false));
    assert(access(image.c_str(), F_OK) == 0);
    unlink(join(policyRoot, "导出").c_str());
  }
  std::cout << Json{{"root", root},
                    {"package", package},
                    {"sessions", db.summary()["sessions"]},
                    {"passed", true}}
                   .dump()
            << std::endl;
}
