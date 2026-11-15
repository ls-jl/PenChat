#include "app.hpp"
#include "empty_state.hpp"
#include "tools.hpp"
#include <ctime>
namespace penchat {
std::string App::reasoningSearchText(const Json &metadata,bool active) const {
  auto stringValue=[](const Json &value,const char *key){const auto found=value.find(key);return found!=value.end()&&found->is_string()?found->get<std::string>():std::string();};
  const auto records=metadata.value("web_records",Json::array());
  if(!records.is_array())return {};
  std::string text;
  const size_t begin=records.size()>24?records.size()-24:0;
  for(size_t i=begin;i<records.size();++i) {
    const auto &record=records[i];if(!record.is_object())continue;
    const auto name=stringValue(record,"name"),status=stringValue(record,"status");
    const auto args=objectField(record,"arguments");
    std::string query=name=="web_search"?stringValue(args,"query"):name=="web_open"?stringValue(args,"url"):std::string();
    if(query.empty()&&name=="web_search")query=stringValue(objectField(record,"result"),"query");
    for(auto &c:query)if(c=='\n'||c=='\r'||c=='\t')c=' ';
    auto chars=characters(query);if(chars.size()>160){query.clear();for(size_t j=0;j<160;++j)query+=chars[j];query+="…";}
    const bool running=status=="running"&&active,stopped=status=="cancelled"||(status=="running"&&!active);
    const std::string verb=name=="web_search"?"搜索":name=="web_read_image"?"获取图片":"阅读网页";
    const std::string caption=running?"正在"+verb:stopped?verb+"已停止":status=="error"?verb+"未完成":name=="web_search"?"搜索了":"已"+verb;
    if(!text.empty())text+='\n';
    text+=caption+(query.empty()?std::string():"（"+query+"）")+(running?"…":"");
  }
  return text;
}
Json App::webMetadata(const std::string &id) const {
  if (generating_ && id == assistant_)
    return reasoningMetadata_;
  const auto found = webMessageMetadata_.find(id);
  return found == webMessageMetadata_.end() ? Json::object() : found->second;
}
void App::webPolicyPicker() {
  choiceMenu("new_chat_web", {{"on","默认开启"},{"off","默认关闭"},{"recent","使用最近一次状态"}},
    db_.settings().value("new_chat_web",std::string("on")),
    [this](const std::string &key){db_.setting("new_chat_web",key);build();});
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
    readableLabel(pageContent_, text, uiMetrics_.dp(10), y, w - uiMetrics_.dp(20), uiMetrics_.fontSize(UiFont::FormNote), theme::palette().muted);
    y += uiMetrics_.dp(30);
    if (record.value("status", std::string()) == "error") {
      auto error = record.value("result", Json::object()).value("error", std::string("工具失败"));
      auto item = readableLabel(pageContent_, error, uiMetrics_.dp(10), y, w - uiMetrics_.dp(20), uiMetrics_.fontSize(UiFont::FormHint), theme::palette().danger);
      lv_obj_update_layout(item);
      y += lv_obj_get_height(item) + uiMetrics_.dp(12);
    }
  }
  for (const auto &source : webDetails_.value("web_sources", Json::array())) {
    auto title = source.value("title", source.value("url", std::string()));
    auto sourceButton=button(
        pageContent_, title, uiMetrics_.dp(10), y, w - uiMetrics_.dp(20), uiMetrics_.dp(48), [this, source] { openWebSource(source); },
        theme::palette().surface, LV_TEXT_ALIGN_LEFT);
    readLinkText(lv_obj_get_child(sourceButton,0),title,source.value("url",std::string()));
    y += uiMetrics_.dp(56);
  }
  if (!y)
    statusState(pageContent_,{StatusKind::Empty,"还没有检索记录","模型在回复中使用联网检索后，搜索过程与网页来源会出现在这里。","globe"},uiMetrics_,
                font(uiMetrics_.fontSize(UiFont::EmptyTitle)),font(uiMetrics_.fontSize(UiFont::FormNote)),
                uiMetrics_.dp(10),0,w-uiMetrics_.dp(20),
                std::max(1,int(lv_obj_get_height(body_))-int(lv_obj_get_y(pageContent_))-uiMetrics_.dp(8)));
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
      readableLabel(pageContent_, webSource_.value("url", std::string()), uiMetrics_.dp(10), y, w - uiMetrics_.dp(20), uiMetrics_.fontSize(UiFont::Detail), theme::palette().muted);
  lv_obj_update_layout(address);
  y += lv_obj_get_height(address) + uiMetrics_.dp(8);
  const auto stamp = webSource_.value("fetched_at", int64_t(0));
  if (stamp) {
    time_t seconds = stamp / 1000;
    tm local{};
    char formatted[32]{};
    localtime_r(&seconds, &local);
    strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M", &local);
    readableLabel(pageContent_, std::string("获取时间：") + formatted, uiMetrics_.dp(10), y, w - uiMetrics_.dp(20), uiMetrics_.fontSize(UiFont::Detail), theme::palette().muted);
    y += uiMetrics_.dp(28);
  }
  button(pageContent_, "重新获取网页", uiMetrics_.dp(10), y, w - uiMetrics_.dp(20), uiMetrics_.dp(44),
         [this] { fetchWebSource(webSource_.value("url", std::string())); });
  y += uiMetrics_.dp(52);
  auto content = box(pageContent_, uiMetrics_.dp(10), y, w - uiMetrics_.dp(20), LV_SIZE_CONTENT, theme::palette().background);
  lv_obj_set_scrollable(content, false);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  const auto text = webSource_.value("text", std::string());
  if (text.empty())
    statusState(content,{StatusKind::Empty,"尚无正文快照","点击上方“重新获取网页”保存可阅读的正文；网页不会执行脚本。","globe"},uiMetrics_,
                font(uiMetrics_.fontSize(UiFont::FormCaption)),font(uiMetrics_.fontSize(UiFont::FormNote)),0,0,w-uiMetrics_.dp(20));
  else {
    auto begin = std::min(webSourceOffset_, text.size());
    while (begin && begin < text.size() && (static_cast<unsigned char>(text[begin]) & 0xc0) == 0x80)
      --begin;
    auto end = std::min(begin + 16384, text.size());
    while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
      --end;
    if (text.size() > 16384) {
      readableLabel(content, "正文分段阅读 · 第 " + std::to_string(begin / 16384 + 1) + " 段", 0, 0, w - uiMetrics_.dp(20),
            uiMetrics_.fontSize(UiFont::Detail), theme::palette().muted);
      if (begin)
        button(content, "上一段", 0, 0, w - uiMetrics_.dp(20), uiMetrics_.dp(44), [this] {
          webSourceOffset_ = webSourceOffset_ > 16384 ? webSourceOffset_ - 16384 : 0;
          build();
        });
      if (end < text.size())
        button(content, "下一段", 0, 0, w - uiMetrics_.dp(20), uiMetrics_.dp(44), [this, end] {
          webSourceOffset_ = end;
          build();
        });
    }
    auto body=readableLabel(content, text.substr(begin, end - begin), 0, 0, w - uiMetrics_.dp(20), readingFontSize());
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
        content, title, 0, 0, w - uiMetrics_.dp(20), uiMetrics_.dp(44), [this, url] { fetchWebSource(url); }, theme::palette().surface,
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
