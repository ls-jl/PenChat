#pragma once
#include "common.hpp"
#include "display_surface.h"
#include <lvgl/lvgl.h>
namespace penchat {
class Display {
  std::unique_ptr<camera::DisplaySurface> surface_;
  lv_display_t *display_ = nullptr;
  std::vector<uint8_t> buffer_;
  Screen screen_;
  int refreshRate_ = 30;
  int topHeight_ = 0;
  std::string error_;
  bool shown_ = true, headless_ = false, mediaPaused_ = false, pending_ = false, presented_ = false;

public:
  Display(const Screen &s, bool headless);
  ~Display();
  void visible(bool value);
  void topHeight(int height);
  int topHeight() const { return topHeight_; }
  bool supportsTopRegion() const { return headless_ || (surface_ && surface_->supportsTopRegion()); }
  void mediaHandoff(bool release);
  void check();
  // Call on the LVGL UI thread. Unsupported values restore the geometry default.
  void setRefreshRate(int hz);
  int refreshRate() const { return refreshRate_; }
  // Diagnostic reads of the actual LVGL timers; zero means no timer exists.
  uint32_t refreshPeriod() const;
  uint32_t animationPeriod() const;
  uint32_t inputPeriod() const;
  bool presented() const {return presented_;}
  bool pending() const {return pending_;}
  bool visible() const { return shown_; }
  std::array<uint64_t,4> stacking() {return surface_?surface_->stacking():std::array<uint64_t,4>{{0,0,0,0}};}
  bool systemOverlaySafe() const { return headless_ || (surface_ && surface_->systemOverlaySafe()); }
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
  ~Input();
  void touch(int x, int y, bool down);
  void release();
  void cancel();
  // Outside LVGL callbacks: revoke a gesture and settle only elastic overflow.
  void interrupt();
};
class Bridge {
  int fd_ = -1;
  int generation_ = 0;
  uint64_t requestSequence_ = 0;

public:
  explicit Bridge(int fd) : fd_(fd) {}
  ~Bridge();
  Bridge(const Bridge &) = delete;
  Bridge &operator=(const Bridge &) = delete;
  bool connected() const { return fd_ >= 0; }
  int generation() const { return generation_; }
  void generation(int n) { generation_ = n; }
  bool send(const std::string &type, Json data = Json::object());
  std::vector<Json> receive();
};
lv_font_t *font(int size, bool bold = false);
void initializeFonts(const std::string &path, const std::string &fallbackPath);
void shutdownFonts();
// Call only after the display has finished drawing a frame, never from a glyph callback.
void trimFontCache();
Json fontCacheInfo();
// UI-thread counters; no framebuffer capture or database work.
Json displayTimingInfo();
} // namespace penchat
