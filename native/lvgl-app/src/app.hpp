#pragma once
#include "database.hpp"
#include "images.hpp"
#include "network.hpp"
#include "platform.hpp"
#include "pull_geometry.hpp"
#include "reasoning.hpp"
#include "scanner.hpp"
#include "theme.hpp"
#include <map>
#include <set>
namespace penchat {
class App {
  struct Texture {
    lv_image_dsc_t descriptor{};
    std::shared_ptr<ImageFrame> frame;
  };
  struct Raster {
    lv_obj_t *holder, *owner;
    Block block;
    int width, height, contentWidth;
    bool subdued;
    int tableId;
    std::string tableKey;
    std::map<std::pair<int, int>, lv_obj_t *> tiles;
  };
  Json lastTouch_ = Json::object();
  Database &db_;
  Bridge &bridge_;
  Display &display_;
  Input &input_;
  Screen screen_;
  Events events_;
  Images images_;
  Camera camera_;
  ScanService scanner_;
  uint64_t scanId_ = 0, scanGeneration_ = 0, scanConfirmed_ = 0;
  std::string scanSession_;
  Json scanResult_ = Json::object();
  bool scanConfirming_ = false;
  lv_obj_t *scanImage_ = nullptr, *scanLabel_ = nullptr, *scanUse_ = nullptr,
           *scanHint_ = nullptr;
  std::function<void()> afterScanRelease_;
  void syncScanner();
  void scanPage();
  void scanEvent(const Event &);
  void whenScannerReleased(std::function<void()>);
  RichText rich_;
  std::string workspace_, dataDir_, view_ = "chat", session_, draft_,
                                    assistant_, output_, reasoning_, notice_;
  Json providerForm_, rows_ = Json::array();
  Json sessionPreferences_ = Json::object(),
       reasoningMetadata_ = Json::object(), modelForm_ = Json::object();
  std::string effortMemory_, popupKind_;
  std::set<std::string> expandedReasoning_;
  std::map<std::string, bool> reasoningOverrides_;
  std::set<lv_obj_t *> chatPhotos_;
  std::string imageDeleteId_;
  void reasoningDisplayPicker();
  std::vector<lv_obj_t *> reasoningHeaders_;
  bool modelAdvanced_ = false, hasConversationContent_ = false;
  bool navigationPointerDown_ = false, navigationBackdrop_ = false;
  int navigationStartX_ = 0, navigationStartY_ = 0, navigationDistance_ = 0;
  Json currentModel();
  void chooseModel(const std::string &id);
  void chooseEffort(const std::string &key);
  void modelPicker(bool forTitles = false);
  void effortPicker();
  void modelPage();
  void savePreferences();
  void bindPreferences(const Json &model);
  std::deque<Json> titleJobs_;
  std::thread titleWorker_;
  std::atomic<bool> titleCancel_{false}, titleBusy_{false};
  std::string titleSession_, historyContextId_, longPressedSession_;
  bool historyDirty_ = false;
  int archiveOffset_ = 0;
  std::vector<std::pair<std::string, lv_obj_t *>> historyRows_;
  int titleWidth() const;
  int titleLimit() const;
  std::string fitTitle(const std::string &raw) const;
  void scheduleTitle();
  void tickTitles();
  void archivesPage();
  void historyContext(const std::string &id);
  void closeHistoryContext();
  void retireSession(const std::string &id, bool archive);
  lv_obj_t *historyRow(lv_obj_t *parent, const Json &session, int y, int width);
  int messageOffset_ = 0, historyOffset_ = 0, networkGeneration_ = 0;
  bool generating_ = false, visible_ = true, keyboard_ = false;
  std::atomic<bool> cancel_{false};
  std::thread network_;
  std::thread modelWorker_;
  std::atomic<bool> modelCancel_{false}, modelsBusy_{false};
  lv_obj_t *screenObj_ = nullptr, *body_ = nullptr, *messageContent_ = nullptr,
           *composer_ = nullptr, *status_ = nullptr, *cameraImage_ = nullptr,
           *liveContent_ = nullptr;
  std::vector<Raster> rasters_;
  std::vector<std::pair<lv_obj_t *, std::string>> imageRequests_;
  std::map<lv_obj_t *, std::unique_ptr<Texture>> textures_;
  std::map<lv_obj_t *, lv_obj_t *> placeholders_;
  std::function<void(const std::string &)> editCompletion_;
  std::function<void()> editCancellation_;
  std::string keyboardId_, ocrId_, ocrNotice_;
  bool ocrActive_ = false;
  int modelGeneration_ = 0;
  int64_t renderDue_ = 0, lastCameraFrame_ = 0;
  bool dirty_ = false;
  bool chatFollowing_ = true, chatLayout_ = false;
  int chatScroll_ = 0, chatControlsTarget_ = -1;
  std::string builtView_;
  lv_obj_t *chatControls_ = nullptr, *toBottom_ = nullptr;
  lv_obj_t *attachments_ = nullptr, *modal_ = nullptr,
           *pullIndicator_ = nullptr, *pullArc_ = nullptr,
           *pullLabel_ = nullptr, *pullBadge_ = nullptr;
  lv_obj_t *navigation_ = nullptr, *navigationList_ = nullptr,
           *navigationActive_ = nullptr;
  bool navigationOpen_ = false;
  int navigationScroll_ = 0;
  bool touchDown_ = false, pullCandidate_ = false, pulling_ = false;
  bool pullNeedsPause_ = false, pullTrackActivity_ = false;
  bool pullCreateAllowed_ = false;
  int64_t pullLastActivity_ = 0, pullMotionTime_ = 0, pullSlowTime_ = 0;
  int pullMotionY_ = 0, pullSlowDistance_ = 0;
  PullGeometry pullGeometry_;
  int pullStartX_ = 0, pullStartY_ = 0, pullProgress_ = 0,
      pullVisualOrigin_ = 0;
  int albumPage_ = 0;
  std::vector<std::string> modelNames_;
  lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                uint32_t color = theme::background);
  lv_obj_t *label(lv_obj_t *parent, const std::string &text, int x, int y,
                  int w, int size = 21, uint32_t color = theme::text);
  lv_obj_t *button(lv_obj_t *parent, const std::string &text, int x, int y,
                   int w, int h, std::function<void()> click,
                   uint32_t color = theme::surface,
                   lv_text_align_t align = LV_TEXT_ALIGN_CENTER);
  lv_obj_t *imageWidget(lv_obj_t *parent);
  void image(lv_obj_t *parent, const std::string &path, int x, int y, int w,
             int h, std::function<void()> click = {}, bool rightAligned = false,
             bool chatPhoto = false, const std::string &pendingMessage = "");
  void texture(lv_obj_t *object, std::shared_ptr<ImageFrame> frame,
               bool premultiplied = false);
  lv_obj_t *icon(lv_obj_t *parent, const std::string &kind, int x, int y, int w,
                 int h, std::function<void()> action = {},
                 uint32_t background = theme::background,
                 uint32_t ink = theme::text);
  void closeAttachments();
  void toggleAttachments();
  void resetPull();
  bool chatTouch(int x, int y, bool pressed);
  void clear();
  void build();
  void chat();
  void chatScrolled();
  void updateChatControls(bool animate);
  void jumpToBottom();
  void pageHeader(const std::string &title);
  void settingsPage();
  void providerPage();
  void editProvider(const std::string &id);
  void startKeyScan();
  void stopKeyScan();
  void keyScanPage();
  void historyDrawer();
  void toggleNavigation();
  void goBack();
  void closeNavigation(bool resumeCamera = true);
  void cameraPage();
  void cropPage();
  void updateCrop();
  void beginCrop(const Json &photo);
  void confirmCrop();
  void clearCrop(bool keepAlbum = false);
  Json cropPhoto_, cropResult_;
  std::thread cropWorker_;
  int cropGeneration_ = 0;
  bool cropBusy_ = false, cropSaveAlbum_ = false;
  int cropX_ = 0, cropY_ = 0, cropW_ = 0, cropH_ = 0;
  int cropPreviewW_ = 0, cropPreviewH_ = 0;
  lv_obj_t *cropSelection_ = nullptr;
  lv_obj_t *cropShades_[4] = {};

  void albumPage();
  void renderMessages(bool bottom);
  void richMessage(lv_obj_t *parent, const std::string &text, int width,
                   bool subdued = false, const std::string &context = "");
  void paintVisible();
  void richScrolled(lv_obj_t *object);
  bool syncingTableScroll_ = false;
  std::map<std::string, int> tableScrolls_;
  void renderLiveMessage();
  void refreshImages();
  void edit(const std::string &text, const std::string &placeholder,
            std::function<void(const std::string &)> callback,
            int maxLength = 4096, const char *inputType = "ZhCNPreferred",
            std::function<void()> cancelled = {});
  bool canSend();
  void sendMessage(const std::string &retry = "");
  void attach(const Json &image);
  void newChat();
  void stopNetwork();
  void fetchModels();
  void saveProvider();
  void show(const std::string &view);
  void error(const std::string &message);
  void modal(const std::string &title, const std::string &text);

public:
  App(Database &, Bridge &, Display &, Input &, const Screen &,
      std::string workspace, std::string dataDir, const std::string &fontPath,
      const std::string &resources);
  ~App();
  void tick();
  void handle(const Json &message);
  void suspend(bool value);
  void shutdown();
  void demo(const std::string &markdown);
  Json diagnostics() const;
};
} // namespace penchat
