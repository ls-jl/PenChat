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
  // A transient global overlay must not disarm and destroy the result that
  // this page (or its original-image reader) still owns. Real backgrounding
  // continues through suspend() and its normal cancel/join/release path.
  if(active&&pageVisible_&&(!windowFocused_||!inputFocused_))return;
  bool eligible = bridge_.generation() > 0 && visible_ && inputFocused_ && !keyboard_ &&
                  !editor_.active && (!imageViewer_ || viewerScanResult_) && !imageStoreBusy_ && !contextBusy_ && !replyBusy_ && !editorOcrActive_ && editorOcrStopId_.empty() &&
                  !navigationOpen_ && !modal_ && !attachments_ && !pulling_ &&
                  (view_ == "chat" || active);
  const bool ocrKeys=scanner_.nativeOcrKeys()&&editor_.active&&visible_&&inputFocused_&&!keyboard_;
  scanner_.eligible(eligible, session_, ocrKeys);
  if (scanHint_) {
    auto state = scanner_.state().value("type", std::string());
    const char *text = !eligible          ? ""
                       : state == "ready" ? "正向扫描\n就绪"
                       : (state == "error" || state == "fatal") ? "扫描\n不可用"
                       : eligible ? "扫描\n准备中"
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
  lv_obj_set_scrollable(body_, false);
  const auto workspace=layout().scanWorkspace();
  scanLabel_ = label(body_, "请从左向右扫描，松开笔头后完成拼接", workspace.caption.x, workspace.caption.y, workspace.caption.width, 19,
                     theme::muted);
  lv_label_set_long_mode(scanLabel_, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(scanLabel_, font(19)->line_height);
  const bool portrait=layout().portrait();
  auto holder = box(body_,workspace.preview.x,workspace.preview.y,workspace.preview.width,workspace.preview.height,theme::surface);
  scanHolder_=holder;
  scanWarning_=label(body_,"",10,28,screen_.width-120,17,theme::danger);
  lv_obj_set_hidden(scanWarning_,true);
  lv_obj_set_clickable(holder,true);
  lv_obj_add_event_cb(holder, [](lv_event_t *e) {
    auto self=static_cast<App *>(lv_event_get_user_data(e));
    self->events_.push("image_scan_open",{{"scan",self->scanId_},{"generation",self->scanGeneration_},{"session",self->scanSession_}});
  }, LV_EVENT_CLICKED, this);
  lv_obj_set_scrollable(holder, false);
  scanImage_ = imageWidget(holder);
  if (auto frame = scanner_.preview(scanId_)) {
    texture(scanImage_, frame);
    lv_image_set_scale(scanImage_,
                       std::min(256, std::min((lv_obj_get_width(holder)-8) * 256 / frame->width,
                                              (lv_obj_get_height(holder)-8) * 256 / frame->height)));
  }
  lv_obj_center(scanImage_);
  bool usable = scanResult_.value("usable", false);
  const bool partial = scanResult_.value("status", 1) != 1;
  scanUse_ = button(body_, !usable ? (scanResult_.empty() ? "等待扫描结果" : "无可用图像") : partial ? "使用部分图像" : "使用图片", workspace.use.x, workspace.use.y,
                    workspace.use.width, workspace.use.height, [this] {
                      if (scanConfirming_ || scanConfirmed_ == scanId_ ||
                          !scanResult_.value("usable", false))
                        return;
                      scanConfirming_ = true;
                      lv_obj_set_clickable(scanUse_, false);
                      lv_label_set_text(scanLabel_, "正在保存扫描图片…");
                      scanner_.confirm(scanId_);
                    });
  lv_obj_set_clickable(scanUse_, usable && !scanConfirming_);
  if (usable) primary(scanUse_);
  else {
    lv_obj_set_style_bg_color(scanUse_, lv_color_hex(theme::surface), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(scanUse_, 0), lv_color_hex(theme::muted), 0);
  }
  button(body_, "取消", portrait?16:screen_.width-305, portrait?screen_.height-58:86, portrait?screen_.width-32:200, 44, [this] { show("chat"); });
  if (!scanResult_.empty()) {
    auto c = scanResult_.value("counts", Json::object());
    std::string text;
    if (scanResult_.contains("error"))
      text = scanReason(scanResult_["error"].get<std::string>());
    else if (!usable)
      text = scanResult_.value("reason", std::string()).empty()
                 ? "没有可用图像，按下笔头即可重扫"
                 : "扫描失败：" +
                       scanReason(scanResult_.value("reason", std::string()));
    else if (partial)
      text = "扫描未完整完成：" +
             scanReason(scanResult_.value("reason", std::string())) +
             "；未定位帧 " + std::to_string(c.value("unlocated", uint64_t(0))) +
             "，请确认。";
    else
      text = "拼接完成：选中 " +
             std::to_string(c.value("selected", uint64_t(0))) + " 帧" +
             (c.value("warnings", 0u) ? "，存在光照估计回退，请核对"
                                      : "，请确认使用");
    if(partial) {
      lv_obj_set_width(scanLabel_,portrait?screen_.width-88:screen_.width-330);
      lv_label_set_long_mode(scanLabel_,LV_LABEL_LONG_MODE_WRAP);
      lv_obj_set_height(scanLabel_,LV_SIZE_CONTENT);
    }
    lv_label_set_text(scanLabel_, text.c_str());
    updateScanWarning(c);
  }
}
void App::updateScanWarning(const Json &counts) {
  if(!scanWarning_ || !scanHolder_)return;
  const auto lost=counts.value("dropped_raw",uint64_t(0));
  const auto workspace=layout().scanWorkspace();
  lv_obj_update_layout(scanLabel_);
  const int top=std::max(workspace.captionFloor,int(lv_obj_get_y(scanLabel_)+lv_obj_get_height(scanLabel_)+8));
  lv_obj_set_pos(scanWarning_,workspace.warning.x,top);
  lv_obj_set_width(scanWarning_,workspace.warning.width);
  lv_label_set_long_mode(scanWarning_,LV_LABEL_LONG_MODE_WRAP);
  lv_label_set_text(scanWarning_,("本次漏采"+std::to_string(lost)+"帧，请放大检查是否有缺字、重影或错位。").c_str());
  lv_obj_set_hidden(scanWarning_,!lost);lv_obj_update_layout(scanWarning_);
  const int extra=lost?lv_obj_get_height(scanWarning_)+8:0;
  const int holderTop=top+extra+4;
  lv_obj_set_y(scanHolder_,holderTop);
  lv_obj_set_height(scanHolder_,std::max(24,workspace.previewBottom-holderTop));
  if(auto frame=scanner_.preview(scanId_)) {
    lv_image_set_scale(scanImage_,std::min(256,std::min((lv_obj_get_width(scanHolder_)-8)*256/frame->width,
      (lv_obj_get_height(scanHolder_)-8)*256/frame->height)));lv_obj_center(scanImage_);
  }
}
void App::viewScanResult() {
  if(view_!="scan" || imageViewer_ || scanConfirming_ || !scanResult_.value("usable",false) ||
     scanResult_.value("type",std::string())!="finished" || !visible_)return;
  openImageViewer("",true);scanViewRequest_=scanner_.prepareReview(scanId_);
}
void App::scanEvent(const Event &event) {
  if(event.type=="scan_media_release") {
    display_.mediaHandoff(true);scanner_.mediaReleased(event.data.at("token").get<uint64_t>());return;
  }
  if(event.type=="scan_media_resume") {display_.mediaHandoff(false);return;}
  const auto &j = event.data;
  if (j.value("generation", uint64_t(0)) != scanner_.generation() ||
      j.value("session", std::string()) != session_)
    return;
  if (event.type == "scan_ocr_key") {
    if (editor_.active && editorOcrActive_ && !editorOcrPreparing_ && visible_ && !keyboard_)
      bridge_.send("ocr.pen", {{"requestId",editorOcrId_},{"session",editor_.id},
        {"down",j.value("down",false)},{"sequence",j.value("sequence",uint64_t(0))}});
    return;
  }
  if (event.type == "scan_started") {
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
    show("scan");
    return;
  }
  if (event.type == "scan_error") {
    if (view_ == "scan") {
      if(viewerScanResult_)closeImageViewer(false);
      scanResult_ = {{"usable", false},
                     {"error", j.value("error", std::string("扫描失败"))}};
      scanConfirming_ = false;
      build();
    }
    return;
  }
  if (view_ != "scan" || !scanId_ ||
      j.value("scanId", uint64_t(0)) != scanId_ ||
      scanGeneration_ != scanner_.generation() || scanSession_ != session_)
    return;
  if((event.type=="scan_review_ready" || event.type=="scan_review_error")) {
    if(!imageViewer_ || !viewerScanResult_ || !scanViewRequest_ ||
       j.value("requestId",uint64_t(0))!=scanViewRequest_)return;
    if(event.type=="scan_review_error") {
      viewerFailed_=true;lv_label_set_text(viewerStatus_,"原图准备失败，请关闭后重试");return;
    }
    viewerKey_=images_.request(j.at("image").at("path").get<std::string>(),32768,32768);
    lv_label_set_text(viewerStatus_,"正在加载原图…");refreshImageViewer();return;
  }
  if (event.type == "scan_preview" && scanImage_) {
    if (auto frame = scanner_.preview(scanId_)) {
      texture(scanImage_, frame);
      lv_image_set_scale(scanImage_,
                         std::min(256, std::min((lv_obj_get_width(lv_obj_get_parent(scanImage_))-8) * 256 / frame->width,
                                                (lv_obj_get_height(lv_obj_get_parent(scanImage_))-8) * 256 / frame->height)));
      lv_obj_center(scanImage_);
    }
  } else if (event.type == "scan_progress" && scanLabel_) {
    auto c = j.value("counts", Json::object());
    auto text = std::string(j.value("phase", std::string()) == "processing" ? "拼接中 · 选中 " : "正向扫描 · 选中 ") + std::to_string(c.value("selected", uint64_t(0))) +
                " 帧 · 已拼接 " +
                std::to_string(c.value("committed", uint64_t(0))) + " 帧 · 待处理 " +
                std::to_string(c.value("pending", uint64_t(0))) + " 帧";
    lv_label_set_text(scanLabel_, text.c_str());
    updateScanWarning(c);
  } else if (event.type == "scan_processing" && scanLabel_)
    lv_label_set_text(scanLabel_, "采集已结束，正在完成剩余拼接…");
  else if (event.type == "scan_finished") {
    diagnosticRecord("scanner", "finished", j.value("counts",Json::object()));
    scanResult_ = j;
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
        if (!ok)
          return;
        scanConfirmed_ = scan;
        scanner_.discard(scan);
        if (session != session_)
          return;
        show("chat");
        chatFollowing_ = true;
        messageOffset_ = 0;
        build();
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
