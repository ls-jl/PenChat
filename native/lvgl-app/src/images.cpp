#include "device_profile.h"
#include "images.hpp"
#include "accelerometer.h"
#include "photo_orientation.hpp"
#include "camera_logic.h"
#include "network.hpp"
#include "overlay.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "util.h"
#include <cstring>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unistd.h>
#include <webp/decode.h>
namespace penchat {
Images::Images(Events &e) : events_(e) {
  worker_ = std::thread([this] { work(); });
}
Images::~Images() { stop(); }
void Images::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    cancel_ = true;
    queue_.clear();
    pending_.clear();
  }
  condition_.notify_one();
  if (worker_.joinable())
    worker_.join();
}
void Images::trimCache(size_t incoming) {
  while (!order_.empty() && (cache_.size() >= 128 || bytes_ + incoming > 32 * 1024 * 1024)) {
    const auto old = order_.front(); order_.pop_front();
    const auto found = cache_.find(old);
    if (found != cache_.end()) {
      if (found->second) bytes_ -= found->second->pixels.size();
      cache_.erase(found);
    }
  }
}
std::string Images::request(const std::string &path, int w, int h,
                            bool upscale) {
  auto key = path + ":" + std::to_string(w) + "x" + std::to_string(h) +
             (upscale ? ":up" : "");
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopped_) return key;
  if (!cache_.count(key) && pending_.insert(key).second) {
    if (queue_.size() >= 64) {
      pending_.erase(key); trimCache(0); cache_[key] = nullptr; order_.push_back(key);
      events_.pushLatest("image_error", {{"key", key}, {"error", "图片队列已满，请稍后重新打开"}});
      return key;
    }
    queue_.push_back({path, key, w, h, upscale});
    condition_.notify_one();
  }
  return key;
}
std::shared_ptr<ImageFrame> Images::get(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cache_.find(key);
  return it == cache_.end() ? nullptr : it->second;
}
bool Images::failed(const std::string &key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = cache_.find(key);
  return it != cache_.end() && !it->second;
}
void Images::work() {
  for (;;) {
    Request r;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      condition_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
      if (stopped_)
        return;
      r = queue_.front();
      queue_.pop_front();
    }
    try {
      std::string bytes;
      if (r.path.find("https://") == 0 || r.path.find("http://") == 0) {
        ApiOptions options; options.timeout = 60;
        auto response = http(r.path, "GET", Json::object(), "", cancel_,
          [&bytes](const char *data,size_t count) {
            if(count>16*1024*1024-bytes.size())throw std::runtime_error("Image too large");
            bytes.append(data,count);
          }, options);
        if (response.status < 200 || response.status >= 300 ||
            !response.error.empty())
          throw std::runtime_error("Image download failed");
      } else if (r.path.find("data:image/") == 0) {
        auto comma = r.path.find(',');
        if (comma == std::string::npos ||
            r.path.find(";base64,") == std::string::npos)
          throw std::runtime_error("Unsupported inline image");
        int bits = 0;
        unsigned value = 0;
        const std::string alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = comma + 1; i < r.path.size(); ++i) {
          if (r.path[i] == '=')
            break;
          auto n = alphabet.find(r.path[i]);
          if (n == std::string::npos)
            continue;
          value = (value << 6) | n;
          bits += 6;
          if (bits >= 8) {
            bits -= 8;
            bytes.push_back(char(value >> bits));
          }
          if (bytes.size() > 16 * 1024 * 1024)
            throw std::runtime_error("Image too large");
        }
      } else {
        auto path = r.path;
        if (path.find("file://") == 0)
          path = path.substr(7);
        bytes = fileText(path, 16 * 1024 * 1024);
      }
      int w = 0, h = 0, n = 0;
      const auto *source = reinterpret_cast<const uint8_t *>(bytes.data());
      bool webp = bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 &&
                  bytes.compare(8, 4, "WEBP") == 0;
      if (bytes.empty() || bytes.size() > 16 * 1024 * 1024 ||
          !(webp ? WebPGetInfo(source, bytes.size(), &w, &h)
                 : stbi_info_from_memory(source, bytes.size(), &w, &h, &n)) ||
          w < 1 || h < 1 || w > 65535 || h > 65535 || int64_t(w) * h > 16000000)
        throw std::runtime_error("Unsupported or oversized image");
      std::vector<unsigned char> decoded;
      unsigned char *data = nullptr;
      std::unique_ptr<unsigned char, void (*)(void *)> owned(nullptr,
                                                             stbi_image_free);
      if (webp) {
        decoded.resize(size_t(w) * h * 4);
        data = WebPDecodeRGBAInto(source, bytes.size(), decoded.data(),
                                  decoded.size(), w * 4);
      } else {
        data = stbi_load_from_memory(source, bytes.size(), &w, &h, &n, 4);
        owned.reset(data);
      }
      if (!data)
        throw std::runtime_error("Image decode failed");
      float scale = std::min(float(r.width) / w, float(r.height) / h);
      // Every cached image, including long scan details, is at most 16 MiB.
      scale = std::min(scale, float(std::sqrt(4000000.0 / (double(w) * h))));
      if (!r.upscale)
        scale = std::min(1.f, scale);
      auto image = std::make_shared<ImageFrame>();
      image->width = std::max(1, int(w * scale));
      image->height = std::max(1, int(h * scale));
      image->pixels.resize(size_t(image->width) * image->height * 4);
      for (int y = 0; y < image->height; ++y) {
        if (cancel_) return;
        for (int x = 0; x < image->width; ++x) {
          auto in =
              data +
              (size_t(int64_t(y) * h / image->height) * w +
               size_t(int64_t(x) * w / image->width)) * 4;
          auto out = image->pixels.data() + (size_t(y) * image->width + x) * 4;
          out[0] = in[2];
          out[1] = in[1];
          out[2] = in[0];
          out[3] = in[3];
        }
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_) return;
        auto size = image->pixels.size();
        trimCache(size);
        cache_[r.key] = image;
        order_.push_back(r.key);
        bytes_ += size;
        pending_.erase(r.key);
      }
      events_.push("image_ready", {{"key", r.key}});
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopped_) return;
      trimCache(0);
      cache_[r.key] = nullptr; order_.push_back(r.key);
      pending_.erase(r.key);
      events_.push("image_error", {{"key", r.key}, {"error", e.what()}});
    }
  }
}
PhotoTransforms Camera::transforms(int pose) const {
  static const int correction = photoPreviewCorrection(fileText("/proc/device-tree/model", 4096));
  return photoTransforms(pose, fixedWorkspace_, correction);
}
void Camera::start() {
  stop();
  stop_ = false;
  save_ = false;
  thread_ = std::thread([this] { work(); });
}
void Camera::stop() {
  ++generation_;
  stop_ = true;
  save_ = false;
  if (thread_.joinable())
    thread_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  frame_.reset();
}
std::shared_ptr<ImageFrame> Camera::frame() {
  std::lock_guard<std::mutex> lock(mutex_);
  return frame_;
}
void Camera::work() {
  const auto generation=generation_.load();
  auto event=[this,generation](const std::string &name,Json data){data["generation"]=generation;events_.push(name,std::move(data));};
  camera::Capture capture;
  int sensorSession = 0;
  PhotoOrientation orientationFilter;
  bool highResolution=false;
  int64_t nextSensor = 0;
  int64_t nextPreview = 0;
  // Latest frame, displayed frame and one writable frame; reuse only an owner
  // not published to either reader. The pool dies when the camera worker exits.
  std::array<std::shared_ptr<ImageFrame>,3> previews;
  try {
    if(!camera::deviceCapabilities().photoCamera)
      throw std::runtime_error("此机型没有独立拍照设备，可使用笔头扫描或相册");
    highResolution=fileText("/proc/device-tree/model",4096).find("RK3562 Y07 ")!=std::string::npos;
    if(highResolution&&fileText("/sys/class/video4linux/v4l-subdev7/name",4096).find("gc08a8")==std::string::npos)
      throw std::runtime_error("Y07相机传感器与高清配置不匹配");
#ifdef PENCHAT_NATIVE_DEVICE
    if (manualOrientation_ < 0) {
      const auto sensor = camera::startAccelerometer(workspace_);
      if (sensor.ok) sensorSession = sensor.session;
      event("camera_sensor", {{"available",sensor.ok}});
    }
#endif
    if (!capture.open("", 960, 720))
      throw std::runtime_error(capture.lastError());
    event("camera_started",
                 {{"width", capture.width()}, {"height", capture.height()}});
    while (!stop_) {
      if (sensorSession && ticks() >= nextSensor) {
        nextSensor = ticks()+200;
        const auto sample = camera::readAccelerometer(sensorSession);
        if (!sample.ok) {
          camera::stopAccelerometer(sensorSession);sensorSession=0;
          event("camera_sensor", {{"available",false}});
        }
        if (sample.ok && !save_) {
          int next = orientationFilter.update(sample.x,sample.y,sample.z,ticks(),orientation_);
          if (next != orientation_) {
            orientation_ = next;
            event("camera_orientation", {{"angle",next}});
          }
        }
      }
      camera::Capture high;
      camera::Capture *source=&capture;
      bool pausedPreview=false;
      camera::CaptureFrame f;
      if(highResolution&&save_.load()) {
        bool ready=high.open("/dev/video29",3264,2448);
        if(!ready){capture.close();pausedPreview=true;ready=high.open("/dev/video29",3264,2448);}
        bool received=false;
        if(ready)for(int tries=0;tries<10&&!stop_;++tries)if(high.grab(&f,200)){received=true;break;}
        if(!received&&!stop_&&!pausedPreview) {
          high.close();capture.close();pausedPreview=true;
          if(high.open("/dev/video29",3264,2448))for(int tries=0;tries<10&&!stop_;++tries)if(high.grab(&f,200)){received=true;break;}
        }
        if(stop_)break;
        if(!received||f.width!=3264||f.height!=2448)throw std::runtime_error("高清照片采集失败，请重试");
        source=&high;
      } else if (!capture.grab(&f, 200))continue;
      if(source==&capture) {
        // Capture requests four V4L2 buffers. Drain at most the other three:
        // preview freshness matters more than replaying already stale frames.
        // This is not the scanner's ordered/no-drop capture path.
        for(int queued=0;queued<3&&!stop_;++queued) {
          camera::CaptureFrame newer;
          if(!capture.grab(&newer,0))break;
          if(!capture.release(f.index)) {
            capture.release(newer.index);
            throw std::runtime_error("Cannot release camera preview buffer");
          }
          f=newer;
        }
      }
      if (f.width < 2 || f.height < 2 || f.width > 4096 || f.height > 4096 ||
          f.yStride < f.width || f.uvStride < f.width ||
          f.size < size_t(f.yStride) * f.height ||
          (f.uv ? f.uvSize < size_t(f.uvStride) * f.height / 2
                : f.size < size_t(f.yStride) * f.height +
                               size_t(f.uvStride) * f.height / 2)) {
        source->release(f.index);
        continue;
      }
      if(stop_){source->release(f.index);break;}
      // A shutter arriving during preview conversion remains pending for the
      // next frame. Y07 can only consume it after receiving the true HD frame.
      const bool takePhoto=(!highResolution||source==&high)&&save_.exchange(false);
      if(!takePhoto) {
        const auto now=ticks();
        if(now<nextPreview){source->release(f.index);continue;}
        // Allow the small timestamp jitter of a 30fps sensor; an exact 33ms
        // gate can otherwise reject alternate frames that arrive 32ms apart.
        nextPreview=now+30;
        std::shared_ptr<ImageFrame> frame;
        for(auto &slot:previews) {
          if(!slot)slot=std::make_shared<ImageFrame>();
          if(slot.use_count()==1){frame=slot;break;}
        }
        if(!frame){source->release(f.index);continue;}
        frame->cameraPose=orientation_.load();
        frame->rotation=transforms(frame->cameraPose).preview;
        const int rotatedW=frame->rotation%180?f.height:f.width;
        const int rotatedH=frame->rotation%180?f.width:f.height;
        float scale=std::min(1.f,(highResolution?720.f:240.f)/f.height);
        const int targetW=previewWidth_.load(),targetH=previewHeight_.load();
        if(targetW>0&&targetH>0)
          scale=std::min(scale,std::min(float(targetW)/rotatedW,float(targetH)/rotatedH));
        frame->width=std::max(1,int(rotatedW*scale));
        frame->height=std::max(1,int(rotatedH*scale));
        frame->opaque=true;
        frame->pixels.resize(size_t(frame->width)*frame->height*4);
        // Convert directly from the mapped sensor planes. No packed NV12 copy
        // or full-size intermediate is needed for a disposable preview.
        camera::nv12ToBgra(f.data,f.uv?f.uv:f.data+size_t(f.yStride)*f.height,
          f.width,f.height,f.yStride,f.uvStride,frame->pixels.data(),
          frame->width,frame->height,frame->rotation);
        if(!source->release(f.index))throw std::runtime_error("Cannot release camera preview buffer");
        if(stop_)break;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          frame_=std::move(frame);
        }
        continue;
      }
      std::vector<uint8_t> bytes(size_t(f.width) * f.height * 3 / 2);
      for (int y = 0; y < f.height; ++y)
        std::memcpy(bytes.data() + size_t(y) * f.width,
                    f.data + size_t(y) * f.yStride, f.width);
      for (int y = 0; y < f.height / 2; ++y)
        std::memcpy(bytes.data() + size_t(f.width) * f.height +
                        size_t(y) * f.width,
                    (f.uv ? f.uv : f.data + size_t(f.yStride) * f.height) +
                        size_t(y) * f.uvStride,
                    f.width);
      source->release(f.index);
      if(source==&high)high.close();
      if(stop_)break;
      if (takePhoto) {
        ensureDirectory(directory_);
        struct ReservedPhoto {
          std::string final,part;
          bool published=false;
          ~ReservedPhoto(){if(!published){if(!part.empty())unlink(part.c_str());if(!final.empty())unlink(final.c_str());}}
        } reserved;
        auto &final=reserved.final;auto &part=reserved.part;
        int error = 0;
        if (!camera::reserveMediaPaths(directory_, "IMG", "jpg", &final, &part,
                                       &error))
          throw std::runtime_error("Cannot reserve photo file");
        std::vector<uint8_t> rgb(size_t(f.width) * f.height * 3);
        for (int y = 0; y < f.height && !stop_; ++y)
          for (int x = 0; x < f.width; ++x) {
            int Y = bytes[size_t(y) * f.width + x] - 16;
            auto off =
                size_t(f.width) * f.height + size_t(y / 2) * f.width + (x & ~1);
            int U = bytes[off] - 128, V = bytes[off + 1] - 128;
            auto clamp = [](int n) {
              return uint8_t(std::max(0, std::min(255, n)));
            };
            auto out = rgb.data() + (size_t(y) * f.width + x) * 3;
            out[0] = clamp((298 * Y + 409 * V + 128) >> 8);
            out[1] = clamp((298 * Y - 100 * U - 208 * V + 128) >> 8);
            out[2] = clamp((298 * Y + 516 * U + 128) >> 8);
          }
        if(stop_)break;
        const int angle = captureOrientation_;
        const int photoW = angle % 180 ? f.height : f.width;
        const int photoH = angle % 180 ? f.width : f.height;
        if (angle) rgb = rotatePhoto(rgb,f.width,f.height,angle);
        if(stop_){unlink(part.c_str());break;}
        if (!stbi_write_jpg(part.c_str(), photoW, photoH, 3, rgb.data(),
                            92) ||
            rename(part.c_str(), final.c_str())) {
          unlink(part.c_str());
          throw std::runtime_error("Cannot save photo");
        }
        if(stop_)break;
        event("photo", {{"path", final},
                               {"width", photoW},
                               {"height", photoH},
                               {"orientation",angle},
                               {"source", "camera"}});
        reserved.published=true;
      }
      if(pausedPreview&&!stop_&&!capture.open("/dev/video30",960,720))throw std::runtime_error("无法恢复相机预览");
    }
  } catch (const std::exception &e) {
    event("camera_error", {{"error", e.what()}});
  }
  capture.close();
  if (sensorSession) camera::stopAccelerometer(sensorSession);
}
} // namespace penchat
