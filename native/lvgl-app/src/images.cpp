#include "images.hpp"
#include "camera_logic.h"
#include "network.hpp"
#include "overlay.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "util.h"
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <webp/decode.h>
namespace penchat {
Images::Images(Events &e) : events_(e) {
  worker_ = std::thread([this] { work(); });
}
Images::~Images() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_ = true;
    cancel_ = true;
  }
  condition_.notify_one();
  if (worker_.joinable())
    worker_.join();
}
std::string Images::request(const std::string &path, int w, int h) {
  auto key = path + ":" + std::to_string(w) + "x" + std::to_string(h);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!cache_.count(key) && pending_.insert(key).second) {
    queue_.push_back({path, key, w, h});
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
        auto response = http(r.path, "GET", Json::object(), "", cancel_);
        if (response.status < 200 || response.status >= 300 || !response.error.empty())
          throw std::runtime_error("Image download failed");
        bytes = std::move(response.body);
      } else if(r.path.find("data:image/")==0){
        auto comma=r.path.find(',');if(comma==std::string::npos||r.path.find(";base64,")==std::string::npos)throw std::runtime_error("Unsupported inline image");
        int bits=0;unsigned value=0;const std::string alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for(size_t i=comma+1;i<r.path.size();++i){if(r.path[i]=='=')break;auto n=alphabet.find(r.path[i]);if(n==std::string::npos)continue;value=(value<<6)|n;bits+=6;if(bits>=8){bits-=8;bytes.push_back(char(value>>bits));}if(bytes.size()>16*1024*1024)throw std::runtime_error("Image too large");}
      }else{auto path=r.path;if(path.find("file://")==0)path=path.substr(7);bytes=fileText(path,16*1024*1024);}
      int w = 0, h = 0, n = 0;
      const auto *source = reinterpret_cast<const uint8_t *>(bytes.data());
      bool webp = bytes.size() >= 12 && bytes.compare(0, 4, "RIFF") == 0 &&
                  bytes.compare(8, 4, "WEBP") == 0;
      if (bytes.empty() || bytes.size() > 16 * 1024 * 1024 ||
          !(webp ? WebPGetInfo(source, bytes.size(), &w, &h)
                 : stbi_info_from_memory(source, bytes.size(), &w, &h, &n)) ||
          w < 1 || h < 1 || w > 8192 || h > 8192 || int64_t(w) * h > 16000000)
        throw std::runtime_error("Unsupported or oversized image");
      std::vector<unsigned char> decoded;
      unsigned char *data = nullptr;
      std::unique_ptr<unsigned char, void (*)(void *)> owned(nullptr, stbi_image_free);
      if (webp) {
        decoded.resize(size_t(w) * h * 4);
        data = WebPDecodeRGBAInto(source, bytes.size(), decoded.data(), decoded.size(), w * 4);
      } else {
        data = stbi_load_from_memory(source, bytes.size(), &w, &h, &n, 4);
        owned.reset(data);
      }
      if (!data)
        throw std::runtime_error("Image decode failed");
      float scale = std::min(1.f, std::min(float(r.width) / w, float(r.height) / h));
      auto image = std::make_shared<ImageFrame>();
      image->width = std::max(1, int(w * scale));
      image->height = std::max(1, int(h * scale));
      image->pixels.resize(size_t(image->width) * image->height * 4);
      for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x) {
          auto in = data + (size_t(y * h / image->height) * w + x * w / image->width) * 4;
          auto out = image->pixels.data() + (size_t(y) * image->width + x) * 4;
          out[0] = in[2];
          out[1] = in[1];
          out[2] = in[0];
          out[3] = in[3];
        }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto size = image->pixels.size();
        while (!order_.empty() && bytes_ + size > 32 * 1024 * 1024) {
          auto old = order_.front();
          order_.pop_front();
          bytes_ -= cache_[old]->pixels.size();
          cache_.erase(old);
        }
        cache_[r.key] = image;
        order_.push_back(r.key);
        bytes_ += size;
        pending_.erase(r.key);
      }
      events_.push("image_ready", {{"key", r.key}});
    } catch (const std::exception &e) {
      std::lock_guard<std::mutex> lock(mutex_);
      cache_[r.key] = nullptr;
      pending_.erase(r.key);
      events_.push("image_error", {{"key", r.key}, {"error", e.what()}});
    }
  }
}
void Camera::start() {
  stop();
  stop_ = false;
  save_ = false;
  thread_ = std::thread([this] { work(); });
}
void Camera::stop() {
  stop_ = true;
  save_ = false;
  if (thread_.joinable())
    thread_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  frame_.reset();
  nv12_.clear();
}
std::shared_ptr<ImageFrame> Camera::frame() {
  std::lock_guard<std::mutex> lock(mutex_);
  return frame_;
}
void Camera::work() {
  camera::Capture capture;
  try {
    if (!capture.open("", 960, 720))
      throw std::runtime_error(capture.lastError());
    events_.push("camera_started", {{"width", capture.width()}, {"height", capture.height()}});
    while (!stop_) {
      camera::CaptureFrame f;
      if (!capture.grab(&f, 200))
        continue;
      if (f.width < 2 || f.height < 2 || f.width > 4096 || f.height > 4096 || f.yStride < f.width ||
          f.uvStride < f.width || f.size < size_t(f.yStride) * f.height ||
          (f.uv ? f.uvSize < size_t(f.uvStride) * f.height / 2
                : f.size < size_t(f.yStride) * f.height + size_t(f.uvStride) * f.height / 2)) {
        capture.release(f.index);
        continue;
      }
      std::vector<uint8_t> bytes(size_t(f.width) * f.height * 3 / 2);
      for (int y = 0; y < f.height; ++y)
        std::memcpy(bytes.data() + size_t(y) * f.width, f.data + size_t(y) * f.yStride, f.width);
      for (int y = 0; y < f.height / 2; ++y)
        std::memcpy(bytes.data() + size_t(f.width) * f.height + size_t(y) * f.width,
                    (f.uv ? f.uv : f.data + size_t(f.yStride) * f.height) + size_t(y) * f.uvStride,
                    f.width);
      capture.release(f.index);
      auto frame = std::make_shared<ImageFrame>();
      float scale = std::min(1.f, 240.f / f.height);
      frame->width = int(f.width * scale);
      frame->height = int(f.height * scale);
      frame->pixels.resize(size_t(frame->width) * frame->height * 4);
      camera::nv12ToBgra(bytes.data(), bytes.data() + size_t(f.width) * f.height, f.width, f.height,
                         f.width, f.width, frame->pixels.data(), frame->width, frame->height, 0);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        frame_ = frame;
        width_ = f.width;
        height_ = f.height;
        nv12_ = bytes;
      }
      if (save_.exchange(false)) {
        ensureDirectory(directory_);
        std::string final, part;
        int error = 0;
        if (!camera::reserveMediaPaths(directory_, "IMG", "jpg", &final, &part, &error))
          throw std::runtime_error("Cannot reserve photo file");
        std::vector<uint8_t> rgb(size_t(f.width) * f.height * 3);
        for (int y = 0; y < f.height; ++y)
          for (int x = 0; x < f.width; ++x) {
            int Y = bytes[size_t(y) * f.width + x] - 16;
            auto off = size_t(f.width) * f.height + size_t(y / 2) * f.width + (x & ~1);
            int U = bytes[off] - 128, V = bytes[off + 1] - 128;
            auto clamp = [](int n) { return uint8_t(std::max(0, std::min(255, n))); };
            auto out = rgb.data() + (size_t(y) * f.width + x) * 3;
            out[0] = clamp((298 * Y + 409 * V + 128) >> 8);
            out[1] = clamp((298 * Y - 100 * U - 208 * V + 128) >> 8);
            out[2] = clamp((298 * Y + 516 * U + 128) >> 8);
          }
        if (!stbi_write_jpg(part.c_str(), f.width, f.height, 3, rgb.data(), 92) ||
            rename(part.c_str(), final.c_str())) {
          unlink(part.c_str());
          throw std::runtime_error("Cannot save photo");
        }
        events_.push(
            "photo",
            {{"path", final}, {"width", f.width}, {"height", f.height}, {"source", "camera"}});
      }
    }
  } catch (const std::exception &e) {
    events_.push("camera_error", {{"error", e.what()}});
  }
  capture.close();
}
} // namespace penchat
