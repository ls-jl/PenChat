#pragma once
#include "common.hpp"
#include <stdexcept>
namespace penchat {
class Database;
struct BackupRestoreError:std::runtime_error {using std::runtime_error::runtime_error;};
// Validation/planning run off LVGL; apply runs only after App shutdown. All live
// database changes still execute on Database's one worker.
class BackupRestore {
  struct Impl;
  std::unique_ptr<Impl> p_;
public:
  BackupRestore(Database &,Events &,std::string data,std::string publicRoot);
  ~BackupRestore();
  bool start(const std::string &operation,Json args=Json::object());
  void stop(bool keepPlan=false);
  void cancel();
  bool busy() const;
  uint64_t token() const;
  Json commit(const Json &decisions,const std::function<void(const Json &)> &progress={});
  static Json lastResult(const std::string &data,const std::string &operation);
  static std::string publicDirectory(const std::string &data);
  // Recover an interrupted file/SQLite transaction before opening the main DB.
  static void recover(const std::string &data,const std::string &publicRoot);
};
} // namespace penchat
