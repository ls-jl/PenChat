#include "app.hpp"
#include <algorithm>
namespace penchat {
namespace {
std::string scanReason(const std::string &reason) {
  if (reason.find("temporary queue storage stalled") != std::string::npos ||
      reason.find("temporary queue I/O") != std::string::npos)
    return "扫描暂存写入过慢或失败，请稍后重试";
  if (reason.find("16 MiB") != std::string::npos)
    return "图片超过16MiB或保存失败，请缩短扫描后重试";
  if (reason == "sensor_sequence_gap" || reason == "sensor_sequence_reset")
    return "采集帧不连续";
  if (reason.find("capacity") != std::string::npos ||
      reason.find("rss") != std::string::npos)
    return "扫描缓存已满";
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
  bool eligible = bridge_.generation() > 0 && visible_ && !keyboard_ &&
                  !editor_.active && !imageViewer_ && !imageStoreBusy_ && !contextBusy_ && !editorOcrActive_ &&
                  !navigationOpen_ && !modal_ && !attachments_ && !pulling_ &&
                  (view_ == "chat" || active);
  scanner_.eligible(eligible, session_);
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
  scanner_.eligible(false, session_);
  if (scanner_.released())
    callback();
  else
    afterScanRelease_ = std::move(callback);
}
void App::scanPage() {
  lv_obj_set_scrollable(body_, false);
  scanLabel_ = label(body_, "请从左向右扫描，松开笔头后完成拼接", 10, 0, screen_.width - 120, 19,
                     theme::muted);
  lv_label_set_long_mode(scanLabel_, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(scanLabel_, font(19)->line_height);
  auto holder = box(body_, 10, 32, screen_.width - 330, screen_.height - 88, theme::surface);
  lv_obj_set_scrollable(holder, false);
  scanImage_ = imageWidget(holder);
  if (auto frame = scanner_.preview(scanId_)) {
    texture(scanImage_, frame);
    lv_image_set_scale(scanImage_,
                       std::min(256, std::min((screen_.width - 340) * 256 / frame->width,
                                              (screen_.height - 92) * 256 / frame->height)));
  }
  lv_obj_center(scanImage_);
  bool usable = scanResult_.value("usable", false);
  const bool partial = scanResult_.value("status", 1) != 1;
  scanUse_ = button(body_, !usable ? (scanResult_.empty() ? "等待扫描结果" : "无可用图像") : partial ? "使用部分图像" : "使用图片", screen_.width - 305, 32,
                    200, 44, [this] {
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
  button(body_, "取消", screen_.width - 305, 86, 200, 44, [this] { show("chat"); });
  if (!scanResult_.empty()) {
    auto c = scanResult_.value("counts", Json::object());
    std::string text;
    if (scanResult_.contains("error"))
      text = scanReason(scanResult_["error"].get<std::string>());
    else if (!usable)
      text = scanResult_.value("reason", std::string()).empty()
                 ? "没有可用图像，请重新扫描"
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
    if (c.value("recovered_gaps", uint64_t(0)))
      text = "传感器漏 " + std::to_string(c.value("recovered_gaps", uint64_t(0))) +
             " 帧（已续扫） · " + text;
    lv_label_set_text(scanLabel_, text.c_str());
  }
}
void App::scanEvent(const Event &event) {
  const auto &j = event.data;
  if (j.value("generation", uint64_t(0)) != scanner_.generation() ||
      j.value("session", std::string()) != session_)
    return;
  if (event.type == "scan_started") {
    if (view_ != "chat" || !visible_ || keyboard_ || imageViewer_ || modal_ || attachments_ ||
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
  if (event.type == "scan_preview" && scanImage_) {
    if (auto frame = scanner_.preview(scanId_)) {
      texture(scanImage_, frame);
      lv_image_set_scale(scanImage_,
                         std::min(256, std::min((screen_.width - 340) * 256 / frame->width,
                                                (screen_.height - 92) * 256 / frame->height)));
      lv_obj_center(scanImage_);
    }
  } else if (event.type == "scan_progress" && scanLabel_) {
    auto c = j.value("counts", Json::object());
    auto text = std::string(j.value("phase", std::string()) == "processing" ? "拼接中 · 选中 " : "正向扫描 · 选中 ") + std::to_string(c.value("selected", uint64_t(0))) +
                " 帧 · 已拼接 " +
                std::to_string(c.value("committed", uint64_t(0))) + " 帧 · 待处理 " +
                std::to_string(c.value("pending", uint64_t(0))) + " 帧";
    lv_label_set_text(scanLabel_, text.c_str());
  } else if (event.type == "scan_processing" && scanLabel_)
    lv_label_set_text(scanLabel_, "采集已结束，正在完成剩余拼接…");
  else if (event.type == "scan_finished") {
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
