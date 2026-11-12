#include "balance.hpp"
#include "database.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
using namespace penchat;
Json provider(const std::string &vendor, const std::string &base) {
  return {{"id", vendor}, {"api_key", "fixture-secret-never-log"}, {"base_url", base},
          {"metadata_json", {{"balance_vendor", vendor}}}};
}
int main() {
  auto deep = provider("deepseek", "https://api.deepseek.com/v1/responses");
  assert(balanceEndpoint(deep) == "https://api.deepseek.com/user/balance");
  for (const auto &url : {"https://api.deepseek.com.evil.invalid/v1", "http://api.deepseek.com/v1",
                         "https://api.deepseek.com:444/v1", "https://api.deepseek.com@evil.invalid/v1",
                         "https://user@api.deepseek.com/v1", "https://example.invalid/api.deepseek.com"}) {
    auto bad = deep; bad["base_url"] = url;
    std::atomic<bool> cancel{false}; int calls = 0;
    auto result = queryBalance(bad, cancel, [&](const std::string &, const Json &, std::atomic<bool> &, const ApiOptions &) {
      ++calls; return HttpResult{};
    });
    assert(!calls && result.at("status") == "error");
    assert(result.dump().find("fixture-secret") == std::string::npos);
  }
  const std::string deepBody = R"({"is_available":true,"balance_infos":[{"currency":"CNY","total_balance":"0.000000000000000123","granted_balance":"0","topped_up_balance":"0.000000000000000123"},{"currency":"USD","total_balance":"-0.12"}]})";
  auto parsed = parseBalance("deepseek", deepBody);
  assert(parsed["balances"].size() == 2 && parsed["balances"][0]["amount"] == "0.000000000000000123");
  assert(parsed["balances"][1]["amount"] == "-0.12");
  auto kimi = parseBalance("kimi", R"({"data":{"available_balance":0.000000000000000123,"cash_balance":1e-8,"voucher_balance":0}})");
  assert(kimi["balances"][0]["amount"] == "0.000000000000000123");
  assert(kimi["balances"][0]["details"][0]["amount"] == "0.00000001");
  auto silicon = parseBalance("siliconflow", R"({"code":20000,"status":true,"data":{"status":"normal","totalBalance":"0","balance":"0","chargeBalance":"0","email":"must-not-retain"}})");
  assert(silicon["balances"][0]["amount"] == "0" && silicon.dump().find("must-not-retain") == std::string::npos);
  assert(parseBalance("302ai", R"({"data":{"balance":"12.3456"}})")["balances"][0]["unit"] == "平台单位");
  for (const auto &body : {"{}", "<html>error</html>", "{\"data\":{\"balance\":null}}", "{\"data\":{\"balance\":\"NaN\"}}", "{\"data\":{\"balance\":true}}", "{\"data\":{\"balance\":\"1e999\"}}"}) {
    bool rejected = false;
    try { parseBalance("302ai", body); } catch (...) { rejected = true; }
    assert(rejected);
  }
  std::atomic<bool> cancelled{false};
  for (long status : {200,401,403,404,410,429,500,503}) {
    int calls = 0;
    auto result = queryBalance(deep, cancelled, [&](const std::string &url, const Json &headers, std::atomic<bool> &, const ApiOptions &options) {
      ++calls; assert(options.timeout == 15 && url == balanceEndpoint(deep));
      assert(headers.at("Authorization") == "Bearer fixture-secret-never-log");
      HttpResult r; r.status = status; r.body = status == 200 ? deepBody : "fixture-secret-never-log private profile"; return r;
    });
    assert(calls == 1 && result.at("status") == (status == 200 ? "ready" : "error"));
    assert(result.dump().find("fixture-secret") == std::string::npos);
  }
  auto timeout = queryBalance(deep, cancelled, [](const std::string &, const Json &, std::atomic<bool> &, const ApiOptions &) {
    HttpResult r; r.error = "private key and endpoint must never escape"; r.curlCode = 28; return r;
  });
  assert(timeout.at("error") == "余额查询超时，请手动重试");
  auto forbidden = queryBalance(provider("302ai", "https://api.302.ai/v1"), cancelled,
      [](const std::string &, const Json &, std::atomic<bool> &, const ApiOptions &) { HttpResult r; r.status = 403; return r; });
  assert(forbidden.at("error").get<std::string>().find("302.AI") != std::string::npos);
  cancelled = true;
  assert(queryBalance(deep, cancelled).at("status") == "cancelled");
  cancelled = false;
  auto changed = deep; changed["api_key"] = "another-key";
  assert(balanceIdentity(deep) != balanceIdentity(changed));
  changed = deep; changed["metadata_json"]["balance_vendor"] = "kimi";
  assert(balanceIdentity(deep) != balanceIdentity(changed));
  Events events;
  std::atomic<int> active{0}, peak{0}, started{0};
  {
    BalanceService service(events, [&](const Json &, std::atomic<bool> &cancel) {
      const int n = ++active; ++started;
      int old = peak.load(); while (n > old && !peak.compare_exchange_weak(old, n)) {}
      while (!cancel) usleep(1000);
      --active; return Json{{"status", "cancelled"}};
    });
    Json jobs = Json::array();
    for (int i = 0; i < 6; ++i) { auto p = deep; p["id"] = std::to_string(i); jobs.push_back(p); }
    service.request(jobs, 1);
    for (int i = 0; i < 1000 && started < 2; ++i) usleep(1000);
    assert(started == 2 && peak == 2);
    service.cancel(); service.stop();
    assert(active == 0 && started == 2 && events.drain().empty());
  }
  {
    BalanceService service(events, [&](const Json &, std::atomic<bool> &) { return parsed; });
    service.request(Json::array({deep}), 17);
    std::deque<Event> got;
    for (int i = 0; i < 1000 && got.empty(); ++i) { usleep(1000); got = events.drain(); }
    assert(got.size() == 1 && got[0].data["generation"] == 17);
    assert(got[0].data["identity"] == balanceIdentity(deep));
  }
  char folder[] = "/tmp/penchat-balance-XXXXXX"; assert(mkdtemp(folder));
  std::string path = std::string(folder) + "/penchat.db", id;
  {
    Database db(path);
    Json form = {{"name", "Fixture"}, {"baseURL", "https://api.deepseek.com/v1"},
                 {"apiKey", "fake"}, {"api", "responses"}};
    id = db.saveProvider(form, {}, false);
    assert(db.providerForm(id).at("balance_vendor") == "");
    db.addProviderModels(id, {"fixture-model"});
    auto before = db.query("SELECT * FROM model_configs"); form = db.providerForm(id); form["balance_vendor"] = "deepseek";
    db.saveProvider(form, {}, false);
    assert(db.query("SELECT * FROM model_configs") == before && db.providerForm(id).at("balance_vendor") == "deepseek");
  }
  { Database db(path); assert(db.providerForm(id).at("balance_vendor") == "deepseek"); }
  unlink(path.c_str()); rmdir(folder);
  std::cout << "Balance adapters, exact decimals, isolation, errors, cancellation, concurrency and persistence passed\n";
}
