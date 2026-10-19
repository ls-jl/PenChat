#include "app.hpp"
#include "stb_image.h"
#include "stb_image_write.h"
#include "util.h"
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

namespace penchat {
void App::clearCrop(bool keepAlbum) {
  if (cropWorker_.joinable())
    cropWorker_.join();
  if (!keepAlbum && cropResult_.contains("album_path"))
    unlink(cropResult_["album_path"].get<std::string>().c_str());
  // This directory contains only this application's unconfirmed camera files.
  auto directory = join(dataDir_, "camera-tmp");
  if (auto dir = opendir(directory.c_str())) {
    while (auto entry = readdir(dir)) {
      std::string name = entry->d_name;
      if (name != "." && name != "..")
        unlink(join(directory, name).c_str());
    }
    closedir(dir);
  }
  ++cropGeneration_;
  cropPhoto_ = cropResult_ = Json::object();
  cropBusy_ = false;
  cropSelection_ = cropBitmap_ = nullptr;
}
void App::beginCrop(const Json &photo) {
  camera_.stop();
  cameraCapturing_ = false;
  pendingCameraAngle_ = -1;
  cameraAngle_ = photo.value("orientation", 0);
  cropPhoto_ = photo;
  cropResult_ = Json::object();
  cropSaveAlbum_ = db_.settings()
                       .value("penchat_settings", Json::object())
                       .value("saveCameraToAlbum", false);
  const bool portrait = cameraAngle_ % 180;
  cropViewportW_ = portrait ? photoWidth() - 88 : screen_.width - 330;
  cropViewportH_ = portrait ? photoHeight() - 56 - 260 : screen_.height - 88;
  const double scale =
      std::min(double(cropViewportW_) / photo.at("width").get<int>(),
               double(cropViewportH_) / photo.at("height").get<int>());
  cropPreviewW_ = std::max(1, int(photo.at("width").get<int>() * scale));
  cropPreviewH_ = std::max(1, int(photo.at("height").get<int>() * scale));
  cropX_ = cropY_ = 0;
  cropW_ = cropPreviewW_;
  cropH_ = cropPreviewH_;
  cropZoom_ = 1;
  cropPanMode_ = false;
  cropPanX_ = (cropViewportW_ - cropPreviewW_) / 2.;
  cropPanY_ = (cropViewportH_ - cropPreviewH_) / 2.;
  cropImageKey_ =
      images_.request(photo.at("path").get<std::string>(), 1600, 1600);
  show("crop");
}
void App::updateCrop() {
  if (!cropSelection_)
    return;
  const double iw = cropPreviewW_ * cropZoom_, ih = cropPreviewH_ * cropZoom_;
  cropPanX_ =
      std::max(44. - iw, std::min(double(cropViewportW_ - 44), cropPanX_));
  cropPanY_ =
      std::max(44. - ih, std::min(double(cropViewportH_ - 44), cropPanY_));
  if (cropBitmap_)
    if (auto frame = images_.get(cropImageKey_)) {
      lv_image_set_pivot(cropBitmap_, 0, 0);
      lv_image_set_inner_align(cropBitmap_, LV_IMAGE_ALIGN_TOP_LEFT);
      lv_image_set_scale(cropBitmap_, int(iw * 256 / frame->width));
      lv_obj_set_size(cropBitmap_, int(std::round(iw)), int(std::round(ih)));
      lv_obj_set_pos(cropBitmap_, int(std::round(cropPanX_)),
                     int(std::round(cropPanY_)));
    }
  const int x = int(std::round(cropPanX_ + cropX_ * cropZoom_)),
            y = int(std::round(cropPanY_ + cropY_ * cropZoom_));
  const int w = int(std::round(cropW_ * cropZoom_)),
            h = int(std::round(cropH_ * cropZoom_));
  lv_obj_set_pos(cropSelection_, x, y);
  lv_obj_set_size(cropSelection_, w, h);
  const int left = std::max(0, std::min(cropViewportW_, x)),
            right = std::max(0, std::min(cropViewportW_, x + w));
  const int top = std::max(0, std::min(cropViewportH_, y)),
            bottom = std::max(0, std::min(cropViewportH_, y + h));
  const int rects[4][4] = {{0, 0, cropViewportW_, top},
                           {0, bottom, cropViewportW_, cropViewportH_ - bottom},
                           {0, top, left, bottom - top},
                           {right, top, cropViewportW_ - right, bottom - top}};
  for (int i = 0; i < 4; ++i) {
    lv_obj_set_pos(cropShades_[i], rects[i][0], rects[i][1]);
    lv_obj_set_size(cropShades_[i], rects[i][2], rects[i][3]);
  }
}
void App::cropView(const std::string &action) {
  if (action == "pan")
    cropPanMode_ = !cropPanMode_;
  else {
    if (action == "plus")
      cropZoom_ = std::min(16., cropZoom_ * 2);
    else if (action == "minus")
      cropZoom_ = std::max(1., cropZoom_ / 2);
    else if (action == "fit")
      cropZoom_ = 1;
    else if (action == "selection")
      cropZoom_ = std::max(
          1., std::min(16., .85 * std::min(double(cropViewportW_) / cropW_,
                                           double(cropViewportH_) / cropH_)));
    cropPanMode_ = cropZoom_ > 1;
    cropPanX_ = cropViewportW_ / 2. - (cropX_ + cropW_ / 2.) * cropZoom_;
    cropPanY_ = cropViewportH_ / 2. - (cropY_ + cropH_ / 2.) * cropZoom_;
    if (action == "fit") {
      cropPanX_ = (cropViewportW_ - cropPreviewW_) / 2.;
      cropPanY_ = (cropViewportH_ - cropPreviewH_) / 2.;
    }
  }
  build();
}
void App::cropPage() {
  lv_obj_set_scrollable(body_, false);
  lv_obj_update_layout(body_);
  const bool portrait = cameraAngle_ % 180;
  const int bodyH = lv_obj_get_height(body_);
  auto viewport = box(body_, portrait ? 44 : 12, portrait ? 12 : 44,
                      cropViewportW_, cropViewportH_, theme::sidebar);
  lv_obj_set_scrollable(viewport, false);
  cropBitmap_ = imageWidget(viewport);
  lv_obj_set_clickable(cropBitmap_, false);
  auto loading =
      label(viewport, "正在载入…", 8, 8, cropViewportW_ - 16, 17, theme::muted);
  lv_obj_set_clickable(loading, false);
  placeholders_[cropBitmap_] = loading;
  imageRequests_.push_back({cropBitmap_, cropImageKey_});
  auto overlay = box(viewport, 0, 0, cropViewportW_, cropViewportH_);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollable(overlay, false);
  lv_obj_set_clickable(overlay, true);
  lv_obj_set_press_lock(overlay, true);
  lv_obj_set_scroll_chain_hor(overlay, false);
  lv_obj_set_scroll_chain_ver(overlay, false);
  for (auto &shade : cropShades_) {
    shade = box(overlay, 0, 0, 0, 0, 0x000000);
    lv_obj_set_style_bg_opa(shade, LV_OPA_60, 0);
    lv_obj_set_clickable(shade, false);
    lv_obj_set_scrollable(shade, false);
  }
  cropSelection_ = box(overlay, 0, 0, 1, 1);
  lv_obj_set_style_bg_opa(cropSelection_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cropSelection_, 2, 0);
  lv_obj_set_style_border_color(cropSelection_, lv_color_hex(theme::text), 0);
  lv_obj_set_scrollable(cropSelection_, false);
  lv_obj_set_clickable(cropSelection_, false);
  for (auto align :
       {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_LEFT,
        LV_ALIGN_BOTTOM_RIGHT, LV_ALIGN_TOP_MID, LV_ALIGN_BOTTOM_MID,
        LV_ALIGN_LEFT_MID, LV_ALIGN_RIGHT_MID}) {
    bool edgeH = align == LV_ALIGN_TOP_MID || align == LV_ALIGN_BOTTOM_MID,
         edgeV = align == LV_ALIGN_LEFT_MID || align == LV_ALIGN_RIGHT_MID;
    auto handle = box(cropSelection_, 0, 0,
                      edgeH   ? 28
                      : edgeV ? 4
                              : 12,
                      edgeV   ? 28
                      : edgeH ? 4
                              : 12,
                      theme::text);
    lv_obj_align(handle, align, 0, 0);
    lv_obj_set_clickable(handle, false);
    lv_obj_set_scrollable(handle, false);
  }
  updateCrop();
  struct Drag {
    App *app;
    double x = 0, y = 0, panX = 0, panY = 0;
    int left = 0, top = 0, w = 0, h = 0, edges = 0;
    bool active = false;
  };
  auto drag = new Drag{this};
  lv_obj_add_event_cb(
      overlay,
      [](lv_event_t *event) {
        auto d = static_cast<Drag *>(lv_event_get_user_data(event));
        auto a = d->app;
        auto code = lv_event_get_code(event);
        if (code == LV_EVENT_DELETE) {
          delete d;
          return;
        }
        if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
          d->active = false;
          return;
        }
        if (a->cropBusy_ ||
            (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) ||
            !lv_indev_active())
          return;
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        point = a->photoPoint(point);
        lv_area_t bounds;
        lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_target(event)),
                          &bounds);
        double px = point.x - bounds.x1, py = point.y - bounds.y1;
        const double zoom = a->cropZoom_;
        if (code == LV_EVENT_PRESSED) {
          d->x = px;
          d->y = py;
          d->left = a->cropX_;
          d->top = a->cropY_;
          d->w = a->cropW_;
          d->h = a->cropH_;
          d->edges = 0;
          d->panX = a->cropPanX_;
          d->panY = a->cropPanY_;
          double x = (px - d->panX) / zoom, y = (py - d->panY) / zoom;
          const double hx = std::min(22. / zoom, d->w / 4.),
                       hy = std::min(22. / zoom, d->h / 4.);
          d->active = a->cropPanMode_ ||
                      (x >= d->left - hx && x <= d->left + d->w + hx &&
                       y >= d->top - hy && y <= d->top + d->h + hy);
          if (std::abs(x - d->left) <= hx)
            d->edges |= 1;
          else if (std::abs(x - d->left - d->w) <= hx)
            d->edges |= 2;
          if (std::abs(y - d->top) <= hy)
            d->edges |= 4;
          else if (std::abs(y - d->top - d->h) <= hy)
            d->edges |= 8;
        } else if (d->active) {
          if (a->cropPanMode_) {
            a->cropPanX_ = d->panX + px - d->x;
            a->cropPanY_ = d->panY + py - d->y;
          } else {
            int dx = int(std::round((px - d->x) / zoom)),
                dy = int(std::round((py - d->y) / zoom));
            if (!d->edges) {
              a->cropX_ =
                  std::max(0, std::min(a->cropPreviewW_ - d->w, d->left + dx));
              a->cropY_ =
                  std::max(0, std::min(a->cropPreviewH_ - d->h, d->top + dy));
            } else {
              int left = d->left, top = d->top, right = left + d->w,
                  bottom = top + d->h;
              const int minW = std::min(a->cropPreviewW_,
                                        std::max(2, int(std::ceil(44 / zoom)))),
                        minH = std::min(a->cropPreviewH_,
                                        std::max(2, int(std::ceil(44 / zoom))));
              if (d->edges & 1)
                left = std::max(0, std::min(right - minW, left + dx));
              if (d->edges & 2)
                right = std::max(left + minW,
                                 std::min(a->cropPreviewW_, right + dx));
              if (d->edges & 4)
                top = std::max(0, std::min(bottom - minH, top + dy));
              if (d->edges & 8)
                bottom = std::max(top + minH,
                                  std::min(a->cropPreviewH_, bottom + dy));
              a->cropX_ = left;
              a->cropY_ = top;
              a->cropW_ = right - left;
              a->cropH_ = bottom - top;
            }
          }
          a->updateCrop();
        }
      },
      LV_EVENT_ALL, drag);
  const int rail = portrait ? 12 : screen_.width - 300,
            railW = portrait ? photoWidth() - 24 : 200;
  const int toolsY = portrait ? bodyH - 244 : 42;
  if (!portrait)
    label(body_, "裁剪 · " + std::to_string(int(std::round(cropZoom_))) + "×",
          rail, 8, railW, 21);
  int bw = (railW - 12) / 4;
  const char *labels[] = {"−", "+", "全图", "选区"};
  const char *actions[] = {"minus", "plus", "fit", "selection"};
  for (int i = 0; i < 4; ++i) {
    auto control = button(body_, labels[i], rail + i * (bw + 4), toolsY, bw, 44,
                          [this, i, actions] {
                            events_.push("crop_view", {{"action", actions[i]}});
                          });
    auto caption = lv_obj_get_child(control, 0);
    lv_obj_set_style_text_font(caption, font(17), 0);
    lv_obj_set_width(caption, bw - 4);
    lv_obj_center(caption);
  }
  button(body_, cropPanMode_ ? "模式：移图片" : "模式：调选框", rail,
         toolsY + 50, railW, 44, [this] {
           events_.push("crop_view", {{"action", "pan"}});
         });
  auto check = lv_checkbox_create(body_);
  lv_checkbox_set_text(check, "加入系统相册");
  lv_obj_set_pos(check, rail, toolsY + 100);
  lv_obj_set_size(check, railW, 44);
  lv_obj_set_style_text_font(check, font(19), 0);
  lv_obj_set_style_text_color(check, lv_color_hex(theme::text), 0);
  if (cropSaveAlbum_)
    lv_obj_add_state(check, LV_STATE_CHECKED);
  lv_obj_add_event_cb(
      check,
      [](lv_event_t *e) {
        auto a = static_cast<App *>(lv_event_get_user_data(e));
        a->cropSaveAlbum_ = lv_obj_has_state(
            static_cast<lv_obj_t *>(lv_event_get_target(e)), LV_STATE_CHECKED);
        auto settings =
            a->db_.settings().value("penchat_settings", Json::object());
        settings["saveCameraToAlbum"] = a->cropSaveAlbum_;
        a->db_.setting("penchat_settings", settings);
      },
      LV_EVENT_VALUE_CHANGED, this);
  const int footerY = portrait ? bodyH - 48 : 208;
  auto confirm =
      button(body_, cropBusy_ ? "处理中" : "确定", rail, footerY,
             (railW - 8) / 2, 44, [this] { events_.push("crop_confirm"); });
  button(body_, "取消", rail + (railW + 8) / 2, footerY, (railW - 8) / 2, 44,
         [this] { show("camera"); });
  if (cropBusy_) {
    lv_obj_add_state(check, LV_STATE_DISABLED);
    lv_obj_add_state(confirm, LV_STATE_DISABLED);
  }
}
void App::confirmCrop() {
  if (cropWorker_.joinable())
    cropWorker_.join();
  // A retry after a failed database write must not leave an extra album copy.
  if (cropResult_.contains("album_path"))
    unlink(cropResult_["album_path"].get<std::string>().c_str());
  cropResult_ = Json::object();
  cropBusy_ = true;
  const auto source = cropPhoto_.at("path").get<std::string>();
  const auto output = join(dataDir_, "camera-tmp/cropped.jpg");
  const bool album = cropSaveAlbum_;
  const double x = double(cropX_) / cropPreviewW_,
               y = double(cropY_) / cropPreviewH_;
  const double right = double(cropX_ + cropW_) / cropPreviewW_,
               bottom = double(cropY_ + cropH_) / cropPreviewH_;
  const int generation = ++cropGeneration_;
  cropWorker_ = std::thread(
      [this, source, output, album, x, y, right, bottom, generation] {
        std::string albumPath, part;
        try {
          int w = 0, h = 0, n = 0;
          if (!stbi_info(source.c_str(), &w, &h, &n) || w < 1 || h < 1 ||
              int64_t(w) * h > 16000000)
            throw std::runtime_error("无法读取照片");
          std::unique_ptr<unsigned char, void (*)(void *)> pixels(
              stbi_load(source.c_str(), &w, &h, &n, 3), stbi_image_free);
          if (!pixels)
            throw std::runtime_error("照片解码失败");
          int left = std::min(w - 1, int(std::floor(x * w))),
              top = std::min(h - 1, int(std::floor(y * h)));
          int cw = std::max(1, std::min(w, int(std::ceil(right * w))) - left);
          int ch = std::max(1, std::min(h, int(std::ceil(bottom * h))) - top);
          std::vector<unsigned char> cropped(size_t(cw) * ch * 3);
          for (int row = 0; row < ch; ++row)
            std::memcpy(cropped.data() + size_t(row) * cw * 3,
                        pixels.get() + (size_t(top + row) * w + left) * 3,
                        size_t(cw) * 3);
          if (!stbi_write_jpg(output.c_str(), cw, ch, 3, cropped.data(), 92))
            throw std::runtime_error("裁剪保存失败");
          if (album) {
            std::string directory = "/userdisk/Pictures";
#ifndef __aarch64__
            if (const char *test = getenv("PENCHAT_TEST_ALBUM"))
              directory = test;
#endif
            ensureDirectory(directory);
            int error = 0;
            if (!camera::reserveMediaPaths(directory, "PenChat", "jpg",
                                           &albumPath, &part, &error))
              throw std::runtime_error("无法写入系统相册，请取消勾选后重试");
            atomicText(part, fileText(output, 16 * 1024 * 1024));
            if (rename(part.c_str(), albumPath.c_str()))
              throw std::runtime_error("系统相册保存失败");
          }
          cropResult_ = {{"path", output},
                         {"width", cw},
                         {"height", ch},
                         {"source", "camera"},
                         {"ephemeral", true}};
          if (!albumPath.empty())
            cropResult_["album_path"] = albumPath;
        } catch (const std::exception &e) {
          if (!albumPath.empty())
            unlink(albumPath.c_str());
          if (!part.empty())
            unlink(part.c_str());
          cropResult_ = {{"error", e.what()}};
        }
        events_.push("crop_done", {{"generation", generation}});
      });
  build();
}
} // namespace penchat
