#pragma once
#include "common.hpp"
#include <future>
struct sqlite3;
namespace penchat {
class Database {
  sqlite3 *db_ = nullptr;
  std::string assetDirectory_;
  std::recursive_mutex mutex_;
  std::mutex jobsMutex_;
  std::condition_variable jobsReady_;
  std::deque<std::function<void()>> jobs_;
  bool stopping_ = false;
  std::thread worker_;
  Json perform(std::function<Json()> task);
  Json queryOnWorker(const std::string &sql, const Json &args);

public:
  explicit Database(const std::string &path);
  ~Database();
  Json query(const std::string &sql, const Json &args = Json::array());
  void execute(const std::string &sql, const Json &args = Json::array());
  Json settings();
  void setting(const std::string &key, const Json &value);
  Json sessions(int offset = 0, bool archived = false);
  Json sessionInfo(const std::string &session);
  void renameSession(const std::string &session, const std::string &title);
  void pinSession(const std::string &session, bool pinned);
  void archiveSession(const std::string &session, bool archived);
  Json prepareTitle(const std::string &session, const std::string &assistant,
                    const std::string &model);
  Json pendingTitles();
  bool startTitle(const std::string &session, const std::string &token);
  bool finishTitle(const std::string &session, const std::string &token,
                   const std::string &title);
  Json messages(const std::string &session, int offset = 0, int limit = 30);
  std::string createSession();
  void removeSession(const std::string &session);
  std::string append(const std::string &session, const std::string &role,
                     const std::string &text,
                     const Json &image = Json::object(),
                     const Json &preferences = Json::object());
  void updateMessage(const std::string &id, const std::string &text,
                     const std::string &reasoning, const std::string &status,
                     const Json &error = Json(),
                     const Json &metadata = Json::object());
  Json providers();
  Json providerForm(const std::string &id);
  Json models();
  Json activeModel();
  Json sessionPreferences(const std::string &session);
  void saveSessionPreferences(const std::string &session,
                              const Json &preferences);
  void saveModelCapabilities(const std::string &id, const Json &reasoning);
  void recordRequest(const std::string &message, const Json &model);
  void saveProvider(const Json &form, const std::vector<std::string> &names);
  void selectModel(const std::string &id);
  Json responseInput(const std::string &session,
                     const std::string &beforeMessage,
                     const Json &model = Json::object());
  Json summary();
};
} // namespace penchat
