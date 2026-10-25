#pragma once
#include "common.hpp"
#include <future>
#include <map>
#include <set>
struct sqlite3;
struct sqlite3_stmt;
namespace penchat {
class Database {
  sqlite3 *db_ = nullptr;
  std::string assetDirectory_;
  unsigned assetLeases_ = 0;
  std::map<std::string, sqlite3_stmt *> statements_;
  Json settingsCache_, modelsCache_;
  bool onWorker() const { return std::this_thread::get_id() == worker_.get_id(); }
  std::string archiveCopy(const Json &snapshot, const std::string &label, const std::string &key);
  void copyConversationRows(const Json &session, const Json &rows,
                            std::atomic<bool> *cancel = nullptr);
  void deleteConversationRows(const std::string &session, const Json &rows);
  void refreshConversationCount(const std::string &session);
  void migrate(const std::string &path);
  void collectAssets(bool discover = false);
  void deferAssetCollection();
  void *gcDirectory_ = nullptr;
  std::string gcCursor_;
  bool gcScheduled_ = false;
  void enqueueAssets(const Json &assets);
  void detachSharedAssets(const std::string &column, const std::string &id);
  void savePending(const std::string &id);
  std::map<std::string, Json> pendingSaves_, failedSaves_, saveVersions_;
  std::set<std::string> scheduledSaves_;
  int latestSaveGeneration_ = -1;
  Events saveEvents_;
  std::map<std::string, std::map<int, Json>> sessionCursors_;
  std::map<std::string, std::map<int, int64_t>> messageCursors_;
  uint64_t queryCount_ = 0, transactionCount_ = 0;
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
  void queueSetting(const std::string &key, const Json &value);
  Json sessions(int offset = 0, bool archived = false);
  Json sessionsAfter(const Json &cursor, bool archived = false);
  Json messagesBefore(const std::string &session, int64_t before, int limit = 30);
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
  bool pendingImage(const std::string &session, const std::string &message);
  bool removePendingImage(const std::string &session, const std::string &message);
  std::string append(const std::string &session, const std::string &role,
                     const std::string &text, const Json &image = Json::object(),
                     const Json &preferences = Json::object());
  void updateMessage(const std::string &id, const std::string &text,
                     const std::string &reasoning, const std::string &status,
                     const Json &error = Json(), const Json &metadata = Json::object());
  void queueMessage(const std::string &session, const std::string &id, int generation,
                    uint64_t version, const std::string &text,
                    const std::string &reasoning, const std::string &status,
                    const Json &error, const Json &metadata);
  std::deque<Event> saveEvents() { return saveEvents_.drain(); }
  void retrySaves();
  void flush();
  Json storageStats();
  Json requestModel(const Json &model);
  Json providers();
  Json providerForm(const std::string &id);
  Json models();
  Json activeModel();
  Json sessionPreferences(const std::string &session);
  void saveSessionPreferences(const std::string &session, const Json &preferences);
  void saveModelCapabilities(const std::string &id, const Json &reasoning,
                             int budget = 0, int maxOutput = 0);
  void recordRequest(const std::string &message, const Json &model,
                     int64_t sentAt=0,int64_t startedAt=0);
  void recordActivity(const std::string &session,int64_t stamp);
  void saveProvider(const Json &form, const std::vector<std::string> &names);
  void selectModel(const std::string &id);
  Json responseInput(const std::string &session, const std::string &beforeMessage,
                     const Json &model = Json::object());
  void transferSnapshot(const std::string &path);
  void releaseTransferSnapshot();
  Json importTransfer(const Json &bundle);
  Json contextSnapshot(const std::string &session, bool lease = false);
  Json commitCompression(const Json &snapshot, Json checkpoint,
                         std::atomic<bool> &cancel);
  Json messageEditSnapshot(const std::string &session, const std::string &message);
  Json commitMessageEdit(const Json &snapshot, const Json &images,
                         const std::string &text, const Json &preferences,
                         bool archive, int64_t sentAt);
  Json commitRegeneration(const Json &snapshot, const std::string &message,
                          bool archive, Json checkpoint, std::atomic<bool> &cancel);
  Json createBranch(const Json &snapshot, const std::string &message,
                    const Json &preferences, std::atomic<bool> &cancel);
  Json summary();
};
} // namespace penchat
