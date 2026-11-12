#pragma once
#include "network.hpp"
namespace penchat {
std::string balanceVendorName(const std::string &vendor);
std::string balanceEndpoint(const Json &provider);
std::string balanceIdentity(const Json &provider);
Json parseBalance(const std::string &vendor, const std::string &body);
using BalanceTransport = std::function<HttpResult(const std::string &, const Json &,
                                                  std::atomic<bool> &, const ApiOptions &)>;
Json queryBalance(const Json &provider, std::atomic<bool> &cancel,
                  const BalanceTransport &transport = {});
class BalanceService {
public:
  using Query = std::function<Json(const Json &, std::atomic<bool> &)>;
  explicit BalanceService(Events &events, Query query = {});
  ~BalanceService();
  void request(const Json &providers, uint64_t generation);
  void cancel();
  void stop();
private:
  struct Job { Json provider; uint64_t generation; std::shared_ptr<std::atomic<bool>> cancel; };
  Events &events_;
  Query query_;
  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<Job> jobs_;
  std::shared_ptr<std::atomic<bool>> cancel_;
  bool stopped_ = false;
  std::thread workers_[2];
  void run();
};
} // namespace penchat
