#include "app.hpp"
#include "menu_action.hpp"
#include "ui_motion.hpp"
#include "empty_state.hpp"
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
  auto wallet=icon(parent,"wallet",x,y,uiMetrics_.touchTarget(48),uiMetrics_.touchTarget(48),[this,held]{if(!*held)openBalances();},background);
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
  auto list=contextMenu("balances",1,uiMetrics_.dp(layout().portrait()?340:360));
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
  balanceRefresh_=button(contextMenuPanel_,"刷新",width-padding-uiMetrics_.dp(112),padding,uiMetrics_.dp(112),uiMetrics_.dp(44),[this]{refreshBalances();},theme::palette().surface);
  lv_obj_set_style_radius(balanceRefresh_,uiMetrics_.dp(22),0);
  auto divider=box(contextMenuPanel_,padding+uiMetrics_.dp(12),padding+uiMetrics_.dp(49),width-2*padding-uiMetrics_.dp(24),uiMetrics_.dp(1),theme::palette().border);
  lv_obj_set_clickable(divider,false);lv_obj_set_scrollable(divider,false);
  lv_obj_set_y(list,padding+uiMetrics_.dp(52));lv_obj_set_height(list,std::max(1,int(lv_obj_get_height(contextMenuPanel_))-2*padding-uiMetrics_.dp(52)));
  refreshBalances();
  motion::present(contextMenuPanel_,1,uiMetrics_);
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
void App::layoutBalances() {
  if(contextMenuKind_!="balances"||!contextMenuPanel_||!contextMenuList_)return;
  auto reader=contextMenuList_;
  const int scroll=std::max(0,int(lv_obj_get_scroll_y(reader)));
  const int padding=uiMetrics_.dp(layout().portrait()?8:4),header=uiMetrics_.dp(52);
  int content=uiMetrics_.dp(8);
  for(uint32_t i=0;i<lv_obj_get_child_count(reader);++i) {
    auto child=lv_obj_get_child(reader,i);
    if(!lv_obj_has_flag(child,LV_OBJ_FLAG_HIDDEN))content+=lv_obj_get_height(child)+uiMetrics_.dp(8);
  }
  const auto safe=popupSafeArea(screen_.width,screen_.height,roundedDisplay_,uiMetrics_);
  const int cap=layout().portrait()?std::min(safe.height,screen_.height/3):safe.height;
  const int height=std::min(cap,header+2*padding+content);
  const LayoutRect anchor{lastTouch_.value("x",screen_.width/2),lastTouch_.value("y",screen_.height/2),1,1};
  const auto area=placeContextPopup(screen_.width,screen_.height,roundedDisplay_,anchor,
      lv_obj_get_width(contextMenuPanel_),height,0,uiMetrics_);
  lv_obj_set_y(contextMenuPanel_,area.y);lv_obj_set_height(contextMenuPanel_,height);
  // Portrait balance access stays wholly inside the retained history drawer.
  if(layout().portrait()&&navigationOpen_) {
    const int left=std::max(safe.x,layout().safety().sideInset(area.y,height));
    lv_obj_set_x(contextMenuPanel_,left);
  } else lv_obj_set_x(contextMenuPanel_,area.x);
  lv_obj_set_height(reader,std::max(1,height-header-2*padding));
  lv_obj_update_layout(reader);lv_obj_scroll_to_y(reader,scroll,LV_ANIM_OFF);
}
void App::balancePage() {
  if(contextMenuKind_!="balances"||!contextMenuList_)return;
  auto reader=contextMenuList_;
  const int scroll=std::max(0,int(lv_obj_get_scroll_y(reader)));
  lv_obj_clean(reader);
  lv_obj_set_flex_flow(reader,LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(reader,0,0);
  lv_obj_set_style_pad_row(reader,uiMetrics_.dp(8),0);
  if(balanceRefresh_) {
    lv_label_set_text(lv_obj_get_child(balanceRefresh_,0),balancePending_.empty()?"刷新":"刷新中…");
    lv_obj_set_disabled(balanceRefresh_,!balancePending_.empty());
  }
  const int left=uiMetrics_.dp(10),width=std::max(1,int(lv_obj_get_width(reader))-2*left);
  balanceDetailActions_.clear();
  auto text=[&](lv_obj_t *parent,const std::string &value,int size,uint32_t color) {
    auto line=readableLabel(parent,value,0,0,width,uiMetrics_.dp(size),color);
    lv_obj_set_style_margin_left(line,left,0);lv_obj_update_layout(line);return line;
  };
  int count=0;
  for(const auto &provider:db_.providers()) {
    if(objectField(provider,"metadata_json").value("balance_vendor",std::string()).empty())continue;
    if(count++) {
      auto divider=box(reader,left,0,width,uiMetrics_.dp(1),theme::palette().border);
      lv_obj_set_style_margin_left(divider,left,0);lv_obj_set_clickable(divider,false);lv_obj_set_scrollable(divider,false);
    }
    const auto id=provider.at("id").get<std::string>();
    auto found=balanceResults_.find(id);
    const bool have=found!=balanceResults_.end()&&found->second.value("identity",std::string())==balanceIdentity(provider);
    const Json result=have?found->second:Json::object();
    const auto balances=result.value("balances",Json::array());
    bool details=false;for(const auto &money:balances)details=details||!money.at("details").empty();
    const bool expanded=balanceExpanded_.count(id);
    const int headingHeight=details?uiMetrics_.dp(44):int(font(uiMetrics_.fontSize(UiFont::FormNote))->line_height);
    auto heading=box(reader,0,0,lv_obj_get_width(reader),headingHeight,theme::palette().popup);
    lv_obj_set_scrollable(heading,false);lv_obj_set_clickable(heading,false);
    auto caption=label(heading,provider.value("name",std::string("服务商")),left,0,
        width-(details?uiMetrics_.dp(52):0),uiMetrics_.fontSize(UiFont::FormNote),theme::palette().muted);
    lv_label_set_long_mode(caption,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(caption,font(uiMetrics_.fontSize(UiFont::FormNote))->line_height);
    lv_obj_align(caption,LV_ALIGN_LEFT_MID,left,0);
    lv_obj_t *arrow=nullptr;
    if(details) {
      arrow=icon(heading,"chevron-right",left+width-uiMetrics_.dp(44),0,uiMetrics_.dp(44),uiMetrics_.dp(44),{},theme::palette().popup,theme::palette().muted);
      setDisclosureProgress(arrow,expanded?1000:0);
      lv_obj_set_style_bg_color(arrow,lv_color_hex(theme::palette().surface),LV_STATE_PRESSED);
      balanceDetailActions_[id]=arrow;
    }
    const bool pending=balancePending_.count(id);
    if(pending)text(reader,balances.empty()?"正在查询…":"正在刷新，以下为上次结果",17,theme::palette().muted);
    else if(result.value("status",std::string())=="error") {
      text(reader,result.value("error",std::string("余额查询失败")),17,theme::palette().danger);
      if(!balances.empty())text(reader,"刷新失败，以下为上次结果",16,theme::palette().muted);
    }
    if(balances.empty())text(reader,"余额：未知",23,theme::palette().text);
    for(const auto &money:balances)
      text(reader,money.at("amount").get<std::string>()+" "+money.at("unit").get<std::string>(),26,theme::palette().text);
    if(details) {
      auto content=box(reader,0,0,lv_obj_get_width(reader),LV_SIZE_CONTENT,theme::palette().popup);
      lv_obj_set_scrollable(content,false);lv_obj_set_clickable(content,false);
      lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(content,uiMetrics_.dp(8),0);
      int natural=0;
      for(const auto &money:balances)for(const auto &item:money.at("details")) {
        const auto value=item.at("amount").get<std::string>();
        auto line=text(content,item.at("label").get<std::string>()+"："+(value.empty()?"未知":value+" "+money.at("unit").get<std::string>()),18,theme::palette().muted);
        natural+=lv_obj_get_height(line)+uiMetrics_.dp(8);
      }
      natural=std::max(0,natural-uiMetrics_.dp(8));
      lv_obj_set_height(content,expanded?natural:0);
      // Height changes remeasure only the popup. Results and disclosure keep the
      // same scroll position and all account/network work stays asynchronous.
      struct Resize {App *app;lv_obj_t *reader;};
      auto resize=new Resize{this,reader};
      lv_obj_add_event_cb(content,[](lv_event_t *event){
        auto value=static_cast<Resize *>(lv_event_get_user_data(event));
        if(lv_event_get_code(event)==LV_EVENT_DELETE){delete value;return;}
        if(value->app->contextMenuList_==value->reader)value->app->layoutBalances();
      },LV_EVENT_SIZE_CHANGED,resize);
      // The delete callback is independent because SIZE_CHANGED filters events.
      lv_obj_add_event_cb(content,[](lv_event_t *event){delete static_cast<Resize *>(lv_event_get_user_data(event));},LV_EVENT_DELETE,resize);
      menuAction(arrow,[this,id,content,arrow,natural]{
        const bool opening=!balanceExpanded_.count(id);
        if(opening)balanceExpanded_.insert(id);else balanceExpanded_.erase(id);
        motion::disclosure(content,arrow,opening,natural);
      });
    }
    if(result.contains("queried_at")) {
      time_t seconds=result.at("queried_at").get<int64_t>()/1000;
      tm local{};char formatted[40]{};localtime_r(&seconds,&local);
      strftime(formatted,sizeof(formatted),"%Y-%m-%d %H:%M:%S",&local);
      text(reader,std::string("查询时间：")+formatted,14,theme::palette().muted);
    }
    auto space=box(reader,0,0,1,uiMetrics_.dp(8),theme::palette().popup);
    lv_obj_set_clickable(space,false);lv_obj_set_scrollable(space,false);
  }
  if(!count) {
    auto empty=statusState(reader,{StatusKind::Empty,"尚未启用余额查询","在 Provider 详情中选择余额查询厂商并保存。","wallet"},
        uiMetrics_,font(uiMetrics_.fontSize(UiFont::Body)),font(uiMetrics_.fontSize(UiFont::FormNote)),0,0,width);
    lv_obj_set_style_margin_left(empty,left,0);
  }
  lv_obj_update_layout(reader);layoutBalances();lv_obj_scroll_to_y(reader,scroll,LV_ANIM_OFF);
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
