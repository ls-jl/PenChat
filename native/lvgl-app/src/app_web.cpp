#include "app.hpp"
#include "tools.hpp"
#include <ctime>
namespace penchat {
Json App::webMetadata(const std::string &id) const {
  if (generating_ && id == assistant_)
    return reasoningMetadata_;
  const auto found = webMessageMetadata_.find(id);
  return found == webMessageMetadata_.end() ? Json::object() : found->second;
}
void App::webPolicyPicker() {
  modal("新聊天联网搜索", "仅影响新建聊天；上拉新建继承上一会话。开启后由模型决定是否搜索。");
  auto list = lv_obj_get_child(modal_, 1);
  lv_obj_clean(list);
  int y = 0;
  for (const auto &entry :
       {std::make_pair("on", "默认开启"), {"off", "默认关闭"}, {"recent", "使用最近一次状态"}}) {
    const std::string key = entry.first;
    button(list, entry.second, 4, y, screen_.width - 145, 44, [this, key] {
      db_.setting("new_chat_web", key);
      build();
    });
    y += 48;
  }
}
void App::cancelWebReader() {
  webReaderCancel_ = true;
  ++webReaderGeneration_;
  if (webReaderWorker_.joinable())
    webReaderWorker_.join();
}
void App::openWebDetails(const std::string &id) {
  webDetails_ = webMetadata(id);
  webReturnView_ = view_ == "archive_chat" ? "archive_chat" : "chat";
  show("web_details");
}
void App::webDetailsPage() {
  pageHeader("检索记录与来源");
  const int w = layout().contentWidth();
  int y = 0;
  for (const auto &record : webDetails_.value("web_records", Json::array())) {
    const auto name = record.value("name", std::string());
    auto title = name == "web_search" ? "搜索" : name == "web_read_image" ? "获取图片" : "阅读网页";
    const auto state = record.value("status", std::string());
    auto text = std::string(title) + " · " +
                (state == "running"     ? "处理中"
                 : state == "complete"  ? "已完成"
                 : state == "cancelled" ? "已停止"
                                        : "未能完成");
    readableLabel(pageContent_, text, 10, y, w - 20, 18, theme::muted);
    y += 30;
    if (record.value("status", std::string()) == "error") {
      auto error = record.value("result", Json::object()).value("error", std::string("工具失败"));
      auto item = readableLabel(pageContent_, error, 10, y, w - 20, 17, theme::danger);
      lv_obj_update_layout(item);
      y += lv_obj_get_height(item) + 12;
    }
  }
  for (const auto &source : webDetails_.value("web_sources", Json::array())) {
    auto title = source.value("title", source.value("url", std::string()));
    auto sourceButton=button(
        pageContent_, title, 10, y, w - 20, 48, [this, source] { openWebSource(source); },
        theme::surface, LV_TEXT_ALIGN_LEFT);
    readLinkText(lv_obj_get_child(sourceButton,0),title,source.value("url",std::string()));
    y += 56;
  }
  if (!y)
    readableLabel(pageContent_, "还没有可查看的检索记录", 10, 8, w - 20, 20, theme::muted);
}
void App::openWebSource(Json source) {
  if (view_ != "web_details" && view_ != "web_source") {
    webReturnView_ = view_ == "archive_chat" ? "archive_chat" : "chat";
    webDetails_ = Json::object();
  }
  webSourceOffset_ = 0;
  webSource_ = std::move(source);
  show("web_source");
}
void App::webSourcePage() {
  pageHeader(webSource_.value("title", std::string("网页来源")));
  const int w = layout().contentWidth();
  int y = 0;
  auto address =
      readableLabel(pageContent_, webSource_.value("url", std::string()), 10, y, w - 20, 16, theme::muted);
  lv_obj_update_layout(address);
  y += lv_obj_get_height(address) + 8;
  const auto stamp = webSource_.value("fetched_at", int64_t(0));
  if (stamp) {
    time_t seconds = stamp / 1000;
    tm local{};
    char formatted[32]{};
    localtime_r(&seconds, &local);
    strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M", &local);
    readableLabel(pageContent_, std::string("获取时间：") + formatted, 10, y, w - 20, 16, theme::muted);
    y += 28;
  }
  button(pageContent_, "重新获取网页", 10, y, w - 20, 44,
         [this] { fetchWebSource(webSource_.value("url", std::string())); });
  y += 52;
  auto content = box(pageContent_, 10, y, w - 20, LV_SIZE_CONTENT, theme::background);
  lv_obj_set_scrollable(content, false);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  const auto text = webSource_.value("text", std::string());
  if (text.empty())
    readableLabel(content, "暂无正文快照，点击重新获取；网页不执行脚本。", 0, 0, w - 20, 18, theme::muted);
  else {
    auto begin = std::min(webSourceOffset_, text.size());
    while (begin && begin < text.size() && (static_cast<unsigned char>(text[begin]) & 0xc0) == 0x80)
      --begin;
    auto end = std::min(begin + 16384, text.size());
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
      --end;
    if (text.size() > 16384) {
      readableLabel(content, "正文分段阅读 · 第 " + std::to_string(begin / 16384 + 1) + " 段", 0, 0, w - 20,
            16, theme::muted);
      if (begin)
        button(content, "上一段", 0, 0, w - 20, 44, [this] {
          webSourceOffset_ = webSourceOffset_ > 16384 ? webSourceOffset_ - 16384 : 0;
          build();
        });
      if (end < text.size())
        button(content, "下一段", 0, 0, w - 20, 44, [this, end] {
          webSourceOffset_ = end;
          build();
        });
    }
    auto body=readableLabel(content, text.substr(begin, end - begin), 0, 0, w - 20, readingFontSize());
    readSlice(body,text,begin);
  }
  for (const auto &link : webSource_.value("links", Json::array())) {
    const auto url = link.value("url", std::string());
    if (url.empty())
      continue;
    auto title = link.value("text", url);
    if (title.empty())
      title = url;
    auto linkButton=button(
        content, title, 0, 0, w - 20, 44, [this, url] { fetchWebSource(url); }, theme::surface,
        LV_TEXT_ALIGN_LEFT);
    readLinkText(lv_obj_get_child(linkButton,0),title,url);
  }
}
void App::fetchWebSource(const std::string &url) {
  cancelWebReader();
  webReaderCancel_ = false;
  const auto token = ++webReaderGeneration_;
  webReaderWorker_ = std::thread([this, url, token] {
    try {
      WebProcess worker(join(workspace_, "assets/bin/penchat-web-search"));
      auto page = worker.call("read_source", {{"url", url}}, webReaderCancel_);
      page["fetched_at"] = nowMs();
      events_.push("web_reader", {{"token", token}, {"source", page}});
    } catch (const std::exception &) {
      if (!webReaderCancel_)
        events_.push("web_reader", {{"token", token},
                                    {"error", "网页获取失败，可返回已保存的来源内容后手动重试。"}});
    }
  });
}
void App::webEvent(const Event &event) {
  if (view_ != "web_source" || event.data.value("token", 0) != webReaderGeneration_)
    return;
  if (event.data.contains("error")) {
    modal("无法读取网页", event.data.at("error"));
    return;
  }
  webSourceOffset_ = 0;
  webSource_ = event.data.at("source");
  build();
}
} // namespace penchat
