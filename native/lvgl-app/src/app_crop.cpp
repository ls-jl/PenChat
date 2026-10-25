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
  if (cropWorker_.joinable()) cropWorker_.join();
  if (!keepAlbum && cropResult_.contains("album_path"))
    unlink(cropResult_["album_path"].get<std::string>().c_str());
  auto directory = join(dataDir_, "camera-tmp");
  if (auto dir = opendir(directory.c_str())) {
    while (auto entry = readdir(dir)) {
      std::string name = entry->d_name;
      if (name != "." && name != "..") unlink(join(directory, name).c_str());
    }
    closedir(dir);
  }
  ++cropGeneration_;
  cropPhoto_ = cropResult_ = Json::object();
  cropBusy_ = cropLoaded_ = false;
  cropSelection_ = cropBitmap_ = cropViewport_ = nullptr;
  cropActions_.clear(); imageGesture_ = {};
}
void App::beginCrop(const Json &photo) {
  camera_.stop(); cameraCapturing_ = false; pendingCameraAngle_ = -1;
  cameraAngle_ = photo.value("orientation", 0);
  cropPhoto_ = photo; cropResult_ = Json::object();
  cropSaveAlbum_ = db_.settings().value("penchat_settings", Json::object()).value("saveCameraToAlbum", false);
  const bool narrow = photoWidth() < 440;
  cropViewportW_ = std::max(1, photoWidth() - 16);
  cropViewportH_ = std::max(1, photoHeight() - (narrow ? 120 : 68));
  cropLoaded_ = false; imageGesture_ = {}; imageTouchPeak_ = imagePinchUpdates_ = 0;
  cropImageKey_ = images_.request(photo.at("path").get<std::string>(), 1600, 1600);
  show("crop");
}
void App::resetCrop() {
  cropZoom_ = 1;
  cropPanX_ = (cropViewportW_ - cropPreviewW_) / 2.;
  cropPanY_ = (cropViewportH_ - cropPreviewH_) / 2.;
  cropX_ = cropPanX_; cropY_ = cropPanY_;
  cropW_ = cropPreviewW_; cropH_ = cropPreviewH_;
  imageGesture_ = {}; input_.cancel();
  updateCrop();
}
void App::updateCrop() {
  if (!cropSelection_ || !cropViewport_) return;
  auto frame = images_.get(cropImageKey_);
  if (!frame) return;
  if (!cropLoaded_) {
    // The image widget retains its intrinsic bitmap dimensions. A viewport's
    // letterbox background must never become part of the scaled image itself.
    cropBaseScale_ = std::max(1., std::floor(256 * std::min(
        double(cropViewportW_) / frame->width, double(cropViewportH_) / frame->height))) / 256;
    cropPreviewW_ = frame->width * cropBaseScale_;
    cropPreviewH_ = frame->height * cropBaseScale_;
    cropLoaded_ = true;
    resetCrop();
    if (cropActions_.count("confirm") && !cropBusy_)
      lv_obj_remove_state(cropActions_["confirm"], LV_STATE_DISABLED);
    return;
  }
  const int scale = std::max(1, std::min(65535, int(std::lround(cropBaseScale_ * cropZoom_ * 256))));
  cropZoom_ = double(scale) / (cropBaseScale_ * 256);
  const double iw = cropPreviewW_ * cropZoom_, ih = cropPreviewH_ * cropZoom_;
  cropPanX_ = std::max(cropX_ + cropW_ - iw, std::min(cropX_, cropPanX_));
  cropPanY_ = std::max(cropY_ + cropH_ - ih, std::min(cropY_, cropPanY_));
  lv_image_set_pivot(cropBitmap_, 0, 0);
  lv_image_set_scale(cropBitmap_, scale);
  lv_obj_set_pos(cropBitmap_, int(std::lround(cropPanX_)), int(std::lround(cropPanY_)));
  lv_obj_set_pos(cropSelection_, int(std::lround(cropX_)), int(std::lround(cropY_)));
  lv_obj_set_size(cropSelection_, int(std::lround(cropW_)), int(std::lround(cropH_)));
  const int left = int(std::lround(cropX_)), top = int(std::lround(cropY_));
  const int right = int(std::lround(cropX_ + cropW_)), bottom = int(std::lround(cropY_ + cropH_));
  const int rects[4][4] = {{0,0,cropViewportW_,top}, {0,bottom,cropViewportW_,cropViewportH_-bottom},
                          {0,top,left,bottom-top}, {right,top,cropViewportW_-right,bottom-top}};
  for (int i=0; i<4; ++i) {
    lv_obj_set_pos(cropShades_[i],rects[i][0],rects[i][1]);
    lv_obj_set_size(cropShades_[i],std::max(0,rects[i][2]),std::max(0,rects[i][3]));
  }
}
int App::cropEdges(double x,double y) const {
  const double hx=std::min(22.,cropW_/3),hy=std::min(22.,cropH_/3);
  if(x<cropX_-hx || x>cropX_+cropW_+hx || y<cropY_-hy || y>cropY_+cropH_+hy)return 0;
  int edges=0;
  if(std::abs(x-cropX_)<=hx)edges|=1;else if(std::abs(x-cropX_-cropW_)<=hx)edges|=2;
  if(std::abs(y-cropY_)<=hy)edges|=4;else if(std::abs(y-cropY_-cropH_)<=hy)edges|=8;
  return edges;
}
Json App::cropSourceRect() const {
  if (!cropLoaded_ || cropPhoto_.empty()) return Json::array({0,0,0,0});
  const int width=cropPhoto_.value("width",0), height=cropPhoto_.value("height",0);
  const double iw=cropPreviewW_*cropZoom_, ih=cropPreviewH_*cropZoom_;
  const auto edge=[](double n, int size, bool end) {
    const double value=std::max(0.,std::min(double(size),n*size));
    // Avoid a spurious one-pixel border from floating point roundoff.
    return int(end?std::ceil(value-1e-7):std::floor(value+1e-7));
  };
  const int l=edge((cropX_-cropPanX_)/iw,width,false), t=edge((cropY_-cropPanY_)/ih,height,false);
  const int r=edge((cropX_+cropW_-cropPanX_)/iw,width,true), b=edge((cropY_+cropH_-cropPanY_)/ih,height,true);
  return Json::array({l,t,r-l,b-t});
}
void App::cropView(const std::string &action) {
  if (action == "reset") resetCrop();
}
void App::cropPage() {
  lv_obj_set_scrollable(body_,false);
  const int width=photoWidth(), height=photoHeight();
  const bool narrow=width<440;
  cropViewport_=box(body_,8,8,cropViewportW_,cropViewportH_,theme::sidebar);
  lv_obj_set_scrollable(cropViewport_,false);
  cropBitmap_=imageWidget(cropViewport_); lv_obj_set_clickable(cropBitmap_,false);
  auto loading=label(cropViewport_,"正在载入…",8,8,cropViewportW_-16,17,theme::muted);
  lv_obj_set_clickable(loading,false); placeholders_[cropBitmap_]=loading;
  imageRequests_.push_back({cropBitmap_,cropImageKey_});
  auto overlay=box(cropViewport_,0,0,cropViewportW_,cropViewportH_);
  lv_obj_set_style_bg_opa(overlay,LV_OPA_TRANSP,0);
  lv_obj_set_scrollable(overlay,false); lv_obj_set_clickable(overlay,false);
  for (auto &shade:cropShades_) {
    shade=box(overlay,0,0,0,0,0x000000);
    lv_obj_set_style_bg_opa(shade,LV_OPA_60,0); lv_obj_set_clickable(shade,false); lv_obj_set_scrollable(shade,false);
  }
  cropSelection_=box(overlay,0,0,1,1);
  lv_obj_set_style_bg_opa(cropSelection_,LV_OPA_TRANSP,0);
  lv_obj_set_style_border_width(cropSelection_,2,0);
  lv_obj_set_style_border_color(cropSelection_,lv_color_hex(theme::text),0);
  lv_obj_set_scrollable(cropSelection_,false); lv_obj_set_clickable(cropSelection_,false);
  for(auto align:{LV_ALIGN_TOP_LEFT,LV_ALIGN_TOP_RIGHT,LV_ALIGN_BOTTOM_LEFT,LV_ALIGN_BOTTOM_RIGHT,
                  LV_ALIGN_TOP_MID,LV_ALIGN_BOTTOM_MID,LV_ALIGN_LEFT_MID,LV_ALIGN_RIGHT_MID}) {
    const bool h=align==LV_ALIGN_TOP_MID||align==LV_ALIGN_BOTTOM_MID,
               v=align==LV_ALIGN_LEFT_MID||align==LV_ALIGN_RIGHT_MID;
    auto handle=box(cropSelection_,0,0,h?28:v?4:12,v?28:h?4:12,theme::text);
    lv_obj_align(handle,align,0,0);lv_obj_set_clickable(handle,false);lv_obj_set_scrollable(handle,false);
  }
  cropActions_.clear();
  const int y=height-52, bw=narrow?(width-32)/3:72;
  cropActions_["cancel"]=button(body_,"取消",8,y,bw,44,[this]{events_.push("crop_cancel");});
  cropActions_["reset"]=button(body_,"还原",narrow?16+bw:88,y,bw,44,[this]{events_.push("crop_view",{{"action","reset"}});});
  auto check=lv_checkbox_create(body_);cropActions_["album"]=check;
  lv_checkbox_set_text(check,"加入系统相册");
  lv_obj_set_pos(check,narrow?12:168,narrow?height-104:y);
  lv_obj_set_size(check,narrow?width-24:184,44);
  lv_obj_set_style_text_font(check,font(19),0);
  lv_obj_set_style_text_color(check,lv_color_hex(theme::text),0);
  lv_obj_set_style_bg_color(check,lv_color_hex(theme::surface),LV_PART_INDICATOR);
  lv_obj_set_style_border_color(check,lv_color_hex(theme::muted),LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(check,lv_color_hex(theme::accent),LV_PART_INDICATOR|LV_STATE_CHECKED);
  lv_obj_set_style_border_color(check,lv_color_hex(theme::accent),LV_PART_INDICATOR|LV_STATE_CHECKED);
  lv_obj_set_style_radius(check,5,LV_PART_INDICATOR);
  if(cropSaveAlbum_)lv_obj_add_state(check,LV_STATE_CHECKED);
  lv_obj_add_event_cb(check,[](lv_event_t *e){
    auto a=static_cast<App *>(lv_event_get_user_data(e));
    a->cropSaveAlbum_=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);
    auto settings=a->db_.settings().value("penchat_settings",Json::object());
    settings["saveCameraToAlbum"]=a->cropSaveAlbum_;a->db_.setting("penchat_settings",settings);
  },LV_EVENT_VALUE_CHANGED,this);
  auto confirm=button(body_,cropBusy_?"处理中":"确定",width-8-bw,y,bw,44,[this]{events_.push("crop_confirm");});
  cropActions_["confirm"]=confirm;primary(confirm);
  updateCrop();
  if(!cropLoaded_)lv_obj_add_state(confirm,LV_STATE_DISABLED);
  if(cropBusy_)for(auto &entry:cropActions_)lv_obj_add_state(entry.second,LV_STATE_DISABLED);
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
  const auto rect = cropSourceRect();
  const int left=rect[0], top=rect[1], cw=rect[2], ch=rect[3];
  const int expectedW=cropPhoto_.value("width",0), expectedH=cropPhoto_.value("height",0);
  const int generation = ++cropGeneration_;
  cropWorker_ = std::thread(
      [this, source, output, album, left, top, cw, ch, expectedW, expectedH, generation] {
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
          if(w!=expectedW || h!=expectedH || left<0 || top<0 || cw<1 || ch<1 || left+cw>w || top+ch>h)
            throw std::runtime_error("照片尺寸变化，请重新拍照");
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
