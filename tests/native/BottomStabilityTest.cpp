#include "app.hpp"
#include <cassert>
#include <iostream>

namespace penchat {
struct BottomStabilityTest {
  static void wait(App &a) {
    std::this_thread::sleep_for(std::chrono::milliseconds(135));
    a.composerShortcutTick();
    lv_obj_update_layout(a.screenObj_);
  }
  static void position(App &a, int bottom) {
    const int maximum = lv_obj_get_scroll_y(a.body_) + lv_obj_get_scroll_bottom(a.body_);
    a.chatLayout_ = true;
    lv_obj_scroll_to_y(a.body_, std::max(0, maximum - bottom), LV_ANIM_OFF);
    a.chatLayout_ = false;
    a.chatScrolled();
    lv_obj_update_layout(a.screenObj_);
  }
  static void controls(App &a, bool atBottom) {
    assert(a.chatBottomState_.atBottom() == atBottom);
    assert(lv_obj_is_hidden(a.toBottom_) == atBottom);
    assert(a.chatComposerVisible() == (a.layout().portrait() || atBottom));
  }
  static void settleControls(App &a, bool atBottom) {
    // State confirmation is asserted separately before entering here. Drive
    // real LVGL timers through the 160ms motion, then verify final geometry
    // over further frames instead of checking only the visibility booleans.
    const int64_t until = ticks() + 200;
    while (ticks() < until) {
      lv_timer_handler();
      a.composerShortcutTick();
      lv_obj_update_layout(a.screenObj_);
      controls(a, atBottom);
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    const int expected = a.chatComposerVisible() ? a.chatComposerY() :
        a.screen_.height + a.uiMetrics_.composer().hiddenOffset;
    const int y = lv_obj_get_y(a.chatControls_);
    assert(y == expected && a.chatControlsTarget_ == expected);
    lv_area_t button;
    lv_obj_get_coords(a.toBottom_, &button);
    const int bottom = lv_obj_get_scroll_bottom(a.body_);
    for (int i = 0; i < 12; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
      lv_timer_handler();
      a.composerShortcutTick();
      lv_obj_update_layout(a.screenObj_);
      controls(a, atBottom);
      lv_area_t frame;
      lv_obj_get_coords(a.toBottom_, &frame);
      assert(frame.x1 == button.x1 && frame.y1 == button.y1 &&
             frame.x2 == button.x2 && frame.y2 == button.y2);
      assert(lv_obj_get_y(a.chatControls_) == y);
      assert(lv_obj_get_scroll_bottom(a.body_) == bottom);
    }
  }
  static void run(App &a) {
    a.view_ = "chat";
    a.build();
    a.chatLayout_ = true;
    a.rasters_.clear();
    lv_obj_clean(a.messageContent_);
    a.box(a.messageContent_, 0, 0, a.layout().chatMessageWidth(), a.screen_.height * 4);
    a.hasConversationContent_ = true;
    lv_obj_update_layout(a.screenObj_);
    lv_obj_scroll_to_y(a.body_, LV_COORD_MAX, LV_ANIM_OFF);
    a.chatScroll_ = lv_obj_get_scroll_y(a.body_);
    a.chatLayout_ = false;
    a.syncChatBottomState(true);
    a.updateChatControls(false);
    controls(a, true);
    const int padding = lv_obj_get_style_pad_bottom(a.messageContent_, LV_PART_MAIN);
    // Reproduce the old feedback trigger using the actual App/LVGL scroll
    // extent. Repeated control/layout updates must leave both it and padding
    // unchanged, including just outside the former 10px boundary.
    position(a, a.uiMetrics_.dp(11));
    const int boundary = lv_obj_get_scroll_bottom(a.body_);
    for (int i = 0; i < 20; ++i) {
      a.updateChatControls(false);
      lv_obj_update_layout(a.screenObj_);
      assert(lv_obj_get_scroll_bottom(a.body_) == boundary);
      assert(lv_obj_get_style_pad_bottom(a.messageContent_, LV_PART_MAIN) == padding);
      controls(a, true);
    }
    settleControls(a, true);
    position(a, a.uiMetrics_.dp(24) + 30);
    controls(a, true); // Both controls wait before leaving the bottom.
    wait(a);
    controls(a, false);
    settleControls(a, false);
    assert(lv_obj_get_style_pad_bottom(a.messageContent_, LV_PART_MAIN) == padding);
    const int away = lv_obj_get_scroll_bottom(a.body_);
    for (int i = 0; i < 20; ++i) {
      a.updateChatControls(false);
      lv_obj_update_layout(a.screenObj_);
      assert(lv_obj_get_scroll_bottom(a.body_) == away);
      controls(a, false);
    }
    // A direction reversal remains away; reaching the entry threshold needs
    // its own sustained confirmation for the button and composer together.
    position(a, a.uiMetrics_.dp(16));
    wait(a);
    controls(a, false);
    position(a, 0);
    controls(a, false);
    wait(a);
    controls(a, true);
    settleControls(a, true);
    // Composer growth uses one fixed reserve and keeps a following reader at
    // the latest bottom instead of producing an artificial scroll excursion.
    a.chatFollowing_ = true;
    a.draft_ = std::string(1500, 'x');
    a.layoutChatComposer();
    lv_obj_update_layout(a.screenObj_);
    assert(a.atLatestBottom());
    controls(a, true);
    // Suspension must cancel an unfinished transition rather than letting the
    // hidden time complete it. Resume starts a fresh confirmation interval.
    position(a, a.uiMetrics_.dp(24) + 30);
    a.visible_ = false;
    a.composerShortcutTick();
    wait(a);
    controls(a, true);
    a.visible_ = true;
    a.composerShortcutTick();
    controls(a, true);
    wait(a);
    controls(a, false);
    a.jumpToBottom();
    assert(a.atLatestBottom() && a.chatBottomState_.atBottom());
    assert(lv_obj_is_hidden(a.toBottom_));
    assert(a.chatComposerVisible());
    a.clear();
  }
};
} // namespace penchat

int main(int argc, char **argv) {
  using namespace penchat;
  assert(argc == 6);
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return uint32_t(ticks()); });
  initializeFonts(join(argv[1], ".deps/chinese.ttf"), join(argv[1], "assets/fonts/DejaVuSans.ttf"));
  {
    Screen screen; screen.width = std::stoi(argv[4]); screen.height = std::stoi(argv[5]);
    Display display(screen, true); Input input; Bridge bridge(-1);
    ensureDirectory(argv[2]); Database db(join(argv[2], "penchat.db"));
    App app(db, bridge, display, input, screen, argv[1], argv[2], join(argv[1], ".deps/chinese.ttf"),
            join(argv[1], ".deps/microtex/res"), false, Json::object(), UiMetrics(100, std::stoi(argv[3])));
    BottomStabilityTest::run(app);
  }
  lv_deinit();
  shutdownFonts();
  std::cout << "BOTTOM_STABILITY_APP_FIXTURE_OK\n";
}
