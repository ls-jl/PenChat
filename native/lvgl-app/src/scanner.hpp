#pragma once
#include "images.hpp"
#include <memory>
namespace penchat {
// All process, socket and recovery work runs outside LVGL's main thread.
class ScanService {
  struct Impl;
  std::unique_ptr<Impl> p_;

public:
  static void recoverHardware(const std::string &data);
  ScanService(Events &, const std::string &workspace, const std::string &data);
  ~ScanService();
  void stop();
  void mediaReleased(uint64_t token);
  void eligible(bool enabled, const std::string &session);
  uint64_t prepareReview(uint64_t scan);
  void cancelReview(uint64_t scan);
  void confirm(uint64_t scan);
  void discard(uint64_t scan);
  bool released() const;
  bool available() const;
  uint64_t generation() const;
  Json state() const;
  std::shared_ptr<ImageFrame> preview(uint64_t scan);
#ifndef PENCHAT_NATIVE_DEVICE
  void test(const Json &command);
#endif
};
} // namespace penchat
