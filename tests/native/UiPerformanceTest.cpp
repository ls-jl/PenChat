#include "app.hpp"
#include "PaintPerformanceFixture.hpp"
#include <cassert>
#include <iostream>

namespace {
lv_obj_t *trackedText = nullptr, *trackedSend = nullptr;
uint64_t textWrites = 0, sendWrites = 0;
}

extern "C" {
void __real_lv_textarea_set_text(lv_obj_t *, const char *);
void __wrap_lv_textarea_set_text(lv_obj_t *object, const char *text) {
  if (object == trackedText) ++textWrites;
  __real_lv_textarea_set_text(object, text);
}
void __real_lv_obj_set_style_bg_color(lv_obj_t *, lv_color_t, lv_style_selector_t);
void __wrap_lv_obj_set_style_bg_color(lv_obj_t *object, lv_color_t color, lv_style_selector_t selector) {
  if (object == trackedSend && selector == LV_PART_MAIN) ++sendWrites;
  __real_lv_obj_set_style_bg_color(object, color, selector);
}
}

namespace penchat {
struct UiPerformanceTest {
  static void run(App &a, bool baseline) {
    const auto hits = a.editorLayoutCacheHits_, resolves = a.editorLayoutResolves_;
    for (int i = 0; i < 64; ++i) a.editorTick();
    const auto inactiveHits = a.editorLayoutCacheHits_ - hits;
    const auto inactiveResolves = a.editorLayoutResolves_ - resolves;
    if (!baseline) assert(inactiveHits == 0 && inactiveResolves == 0);

    a.clear();
    a.view_ = "chat";
    a.draft_ = "Initial 中文 text";
    a.build();
    // Keep this fixture entirely native: no OCR registration, pen key lease or
    // transport fixture is required to exercise the real retained textarea.
    a.editor_ = EditorState{};
    a.editor_.active = true;
    a.editor_.id = "ui-performance-editor";
    a.editor_.text = a.draft_;
    a.editor_.position(3);
    a.editor_.layout = 1;
    a.editor_.chinese = false;
    a.editor_.limit = 6000;
    a.editor_.multiline = true;
    a.editorInlineChat_ = a.layout().portrait();
    if (a.editorInlineChat_) {
      a.editorTarget_ = a.composer_;
      a.editorTargetKey_ = "chat-composer";
      a.editorTargetView_ = "chat";
      a.editorTargetHeight_ = lv_obj_get_height(a.composer_);
      a.editorComposerOpenHeight_ = (lv_obj_get_height(a.chatControls_) * 7 + 2) / 4;
    }
    a.touchLeaseActive_ = true;
    a.editorBuild();
    assert(a.editorText_ && a.editorRoot_);
    trackedText = a.editorText_;
    auto keys = a.editorKeys_;
    const auto builds = a.editorBuildCount_;
    const bool portrait = a.layout().portrait();

    a.editor_.text = "Changed 中文 value";
    a.editor_.select(1, 5);
    textWrites = 0;
    a.editorSyncText(true);
    const auto changedWrites = textWrites;
    assert(changedWrites == (baseline && portrait ? 2 : 1));
    assert(a.editor_.text == lv_textarea_get_text(a.editorText_));
    assert(lv_textarea_get_cursor_pos(a.editorText_) == 5);
    auto label = lv_textarea_get_label(a.editorText_);
    assert(lv_label_get_text_selection_start(label) == 1);
    assert(lv_label_get_text_selection_end(label) == 5);

    // A repeated sync still restores the requested cursor and selection, but
    // need not clear and reconstruct identical text to do so.
    a.editor_.select(2, 7);
    textWrites = 0;
    a.editorSyncText(true);
    const auto unchangedWrites = textWrites;
    assert(unchangedWrites == (baseline ? 1 : 0));
    assert(lv_textarea_get_cursor_pos(a.editorText_) == 7);
    assert(lv_label_get_text_selection_start(label) == 2);
    assert(lv_label_get_text_selection_end(label) == 7);

    a.editor_.text = std::string(1800, 'x') + " 中文 tail";
    a.editor_.position(editLength(a.editor_.text));
    a.editorFollowCursor_ = true;
    textWrites = 0;
    a.editorSyncText(true);
    const auto longWrites = textWrites;
    assert(longWrites == (baseline && portrait ? 2 : 1));
    assert(a.editor_.text == lv_textarea_get_text(a.editorText_));
    assert(size_t(lv_textarea_get_cursor_pos(a.editorText_)) == a.editor_.cursor);
    assert(lv_label_get_text_selection_start(label) == LV_LABEL_TEXT_SELECTION_OFF);
    assert(lv_label_get_text_selection_end(label) == LV_LABEL_TEXT_SELECTION_OFF);

    // Preserve the existing meaning of text=false. Non-inline editors leave
    // their text alone; inline editors may attach through editorViewport first.
    const auto painted = std::string(lv_textarea_get_text(a.editorText_));
    a.editor_.text = "Different contents";
    a.editor_.position(2);
    textWrites = 0;
    a.editorSyncText(false);
    assert(textWrites == (portrait ? 1 : 0));
    assert(std::string(lv_textarea_get_text(a.editorText_)) == (portrait ? a.editor_.text : painted));
    assert(lv_textarea_get_cursor_pos(a.editorText_) == 2);
    a.editorSyncText(true);

    uint64_t idleWrites = 0, pressedWrites = 0, enabledWrites = 0, disabledWrites = 0, themeWrites = 0;
    if (portrait) {
      a.editorTick();
      trackedSend = lv_obj_get_child(a.chatControls_, -1);
      sendWrites = 0;
      for (int i = 0; i < 64; ++i) a.editorTick();
      idleWrites = sendWrites;
      assert(idleWrites == (baseline ? 64 : 0));
      assert(lv_obj_is_clickable(trackedSend));

      lv_obj_add_state(trackedSend, LV_STATE_PRESSED);
      const auto pressed = lv_obj_get_style_bg_color(trackedSend, LV_PART_MAIN);
      sendWrites = 0;
      for (int i = 0; i < 64; ++i) a.editorTick();
      pressedWrites = sendWrites;
      assert(pressedWrites == (baseline ? 64 : 0));
      assert(lv_color_eq(lv_obj_get_style_bg_color(trackedSend, LV_PART_MAIN), pressed));
      lv_obj_remove_state(trackedSend, LV_STATE_PRESSED);

      a.editor_.text.clear();
      a.editor_.position(0);
      a.editorSyncText(true);
      sendWrites = 0;
      a.editorTick();
      disabledWrites = sendWrites;
      assert(disabledWrites == 1 && !lv_obj_is_clickable(trackedSend));
      lv_style_value_t color;
      assert(lv_obj_get_local_style_prop(trackedSend, LV_STYLE_BG_COLOR, &color, LV_PART_MAIN) == LV_STYLE_RES_FOUND);
      assert(lv_color_eq(color.color, lv_color_hex(theme::palette().border)));
      a.editorSendAssets_ = true;
      sendWrites = 0;
      a.editorTick();
      enabledWrites = sendWrites;
      assert(enabledWrites == 1 && lv_obj_is_clickable(trackedSend));
      assert(lv_obj_get_local_style_prop(trackedSend, LV_STYLE_BG_COLOR, &color, LV_PART_MAIN) == LV_STYLE_RES_FOUND);
      assert(lv_color_eq(color.color, lv_color_hex(theme::palette().text)));
      const auto originalMode = theme::mode();
      theme::setMode(originalMode == theme::Mode::Dark ? theme::Mode::Light : theme::Mode::Dark);
      sendWrites = 0;
      a.editorTick();
      themeWrites = sendWrites;
      assert(themeWrites == 1);
      assert(lv_obj_get_local_style_prop(trackedSend, LV_STYLE_BG_COLOR, &color, LV_PART_MAIN) == LV_STYLE_RES_FOUND);
      assert(lv_color_eq(color.color, lv_color_hex(theme::palette().text)));
      theme::setMode(originalMode);
      a.editorTick();
    }
    assert(a.editorBuildCount_ == builds);
    assert(a.editorKeys_.size() == keys.size());
    for (size_t i = 0; i < keys.size(); ++i) assert(a.editorKeys_[i].object == keys[i].object);
    std::cout << "{\"size\":[" << a.screen_.width << ',' << a.screen_.height
              << "],\"inactive_layout_hits\":" << inactiveHits
              << ",\"inactive_layout_resolves\":" << inactiveResolves
              << ",\"changed_text_writes\":" << changedWrites
              << ",\"unchanged_text_writes\":" << unchangedWrites
              << ",\"long_text_writes\":" << longWrites
              << ",\"idle_send_writes_64_ticks\":" << idleWrites
              << ",\"pressed_send_writes_64_ticks\":" << pressedWrites
              << ",\"disable_send_writes\":" << disabledWrites
              << ",\"enable_send_writes\":" << enabledWrites
              << ",\"theme_send_writes\":" << themeWrites << "}\n";
    trackedText = trackedSend = nullptr;
    a.editorFinish(false);
    a.clear();
  }
};
}

int main(int argc, char **argv) {
  using namespace penchat;
  const bool baseline = argc > 1 && std::string(argv[argc - 1]) == "--baseline";
  const int sizeArgs = argc - (baseline ? 1 : 0);
  assert(sizeArgs == 3 || sizeArgs == 5);
  const auto size = sizeArgs == 5 ? std::make_pair(std::stoi(argv[3]), std::stoi(argv[4]))
                                : std::make_pair(568, 1210);
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return uint32_t(ticks()); });
  initializeFonts(join(argv[1], ".deps/chinese.ttf"), join(argv[1], "assets/fonts/DejaVuSans.ttf"));
  // MicroTeX owns process-global teardown state, so run each geometry in its
  // own invocation, matching the existing UiControlsTest process lifetime.
  {
    const auto directory = join(argv[2], std::to_string(size.first));
    ensureDirectory(directory);
    Screen screen; screen.width = size.first; screen.height = size.second;
    Display display(screen, true);
    Input input;
    Bridge bridge(-1);
    Database db(join(directory, "penchat.db"));
    App app(db, bridge, display, input, screen, argv[1], directory,
            join(argv[1], ".deps/chinese.ttf"), join(argv[1], ".deps/microtex/res"), false);
    UiPerformanceTest::run(app, baseline);
    PaintPerformanceTest::run(app);
  }
  lv_deinit();
  shutdownFonts();
  std::cout << "UI_PERFORMANCE_SETTER_COUNTS_AND_EDITOR_STATE_OK\n";
}
