#pragma once
#include "database.hpp"
namespace penchat {
// One background operation at a time; only structured results reach LVGL.
class Transfer {
  Database &db_;
  Events &events_;
  std::string data_, root_, stage_;
  std::thread worker_;
  std::atomic<bool> cancel_{false}, busy_{false};
  uint64_t token_ = 0;
  Json preview_;
  void check();
  void directories();
  Json execute(const std::string &op, const Json &args);
  Json exportData(const Json &args);
  Json previewFile(const Json &args);
  Json importData(const Json &args);

public:
  Transfer(Database &, Events &, const std::string &data);
  ~Transfer();
  bool start(const std::string &op, Json args = Json::object());
  void stop();
  bool finish(bool persistent);
  void cancel() { cancel_ = true; }
  uint64_t token() const { return token_; }
  bool busy() const { return busy_; }
  const std::string &root() const { return root_; }
};
} // namespace penchat
