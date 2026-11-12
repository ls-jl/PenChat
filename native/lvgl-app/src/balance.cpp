#include "balance.hpp"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <cctype>
#include <regex>
#include <stdexcept>
namespace penchat {
namespace {
struct BalanceError : std::runtime_error { using std::runtime_error::runtime_error; };
// Keep the source decimal lexeme: JSON floating amounts must not first round
// through binary double. All other JSON types retain their normal meaning.
struct DecimalJson : nlohmann::json_sax<Json> {
  Json root;
  std::vector<Json> stack;
  std::vector<std::string> keys;
  void value(Json v) {
    if (stack.empty()) root = std::move(v);
    else if (stack.back().is_array()) stack.back().push_back(std::move(v));
    else stack.back()[keys.back()] = std::move(v);
  }
  bool null() override { value(nullptr); return true; }
  bool boolean(bool v) override { value(v); return true; }
  bool number_integer(number_integer_t v) override { value(v); return true; }
  bool number_unsigned(number_unsigned_t v) override { value(v); return true; }
  bool number_float(number_float_t, const string_t &v) override { value(v); return true; }
  bool string(string_t &v) override { value(v); return true; }
  bool binary(binary_t &) override { return false; }
  bool key(string_t &v) override { keys.back() = v; return true; }
  bool start_object(std::size_t) override { return begin(Json::object()); }
  bool start_array(std::size_t) override { return begin(Json::array()); }
  bool begin(Json v) {
    if (stack.size() >= 12) return false;
    stack.push_back(std::move(v)); keys.emplace_back(); return true;
  }
  bool end_object() override { return end(); }
  bool end_array() override { return end(); }
  bool end() {
    auto v = std::move(stack.back()); stack.pop_back(); keys.pop_back(); value(std::move(v)); return true;
  }
  bool parse_error(std::size_t, const std::string &, const nlohmann::detail::exception &) override { return false; }
};
std::string amount(const Json &v) {
  if (!v.is_string() && !v.is_number_integer()) throw BalanceError("余额金额格式异常");
  auto text = v.is_string() ? v.get<std::string>() : v.dump();
  static const std::regex decimal("^-?(0|[1-9][0-9]*)(\\.[0-9]+)?([eE][+-]?[0-9]+)?$");
  if (text.size() > 64 || !std::regex_match(text, decimal)) throw BalanceError("余额金额格式异常");
  auto exponent = text.find_first_of("eE");
  if (exponent == std::string::npos) return text;
  const auto tail = text.substr(exponent + 1);
  if (tail.size() > 4) throw BalanceError("余额金额超出显示范围");
  int power = std::stoi(tail);
  if (power < -32 || power > 32) throw BalanceError("余额金额超出显示范围");
  auto base = text.substr(0, exponent);
  const bool negative = base[0] == '-';
  if (negative) base.erase(0, 1);
  auto dot = base.find('.');
  int position = int(dot == std::string::npos ? base.size() : dot) + power;
  if (dot != std::string::npos) base.erase(dot, 1);
  if (position <= 0) base = "0." + std::string(-position, '0') + base;
  else if (position >= int(base.size())) base += std::string(position - int(base.size()), '0');
  else base.insert(size_t(position), ".");
  return (negative ? "-" : "") + base;
}
Json money(const Json &data, const char *total, const std::string &unit,
           std::initializer_list<std::pair<const char *, const char *>> fields) {
  if (!data.is_object() || !data.contains(total)) throw BalanceError("余额响应缺少必要字段");
  Json result = {{"amount", amount(data.at(total))}, {"unit", unit}, {"details", Json::array()}};
  for (const auto &f : fields)
    result["details"].push_back({{"label", f.second},
        {"amount", data.contains(f.first) && !data.at(f.first).is_null() ? amount(data.at(f.first)) : ""}});
  return result;
}
} // namespace
std::string balanceVendorName(const std::string &vendor) {
  return vendor == "deepseek" ? "DeepSeek" : vendor == "kimi" ? "Kimi / Moonshot" :
         vendor == "siliconflow" ? "硅基流动" : vendor == "302ai" ? "302.AI" : "不查询";
}
std::string balanceEndpoint(const Json &provider) {
  const auto vendor = objectField(provider, "metadata_json").value("balance_vendor", std::string());
  std::string host, path;
  if (vendor == "deepseek") { host = "api.deepseek.com"; path = "/user/balance"; }
  else if (vendor == "kimi") { host = "api.moonshot.cn"; path = "/v1/users/me/balance"; }
  else if (vendor == "siliconflow") { host = "api.siliconflow.cn"; path = "/v1/user/info"; }
  else if (vendor == "302ai") { host = "api.302.ai"; path = "/dashboard/balance"; }
  else throw BalanceError("尚未启用余额查询");
  // Parse, rather than substring-match, the origin. Never forward a relay key
  // to an official vendor chosen in the form, nor accept a URL userinfo trick.
  CURLU *url = curl_url();
  if (!url) throw BalanceError("无法解析服务地址");
  auto input = provider.value("base_url", std::string());
  const bool parsed = curl_url_set(url, CURLUPART_URL, input.c_str(), 0) == CURLUE_OK;
  auto part = [&](CURLUPart kind) {
    char *v = nullptr; std::string out;
    if (curl_url_get(url, kind, &v, 0) == CURLUE_OK) { out = v; curl_free(v); }
    return out;
  };
  auto actual = part(CURLUPART_HOST), scheme = part(CURLUPART_SCHEME), port = part(CURLUPART_PORT);
  std::transform(actual.begin(), actual.end(), actual.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  const bool valid = parsed && scheme == "https" && actual == host && (port.empty() || port == "443") &&
                     part(CURLUPART_USER).empty() && part(CURLUPART_PASSWORD).empty();
  curl_url_cleanup(url);
  if (!valid) throw BalanceError("当前地址不支持此厂商的余额查询");
  return "https://" + host + path;
}
std::string balanceIdentity(const Json &provider) {
  const auto metadata = objectField(provider, "metadata_json");
  const auto raw = Json::array({provider.value("id", Json()), provider.value("base_url", Json()),
      provider.value("api_key", Json()), metadata.value("balance_vendor", Json()),
      metadata.value("credential_revision", Json()), metadata.value("compatibility", Json())}).dump();
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const unsigned char *>(raw.data()), raw.size(), hash);
  const char *hex = "0123456789abcdef"; std::string result;
  for (auto c : hash) { result += hex[c >> 4]; result += hex[c & 15]; }
  return result;
}
Json parseBalance(const std::string &vendor, const std::string &body) {
  if (body.size() > 65536) throw BalanceError("余额响应过大");
  DecimalJson parsed;
  if (!Json::sax_parse(body, &parsed) || !parsed.root.is_object()) throw BalanceError("余额响应格式异常");
  const auto &root = parsed.root;
  if (root.contains("error") && !root.at("error").is_null()) throw BalanceError(vendor == "302ai" ? "302.AI未返回余额，请检查 API Key 和余额查询权限" : "余额接口未返回有效结果");
  if (root.contains("status") && root.at("status") == Json(false)) throw BalanceError("余额查询未成功，请检查密钥和查询权限");
  Json values = Json::array();
  if (vendor == "deepseek") {
    const auto rows = root.value("balance_infos", Json());
    if (!rows.is_array() || rows.empty() || rows.size() > 8) throw BalanceError("余额响应缺少必要字段");
    for (const auto &row : rows) {
      const auto unit = row.value("currency", std::string());
      if (unit != "CNY" && unit != "USD") throw BalanceError("余额币种暂不支持");
      values.push_back(money(row, "total_balance", unit, {{"granted_balance", "赠送余额"}, {"topped_up_balance", "充值余额"}}));
    }
  } else if (vendor == "kimi") {
    values.push_back(money(root.value("data", Json()), "available_balance", "CNY", {{"cash_balance", "现金余额"}, {"voucher_balance", "代金券余额"}}));
  } else if (vendor == "siliconflow") {
    if (root.value("code", Json()) != Json(20000)) throw BalanceError("余额接口未返回有效结果");
    const auto data = root.value("data", Json());
    if (data.is_object() && data.value("status", Json()) == Json("")) throw BalanceError("余额接口当前不可用，请到服务商控制台核对");
    values.push_back(money(data, "totalBalance", "CNY", {{"chargeBalance", "充值余额"}, {"balance", "非充值余额"}}));
  } else if (vendor == "302ai") {
    values.push_back(money(root.value("data", Json()), "balance", "平台单位", {}));
  } else throw BalanceError("不支持此余额查询厂商");
  return {{"balances", values}};
}
Json queryBalance(const Json &provider, std::atomic<bool> &cancel, const BalanceTransport &transport) {
  Json result = {{"status", "error"}};
  try {
    const auto url = balanceEndpoint(provider);
    if (cancel) return {{"status", "cancelled"}};
    const auto key = provider.value("api_key", std::string());
    if (key.empty() || key.size() > 4096 || key.find('\0') != std::string::npos || key.find_first_of("\r\n") != std::string::npos) throw BalanceError("请先保存有效的 API Key");
    Json headers = {{"Authorization", "Bearer " + key}, {"Accept", "application/json"}};
    Json model = {{"provider_metadata", objectField(provider, "metadata_json")}};
    auto options = apiOptions(model); options.timeout = 15;
    HttpResult response;
    if (transport) response = transport(url, headers, cancel, options);
    else {
      std::string body; bool oversized = false;
      response = http(url, "GET", headers, "", cancel, [&](const char *data, size_t n) {
        if (body.size() + n > 65536) { oversized = true; throw BalanceError("余额响应过大"); }
        body.append(data, n);
      }, options, {}, {provider.value("id", std::string()), url, balanceIdentity(provider), "balance"});
      if (!cancel && oversized) throw BalanceError("余额响应过大");
      if (response.status >= 200 && response.status < 300) response.body = std::move(body);
    }
    if (cancel) return {{"status", "cancelled"}};
    const auto vendor = objectField(provider, "metadata_json").value("balance_vendor", std::string());
    if (response.status == 401) throw BalanceError("密钥无效或已失效，请检查 API Key");
    if (response.status == 403) throw BalanceError(vendor == "302ai" ? "请在302.AI开启此 API Key 的余额查询权限" : "当前密钥没有余额查询权限");
    if (response.status == 404 || response.status == 410) throw BalanceError("余额接口当前不可用，请到服务商控制台核对");
    if (response.status == 429) throw BalanceError("查询过于频繁，请稍后手动刷新");
    if (response.status >= 500) throw BalanceError("服务商暂时不可用，请稍后手动刷新");
    if (!response.error.empty()) throw BalanceError(response.curlCode == CURLE_OPERATION_TIMEDOUT ? "余额查询超时，请手动重试" : "网络连接失败，请检查网络或代理");
    if (response.status < 200 || response.status >= 300) throw BalanceError("余额查询失败（HTTP " + std::to_string(response.status) + "）");
    result = parseBalance(vendor, response.body);
    result["status"] = "ready"; result["queried_at"] = nowMs();
  } catch (const BalanceError &e) { result["error"] = e.what(); }
  catch (...) { result["error"] = "余额响应格式异常"; }
  return result;
}
BalanceService::BalanceService(Events &events, Query query) : events_(events), query_(std::move(query)) {
  if (!query_) query_ = [](const Json &p, std::atomic<bool> &c) { return queryBalance(p, c); };
  try { for (auto &worker : workers_) worker = std::thread([this] { run(); }); }
  catch (...) { stop(); throw; }
}
BalanceService::~BalanceService() { stop(); }
void BalanceService::request(const Json &providers, uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopped_) return;
  if (cancel_) *cancel_ = true;
  jobs_.clear(); cancel_ = std::make_shared<std::atomic<bool>>(false);
  for (const auto &p : providers) jobs_.push_back({p, generation, cancel_});
  ready_.notify_all();
}
void BalanceService::cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (cancel_) *cancel_ = true;
  jobs_.clear();
}
void BalanceService::stop() {
  { std::lock_guard<std::mutex> lock(mutex_); stopped_ = true; if (cancel_) *cancel_ = true; jobs_.clear(); }
  ready_.notify_all();
  for (auto &worker : workers_) if (worker.joinable()) worker.join();
}
void BalanceService::run() {
  while (true) {
    Job job;
    { std::unique_lock<std::mutex> lock(mutex_); ready_.wait(lock, [&] { return stopped_ || !jobs_.empty(); });
      if (stopped_) return;
      job = std::move(jobs_.front()); jobs_.pop_front(); }
    if (*job.cancel) continue;
    Json result;
    try { result = query_(job.provider, *job.cancel); }
    catch (...) { result = {{"status", "error"}, {"error", "余额查询失败"}}; }
    if (*job.cancel) continue;
    result["id"] = job.provider.at("id"); result["generation"] = job.generation;
    result["identity"] = balanceIdentity(job.provider);
    events_.push("balance_result", std::move(result));
  }
}
} // namespace penchat
