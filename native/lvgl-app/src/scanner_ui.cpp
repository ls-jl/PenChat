#include "app.hpp"
#include <algorithm>
namespace penchat {
namespace {
std::string scanReason(const std::string &reason) {
  if (reason.find("16 MiB") != std::string::npos)
    return "图片超过16MiB或保存失败，请缩短扫描后重试";
  if (reason == "sensor_sequence_gap" || reason == "sensor_sequence_reset")
    return "采集帧不连续";
  if (reason == "geometry_unstable")
    return "角度或对齐异常，请贴纸平稳扫描";
  if (reason == "system_memory_low")
    return "系统可用内存不足，已保留本次结果";
  if (reason == "system_memory_unavailable")
    return "无法读取系统内存状态，已停止扫描";
  if (reason == "canvas_allocation_failed" || reason == "render_memory_allocation_failed")
    return "拼接画布内存申请失败";
  if (reason == "memory_allocation_failed" || reason == "preprocessing_allocation_or_conversion_failure")
    return "扫描内存申请或图像处理失败";
  if (reason.find("calibration") != std::string::npos)
    return "光照校准资源不匹配";
  if (reason.find("rknn") != std::string::npos)
    return "NPU拼接失败，请重新扫描";
  if (reason.find("engine unavailable") != std::string::npos)
    return "拼接引擎未就绪，请稍后重试";
  return reason;
}
} // namespace
void App::syncScanner() {
  bool active = view_ == "scan" && scanId_ && scanSession_ == session_;
  // Overlay/lease loss gates triggers without disarming the healthy worker,
  // changing its generation or releasing a review result and its readers.
  if(!pageVisible_||!pageAttached_||keyboard_) {scanner_.eligible(false,session_);return;}
  const bool covered=!inputFocused_;
  scanner_.pauseTriggers(covered,penObserverReady_);
  if(covered) {
    if(scanner_.listening()&&!penObserverReady_&&penObserverRequest_.empty()) {
      penObserverRequest_=uid("pen-observer");
      bridge_.send("pen.observe",{{"requestId",penObserverRequest_},{"enabled",true}});
    }
    return;
  }
  if(active&&scanConfirming_) {
    // The DB worker still reads the private PNGs. Gate new presses, retaining
    // their owner until the save acknowledgement; a progress modal is no
    // reason to tear down/rearm capture or delete the files being copied.
    scanner_.eligible(true,session_,false,false);
    return;
  }
  bool eligible = bridge_.generation() > 0 && visible_ && inputFocused_ && !keyboard_ &&
                  !editor_.active && !imageStoreBusy_ && !contextBusy_ && !replyBusy_ && !editorOcrActive_ && editorOcrStopId_.empty() &&
                  !navigationOpen_ && !modal_ && !attachments_ && !pulling_ &&
                  (view_ == "chat" || active);
  const bool ocrKeys=scanner_.nativeOcrKeys()&&editor_.active&&visible_&&inputFocused_&&!keyboard_;
  // A local image overlay does not need the system camera. Keep the current
  // capture lease and gate pen presses, avoiding an ISP close/reopen on return.
  scanner_.eligible(eligible, session_, ocrKeys, !imageViewer_);
  if (scanHint_) {
    auto state = scanner_.state().value("type", std::string());
    const bool portrait = layout().portrait();
    const char *text = !eligible          ? ""
                       : state == "ready" ? (portrait?"扫描就绪":"扫描\n就绪")
                       : (state == "error" || state == "fatal") ? (portrait?"扫描不可用":"扫描\n不可用")
                       : eligible ? (portrait?"扫描准备中":"扫描\n准备中")
                                  : "";
    if (std::string(lv_label_get_text(scanHint_)) != text)
      lv_label_set_text(scanHint_, text);
  }
}
void App::whenScannerReleased(std::function<void()> callback) {
  scanner_.eligible(false, session_, scanner_.nativeOcrKeys()&&editor_.active&&visible_&&inputFocused_&&!keyboard_);
  if (scanner_.released())
    callback();
  else
    afterScanRelease_ = std::move(callback);
}
void App::scanPage() {
  const auto workspace=layout().scanWorkspace();
  lv_obj_set_x(body_,0);
  lv_obj_set_size(body_,screen_.width,workspace.preview.y);
  lv_obj_set_scrollable(body_,true);
  auto holder = box(screenObj_,workspace.preview.x,workspace.preview.y,workspace.preview.width,workspace.preview.height);
  lv_obj_set_style_bg_opa(holder,LV_OPA_TRANSP,0);
  scanHolder_=holder;
  scanWarning_=label(body_,"",workspace.warning.x,workspace.warning.y,workspace.warning.width,
    uiMetrics_.fontSize(UiFont::FormHint),theme::palette().danger);
  lv_label_set_long_mode(scanWarning_,LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_hidden(scanWarning_,true);
  lv_obj_set_clickable(holder,true);
  lv_obj_add_event_cb(holder, [](lv_event_t *e) {
    auto self=static_cast<App *>(lv_event_get_user_data(e));
    self->events_.push("image_scan_open",{{"scan",self->scanId_},{"generation",self->scanGeneration_},{"session",self->scanSession_}});
  }, LV_EVENT_CLICKED, this);
  lv_obj_set_scrollable(holder, false);
  scanImage_ = imageWidget(holder);
  bool usable = scanResult_.value("usable", false);
  const bool partial = scanResult_.value("status", 1) != 1;
  scanUse_ = button(screenObj_, scanConfirming_ ? "保存中…" : !usable ? (scanResult_.empty() ? "等待扫描结果" : "无可用图像") : partial ? "使用部分图像" : "使用图片", workspace.use.x, workspace.use.y,
                    workspace.use.width, workspace.use.height, [this] {
                      if (scanConfirming_ || scanCancelPending_ || scanConfirmed_ == scanId_ ||
                          !scanResult_.value("usable", false))
                        return;
                      scanConfirming_ = true;
                      scanConfirmClickedAt_=ticks();
                      lv_obj_set_clickable(scanUse_, false);
                      lv_obj_set_clickable(scanCancel_, false);
                      lv_label_set_text(lv_obj_get_child(scanUse_,0), "保存中…");
                      scanner_.confirm(scanId_);
                    });
  lv_obj_set_clickable(scanUse_, usable && !scanConfirming_ && !scanCancelPending_);
  if (usable) primary(scanUse_);
  else {
    lv_obj_set_style_bg_color(scanUse_, lv_color_hex(theme::palette().surface), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(scanUse_, 0), lv_color_hex(theme::palette().muted), 0);
  }
  scanCancel_=button(screenObj_, "取消", workspace.cancel.x, workspace.cancel.y, workspace.cancel.width, workspace.cancel.height, [this] { cancelScanReview(); });
  lv_obj_set_clickable(scanCancel_,!scanConfirming_ && !scanCancelPending_);
  updateScanWarning(scanResult_.value("counts",Json::object()));
  refreshScanPreview();
}
void App::cancelScanReview() {
  if (scanConfirming_ || scanCancelPending_) return;
  scanCancelPending_ = true;
  if (scanUse_) lv_obj_set_clickable(scanUse_,false);
  if (scanCancel_) lv_obj_set_clickable(scanCancel_,false);
  events_.push("cancel_scan_review",{{"scan",scanId_},{"generation",scanGeneration_},{"session",scanSession_}});
}
void App::refreshScanPreview() {
  if (!scanImage_) return;
  auto frame=scanner_.preview(scanId_);
  if (!frame) return;
  lv_obj_update_layout(scanHolder_);
  const int width=lv_obj_get_width(scanHolder_),height=lv_obj_get_height(scanHolder_);
  const auto scale=std::min(width*LV_SCALE_NONE/frame->width,height*LV_SCALE_NONE/frame->height);
  lv_obj_set_hidden(scanImage_,scale<1);
  if (scale<1) return;
  texture(scanImage_,frame);
  lv_image_set_scale(scanImage_,scale);
  lv_obj_center(scanImage_);
}
void App::updateScanWarning(const Json &counts) {
  if(!scanWarning_ || !scanHolder_)return;
  const auto lost=counts.value("dropped_raw",uint64_t(0));
  const auto reason=scanResult_.value("reason",std::string());
  std::string warning;
  if (scanResult_.contains("error"))
    warning=scanReason(scanResult_.at("error").get<std::string>());
  else if (!scanResult_.empty() && !scanResult_.value("usable",false))
    warning=reason.empty()?"没有可用图像，按下笔头即可重扫":"扫描失败："+scanReason(reason);
  else if (scanResult_.value("status",1)!=1)
    warning="扫描未完整完成："+scanReason(reason)+"；未定位帧 "+
      std::to_string(counts.value("unlocated",uint64_t(0)))+"，请确认。";
  if (lost) {
    if (!warning.empty()) warning+='\n';
    warning+="本次漏采"+std::to_string(lost)+"帧，请放大检查是否有缺字、重影或错位。";
  }
  if (warning==lv_label_get_text(scanWarning_)) return;
  lv_label_set_text(scanWarning_,warning.c_str());
  lv_obj_set_hidden(scanWarning_,warning.empty());
  lv_obj_update_layout(scanWarning_);
  const auto workspace=layout().scanWorkspace();
  const int minimumPreview=std::min(workspace.preview.height,uiMetrics_.dp(72));
  const int warningBottom=workspace.warning.y+lv_obj_get_height(scanWarning_)+uiMetrics_.dp(8);
  const int holderTop=warning.empty()?workspace.preview.y:
    std::min(workspace.previewBottom-minimumPreview,warningBottom);
  lv_obj_set_height(body_,holderTop);
  const auto area=layout().scanPreview(holderTop,workspace.previewBottom);
  lv_obj_set_pos(scanHolder_,area.x,area.y);
  lv_obj_set_size(scanHolder_,area.width,area.height);
  refreshScanPreview();
}
void App::viewScanResult() {
  if(view_!="scan" || imageViewer_ || scanConfirming_ || scanCancelPending_ || !scanResult_.value("usable",false) ||
     scanResult_.value("type",std::string())!="finished" || !visible_)return;
  scanViewClickedAt_=ticks();scanViewLoadedAt_=0;
  openImageViewer(scanOriginalPath_,true);scanViewRequest_=scanner_.prepareReview(scanId_);
}
void App::scanEvent(const Event &event) {
  const auto &j = event.data;
  if(j.value("generation",uint64_t(0))!=scanner_.generation())return;
  const bool foreground=pageVisible_&&pageAttached_&&windowFocused_&&inputFocused_;
  const bool ocrSafe=!editor_.active||!scanner_.nativeOcrKeys()||(editorOcrActive_&&!editorOcrPreparing_);
  if((event.type=="scan_overlay_pause"&&!j.value("overlay_paused",false)&&foreground&&ocrSafe)||
      (event.type=="scan_released"&&!scanner_.listening())) {
    if(penObserverReady_||!penObserverRequest_.empty()) {
      penObserverReady_=false;penObserverRequest_=uid("pen-observer-off");
      bridge_.send("pen.observe",{{"requestId",penObserverRequest_},{"enabled",false}});
    }
  }
  if(j.value("session",std::string())!=session_)return;
  if (event.type == "scan_ocr_key") {
    if (editor_.active && editorOcrActive_ && !editorOcrPreparing_ && visible_ && !keyboard_)
      bridge_.send("ocr.pen", {{"requestId",editorOcrId_},{"session",editor_.id},
        {"down",j.value("down",false)},{"sequence",j.value("sequence",uint64_t(0))}});
    return;
  }
  if (event.type == "scan_started") {
    if (imageViewer_ && !viewerScanResult_) {
      // A press may already have been queued when the overlay closed the gate.
      // Discard that scan without releasing/reopening the hardware lease.
      scanner_.discard(j.value("scanId", uint64_t(0)));
      return;
    }
    if(editor_.active) {
      // Pen presses during OCR handoff cannot open a scan page or release the
      // key lease before the system OCR route has been acknowledged.
      if(!editorOcrPreparing_)scanner_.eligible(false,session_);
      return;
    }
    diagnosticRecord("scanner", "started");
    const bool retryEmpty = view_ == "scan" &&
        scanResult_.value("type", std::string()) == "finished" &&
        !scanResult_.value("usable", false) && !scanConfirming_;
    if ((view_ != "chat" && !retryEmpty) || !visible_ || keyboard_ || imageViewer_ || modal_ || attachments_ ||
        navigationOpen_) {
      scanner_.eligible(false, session_);
      return;
    }
    scanId_ = j.value("scanId", uint64_t(0));
    scanGeneration_ = scanner_.generation();
    scanConfirmed_ = 0;
    scanSession_ = session_;
    scanResult_ = Json::object();
    scanConfirming_ = false;
    scanOriginalPath_.clear();scanOriginalKey_.clear();scanFinishedAt_=scanPreparedAt_=scanDecodedAt_=scanViewClickedAt_=scanViewLoadedAt_=0;
    scanConfirmClickedAt_=scanStoredAt_=scanExportUs_=0;
    show("scan");
    return;
  }
  if (event.type == "scan_error") {
    if (view_ == "scan") {
      if(viewerScanResult_)closeImageViewer(false);
      if (!scanConfirming_) scanResult_ = {{"usable",false}};
      scanResult_["error"] = j.value("error",std::string("扫描失败"));
      scanConfirming_ = false;
      build();
    }
    return;
  }
  if (view_ != "scan" || !scanId_ ||
      j.value("scanId", uint64_t(0)) != scanId_ ||
      scanGeneration_ != scanner_.generation() || scanSession_ != session_)
    return;
  if(event.type=="scan_prepared") {
    scanOriginalPath_=j.at("image").at("path").get<std::string>();
    scanPreparedAt_=ticks();scanExportUs_=j.value("prepare_us",uint64_t(0));
    scanResult_["preparation"]=j.value("timing",Json::object());
    if(pageVisible_&&pageAttached_&&!scanConfirming_)
      scanOriginalKey_=images_.request(scanOriginalPath_,32768,32768);
    return;
  }
  if((event.type=="scan_review_ready" || event.type=="scan_review_error")) {
    if(!imageViewer_ || !viewerScanResult_ || !scanViewRequest_ ||
       j.value("requestId",uint64_t(0))!=scanViewRequest_)return;
    if(event.type=="scan_review_error") {
      viewerFailed_=true;
      const auto error=redactApiError(j.value("error",std::string("原图准备失败")),Json::object());
      const int code=error.find("cancel")!=std::string::npos?1:
        error.find("memory")!=std::string::npos||error.find("alloc")!=std::string::npos?2:
        error.find("open")!=std::string::npos||error.find("write")!=std::string::npos?3:
        error.find("limit")!=std::string::npos||error.find("size")!=std::string::npos?4:5;
      diagnosticRecord("scanner","review_error",{{"stage",1},{"scan_id",scanId_},{"request_id",scanViewRequest_},{"code",code}});
      lv_label_set_text(viewerStatus_,("原图准备失败："+error+"\n请关闭后重试").c_str());return;
    }
    const auto path=j.at("image").at("path").get<std::string>();
    images_.retry(path);
    viewerKey_=images_.request(path,32768,32768);
    lv_label_set_text(viewerStatus_,"正在加载原图…");refreshImageViewer();return;
  }
  if (event.type == "scan_preview" && scanImage_) {
    refreshScanPreview();
  } else if (event.type == "scan_progress") {
    updateScanWarning(j.value("counts",Json::object()));
  } else if (event.type == "scan_finished") {
    diagnosticRecord("scanner", "finished", j.value("counts",Json::object()));
    scanResult_ = j;
    scanFinishedAt_=ticks();
    build();
  } else if (event.type == "scan_exported" && scanConfirming_ &&
             scanConfirmed_ != scanId_) {
    try {
      auto image = j.at("image");
      auto model = currentModel();
      bindPreferences(model);
      const auto scan = scanId_;
      const auto session = session_;
      storeImage(image, [this, scan, session](bool ok) {
        scanConfirming_ = false;
        if (!ok) {
          if (view_=="scan" && scanId_==scan && session==session_) build();
          return;
        }
        scanStoredAt_=ticks();
        scanConfirmed_ = scan;
        if (session != session_) {scanner_.discard(scan);return;}
        chatFollowing_ = true;
        messageOffset_ = 0;
        show("chat");
        if (db_.settings()
                .value("penchat_settings", Json::object())
                .value("autoSendCapturedContent", false) &&
            !generating_ && messageEdit_.empty())
          sendMessage();
      });
    } catch (const std::exception &e) {
      scanConfirming_ = false;
      scanResult_["error"] = e.what();
      build();
    }
  }
}
} // namespace penchat
