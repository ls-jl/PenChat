#pragma once
#include "graphic/graphic.h"
#include <cairo.h>
namespace penchat {
class CairoGraphics : public tex::Graphics2D {
  cairo_t *c_;
  tex::color color_ = tex::black;
  tex::Stroke stroke_;
  const tex::Font *font_ = nullptr;
  float sx_ = 1, sy_ = 1;

public:
  explicit CairoGraphics(cairo_t *c) : c_(c) {}
  void setColor(tex::color) override;
  tex::color getColor() const override { return color_; }
  void setStroke(const tex::Stroke &) override;
  const tex::Stroke &getStroke() const override { return stroke_; }
  void setStrokeWidth(float w) override {
    stroke_.lineWidth = w;
    cairo_set_line_width(c_, w);
  }
  const tex::Font *getFont() const override { return font_; }
  void setFont(const tex::Font *) override;
  void translate(float x, float y) override { cairo_translate(c_, x, y); }
  void scale(float x, float y) override {
    cairo_scale(c_, x, y);
    sx_ *= x;
    sy_ *= y;
  }
  void rotate(float r) override { cairo_rotate(c_, r); }
  void rotate(float r, float x, float y) override {
    cairo_translate(c_, x, y);
    cairo_rotate(c_, r);
    cairo_translate(c_, -x, -y);
  }
  void reset() override {
    cairo_identity_matrix(c_);
    sx_ = sy_ = 1;
  }
  float sx() const override { return sx_; }
  float sy() const override { return sy_; }
  void drawChar(wchar_t, float, float) override;
  void drawText(const std::wstring &, float, float) override;
  void drawLine(float x, float y, float x2, float y2) override {
    cairo_move_to(c_, x, y);
    cairo_line_to(c_, x2, y2);
    cairo_stroke(c_);
  }
  void drawRect(float x, float y, float w, float h) override {
    cairo_rectangle(c_, x, y, w, h);
    cairo_stroke(c_);
  }
  void fillRect(float x, float y, float w, float h) override {
    cairo_rectangle(c_, x, y, w, h);
    cairo_fill(c_);
  }
  void drawRoundRect(float, float, float, float, float, float) override;
  void fillRoundRect(float, float, float, float, float, float) override;
  cairo_t *context() { return c_; }
};
void setFallbackFont(const std::string &path);
} // namespace penchat
