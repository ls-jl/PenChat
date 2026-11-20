#include "platform.hpp"
#include "refresh_policy.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <vector>

namespace {
std::vector<int> surfaceRates;
unsigned surfaces = 0;
class Surface final : public camera::DisplaySurface {
public:
  Surface() { ++surfaces; }
  bool present(const std::uint8_t*, int, int) override { return true; }
  void hide() override {}
  bool releaseOnHide() const override { return true; }
  void setRefreshRate(int rate) override { surfaceRates.push_back(rate); }
};
std::map<lv_timer_t*, unsigned> calls;
void countTimer(lv_timer_t* timer) { ++calls[timer]; }
void advance(unsigned ms) {
  for (unsigned i = 0; i < ms; ++i) { lv_tick_inc(1); lv_timer_handler(); }
}
void checkTimers(penchat::Display& display, int rate) {
  assert(display.refreshPeriod() == penchat::refresh::periodMs(rate));
  assert(display.animationPeriod() == penchat::refresh::periodMs(rate));
  assert(display.inputPeriod() == penchat::refresh::periodMs(rate));
  std::vector<lv_timer_t*> timers{lv_display_get_refr_timer(display.handle()), lv_anim_get_timer()};
  for (auto input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
    timers.push_back(lv_indev_get_read_timer(input));
  assert(timers.size() >= 3);
  for (auto timer : timers) {
    assert(timer);
    calls[timer] = 0;
    // Count actual LVGL scheduler invocations without widget invalidation or
    // animation suspension changing whether the timer remains active.
    lv_timer_set_cb(timer, countTimer);
    lv_timer_resume(timer);
    lv_timer_reset(timer);
  }
  advance(198);
  for (auto timer : timers) assert(calls[timer] == 198 / penchat::refresh::periodMs(rate));
}
void checkAnimation(int rate) {
  lv_init();
  {
    penchat::Screen screen; screen.width = 80; screen.height = 40;
    penchat::Display display(screen, true);
    display.setRefreshRate(rate);
    struct Animation { unsigned steps = 0; int value = -1; bool done = false; } state;
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &state);
    lv_anim_set_duration(&animation, 198);
    lv_anim_set_values(&animation, 0, 1000);
    lv_anim_set_exec_cb(&animation, [](void* data, int32_t value) {
      auto& state = *static_cast<Animation*>(data); ++state.steps; state.value = value;
    });
    lv_anim_set_completed_cb(&animation, [](lv_anim_t* animation) {
      static_cast<Animation*>(animation->var)->done = true;
    });
    lv_anim_start(&animation);
    advance(197);
    assert(!state.done);
    advance(penchat::refresh::periodMs(rate) + 1);
    assert(state.done && state.value == 1000);
    // Both settings retain elapsed-time animation duration, with more samples
    // at 60Hz. The early-apply callback adds one initial sample.
    assert(state.steps >= (rate == 60 ? 12u : 6u));
    assert(state.steps <= (rate == 60 ? 15u : 8u));
  }
  lv_deinit();
}
}
namespace camera {
std::unique_ptr<DisplaySurface> makeDisplaySurface(const DisplayGeometry&) {
  return std::unique_ptr<DisplaySurface>(new Surface);
}
}
int main() {
  using namespace penchat::refresh;
  assert(defaultRate(960, 266) == 30);
  assert(defaultRate(568, 1210) == 60);
  assert(defaultRate(400, 400) == 30);
  for (int invalid : {0, -1, 1, 16, 33, 59, 61, 120}) {
    assert(!supportedRate(invalid));
    assert(resolveRate(invalid, 960, 266) == 30);
    assert(resolveRate(invalid, 568, 1210) == 60);
  }
  assert(resolveRate(30, 568, 1210) == 30 && resolveRate(60, 960, 266) == 60);
  assert(periodMs(30) == 33 && periodMs(60) == 16);
  Pacing pacing;
  assert(pacing.due(100, 30));
  pacing.presented(100);
  assert(!pacing.due(132, 30) && pacing.remainingMs(132, 30) == 1);
  assert(pacing.due(133, 30));
  assert(pacing.due(116, 60) && !pacing.due(115, 60));
  pacing.presented(UINT32_MAX - 5);
  assert(!pacing.due(9, 60) && pacing.due(10, 60));
  assert(!pacing.due(26, 30) && pacing.due(27, 30));

  lv_init();
  {
    penchat::Screen screen; screen.width = 80; screen.height = 40;
    penchat::Display display(screen, false);
    // The release-on-hide fixture represents an upper overlay. Its surface
    // may be probed at construction, but presenting needs foreground approval.
    assert(!display.visible()&&!display.presented());
    display.visible(true);
    assert(display.inputPeriod() == 0);
    penchat::Input input;
    assert(display.refreshRate() == 30 && surfaceRates.back() == 30);
    checkTimers(display, 30);
    display.setRefreshRate(60);
    assert(display.refreshRate() == 60 && surfaceRates.back() == 60);
    checkTimers(display, 60);
    // Diagnostic getters reflect timer state rather than the configured rate.
    lv_timer_set_period(lv_display_get_refr_timer(display.handle()), 7);
    lv_timer_set_period(lv_anim_get_timer(), 9);
    lv_timer_set_period(lv_indev_get_read_timer(lv_indev_get_next(nullptr)), 11);
    assert(display.refreshPeriod() == 7 && display.animationPeriod() == 9 && display.inputPeriod() == 11);
    display.setRefreshRate(60);
    {
      penchat::Input laterInput;
      checkTimers(display, 60);
      display.setRefreshRate(30); checkTimers(display, 30);
      display.setRefreshRate(60); checkTimers(display, 60);
    }
    display.mediaHandoff(true);
    const auto count = surfaces;
    display.mediaHandoff(false);
    assert(surfaces == count + 1 && surfaceRates.back() == 60);
    display.visible(false); display.visible(true);
    assert(surfaces == count + 2 && surfaceRates.back() == 60);
    display.setRefreshRate(30); checkTimers(display, 30);
  }
  lv_deinit();
  lv_init();
  {
    penchat::Screen screen; screen.width = 40; screen.height = 80;
    penchat::Display display(screen, true);
    // Inputs created after Display inherit the initial portrait timer target.
    penchat::Input input;
    assert(display.refreshRate() == 60);
    checkTimers(display, 60);
    display.setRefreshRate(30); checkTimers(display, 30);
    display.setRefreshRate(120);
    assert(display.refreshRate() == 60); checkTimers(display, 60);
  }
  lv_deinit();
  checkAnimation(30); checkAnimation(60);
  penchat::shutdownFonts();
  std::cout << "REFRESH_POLICY_LVGL_TIMERS_ANIMATION_AND_SURFACE_RECREATION_OK\n";
}
