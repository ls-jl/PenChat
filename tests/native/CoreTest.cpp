#include "accelerometer.h"
#include "editor.hpp"
#include "photo_orientation.hpp"
#include "common.hpp"
#include "database.hpp"
#include "image_data.h"
#include "markdown.hpp"
#include "network.hpp"
#include "pull_geometry.hpp"
#include "reasoning.hpp"
#include "presets.hpp"
#include "reasoning_stream.hpp"
#include "titles.hpp"
#include "utils/indexed_arr.h"
#include <cassert>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstring>
#include <iostream>
#include <unistd.h>
using namespace penchat;
int main(int argc, char **argv) {
  assert(argc == 4);
  {
    EditorState e; e.limit=5;
    assert(e.insert("中😀a") && e.cursor==3 && editUtf16(e.text,e.cursor)==4);
    e.cursor=2; e.erase(); assert(e.text=="中a" && e.cursor==1);
    e.revert(); assert(e.text=="中😀a" && e.cursor==2);
    assert(!e.insert("abcdef"));
    e.all=true; assert(e.insert("替换")); assert(e.text=="替换");
    e.preedit="ni"; assert(e.raw() && e.text=="替换ni"); e.revert();
    assert(e.text=="替换" && e.preedit=="ni");
    e.preedit.clear(); e.limit=100; for(int i=0;i<30;++i)e.insert("x"); assert(e.undo.size()==20);
  }

  {
    Json a = {{"id", "a"}, {"name", "a"}, {"capabilities_json", {{"reasoning", {{"levels", {{"high", "high"}}}}}}}};
    Json b = {{"id", "b"}, {"name", "b"}, {"capabilities_json", {{"reasoning", {{"levels", {{"max", "max"}}}}}}}};
    auto preset = defaultPreset();
    preset["model_id"] = "b"; preset["effort"] = "max";
    preset["prompt_mode"] = "custom"; preset["prompt"] = "Be concise";
    Json prefs = {{"model_id", "a"}, {"effort", "high"}};
    std::string warning;
    auto applied = applyPresetSnapshot(prefs, preset, Json::array({a,b}), a, true, warning);
    assert(applied["model_id"] == "b" && applied["effort"] == "max" && warning.empty());
    auto old = applyPresetSnapshot(prefs, preset, Json::array({a,b}), a, false, warning);
    assert(old["model_id"] == "a" && old["effort"] == "high" && old["system_prompt"] == "Be concise");
    preset["model_when"] = "always"; preset["effort_when"] = "always";
    assert(applyPresetSnapshot(prefs, preset, Json::array({a,b}), a, false, warning)["effort"] == "max");
    preset["model_id"] = "missing";
    auto missing = applyPresetSnapshot(prefs, preset, Json::array({a,b}), a, true, warning);
    assert(missing["model_id"] == "a" && missing["effort"] == "high" && !warning.empty());
    preset["model_id"] = ""; preset["effort"] = "api_default";
    auto api = applyPresetSnapshot(prefs, preset, Json::array({a,b}), a, true, warning);
    assert(resolveReasoning(a, api, "high")["effort_value"] == "");
    preset["prompt_mode"] = "inherit";
    assert(applyPresetSnapshot(applied, preset, Json::array({a,b}), b, true, warning)["system_prompt"] == "Be concise");
    preset["prompt_mode"] = "clear";
    assert(!applyPresetSnapshot(applied, preset, Json::array({a,b}), b, true, warning).contains("system_prompt"));
    assert(presetList(Json::object()).size() == 1);
    auto requestModel = a;
    requestModel["system_prompt"] = "Public system instructions";
    auto responses = requestBody(requestModel, Json::array(), true);
    assert(responses["instructions"] == "Public system instructions");
    requestModel["provider_metadata"] = Json({{"api", "chat"}}).dump();
    auto chat = requestBody(requestModel, Json::array(), true);
    assert(chat["messages"].size() == 1 && chat["messages"][0]["role"] == "system");
    requestModel.erase("system_prompt");
    assert(requestBody(requestModel, Json::array(), true)["messages"].empty());
  }

  const auto pull = pullGeometry({86, 0, 956, 266}, 960, 266, 208);
  assert(pull.start == 12 && pull.fill == 130 && pull.contains(500, 207));
  assert(!pull.contains(500, 208) && !pull.contains(85, 100));
  const auto doubled = pullGeometry({172, 0, 1912, 532}, 1920, 532, 416);
  assert(doubled.start + doubled.fill == 284 && doubled.sideways == 36);
  assert(doubled.slowSpeed == 520 && doubled.stretch == 220);
  // Same display, different usable layout: half the reading span.
  const auto half = pullGeometry({86, 104, 956, 266}, 960, 266, 208);
  assert(half.start + half.fill == 71 && half.slowDistance == 20);
  const auto clipped = pullGeometry({-10, -20, 1000, 300}, 960, 266, 208);
  assert(clipped.start + clipped.fill == 142 && clipped.reader.left == 0);
  const auto invalid = pullGeometry({86, 220, 956, 266}, 960, 266, 208);
  assert(!invalid.contains(500, 230));
  // All raw rotations map to this same logical region; no physical pixels/DPI
  // or raw touch ranges enter the calculation a second time.
  Events latest(true);
  latest.push("response_state", {{"output", "old"}});
  latest.push("response_state", {{"output", "new"}});
  latest.push("done");
  auto latestEvents = latest.drain();
  assert(latestEvents.size() == 2 && latestEvents[0].data["output"] == "new");
  const int fontRows[] = {2, 20, 4, 40};
  tex::IndexedArray<int, 2, 1> indexed(fontRows, 4);
  assert(indexed(2)[1] == 20 && indexed(4)[1] == 40);
  assert(indexed(0) == nullptr && indexed(3) == nullptr &&
         indexed(5) == nullptr);
  tex::IndexedArray<int, 2, 1> emptyIndex(fontRows, 0);
  assert(emptyIndex(2) == nullptr);
  assert(!reasoningCapabilities(
              {{"name", "unknown"},
               {"base_url", "https://api.openai.com/v1"}})["summary"]
              .get<bool>());
  auto standardLevels = supportedEfforts({{"name", "gpt-5.2"}});
  assert(std::find(standardLevels.begin(), standardLevels.end(), "none") !=
         standardLevels.end());
  for (const auto &name : {"gpt-5.2-pro", "gpt-5.2-codex", "gpt-5.1-codex"}) {
    auto levels = supportedEfforts({{"name", name}});
    assert(std::find(levels.begin(), levels.end(), "none") == levels.end());
  }
  Json effortModel = {
      {"name", "unknown"},
      {"provider_metadata", R"({"api":"responses","default_effort":"high"})"}};
  assert(supportedEfforts(effortModel) ==
         std::vector<std::string>({"medium", "high"}));
  assert(resolveReasoning(effortModel, {{"effort", "medium"}},
                          "high")["effective_effort"] == "medium");
  assert(resolveReasoning(effortModel, {{"effort", "max"}},
                          "medium")["effective_effort"] == "medium");
  assert(resolveReasoning(effortModel, {{"effort", "max"}},
                          "xhigh")["effective_effort"] == "high");
  effortModel["provider_metadata"] = R"({"api":"responses"})";
  assert(resolveReasoning(effortModel, Json::object(),
                          "")["effective_effort"] == "");
  effortModel["capabilities_json"] =
      R"({"reasoning":{"levels":{"high":"high","max":"maximum"},"summary":true}})";
  auto resolved = resolveReasoning(effortModel, {{"effort", "max"}}, "");
  assert(supportedEfforts(resolved) ==
         std::vector<std::string>({"high", "max"}));
  assert(requestBody(resolved, Json::array(), true)["reasoning"]["effort"] ==
         "maximum");
  assert(requestBody(resolved, Json::array(), true)["reasoning"]["summary"] ==
         "auto");
  resolved["provider_metadata"] = R"({"api":"chat"})";
  assert(requestBody(resolved, Json::array(), true)["reasoning_effort"] ==
         "maximum");
  resolved = resolveReasoning(effortModel, Json::object(), "");
  resolved["reasoning_summary"] = false;
  assert(!requestBody(resolved, Json::array(), true).contains("reasoning"));
  ReasoningStream stream(1000);
  stream.consume(
      {{"output_index", 0}, {"item", {{"type", "reasoning"}, {"id", "rs"}}}},
      "response.output_item.added", 1100);
  stream.consume({{"output_index", 0}, {"delta", "公开总结"}},
                 "response.reasoning_summary_text.delta", 1200);
  Json reasoningItem = {{"type", "reasoning"},
                        {"id", "rs"},
                        {"status", "completed"},
                        {"encrypted_content", "opaque-test-only"},
                        {"summary", Json::array({{{"type", "summary_text"},
                                                  {"text", "公开总结"}}})}};
  stream.consume({{"output_index", 0}, {"item", reasoningItem}},
                 "response.output_item.done", 2100);
  stream.consume({{"output_index", 1}, {"delta", "答案"}},
                 "response.output_text.delta", 2200);
  stream.consume(
      {{"response",
        {{"output",
          Json::array({reasoningItem,
                       {{"type", "message"},
                        {"content", Json::array({{{"type", "output_text"},
                                                  {"text", "答案"}}})}}})}}}},
      "response.completed", 3000);
  stream.finish(3000);
  auto collected = stream.snapshot(3000);
  assert(collected["reasoning"] == "公开总结" && collected["output"] == "答案");
  assert(collected["metadata"]["reasoning_duration_ms"] == 1000);
  assert(collected["metadata"]["response_reasoning_items"].size() == 1);
  ReasoningStream encrypted(1000);
  encrypted.consume({{"delta", "正文"}}, "response.output_text.delta", 4000);
  reasoningItem["summary"] = Json::array();
  encrypted.consume({{"item", reasoningItem}}, "response.output_item.done",
                    5000);
  encrypted.finish(5000);
  assert(encrypted.snapshot(6000)["reasoning"] == "");
  assert(encrypted.snapshot(6000)["metadata"]["reasoning_duration_ms"] == 3000);
  ReasoningStream emptyReason(1000);
  emptyReason.consume(
      {{"item", {{"type", "reasoning"}, {"summary", Json::array()}}}},
      "response.output_item.done", 1200);
  emptyReason.finish(1500);
  assert(!emptyReason.snapshot(1600)["metadata"]["reasoning_observed"]
              .get<bool>());
  ReasoningStream noReason(1000);
  noReason.consume({{"delta", "普通正文"}}, "response.output_text.delta", 1100);
  noReason.finish(2000);
  assert(
      !noReason.snapshot(2000)["metadata"]["reasoning_observed"].get<bool>());
  ReasoningStream cancelledReason(1000);
  cancelledReason.consume(
      {{"choices",
        Json::array({{{"delta", {{"reasoning_content", "部分思考"}}}}})}},
      "", 1200);
  cancelledReason.finish(1700);
  assert(cancelledReason.snapshot(5000)["metadata"]["reasoning_duration_ms"] ==
         500);
  assert(plainTitle("\n# 标题：\"中文标题\"\n多余解释", 20) == "中文标题");
  assert(plainTitle("这是一个很长很长的标题", 4) == "这是一个");
  auto explicitDefault =
      resolveReasoning(effortModel, {{"effort", "api_default"}}, "high");
  assert(explicitDefault["effective_effort"] == "api_default" &&
         explicitDefault["effort_value"] == "");
  assert(!requestBody(explicitDefault, Json::array(), true)
              .value("reasoning", Json::object())
              .contains("effort"));
  assert(effortChoices({{"name", "gpt-5.2"}})[0] == "none" &&
         effortChoices({{"name", "gpt-5.2"}})[1] == "api_default");
  auto dir = std::string("/tmp/penchat-native-") + std::to_string(getpid());
  ensureDirectory(dir);
  Database db(join(dir, "penchat.db"));
  db.setting("active_session_id", nullptr);
  assert(!db.settings().contains("active_session_id"));
  auto lazySession = uid("session");
  assert(db.query("SELECT id FROM chat_sessions WHERE id=?", {lazySession})
             .empty());
  db.append(lazySession, "user", "首次发送时保存");
  assert(db.messages(lazySession).size() == 1);
  assert(db.settings()["active_session_id"] == lazySession);
  auto invalidImageSession = uid("session");
  bool imageFailed = false;
  try {
    db.append(invalidImageSession, "user", "",
              {{"path", join(dir, "missing.png")}});
  } catch (const std::exception &) {
    imageFailed = true;
  }
  assert(imageFailed);
  assert(
      db.query("SELECT id FROM chat_sessions WHERE id=?", {invalidImageSession})
          .empty());
  assert(db.settings()["active_session_id"] == lazySession);
  db.removeSession(lazySession);
  auto session = db.createSession();
  assert(db.sessions().empty());
  db.append(session, "user", "你好");
  auto assistant = db.append(session, "assistant", "");
  db.updateMessage(assistant, "# hello\n$\\frac{1}{2}$", "reason", "complete");
  assert(db.messages(session).size() == 2);
  assert(db.responseInput(session, assistant).size() == 1);
  auto imageSession = uid("session");
  auto imagePath = join(dir, "fixture.png");
  atomicText(imagePath, "image fixture bytes");
  db.append(imageSession, "user", "",
            {{"path", imagePath}, {"source", "album"}});
  auto imageInput = db.responseInput(imageSession, "");
  assert(imageInput.size() == 1 && imageInput[0]["content"].size() == 2);
  assert(imageInput[0]["content"][0].is_object());
  assert(imageInput[0]["content"][0]["type"] == "input_text");
  assert(imageInput[0]["content"][1]["type"] == "input_image");
  auto chatImage = requestBody(
      {{"name", "fixture"}, {"provider_metadata", "{\"api\":\"chat\"}"}},
      imageInput, true);
  assert(chatImage["messages"][0]["content"][1]["type"] == "image_url");
  auto pendingImageId = db.messages(imageSession)[0]["id"].get<std::string>();
  auto imageCopy =
      db.messages(imageSession)[0]["stored_path"].get<std::string>();
  assert(db.pendingImage(imageSession, pendingImageId));
  assert(db.removePendingImage(imageSession, pendingImageId));
  assert(db.sessionInfo(imageSession).empty() &&
         db.responseInput(imageSession, "").empty());
  assert(access(imagePath.c_str(), R_OK) == 0 &&
         access(imageCopy.c_str(), F_OK) != 0);
  auto sentImage = db.append(imageSession, "user", "", {{"path", imagePath}});
  auto sentReply = db.append(imageSession, "assistant", "reply");
  assert(!db.pendingImage(imageSession, sentImage));
  assert(!db.removePendingImage(imageSession, sentImage));
  auto nextImage = db.append(imageSession, "user", "", {{"path", imagePath}});
  assert(db.removePendingImage(imageSession, nextImage));
  assert(db.messages(imageSession).size() == 2);
  db.removeSession(imageSession);
  db.saveProvider({{"name", "test"},
                   {"baseURL", "http://localhost/v1"},
                   {"apiKey", "fake"}},
                  {"one", "two"});
  assert(db.models().size() == 2);
  assert(db.activeModel()["name"] == "one");
  auto other = db.models()[1]["id"].get<std::string>();
  db.selectModel(other);
  assert(db.activeModel()["name"] == "two");
  const auto providerId = db.activeModel()["provider_id"].get<std::string>();
  const auto selectedId = db.activeModel()["id"];
  db.execute("UPDATE providers SET headers_json=?,metadata_json=? WHERE id=?",
             {"{\"X-Custom\":\"retained\"}",
              "{\"api\":\"responses\",\"keep\":true}", providerId});
  const auto created =
      db.query("SELECT created_at,is_default FROM providers WHERE id=?",
               {providerId})[0];
  auto form = db.providerForm(providerId);
  assert(form["model"] == "one, two" && form["apiKey"] == "fake");
  form["name"] = "renamed";
  form["apiKey"] = "sk-0123456789abcdef0123456789abcdef";
  form["api"] = "chat";
  form["default_effort"] = "high";
  db.saveProvider(form, {"two", "three"});
  assert(db.providers().size() == 1 && db.models().size() == 2);
  assert(db.providerForm(providerId)["default_effort"] == "high");
  assert(db.activeModel()["id"] == selectedId &&
         db.activeModel()["name"] == "two");
  assert(db.activeModel()["api_key"] == form["apiKey"]);
  auto edited =
      db.query("SELECT created_at,is_default,metadata_json,headers_json FROM "
               "providers WHERE id=?",
               {providerId})[0];
  assert(edited["created_at"] == created["created_at"] &&
         edited["is_default"] == created["is_default"]);
  assert(Json::parse(edited["metadata_json"].get<std::string>())["keep"] ==
         true);
  assert(Json::parse(edited["headers_json"].get<std::string>())["X-Custom"] ==
         "retained");
  db.saveProvider(form, {"three"});
  assert(db.activeModel()["name"] == "three" && db.models().size() == 1);
  db.saveProvider(form, {"two", "three"});
  assert(db.query(
             "SELECT id FROM model_configs WHERE provider_id=? AND name='two'",
             {providerId})[0]["id"] == selectedId);
  auto boundModel = db.activeModel();
  boundModel["provider_metadata"] = R"({"api":"responses"})";
  auto preferences =
      Json{{"model_id", boundModel["id"]}, {"effort", "high"}, {"gauge", true}};
  auto pending = uid("session");
  db.saveSessionPreferences(pending, preferences);
  assert(db.sessionPreferences(pending).empty());
  auto message =
      db.append(pending, "user", "保存配置", Json::object(), preferences);
  assert(db.sessionPreferences(pending) == preferences);
  auto reply = db.append(pending, "assistant", "");
  db.recordRequest(reply, resolveReasoning(boundModel, preferences, ""));
  db.updateMessage(
      reply, "正文", "", "complete", Json(),
      {{"reasoning_observed", true},
       {"reasoning_duration_ms", 3000},
       {"response_reasoning_items", Json::array({reasoningItem})}});
  auto replay = db.responseInput(pending, "", boundModel);
  assert(replay.size() == 3 &&
         replay[1]["encrypted_content"] == "opaque-test-only");
  auto different = boundModel;
  different["id"] = "different";
  assert(db.responseInput(pending, "", different).size() == 2);
  different = boundModel;
  different["provider_metadata"] = R"({"api":"chat"})";
  assert(db.responseInput(pending, "", different).size() == 2);
  db.saveModelCapabilities(
      boundModel["id"],
      {{"levels", {{"high", "high"}, {"max", "maximum"}}}, {"summary", false}});
  assert(supportedEfforts(db.activeModel()).size() == 2);
  assert(db.query("SELECT request_json FROM chat_messages WHERE id=?",
                  {reply})[0]["request_json"]
             .get<std::string>()
             .find("api_key") == std::string::npos);
  assert(ocrKeyCandidate(" API Key: sk-Ab0 I1\nxyz ") == "sk-Ab0I1xyz");
  assert(ocrKeyCandidate("  token_a\tb-c  ") == "token_ab-c");
  assert(ocrKeyCandidate("sk-O0lI") == "sk-O0lI");
  assert(ocrKeyCandidate("API_KEY: gsk_x/y+=.0") == "gsk_x/y+=.0");
  assert(ocrKeyCandidate("vendor-sk-0/+/=") == "vendor-sk-0/+/=");
  bool emptyOcr = false;
  try {
    ocrKeyCandidate(" \n\t");
  } catch (...) {
    emptyOcr = true;
  }
  assert(emptyOcr);
  db.setting("penchat_settings", {{"autoSendCapturedContent", true}});
  assert(db.settings()["penchat_settings"]["autoSendCapturedContent"] == true);
  std::vector<std::string> events;
  SseParser parser(
      [&](const std::string &, const std::string &v) { events.push_back(v); });
  std::string wire = "data: "
                     "{\"type\":\"response.output_text.delta\",\"delta\":"
                     "\"中文\"}\r\n\r\ndata: [DONE]\n\n";
  for (char c : wire)
    parser.feed(&c, 1);
  parser.finish();
  assert(events.size() == 2);
  auto markdown =
      "# 标题\n\n**加粗** ~~删除~~ `code`\n\n- [x] task\n  - nested\n\n| A | B "
      "|\n|---|---|\n| 1 | "
      "2 |\n\n$\\frac{1}{2}$ 和 \\(x^2\\)\n\n```cpp\n\\(literal\\)\n```\n";
  auto doc = parseMarkdown(markdown);
  assert(doc.source == markdown);
  bool math = false, code = false, table = false;
  for (auto &b : doc.blocks) {
    table |= b.kind == "table";
    code |= b.kind == "code";
    for (auto &r : b.runs)
      math |= (r.style & Math) != 0;
  }
  assert(math && code && table);
  camera::Acceleration acceleration;
  assert(camera::parseAcceleration("x=1024;y=-20;z=120", &acceleration));
  assert(acceleration.x==1024 && acceleration.y==-20);
  assert(!camera::parseAcceleration("x=999999;y=0;z=0", &acceleration));
  assert(!camera::parseAcceleration("x=1;y=2;z=3 trailing", &acceleration));
  assert(PhotoOrientation::classify(1024,0,0)==0);
  assert(PhotoOrientation::classify(0,1024,0)==90);
  assert(PhotoOrientation::classify(-1024,0,0)==180);
  assert(PhotoOrientation::classify(0,-1024,0)==270);
  assert(PhotoOrientation::classify(0,0,1024)==-1);
  assert(PhotoOrientation::classify(3000,0,0)==-1);
  PhotoOrientation pose;
  assert(pose.update(0,1024,0,100,0)==0);
  assert(pose.update(0,1024,0,500,0)==0);
  assert(pose.update(0,1024,0,700,0)==90);
  assert(pose.update(0,0,1024,800,90)==90);
  std::vector<uint8_t> pixels={1,1,1,2,2,2,3,3,3,4,4,4,5,5,5,6,6,6};
  auto turned=rotatePhoto(pixels,2,3,90);
  assert(turned[0]==5 && turned[3]==3 && turned[6]==1 && turned[9]==6);
  assert(rotatePhoto(turned,3,2,270)==pixels);
  assert(rotatePhoto(rotatePhoto(pixels,2,3,180),2,3,180)==pixels);
  RichText renderer(argv[1], argv[2], argv[3]);
  // Mixed styles within a word must not create new wrap opportunities.
  Block words;
  words.runs = {{"中文中文中 ", "", 0}, {"hel", "word", Bold},
                {"lo", "word", Italic}, {" world", "next", 0}};
  auto wrapped = renderer.layout(words, 150);
  int wordY = -1, nextY = -1;
  for (const auto &hit : wrapped.links) {
    assert(hit.x >= 6 && hit.x + hit.w <= 145);
    if (hit.target == "word") {
      if (wordY < 0) wordY = hit.y;
      assert(hit.y == wordY);
    } else if (hit.target == "next") {
      if (nextY < 0) nextY = hit.y;
      assert(hit.y == nextY);
    }
  }
  assert(wordY > 5);
  words.runs = {{std::string(90, 'W'), "long", 0}};
  auto oversized = renderer.layout(words, 100);
  assert(oversized.links.size() == 90);
  assert(oversized.links.back().y > oversized.links.front().y);
  for (const auto &hit : oversized.links) assert(hit.x + hit.w <= 95);
  words.kind = "code";
  auto unwrapped = renderer.layout(words, 100);
  assert(unwrapped.links.front().y == unwrapped.links.back().y);
  assert(unwrapped.width > 100);
  FT_Library fontLibrary;
  FT_Face symbolFace;
  assert(FT_Init_FreeType(&fontLibrary) == 0);
  assert(FT_New_Face(fontLibrary, argv[3], 0, &symbolFace) == 0);
  for (uint32_t cp : {0x2713u, 0x2714u, 0x2717u, 0x2718u, 0x2611u, 0x2610u,
                      0x2192u, 0x21d2u, 0x221eu})
    assert(FT_Get_Char_Index(symbolFace, cp) != 0);
  FT_Done_Face(symbolFace);
  FT_Done_FreeType(fontLibrary);
  for (const auto &symbol : {"✓", "✔", "✗", "✘", "☑", "☐", "→", "⇒", "∞"}) {
    Block symbolBlock;
    symbolBlock.runs = {{symbol, "", 0}};
    auto symbolLayout = renderer.layout(symbolBlock, 100);
    auto bitmap = renderer.paint(symbolBlock, 100, 0, symbolLayout.height);
    bool visible = false;
    for (size_t i = 3; i < bitmap.pixels.size(); i += 4)
      visible |= bitmap.pixels[i] != 0;
    assert(visible);
  }
  auto tables = parseMarkdown(
      "| Left | Center | Right |\n| :--- | :---: | ---: |\n| a | [b](center) | "
      "[c](right) |\n\n| Other |\n| --- |\n| short |\n");
  renderer.prepare(tables, 780);
  assert(tables.blocks.size() == 4);
  assert(tables.blocks[0].tableId == tables.blocks[1].tableId);
  assert(tables.blocks[2].tableId != tables.blocks[0].tableId);
  assert(tables.blocks[0].columnWidths == tables.blocks[1].columnWidths);
  assert(tables.blocks[2].columnWidths.size() == 1);
  auto aligned = renderer.layout(tables.blocks[1], 780);
  assert(aligned.links.size() == 2);
  assert(aligned.links[0].x > tables.blocks[1].columnWidths[0] + 20);
  assert(aligned.links[1].x > 700);
  Block mixedCell;
  mixedCell.kind = "code";
  mixedCell.runs = {{"value = ", "", 0}, {"x^2", "", Math}};
  auto mixedLayout = renderer.layout(mixedCell, 1);
  mixedCell.runs.erase(mixedCell.runs.begin());
  auto mathOnlyLayout = renderer.layout(mixedCell, 1);
  assert(mixedLayout.height == mathOnlyLayout.height &&
         mixedLayout.width > mathOnlyLayout.width);
  Block multiline = tables.blocks[1];
  multiline.cells[0] = {{"first\nsecond", "", 0}};
  multiline.cells[1] = {{"next", "target", 0}};
  auto multilineLayout = renderer.layout(multiline, 780);
  assert(multilineLayout.height > aligned.height);
  assert(multilineLayout.links[0].x >= multiline.columnWidths[0]);
  for (auto value :
       {"\\frac{1}{2}", "\\sqrt{x^2+y^2}", "\\sum_{i=1}^{n} i",
        "\\int_0^1 x^2 dx", "\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}",
        "\\begin{aligned}a&=b+c\\\\d&=e\\end{aligned}"}) {
    auto b = renderer.formula(value, 22);
    assert(b.width > 0 && b.height > 0 && !b.pixels.empty());
  }
  for (auto &b : doc.blocks) {
    auto layout = renderer.layout(b, 780);
    auto bitmap = renderer.paint(b, 780, 0, std::min(128, layout.height));
    assert(bitmap.width > 0 && bitmap.height > 0);
  }
  for (size_t n = 1; n < sizeof("中文"); ++n) {
    auto partial = parseMarkdown(std::string("中文", n));
    assert(!partial.source.empty());
  }
  for (const auto &prefix :
       {"```cpp\nint x;", "**尚未闭合", "$\\frac{1}{", "\\(x+", "[链接"}) {
    auto partial = parseMarkdown(prefix, true);
    assert(partial.blocks.size() == 1 && partial.blocks[0].runs.size() == 1);
    assert(partial.blocks[0].runs[0].text == prefix &&
           partial.blocks[0].runs[0].style == 0);
  }
  for (size_t n = 1; n <= std::strlen(markdown); ++n)
    assert(parseMarkdown(std::string(markdown, n), true).source ==
           std::string(markdown, n));
  auto invalidFormula = parseMarkdown("$\\definitelyUnknownCommand{x}$");
  auto fallback = renderer.paint(invalidFormula.blocks[0], 780, 0, 64);
  assert(!fallback.pixels.empty());
  auto codeDoc =
      parseMarkdown("````cpp\n\\(x\\)\n````\n\n``\\(y\\)`` &amp; &#x4e2d;\n");
  for (auto &block : codeDoc.blocks)
    for (auto &run : block.runs)
      assert(!(run.style & Math));
  Block wide;
  wide.kind = "code";
  wide.runs.push_back({std::string(50000, 'x'), "", Mono});
  auto wideLayout = renderer.layout(wide, 780);
  assert(wideLayout.width > 500000);
  auto tile = renderer.paint(wide, 780, 0, 64, 21, 20000, 512);
  assert(tile.width == 512 && tile.pixels.size() == 512 * 64 * 4);
  std::vector<std::thread> writers;
  for (int i = 0; i < 4; ++i)
    writers.emplace_back([&] {
      for (int j = 0; j < 10; ++j)
        db.append(session, "user", "并发写入");
    });
  for (auto &writer : writers)
    writer.join();
  assert(db.query("SELECT count(DISTINCT seq) AS n FROM chat_messages WHERE "
                  "session_id=?",
                  {session})[0]["n"] == 42);
  auto job = db.prepareTitle(pending, reply, boundModel["id"]);
  assert(!job.empty());
  assert(db.prepareTitle(pending, reply, boundModel["id"]).empty());
  assert(db.startTitle(pending, job["token"]));
  db.renameSession(pending, "新对话");
  assert(!db.finishTitle(pending, job["token"], "不能覆盖手动标题"));
  db.append(pending, "user", "后续问题");
  assert(db.sessionInfo(pending)["title"] == "新对话");
  db.pinSession(session, true);
  assert(db.sessions()[0]["id"] == session);
  db.archiveSession(session, true);
  assert(db.sessions(0, true).size() == 1 &&
         db.sessions(0, true)[0]["id"] == session);
  db.archiveSession(session, false);
  assert(db.sessions()[0]["id"] == session);
  auto named = uid("session");
  db.append(named, "user", "首问");
  auto answer = db.append(named, "assistant", "");
  db.updateMessage(answer, "完整答复", "", "complete");
  auto naming = db.prepareTitle(named, answer, boundModel["id"]);
  assert(!naming.empty());
  assert(db.startTitle(named, naming["token"]));
  assert(db.finishTitle(named, naming["token"], "自动短标题"));
  assert(db.sessionInfo(named)["title"] == "自动短标题");
  db.removeSession(named);
  assert(!db.finishTitle(named, naming["token"], "不应重新创建"));
  db.removeSession(session);
  db.removeSession(pending);
  db.execute("UPDATE providers SET metadata_json='null' WHERE id=?",
             {providerId});
  assert(db.providerForm(providerId)["default_effort"] == "");
  assert(db.providerForm(providerId)["api"] == "responses");
  assert(db.sessions().empty());
  std::cout
      << "Native database, SSE, Markdown, formulas and rasterization passed\n";
}
