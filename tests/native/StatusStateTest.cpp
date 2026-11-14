#include "empty_state.hpp"
#include "theme.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cassert>
#include <iostream>

using namespace penchat;
namespace {
// Use the shipped Chinese font's real glyph advances for native LVGL wrapping.
// This test exercises layout only, so no raster buffers or display are needed.
struct MeasuredFont {
  FT_Face face = nullptr;
  lv_font_t value{};
  MeasuredFont(FT_Library library, const char *path, int size) {
    assert(!FT_New_Face(library, path, 0, &face));
    assert(!FT_Set_Pixel_Sizes(face, 0, size));
    value.user_data = this;
    value.line_height = (face->size->metrics.height + 63) / 64;
    value.base_line = -(face->size->metrics.descender >> 6);
    value.get_glyph_dsc = [](const lv_font_t *font, lv_font_glyph_dsc_t *glyph, uint32_t cp, uint32_t) {
      auto self = static_cast<MeasuredFont *>(font->user_data);
      if (FT_Load_Char(self->face, cp, FT_LOAD_DEFAULT)) return false;
      glyph->adv_w = std::max(0L, self->face->glyph->advance.x / 64);
      glyph->resolved_font = font;
      glyph->format = LV_FONT_GLYPH_FORMAT_A8;
      return true;
    };
  }
  ~MeasuredFont() { FT_Done_Face(face); }
};
void checkBounds(lv_obj_t *root) {
  lv_obj_update_layout(root);
  for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
    auto child = lv_obj_get_child(root, i);
    if (lv_obj_is_hidden(child)) continue;
    assert(lv_obj_get_x(child) >= 0 && lv_obj_get_y(child) >= 0);
    assert(lv_obj_get_x(child) + lv_obj_get_width(child) <= lv_obj_get_width(root));
    assert(lv_obj_get_y(child) + lv_obj_get_height(child) <= lv_obj_get_height(root));
    if (lv_obj_check_type(child, &lv_label_class))
      assert(lv_label_get_long_mode(child) == LV_LABEL_LONG_MODE_WRAP);
  }
}
} // namespace
int main(int argc, char **argv) {
  assert(argc == 2);
  FT_Library library = nullptr;
  assert(!FT_Init_FreeType(&library));
  lv_init();
  auto display = lv_display_create(960, 1600);
  for (auto mode : {theme::Mode::Dark, theme::Mode::Light}) {
    theme::setMode(mode);
    for (int scale : {75, 100, 125, 150}) {
      const UiMetrics metrics(100, scale);
      MeasuredFont title(library, argv[1], metrics.fontSize(UiFont::EmptyTitle));
      MeasuredFont detail(library, argv[1], metrics.fontSize(UiFont::FormNote));
      for (int width : {240, 320, 568, 640, 960}) {
        const StatusStateSpec spec{StatusKind::Empty, "剪贴板还没有文字",
          "在 PenChat 内长按文字并选择“复制”，之后可在这里点击记录粘贴。", "clipboard"};
        auto compact = statusState(lv_screen_active(), spec, metrics, &title.value, &detail.value, 0, 0, width);
        checkBounds(compact);
        assert(!statusStateFillsViewport(compact));
        assert(lv_obj_is_hidden(lv_obj_get_child(compact, 2)));
        const int textHeight = lv_obj_get_height(compact);
        assert(textHeight < 500); // A compact notice never reserves a page.
        for (int remaining : {1, textHeight, textHeight + metrics.dp(59), textHeight + metrics.dp(60), 1100}) {
          auto full = statusState(lv_screen_active(), spec, metrics, &title.value, &detail.value, 7, 9, width, remaining);
          checkBounds(full);
          assert(lv_obj_get_x(full) == 7 && lv_obj_get_y(full) == 9);
          auto icon = lv_obj_get_child(full, 2);
          assert(lv_obj_is_hidden(icon) == (remaining < textHeight + metrics.dp(48) + metrics.dp(12)));
          if (remaining >= textHeight) assert(lv_obj_get_height(full) == remaining);
          if (!lv_obj_is_hidden(icon)) {
            auto body = lv_obj_get_child(full, 1);
            const int top = lv_obj_get_y(icon);
            const int bottom = lv_obj_get_y(body) + lv_obj_get_height(body);
            assert(std::abs(top - (remaining - bottom)) <= 1);
          }
          assert(arrangeStatusState(full, width + 80));
          checkBounds(full);
          lv_obj_delete(full);
        }
        lv_obj_delete(compact);
      }
      for (auto kind : {StatusKind::Loading, StatusKind::Error, StatusKind::Filtered}) {
        auto root = statusState(lv_screen_active(), {kind,"状态标题","准确说明当前状态与现有操作。","info"},
                                metrics,&title.value,&detail.value,0,0,320,200);
        const auto ink=lv_obj_get_style_text_color(lv_obj_get_child(root,0),LV_PART_MAIN);
        assert(lv_color_to_u32(ink)==lv_color_to_u32(lv_color_hex(kind==StatusKind::Error?theme::palette().danger:theme::palette().text)));
        checkBounds(root);
        lv_obj_delete(root);
      }
    }
  }
  lv_display_delete(display);
  lv_deinit();
  FT_Done_FreeType(library);
  std::cout << "STATUS_STATE_REAL_FONT_LAYOUT_THEME_SCALE_OK\n";
}
