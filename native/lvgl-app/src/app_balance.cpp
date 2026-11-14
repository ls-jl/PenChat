#include "app.hpp"
#include <ctime>
#include <cstdlib>
namespace penchat {
void App::balanceVendorPicker() {
  std::vector<std::pair<std::string,std::string>> options;
  for(const std::string vendor:{"","deepseek","kimi","siliconflow","302ai"})
    options.push_back({vendor,balanceVendorName(vendor)});
  choiceMenu("balance_vendor",options,providerForm_.value("balance_vendor",std::string()),
    [this](const std::string &key){providerForm_["balance_vendor"]=key;build();});
}
void App::cancelBalances() {
  ++balanceGeneration_;
  if (balanceService_) balanceService_->cancel();
  balancePending_.clear();
  balanceDirty_ = false;
}
void App::balanceButton(lv_obj_t *parent,int x,int y,uint32_t background) {
  auto held=std::make_shared<bool>(false);
  auto wallet=icon(parent,"wallet",x,y,uiMetrics_.dp(48),uiMetrics_.dp(48),[this,held]{if(!*held)openBalances();},background);
  balanceShortcut_=wallet;
  struct Press {App *app;std::shared_ptr<bool> held;};
  auto press=new Press{this,held};
  lv_obj_add_event_cb(wallet,[](lv_event_t *event){
    auto p=static_cast<Press *>(lv_event_get_user_data(event));
    const auto code=lv_event_get_code(event);
    if(code==LV_EVENT_DELETE){if(p->app->balanceShortcut_==lv_event_get_target_obj(event))p->app->balanceShortcut_=nullptr;delete p;return;}
    if(code==LV_EVENT_PRESSED)*p->held=false;
    if(code==LV_EVENT_LONG_PRESSED){*p->held=true;p->app->events_.push("balance_hint");}
  },LV_EVENT_ALL,press);
}
void App::openBalances() {
  if (navigationList_) navigationScroll_ = std::max(0, int(lv_obj_get_scroll_y(navigationList_)));
  const bool inDrawer=layout().portrait() && navigationOpen_;
  if(!inDrawer)closeNavigation(false);
  auto list=contextMenu("balances",layout().portrait()?8:4,uiMetrics_.dp(layout().portrait()?340:360));
  if(inDrawer) {
    // The wallet is a drawer action, not a navigation away from history. Keep
    // both its reader and selection alive below the dismissible balance layer.
    const int left=std::max(popupSafeArea(screen_.width,screen_.height,roundedDisplay_,uiMetrics_).x,
        layout().safety().sideInset(lv_obj_get_y(contextMenuPanel_),lv_obj_get_height(contextMenuPanel_)));
    const int width=std::min(int(lv_obj_get_width(contextMenuPanel_)),screen_.width*2/3-left-uiMetrics_.dp(8));
    lv_obj_set_x(contextMenuPanel_,left);
    lv_obj_set_width(contextMenuPanel_,width);
    lv_obj_set_width(list,width-uiMetrics_.dp(16));
  }
  const int padding=uiMetrics_.dp(layout().portrait()?8:4);
  const int width=lv_obj_get_width(contextMenuPanel_);
  auto title=label(contextMenuPanel_,"账户余额",padding+uiMetrics_.dp(12),padding+uiMetrics_.dp(10),width-uiMetrics_.dp(152),uiMetrics_.fontSize(UiFont::Body));
  lv_obj_set_height(title,font(uiMetrics_.fontSize(UiFont::Body))->line_height);
  balanceRefresh_=button(contextMenuPanel_,"刷新",width-padding-uiMetrics_.dp(112),padding,uiMetrics_.dp(112),uiMetrics_.dp(44),[this]{refreshBalances();},theme::surface);
  lv_obj_set_style_radius(balanceRefresh_,uiMetrics_.dp(22),0);
  auto divider=box(contextMenuPanel_,padding+uiMetrics_.dp(12),padding+uiMetrics_.dp(49),width-2*padding-uiMetrics_.dp(24),uiMetrics_.dp(1),theme::border);
  lv_obj_set_clickable(divider,false);lv_obj_set_scrollable(divider,false);
  lv_obj_set_y(list,padding+uiMetrics_.dp(52));lv_obj_set_height(list,lv_obj_get_height(contextMenuPanel_)-2*padding-uiMetrics_.dp(52));
  refreshBalances();
}
void App::refreshBalances() {
  if (contextMenuKind_ != "balances" || !visible_ || !balancePending_.empty()) return;
  Json selected = Json::array();
  auto providers = db_.providers();
  std::set<std::string> existing;
  for (const auto &provider : providers) {
    const auto key = provider.at("id").get<std::string>();
    const auto vendor = objectField(provider, "metadata_json").value("balance_vendor", std::string());
    if (vendor.empty()) continue;
    existing.insert(key);
    auto old = balanceResults_.find(key);
    if (old != balanceResults_.end() && old->second.value("identity", std::string()) != balanceIdentity(provider))
      balanceResults_.erase(old);
    selected.push_back(provider); balancePending_.insert(key);
  }
  for (auto it = balanceResults_.begin(); it != balanceResults_.end();)
    if (!existing.count(it->first)) it = balanceResults_.erase(it); else ++it;
  if (!selected.empty()) {
    if (!balanceService_) {
      BalanceService::Query query;
#ifndef PENCHAT_NATIVE_DEVICE
      // Headless fixture transport is absent from device binaries. It cannot
      // change host validation or the request identity, and never logs a key.
      if (const char *fixture = std::getenv("PENCHAT_TEST_BALANCE_FIXTURE")) {
        const std::string path = fixture;
        query = [path](const Json &p, std::atomic<bool> &cancel) {
          return queryBalance(p, cancel, [&](const std::string &, const Json &, std::atomic<bool> &, const ApiOptions &) {
            const auto data = Json::parse(fileText(path, 262144));
            const auto row = data.at(p.at("id").get<std::string>());
            HttpResult response; response.status = row.value("status", 200);
            response.body = row.at("body").get<std::string>(); return response;
          });
        };
      }
#endif
      balanceService_.reset(new BalanceService(events_, query));
    }
    balanceService_->request(selected, ++balanceGeneration_);
  }
  balancePage();
}
void App::balanceEvent(const Event &event) {
  if (contextMenuKind_ != "balances" || !visible_ || event.data.value("generation", uint64_t(0)) != balanceGeneration_) return;
  const auto id = event.data.value("id", std::string());
  bool valid = false;
  for (const auto &p : db_.providers())
    if (p.at("id") == id && !objectField(p, "metadata_json").value("balance_vendor", std::string()).empty() &&
        balanceIdentity(p) == event.data.value("identity", std::string())) { valid = true; break; }
  balancePending_.erase(id);
  if (valid) {
    auto previous = balanceResults_.find(id);
    Json result = event.data;
    if (result.value("status", std::string()) != "ready" && previous != balanceResults_.end()) {
      if (previous->second.contains("balances")) result["balances"] = previous->second.at("balances");
      if (previous->second.contains("queried_at")) result["queried_at"] = previous->second.at("queried_at");
    }
    balanceResults_[id] = std::move(result);
  }
  balanceDirty_ = true;
}
void App::balancePage() {
  if(contextMenuKind_!="balances"||!contextMenuList_)return;
  auto reader=contextMenuList_;
  const int scroll=lv_obj_get_scroll_y(reader);
  lv_obj_clean(reader);
  if(balanceRefresh_) {
    lv_label_set_text(lv_obj_get_child(balanceRefresh_,0),balancePending_.empty()?"刷新":"刷新中…");
    lv_obj_set_disabled(balanceRefresh_,!balancePending_.empty());
  }
  const int left=uiMetrics_.dp(10);
  const int width=lv_obj_get_width(reader)-2*left;
  balanceDetailActions_.clear();
  int y = uiMetrics_.dp(4), count = 0;
  auto text = [&](const std::string &value, int size, uint32_t color) {
    size=uiMetrics_.dp(size);
    auto line = readableLabel(reader, value, left, y, width, size, color);
    lv_obj_update_layout(line); y += lv_obj_get_height(line) + 8;
  };
  for (const auto &provider : db_.providers()) {
    const auto vendor = objectField(provider, "metadata_json").value("balance_vendor", std::string());
    if (vendor.empty()) continue;
    ++count;
    if(count>1){auto line=box(reader,left,y-uiMetrics_.dp(10),width,uiMetrics_.dp(1),theme::border);lv_obj_set_clickable(line,false);lv_obj_set_scrollable(line,false);}
    const auto id = provider.at("id").get<std::string>();
    auto found = balanceResults_.find(id);
    const bool have = found != balanceResults_.end() && found->second.value("identity", std::string()) == balanceIdentity(provider);
    const Json result = have ? found->second : Json::object();
    const auto balances = result.value("balances", Json::array());
    bool details=false;
    for(const auto &money:balances)details=details||!money.at("details").empty();
    const int captionY=y+(details?std::max(0,(uiMetrics_.dp(44)-int(font(uiMetrics_.fontSize(UiFont::FormNote))->line_height))/2):0);
    auto caption=label(reader,provider.value("name",std::string("服务商")),left,captionY,width-(details?uiMetrics_.dp(52):0),uiMetrics_.fontSize(UiFont::FormNote),theme::muted);
    lv_label_set_long_mode(caption,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_height(caption,font(uiMetrics_.fontSize(UiFont::Body))->line_height);
    if(details) {
      auto arrow=icon(reader,balanceExpanded_.count(id)?"chevron-down":"chevron-right",left+width-uiMetrics_.dp(44),y,uiMetrics_.dp(44),uiMetrics_.dp(44),[this,id]{
        if(balanceExpanded_.count(id))balanceExpanded_.erase(id);else balanceExpanded_.insert(id);
        balancePage();
      },theme::popup,theme::muted);
      lv_obj_set_style_bg_color(arrow,lv_color_hex(theme::surface),LV_STATE_PRESSED);
      balanceDetailActions_[id]=arrow;
    }
    y+=std::max(int(font(uiMetrics_.fontSize(UiFont::FormNote))->line_height),details?44:0)+8;
    const bool pending = balancePending_.count(id);
    if (pending) text(balances.empty() ? "正在查询…" : "正在刷新，以下为上次结果", 17, theme::muted);
    else if (result.value("status", std::string()) == "error") {
      text(result.value("error", std::string("余额查询失败")), 17, theme::danger);
      if (!balances.empty()) text("刷新失败，以下为上次结果", 16, theme::muted);
    }
    if (balances.empty()) text("余额：未知", 23, theme::text);
    for (const auto &money : balances) {
      const auto unit = money.at("unit").get<std::string>();
      text(money.at("amount").get<std::string>() + " " + unit, 26, theme::text);
      if (balanceExpanded_.count(id))
        for (const auto &item : money.at("details")) {
          const auto value = item.at("amount").get<std::string>();
          text(item.at("label").get<std::string>() + "：" + (value.empty() ? "未知" : value + " " + unit), 18, theme::muted);
        }
    }
    if (result.contains("queried_at")) {
      time_t seconds = result.at("queried_at").get<int64_t>() / 1000;
      tm local{}; char formatted[40]{};
      localtime_r(&seconds, &local); strftime(formatted, sizeof(formatted), "%Y-%m-%d %H:%M:%S", &local);
      text(std::string("查询时间：") + formatted, 14, theme::muted);
    }
    y += uiMetrics_.dp(20);
  }
  if (!count) text("尚未启用余额查询。请在模型管理 → Provider详情中选择“余额查询厂商”，保存后返回。", 20, theme::muted);
  lv_obj_update_layout(reader);lv_obj_scroll_to_y(reader,scroll,LV_ANIM_OFF);
}
Json App::balanceDiagnostic() const {
  Json rows = Json::object();
  for (const auto &p : balanceResults_) rows[p.first] = p.second.value("status", std::string());
  Json shortcut;
  if (balanceShortcut_) { lv_area_t a; lv_obj_get_coords(balanceShortcut_, &a); shortcut = Json::array({a.x1,a.y1,a.x2,a.y2}); }
  auto bounds=[](lv_obj_t *o)->Json{if(!o)return Json();lv_area_t r;lv_obj_get_coords(o,&r);return Json::array({r.x1,r.y1,r.x2,r.y2});};
  Json actions=Json::object();if(balanceRefresh_)actions["refresh"]=bounds(balanceRefresh_);
  for(const auto &entry:balanceDetailActions_)actions["detail:"+entry.first]=bounds(entry.second);
  return {{"pending",balancePending_.size()},{"states",rows},{"shortcut",shortcut},{"open",contextMenuKind_=="balances"},
    {"bounds",contextMenuKind_=="balances"?bounds(contextMenuPanel_):Json()},
    {"list",contextMenuKind_=="balances"?bounds(contextMenuList_):Json()},{"actions",actions}};
}
} // namespace penchat
