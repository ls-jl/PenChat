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
  cropSelection_ = nullptr;
}
void App::beginCrop(const Json &photo) {
  camera_.stop();
  cropPhoto_ = photo;
  cropResult_ = Json::object();
  cropSaveAlbum_ = db_.settings()
                       .value("penchat_settings", Json::object())
                       .value("saveCameraToAlbum", false);
  const double scale =
      std::min(double(screen_.width - 330) / photo.at("width").get<int>(),
               double(screen_.height - 88) / photo.at("height").get<int>());
  cropPreviewW_ = std::max(1, int(photo.at("width").get<int>() * scale));
  cropPreviewH_ = std::max(1, int(photo.at("height").get<int>() * scale));
  cropX_ = cropY_ = 0;
  cropW_ = cropPreviewW_;
  cropH_ = cropPreviewH_;
  show("crop");
}
void App::updateCrop() {
  if (!cropSelection_)
    return;
  lv_obj_set_pos(cropSelection_, cropX_, cropY_);
  lv_obj_set_size(cropSelection_, cropW_, cropH_);
  const int rects[4][4] = {
      {0, 0, cropPreviewW_, cropY_},
      {0, cropY_ + cropH_, cropPreviewW_, cropPreviewH_ - cropY_ - cropH_},
      {0, cropY_, cropX_, cropH_},
      {cropX_ + cropW_, cropY_, cropPreviewW_ - cropX_ - cropW_, cropH_}};
  for (int i = 0; i < 4; ++i) {
    lv_obj_set_pos(cropShades_[i], rects[i][0], rects[i][1]);
    lv_obj_set_size(cropShades_[i], rects[i][2], rects[i][3]);
  }
}
void App::cropPage() {
  lv_obj_set_scrollable(body_, false);
  const int areaW = screen_.width - 330;
  auto area =
      box(body_, 12 + (areaW - cropPreviewW_) / 2,
          (screen_.height - cropPreviewH_) / 2, cropPreviewW_, cropPreviewH_);
  lv_obj_set_scrollable(area, false);
  image(area, cropPhoto_.value("path", std::string()), 0, 0, cropPreviewW_,
        cropPreviewH_);
  auto overlay = box(area, 0, 0, cropPreviewW_, cropPreviewH_);
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
  cropSelection_ = box(overlay, cropX_, cropY_, cropW_, cropH_);
  lv_obj_set_style_bg_opa(cropSelection_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cropSelection_, 2, 0);
  lv_obj_set_style_border_color(cropSelection_, lv_color_hex(theme::text), 0);
  lv_obj_set_scrollable(cropSelection_, false);
  lv_obj_set_clickable(cropSelection_, false);
  const lv_align_t corners[] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT,
                                LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT};
  for (auto align : corners) {
    auto dot = box(cropSelection_, 0, 0, 12, 12, theme::text);
    lv_obj_align(dot, align, 0, 0);
    lv_obj_set_clickable(dot, false);
    lv_obj_set_scrollable(dot, false);
  }
  updateCrop();
  struct Drag {
    App *app;
    int x = 0, y = 0, left = 0, top = 0, w = 0, h = 0, corner = -1;
  };
  auto drag = new Drag{this};
  lv_obj_add_event_cb(
      overlay,
      [](lv_event_t *e) {
        auto d = static_cast<Drag *>(lv_event_get_user_data(e));
        auto a = d->app;
        const auto code = lv_event_get_code(e);
        if (code == LV_EVENT_DELETE) {
          delete d;
          return;
        }
        if (a->cropBusy_ ||
            (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING))
          return;
        lv_point_t point;
        lv_indev_get_point(lv_indev_active(), &point);
        lv_area_t bounds;
        lv_obj_get_coords(static_cast<lv_obj_t *>(lv_event_get_target(e)),
                          &bounds);
        int x = point.x - bounds.x1, y = point.y - bounds.y1;
        if (code == LV_EVENT_PRESSED) {
          d->x = x;
          d->y = y;
          d->left = a->cropX_;
          d->top = a->cropY_;
          d->w = a->cropW_;
          d->h = a->cropH_;
          d->corner = -1;
          for (int i = 0; i < 4; ++i) {
            int cx = d->left + (i % 2 ? d->w : 0),
                cy = d->top + (i / 2 ? d->h : 0);
            if (std::abs(x - cx) <= 28 && std::abs(y - cy) <= 28) {
              d->corner = i;
              break;
            }
          }
        } else if (code == LV_EVENT_PRESSING) {
          int dx = x - d->x, dy = y - d->y;
          if (d->corner < 0) {
            a->cropX_ =
                std::max(0, std::min(a->cropPreviewW_ - d->w, d->left + dx));
            a->cropY_ =
                std::max(0, std::min(a->cropPreviewH_ - d->h, d->top + dy));
          } else {
            int left = d->left, top = d->top, right = left + d->w,
                bottom = top + d->h;
            const int minW = std::min(44, a->cropPreviewW_),
                      minH = std::min(44, a->cropPreviewH_);
            if (d->corner % 2)
              right =
                  std::max(left + minW, std::min(a->cropPreviewW_, right + dx));
            else
              left = std::max(0, std::min(right - minW, left + dx));
            if (d->corner / 2)
              bottom =
                  std::max(top + minH, std::min(a->cropPreviewH_, bottom + dy));
            else
              top = std::max(0, std::min(bottom - minH, top + dy));
            a->cropX_ = left;
            a->cropY_ = top;
            a->cropW_ = right - left;
            a->cropH_ = bottom - top;
          }
          a->updateCrop();
        }
      },
      LV_EVENT_ALL, drag);
  const int rail = screen_.width - 300;
  label(body_, "裁剪照片", rail, 14, 195, 22);
  label(body_, "拖动四角裁剪\n拖动选区移动", rail, 48, 195, 17, theme::muted);
  auto check = lv_checkbox_create(body_);
  lv_checkbox_set_text(check, "加入系统相册");
  lv_obj_set_pos(check, rail, 102);
  lv_obj_set_size(check, 200, 44);
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
  auto confirm = button(body_, cropBusy_ ? "处理中…" : "确定", rail, 158, 200,
                        44, [this] { events_.push("crop_confirm"); });
  button(body_, "取消", rail, 210, 200, 44, [this] { show("camera"); });
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
