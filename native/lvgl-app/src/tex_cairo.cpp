#include "tex_cairo.hpp"
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <map>
#include <stdexcept>
namespace {
FT_Library library;
std::string fallback;
struct Face {
  FT_Face ft;
  cairo_font_face_t *cairo;
  ~Face() {
    cairo_font_face_destroy(cairo);
    FT_Done_Face(ft);
  }
};
std::map<std::string, std::shared_ptr<Face>> faces;
std::shared_ptr<Face> face(const std::string &path) {
  if (!library && FT_Init_FreeType(&library))
    throw std::runtime_error("math FreeType init failed");
  auto it = faces.find(path);
  if (it != faces.end())
    return it->second;
  FT_Face ft;
  if (FT_New_Face(library, path.c_str(), 0, &ft))
    throw std::runtime_error("Cannot load math font " + path);
  auto f = std::shared_ptr<Face>(new Face{ft, cairo_ft_font_face_create_for_ft_face(ft, 0)});
  faces[path] = f;
  return f;
}
class Font : public tex::Font {
public:
  std::shared_ptr<Face> f;
  float size;
  int style;
  Font(const std::string &path, float s, int st = 0) : f(face(path)), size(s), style(st) {}
  float getSize() const override { return size; }
  tex::sptr<tex::Font> deriveFont(int s) const override {
    auto p = std::make_shared<Font>(*this);
    p->style = s;
    return p;
  }
  bool operator==(const tex::Font &other) const override {
    auto p = dynamic_cast<const Font *>(&other);
    return p && p->f == f && p->size == size && p->style == style;
  }
  bool operator!=(const tex::Font &o) const override { return !(*this == o); }
};
class Layout : public tex::TextLayout {
  std::wstring text_;
  tex::sptr<tex::Font> font_;

public:
  Layout(std::wstring t, tex::sptr<tex::Font> f) : text_(std::move(t)), font_(std::move(f)) {}
  void getBounds(tex::Rect &r) override {
    auto surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    auto c = cairo_create(surf);
    penchat::CairoGraphics g(c);
    g.setFont(font_.get());
    cairo_text_extents_t e;
    cairo_text_extents(c, tex::wide2utf8(text_).c_str(), &e);
    r = tex::Rect(e.x_bearing, e.y_bearing, e.x_advance, e.height);
    cairo_destroy(c);
    cairo_surface_destroy(surf);
  }
  void draw(tex::Graphics2D &g, float x, float y) override {
    g.setFont(font_.get());
    g.drawText(text_, x, y);
  }
};
void rounded(cairo_t *c, float x, float y, float w, float h, float rx, float ry) {
  rx = std::min(rx, w / 2);
  ry = std::min(ry, h / 2);
  if (rx <= 0 || ry <= 0) {
    cairo_rectangle(c, x, y, w, h);
    return;
  }
  cairo_save(c);
  cairo_translate(c, x, y);
  cairo_scale(c, rx, ry);
  cairo_new_sub_path(c);
  cairo_arc(c, w / rx - 1, 1, 1, -1.5707963, 0);
  cairo_arc(c, w / rx - 1, h / ry - 1, 1, 0, 1.5707963);
  cairo_arc(c, 1, h / ry - 1, 1, 1.5707963, 3.1415927);
  cairo_arc(c, 1, 1, 1, 3.1415927, 4.712389);
  cairo_close_path(c);
  cairo_restore(c);
}
} // namespace
namespace tex {
Font *Font::create(const std::string &file, float size) { return new ::Font(file, size); }
sptr<Font> Font::_create(const std::string &, int style, float size) {
  return std::make_shared<::Font>(fallback, size, style);
}
sptr<TextLayout> TextLayout::create(const std::wstring &text, const sptr<Font> &font) {
  return std::make_shared<Layout>(text, font);
}
} // namespace tex
namespace penchat {
void setFallbackFont(const std::string &p) { fallback = p; }
void CairoGraphics::setColor(tex::color v) {
  color_ = v;
  cairo_set_source_rgba(c_, tex::color_r(v) / 255., tex::color_g(v) / 255., tex::color_b(v) / 255.,
                        tex::color_a(v) / 255.);
}
void CairoGraphics::setStroke(const tex::Stroke &s) {
  stroke_ = s;
  cairo_set_line_width(c_, s.lineWidth);
  cairo_set_line_cap(c_, s.cap == tex::CAP_ROUND    ? CAIRO_LINE_CAP_ROUND
                         : s.cap == tex::CAP_SQUARE ? CAIRO_LINE_CAP_SQUARE
                                                    : CAIRO_LINE_CAP_BUTT);
  cairo_set_line_join(c_, s.join == tex::JOIN_ROUND   ? CAIRO_LINE_JOIN_ROUND
                          : s.join == tex::JOIN_BEVEL ? CAIRO_LINE_JOIN_BEVEL
                                                      : CAIRO_LINE_JOIN_MITER);
  cairo_set_miter_limit(c_, std::max(1.f, s.miterLimit));
}
void CairoGraphics::setFont(const tex::Font *f) {
  font_ = f;
  auto cf = dynamic_cast<const ::Font *>(f);
  if (!cf)
    return;
  cairo_set_font_face(c_, cf->f->cairo);
  cairo_set_font_size(c_, cf->size);
}
void CairoGraphics::drawChar(wchar_t cp, float x, float y) { drawText(std::wstring(1, cp), x, y); }
void CairoGraphics::drawText(const std::wstring &t, float x, float y) {
  cairo_move_to(c_, x, y);
  cairo_show_text(c_, tex::wide2utf8(t).c_str());
}
void CairoGraphics::drawRoundRect(float x, float y, float w, float h, float rx, float ry) {
  rounded(c_, x, y, w, h, rx, ry);
  cairo_stroke(c_);
}
void CairoGraphics::fillRoundRect(float x, float y, float w, float h, float rx, float ry) {
  rounded(c_, x, y, w, h, rx, ry);
  cairo_fill(c_);
}
} // namespace penchat
