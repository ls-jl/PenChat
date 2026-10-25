#pragma once
#include "database.hpp"
#include "transfer.hpp"
#include "editor.hpp"
#include "pinyin.hpp"
#include "editor_layout.hpp"
#include "images.hpp"
#include "image_input.hpp"
#include "network.hpp"
#include "platform.hpp"
#include "pull_geometry.hpp"
#include "reasoning.hpp"
#include "presets.hpp"
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
    int width, height, contentWidth, fontSize;
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
  DirectImageInput imageInput_;
  bool directImageInput_ = false;
  int64_t imageInputExitDue_ = 0;
  Events events_;
  PinyinService pinyin_;
  std::unique_ptr<Transfer> transfer_;
  lv_obj_t *transferProgress_ = nullptr;
  std::string transferBuiltScreen_, transferScreen_ = "home", transferKind_ = "chats", transferOp_, transferError_, transferMapKey_;
  Json transferData_ = Json::object(), transferPreview_ = Json::object(), transferMapping_ = Json::object(), transferExportArgs_;
  std::set<std::string> transferSelected_;
  std::set<size_t> transferIndices_;
  int transferOffset_ = 0;
  bool transferArchived_ = false, transferCopy_ = false, transferMissing_ = false, transferDefaultConfirmed_ = false;
  std::string transferDefaultAction_ = "replace";
  void transferPage();
  void transferRequest(const std::string &, Json args = Json::object());
  void transferEvent(const Event &);
  void openTransfer(const std::string &screen = "home", const std::string &session = "");
  void transferBack();
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
  lv_obj_t *imageViewer_ = nullptr, *viewerCanvas_ = nullptr, *viewerImage_ = nullptr,
           *viewerStatus_ = nullptr, *viewerClose_ = nullptr;
  std::string viewerKey_;
  std::shared_ptr<ImageFrame> viewerFrame_;
  double viewerZoom_ = 1, viewerPanX_ = 0, viewerPanY_ = 0,
         viewerStartPanX_ = 0, viewerStartPanY_ = 0;
  int viewerScroll_ = 0, viewerStartX_ = 0, viewerStartY_ = 0,
      viewerLastX_ = 0, viewerLastY_ = 0, viewerDistance_ = 0,
      viewerLastTapX_ = 0, viewerLastTapY_ = 0;
  int64_t viewerLastTap_ = 0;
  bool viewerPointer_ = false, viewerCanvasTouch_ = false, viewerFailed_ = false;
  void requestImageViewer(const std::string &path);
  void openImageViewer(const std::string &path);
  void closeImageViewer(bool restore = true);
  void refreshImageViewer();
  void updateImageViewer();
  void zoomImageViewer(double zoom, int x = -1, int y = -1);
  void finishImageGesture(bool animate = true);
  void imageReturnTick();
  double imageViewerFit() const;
  bool imageTouch(const Json &);
  struct ImageGesture {
    // 0 idle, 1 pan, 2 resize, 3 pinch, 4 controls, -1 cancelled until release.
    int mode = 0, edges = 0, count = 0;
    bool multi = false;
    double x = 0, y = 0, distance = 0, zoom = 1, panX = 0, panY = 0;
    double left = 0, top = 0, right = 0, bottom = 0;
    std::string first, second;
  } imageGesture_;
  int imageTouchPeak_ = 0, imagePinchUpdates_ = 0;
  uint64_t imageGestureSerial_ = 0;
  bool nonImageMulti_ = false;
  int64_t viewerReturnAt_ = 0;
  double viewerReturnZoom_ = 1;
  Json imageViewerDiagnostic() const;

  std::map<std::string,std::string> sessionDrafts_;
  bool switchDraft(const std::string &next);
  struct ReplyControls {lv_obj_t *bar,*copy,*retry,*branch;bool body,question;};
  std::map<std::string,ReplyControls> replyControls_;
  std::string replyConfirmId_,replyToken_,replySession_;
  Json replyConfirmModel_=Json::object();
  std::thread replyWorker_;
  std::atomic<bool> replyCancel_{false};
  bool replyBusy_=false,contextRegenerating_=false;
  void replyBar(lv_obj_t *,const Json &,int,bool);
  void syncReplyControls();
  void replyToast(const std::string &);
  void retryWarning(const std::string &);
  void branchReply(const std::string &);
  bool replyEvent(const Event &);
  Json replyDiagnostic() const;
  std::string imageDeleteId_;
  Json messageEdit_ = Json::object(), messageEditImages_ = Json::array();
  Json messageEditPreferences_ = Json::object();
  std::string messageEditToken_, messageContextId_, messageEditMemory_;
  std::thread messageEditWorker_;
  lv_obj_t *messagePrepareModal_ = nullptr;
  bool messageEditBusy_ = false, messageEditArchive_ = false;
  int messageEditOffset_ = 0, messageEditScroll_ = 0;
  bool messageEditFollowing_ = true;
  std::vector<std::string> messageEditFiles_;
  std::vector<int64_t> messageEditCursors_;
  std::map<std::string,lv_obj_t *> userBubbles_, messageActions_;
  void initializeMessageEdits();
  void messageLongPress(lv_obj_t *, const std::string &id);
  void messageMenu(const std::string &id);
  void messageEditWarning(const std::string &id);
  void messageEditDefaultPicker();
  void beginMessageEdit(const std::string &id, bool archive);
  void cancelMessageEdit(bool restore = true);
  void commitMessageEdit();
  bool messageEvent(const Event &);
  Json editedMessages();
  Json messageDiagnostic() const;
  void selectMessageText(const std::string &text);
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
  int historyDayKey_ = -1;
  int64_t sendActionAt_ = 0;
  Json messageTimeline_ = Json::object();
  void historyTimePicker();
  void historyCaption(lv_obj_t *,const Json &,int,int,int);
  struct HistoryCaption {lv_obj_t *title,*time;};
  std::map<std::string,HistoryCaption> historyCaptions_;
  lv_obj_t *historyFullTitle_=nullptr,*historyFullTime_=nullptr;
  std::map<std::string,lv_obj_t *> historyActions_;
  int archiveOffset_ = 0;
  std::string archiveHighlight_;
  std::vector<std::pair<std::string, lv_obj_t *>> historyRows_;
  int titleWidth() const;
  int titleLimit() const;
  std::string fitTitle(const std::string &raw) const;
  void scheduleTitle(const std::string &session, const std::string &assistant);
  void tickTitles();
  void archivesPage();
  void openArchiveReader(const std::string &id);
  void archiveReaderPage();
  std::string archiveReaderId_;
  int archiveReaderOffset_=0, archiveListScroll_=0;
  std::vector<std::string> archiveReaderMessages_;
  std::map<std::string,bool> archiveReasoningOverrides_;
  std::map<std::string,lv_obj_t *> archiveViewButtons_;
  void historyContext(const std::string &id);
  void closeHistoryContext();
  void retireSession(const std::string &id, bool archive);
  lv_obj_t *historyRow(lv_obj_t *parent, const Json &session, int y, int width);
  int messageOffset_ = 0, historyOffset_ = 0, networkGeneration_ = 0;
  bool shutdownComplete_ = false;
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
  std::string keyboardId_;
  int modelGeneration_ = 0;
  bool persistDirty_ = false, saveInFlight_ = false;
  uint64_t saveVersion_ = 0;
  int64_t persistDue_ = 0;
  std::string reasoningDisplay_ = "collapsed";
  int lastAssistantSeq_ = 0;
  void persistReply(const std::string &status, const Json &error = Json());
  int64_t renderDue_ = 0, lastCameraFrame_ = 0;
  bool dirty_ = false;
  bool chatFollowing_ = true, chatLayout_ = false;
  int chatScroll_ = 0, chatControlsTarget_ = -1;
  std::string builtView_;
  int settingsScroll_ = 0;
  int readingFontPercent_ = 100, readingFontDraft_ = 100;
  int readingFontSize() const { return (21 * readingFontPercent_ + 50) / 100; }
  void fontSizePage();
  void updateFontPreview(int percent);
  lv_obj_t *fontSlider_ = nullptr, *fontPercentLabel_ = nullptr, *fontPreview_ = nullptr;
  lv_obj_t *settingsHeading_ = nullptr;
  lv_obj_t *pageHeading_ = nullptr, *pageHeader_ = nullptr, *pageContent_ = nullptr;
  std::string builtPageKey_;
  std::map<std::string, lv_obj_t *> pageActions_;
  struct SettingsControl {
    lv_obj_t *row, *title, *detail, *indicator;
    bool toggle;
  };
  std::map<std::string, SettingsControl> settingsControls_;
  lv_obj_t *chatControls_ = nullptr, *toBottom_ = nullptr;
  lv_obj_t *attachments_ = nullptr, *modal_ = nullptr,
           *pullIndicator_ = nullptr, *pullArc_ = nullptr,
           *pullLabel_ = nullptr, *pullBadge_ = nullptr;
  lv_obj_t *navigation_ = nullptr, *navigationList_ = nullptr,
           *navigationActive_ = nullptr;
  bool navigationOpen_ = false;
  int navigationScroll_ = 0;
  bool attachmentMenuTouch_ = false;
  bool composerTapAllowed_ = false;
  int composerTouchX_ = 0, composerTouchY_ = 0;
  bool touchDown_ = false, pullCandidate_ = false, pulling_ = false;
  bool pullNeedsPause_ = false, pullTrackActivity_ = false;
  bool pullCreateAllowed_ = false;
  int64_t pullLastActivity_ = 0, pullMotionTime_ = 0, pullSlowTime_ = 0;
  int pullMotionY_ = 0, pullSlowDistance_ = 0;
  PullGeometry pullGeometry_;
  int pullSensitivityOffset_ = 0;
  lv_obj_t *pullSlider_ = nullptr;
  void pullSensitivityPicker();
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
  void primary(lv_obj_t *button);
  lv_obj_t *fieldButton(lv_obj_t *parent, const std::string &caption,
                        const std::string &value, int x, int y, int width,
                        std::function<void()> action);
  void emptyState(const std::string &title, const std::string &hint);
  lv_obj_t *imageWidget(lv_obj_t *parent);
  void image(lv_obj_t *parent, const std::string &path, int x, int y, int w,
             int h, std::function<void()> click = {}, bool rightAligned = false,
             bool chatPhoto = false, const std::string &pendingMessage = "",
             const std::string &sentMessage = "");
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
  void pageHeader(const std::string &title, int top = 0);
  lv_obj_t *pageAction(const std::string &title, int right, int width, std::function<void()> action);
  std::string pageKey() const;
  Json pageDiagnostic() const;
  void settingsPage();
  void settingsRow(const std::string &id, const std::string &title,
                   const std::string &detail, int x, int y, int width,
                   std::function<void()> action, int checked = -1,
                   bool unavailable = false);
  Json settingsDiagnostic() const;
  void providerPage();
  void apiCompatibilityPage();
  int providerScroll_ = 0;
  void editProvider(const std::string &id);
  void historyDrawer();
  void toggleNavigation();
  void goBack();
  bool navigationRoot() const;
  std::string backContext() const;
  void syncBackPolicy();
  void nativeBack();
  void systemBack(const Json &);
  void routeTouch(const Json &);
  bool backTouch(const Json &);
  void resetBack();
  uint64_t backPolicySequence_ = 0, backPolicyGeneration_ = 0;
  uint64_t backGesture_ = 0, backConsumedGesture_ = 0;
  std::string backPolicyContext_;
  bool backPolicyRoot_ = false, backPolicyOk_ = false, backPolicyBlocked_ = false;
  bool backDown_ = false, backCandidate_ = false, backClaimed_ = false, backAborted_ = false;
  Json backStart_ = Json::object();
  std::string backContext_;
  int backX_ = 0, backY_ = 0;
  lv_obj_t *backHint_ = nullptr;
  void closeNavigation(bool resumeCamera = true);
  void cameraPage();
  int photoWidth() const { return cameraAngle_%180 ? screen_.height : screen_.width; }
  int photoHeight() const { return cameraAngle_%180 ? screen_.width : screen_.height; }
  lv_point_t photoPoint(lv_point_t point) const;
  void cameraMode(bool automatic, int angle);
  void startCamera();
  bool cameraCapturing_ = false;
  std::string cameraError_;
  int cameraAngle_ = 0, pendingCameraAngle_ = -1;
  bool cameraAutomatic_ = true, cameraSensorAvailable_ = false;
  void cropPage();
  void updateCrop();
  void cropView(const std::string &action);
  double cropZoom_ = 1, cropPanX_ = 0, cropPanY_ = 0;
  bool cropLoaded_ = false;
  double cropBaseScale_ = 1;
  int cropViewportW_ = 0, cropViewportH_ = 0;
  lv_obj_t *cropViewport_ = nullptr;
  std::map<std::string, lv_obj_t *> cropActions_;
  void resetCrop();
  Json cropSourceRect() const;
  int cropEdges(double x,double y) const;
  lv_obj_t *cropBitmap_ = nullptr;
  std::string cropImageKey_;
  void beginCrop(const Json &photo);
  void confirmCrop();
  void clearCrop(bool keepAlbum = false);
  Json cropPhoto_, cropResult_;
  std::thread cropWorker_;
  int cropGeneration_ = 0;
  bool cropBusy_ = false, cropSaveAlbum_ = false;
  double cropX_ = 0, cropY_ = 0, cropW_ = 0, cropH_ = 0;
  double cropPreviewW_ = 0, cropPreviewH_ = 0;
  lv_obj_t *cropSelection_ = nullptr;
  lv_obj_t *cropShades_[4] = {};

  void albumPage();
  void renderMessages(bool bottom, bool cached = false);
  void renderMessageRows(lv_obj_t *parent,const Json &messages,const std::string &source,
                         int width,bool readOnly,int lastAssistant,int64_t questionSeq);
  void richMessage(lv_obj_t *parent, const std::string &text, int width,
                   bool subdued = false, const std::string &context = "");
  void paintVisible();
  void richScrolled(lv_obj_t *object);
  bool syncingTableScroll_ = false;
  std::map<std::string, int> tableScrolls_;
  void renderLiveMessage();
  void refreshImages();
  bool editorSelectionOnly_ = false;
  EditorLayout editorLayout() const { return EditorLayout(screen_.width,screen_.height,editorSelectionOnly_); }
  EditorState editor_;
  Json editorLearning_ = Json::array(), editorExact_ = Json::array(), editorPinyinPending_;
  bool editorLayoutMenu_ = false, editorPunctuation_ = false;
  std::map<std::string, lv_obj_t *> editorLocalActions_;
  void keyboardSettingsPage();
  void editorUseLayout(int layout, bool cycle = false);
  void editorRememberLayout();
  EditorClipboard editorClipboard_;
  lv_obj_t *editorHandles_[2] = {nullptr,nullptr};
  lv_obj_t *editorMagnifier_ = nullptr, *editorMagnifierImage_ = nullptr, *editorMagnifierCaret_ = nullptr;
  lv_draw_buf_t *editorMagnifierBuffer_ = nullptr;
  bool editorClipboardOpen_=false, editorClipboardConfirm_=false, editorUndoMenu_=false;
  lv_point_t editorMenuAnchor_{};
  bool editorOcrMulti_=false, editorOcrActive_=false, editorOcrBusy_=false, editorOcrShown_=false;
  bool editorTopBlocked_=false;
  std::string editorOcrId_;
  uint64_t editorOcrCapture_=0, editorPolicySequence_=0,editorOcrInserted_=0;
  bool editorTextTouch_=false, editorTextMoved_=false, editorCursorDrag_=false;
  int editorHandle_=-1, editorTextScroll_=0, editorLastTapX_=0, editorLastTapY_=0, editorTouchX_=0,editorTouchY_=0;
  size_t editorDragAnchor_=0;
  uint64_t editorPressTime_=0, editorLastTap_=0;
  std::vector<Json> editorOcrPending_;
  void editorSyncText(bool text=false);
  void editorMagnify(bool show);
  void editorScroll(int y);
  size_t editorPositionAt(int x,int y) const;
  lv_point_t editorPoint(size_t index) const;
  void editorOcrStart();
  void editorOcrStop();
  void editorOcrResult(const Json &);
  void editorPolicy(bool active);
  lv_obj_t *editorRoot_ = nullptr, *editorText_ = nullptr;
  struct EditorKey { int x,y,w,h; std::string value; lv_obj_t *object; };
  std::vector<EditorKey> editorKeys_;
  EditorKey editorPress_{};
  bool editorChoosing_ = false, editorRebuild_ = false,editorFollowCursor_=true;
  uint64_t editorLatency_ = 0;
  std::vector<std::string> editorDeferred_;
  Json editorCandidates_ = Json::array();
  uint64_t editorRevision_ = 0, editorRequest_ = 0, editorInFlight_ = 0,
           editorDue_ = 0, editorSent_ = 0, editorShiftTime_ = 0, editorRepeat_ = 0;
  int editorHeld_ = -1, editorRepeats_ = 0;
  bool editorPointer_ = false, editorSlid_ = false, editorExpanded_ = false, editorMenu_ = false;
  int editorStartX_ = 0, editorStartY_ = 0;
  void editorBuild();
  void editorAction(const std::string &action);
  void editorFinish(bool confirmed);
  void editorOfficial();
  void editorChanged();
  void editorQuery(const std::string &operation = "query", int candidate = -1);
  void editorResult(const Json &message);
  void editorTick();
  void editorInterrupt();
  void editorTouch(int x, int y, bool pressed, bool cancelled);
  Json editorDiagnostic() const;
  void edit(const std::string &text, const std::string &placeholder,
            std::function<void(const std::string &)> callback,
            int maxLength = 4096, const char *inputType = "ZhCNPreferred",
            std::function<void()> cancelled = {}, bool multiline = false, bool secret = false, bool selectionOnly = false);
  bool canSend();
  void sendMessage(const std::string &retry = "");
  void attach(const Json &image);
  bool imageStoreBusy_ = false;
  std::thread imageStoreWorker_;
  std::function<void(bool)> imageStoreCompletion_;
  void storeImage(const Json &image, std::function<void(bool)> completion);
  void newChat(bool inherit = false);
  Json presetForm_ = Json::object();
  void presetsPage();
  void presetEditor();
  void presetPicker();
  void presetChoices(const std::string &field);
  void applyPreset(const Json &preset);
  bool emptyForPreset();
  std::string presetSummary(const Json &preset);

  bool contextBusy_ = false;
  int contextGeneration_ = 0;
  std::thread contextWorker_;
  std::atomic<bool> contextCancel_{false};
  void startContext(bool manual, const std::string &retry = "",
                    bool regenerate = false, bool archive = false,
                    Json frozenModel = Json::object());
  void cancelContext();
  void contextProgress(const std::string &text);
  void contextEvent(Event &event);
  void contextDetails();
  void openSessionInfo();
  void refreshSessionInfo();
  void sessionInfoPage();
  void updateContextUsage();
  Json contextUsageDiagnostic() const;
  lv_obj_t *contextUsageArc_ = nullptr, *contextUsageLabel_ = nullptr;
  bool sessionInfoEvent(const Event &);
  std::thread sessionInfoWorker_;
  bool sessionInfoBusy_ = false, sessionInfoPending_ = false;
  uint64_t sessionInfoGeneration_ = 0;
  std::string sessionInfoSession_;
  Json sessionInfoData_ = Json::object();
  std::map<std::string, lv_obj_t *> attachmentEntries_;
  void sendPrepared(Json model, Json input, const std::string &retryId,
                    const std::string &draft);
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
