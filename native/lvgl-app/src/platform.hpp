#pragma once
#include "common.hpp"
#include "overlay.h"
#include <lvgl/lvgl.h>
namespace penchat {
class Display {
  camera::Overlay overlay_;
  lv_display_t *display_ = nullptr;
  std::vector<uint8_t> buffer_, rgba_;
  Screen screen_;
  bool shown_ = true, headless_ = false;

public:
  Display(const Screen &s, bool headless);
  ~Display();
  void visible(bool value);
  void screenshot(const std::string &path);
  void flush(lv_display_t *, const lv_area_t *, uint8_t *);
  lv_display_t *handle() { return display_; }
};
class Input {
  lv_indev_t *input_ = nullptr;
  lv_point_t point_{};
  bool pressed_ = false;

public:
  Input();
  void touch(int x, int y, bool down);
  void release();
  void cancel();
};
class Bridge {
  int fd_ = -1;
  int generation_ = 0;
  uint64_t requestSequence_ = 0;

public:
  explicit Bridge(int fd) : fd_(fd) {}
  bool connected() const { return fd_ >= 0; }
  int generation() const { return generation_; }
  void generation(int n) { generation_ = n; }
  bool send(const std::string &type, Json data = Json::object());
  std::vector<Json> receive();
};
lv_font_t *font(int size, bool bold = false);
void initializeFonts(const std::string &path, const std::string &fallbackPath);
} // namespace penchat
