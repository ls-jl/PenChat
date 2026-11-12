#include "app.hpp"
#include <ctime>
#include <cstdlib>
namespace penchat {
void App::balanceVendorPicker() {
  modal("余额查询厂商", "仅用于余额查询，不改变聊天接口；保存服务商后生效。");
  lv_obj_update_layout(modalScroll_);
  auto explanation = lv_obj_get_child(modalScroll_, 0);
  int y = lv_obj_get_y(explanation) + lv_obj_get_height(explanation) + 12;
  const auto selected = providerForm_.value("balance_vendor", std::string());
  for (const std::string vendor : {"", "deepseek", "kimi", "siliconflow", "302ai"}) {
    auto row = button(modalScroll_, balanceVendorName(vendor), 4, y,
                      lv_obj_get_width(modalScroll_) - 8, 48, [this, vendor] {
      providerForm_["balance_vendor"] = vendor;
      build();
    }, theme::surface, LV_TEXT_ALIGN_LEFT);
    if (vendor == selected) primary(row);
    y += 56;
  }
}
void App::cancelBalances() {
  ++balanceGeneration_;
  if (balanceService_) balanceService_->cancel();
  balancePending_.clear();
  balanceDirty_ = false;
}
void App::openBalances() {
  if (navigationList_) navigationScroll_ = std::max(0, int(lv_obj_get_scroll_y(navigationList_)));
  show("balances");
  refreshBalances();
}
void App::refreshBalances(const std::string &id) {
  if (view_ != "balances" || !visible_ || !balancePending_.empty()) return;
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
    if (id.empty() || key == id) { selected.push_back(provider); balancePending_.insert(key); }
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
  build();
}
void App::balanceEvent(const Event &event) {
  if (view_ != "balances" || !visible_ || event.data.value("generation", uint64_t(0)) != balanceGeneration_) return;
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
  pageHeader("账户余额");
  auto all = pageAction(balancePending_.empty() ? "全部刷新" : "正在刷新…", 10, 136,
                        [this] { refreshBalances(); });
  lv_obj_set_disabled(all, !balancePending_.empty());
  const int left = layout().portrait() ? layout().safeSide() : 10;
  const int width = layout().bodyWidth() - 2 * left;
  int y = 4, count = 0;
  auto text = [&](const std::string &value, int size, uint32_t color) {
    auto line = readableLabel(pageContent_, value, left, y, width, size, color);
    lv_obj_update_layout(line); y += lv_obj_get_height(line) + 8;
  };
  for (const auto &provider : db_.providers()) {
    const auto vendor = objectField(provider, "metadata_json").value("balance_vendor", std::string());
    if (vendor.empty()) continue;
    ++count;
    const auto id = provider.at("id").get<std::string>();
    auto found = balanceResults_.find(id);
    const bool have = found != balanceResults_.end() && found->second.value("identity", std::string()) == balanceIdentity(provider);
    const Json result = have ? found->second : Json::object();
    auto caption = label(pageContent_, provider.value("name", std::string("服务商")), left, y, width - 84, 21);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(caption, font(21)->line_height);
    auto refresh = button(pageContent_, "刷新", left + width - 72, y, 72, 48, [this, id] { refreshBalances(id); });
    pageActions_["balance-refresh:" + id] = refresh;
    lv_obj_set_disabled(refresh, !balancePending_.empty());
    label(pageContent_, balanceVendorName(vendor), left, y + font(21)->line_height + 4, width - 84, 16, theme::muted);
    y += std::max(60, font(21)->line_height + font(16)->line_height + 12);
    const auto balances = result.value("balances", Json::array());
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
      text(std::string("查询时间：") + formatted, 16, theme::muted);
    }
    bool details = false;
    for (const auto &money : balances) details = details || !money.at("details").empty();
    if (details) {
      auto toggle = button(pageContent_, balanceExpanded_.count(id) ? "收起明细" : "余额明细", left, y, width, 48, [this, id] {
        if (balanceExpanded_.count(id)) balanceExpanded_.erase(id); else balanceExpanded_.insert(id);
        build();
      });
      pageActions_["balance-detail:" + id] = toggle; y += 56;
    }
    y += 20;
  }
  if (!count) text("尚未启用余额查询。请在模型管理 → Provider连接设置中选择“余额查询厂商”，保存后返回。", 20, theme::muted);
}
Json App::balanceDiagnostic() const {
  Json rows = Json::object();
  for (const auto &p : balanceResults_) rows[p.first] = p.second.value("status", std::string());
  Json shortcut;
  if (balanceShortcut_) { lv_area_t a; lv_obj_get_coords(balanceShortcut_, &a); shortcut = Json::array({a.x1,a.y1,a.x2,a.y2}); }
  return {{"pending", balancePending_.size()}, {"states", rows}, {"shortcut", shortcut}};
}
} // namespace penchat
