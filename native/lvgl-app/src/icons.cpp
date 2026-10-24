#include "icons.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
namespace penchat {
namespace {
struct Icon {
  std::string name;
  uint32_t ink;
};
// Rasterize the native geometry once, then mirror pixel coverage. Snapping
// separate line endpoints and a circle to even-sized button centers gives
// different half-pixel phases, making a mathematically regular gear look tilted.
struct GearMask {
  std::vector<uint8_t> alpha;
  lv_image_dsc_t image{};
  GearMask(int size, double scale) : alpha(size * size) {
    struct Point { double x, y; } points[36];
    const double angles[] = {-30, -19, -10, 10, 19, 30};
    const double radii[] = {8, 8, 11, 11, 8, 8};
    for (int i = 0; i < 36; ++i) {
      const double a = (-90 + i / 6 * 60 + angles[i % 6]) * .0174532925199433;
      points[i] = {std::cos(a) * radii[i % 6] * scale,
                   std::sin(a) * radii[i % 6] * scale};
    }
    const double inner = 3.5 * scale;
    const double center = size * .5;
    constexpr int samples = 8;
    for (int y = 0; y < size / 2; ++y) {
      for (int x = 0; x < size / 2; ++x) {
        int covered = 0;
        for (int sy = 0; sy < samples; ++sy) {
          for (int sx = 0; sx < samples; ++sx) {
            const double px = x + (sx + .5) / samples - center;
            const double py = y + (sy + .5) / samples - center;
            const double r2 = px * px + py * py;
            bool hit = r2 >= (inner - 1) * (inner - 1) &&
                       r2 <= (inner + 1) * (inner + 1);
            for (int i = 0; !hit && i < 36; ++i) {
              const auto a = points[i], b = points[(i + 1) % 36];
              const double dx = b.x - a.x, dy = b.y - a.y;
              const double length2 = dx * dx + dy * dy;
              if (length2 < 1e-10) continue;
              const double t = std::max(0., std::min(1.,
                  ((px - a.x) * dx + (py - a.y) * dy) / length2));
              const double ex = px - a.x - t * dx, ey = py - a.y - t * dy;
              hit = ex * ex + ey * ey <= 1.; // 2px stroke at both icon sizes
            }
            covered += hit;
          }
        }
        const auto value = uint8_t((covered * 255 + samples * samples / 2) /
                                   (samples * samples));
        for (int my : {y, size - 1 - y})
          for (int mx : {x, size - 1 - x}) alpha[my * size + mx] = value;
      }
    }
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_A8;
    image.header.w = image.header.h = size;
    image.header.stride = size;
    image.data_size = alpha.size();
    image.data = alpha.data();
  }
};
void paint(lv_event_t *event) {
  auto icon = static_cast<Icon *>(lv_event_get_user_data(event));
  auto object = static_cast<lv_obj_t *>(lv_event_get_target(event));
  auto layer = lv_event_get_layer(event);
  lv_area_t bounds;
  lv_obj_get_coords(object, &bounds);
  const float cx = (bounds.x1 + bounds.x2) * .5f, cy = (bounds.y1 + bounds.y2) * .5f;
  const float scale =
      std::min(lv_obj_get_width(object), lv_obj_get_height(object)) <= 30 ? 0.75f : 1.f;
  const auto ink = lv_color_hex(icon->ink);
  lv_draw_line_dsc_t visual;
  lv_draw_line_dsc_init(&visual);
  lv_obj_init_draw_line_dsc(object, LV_PART_MAIN, &visual);
  const auto opacity = visual.opa;
  auto line = [&](float x, float y, float u, float v) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = ink;
    d.opa = opacity;
    d.width = 2;
    d.round_start = d.round_end = 1;
    d.p1 = {int32_t(std::lround(cx + x * scale)), int32_t(std::lround(cy + y * scale))};
    d.p2 = {int32_t(std::lround(cx + u * scale)), int32_t(std::lround(cy + v * scale))};
    lv_draw_line(layer, &d);
  };
  auto rect = [&](float x, float y, float w, float h, int radius, bool fill = false) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = radius;
    d.bg_color = d.border_color = ink;
    d.bg_opa = fill ? opacity : LV_OPA_TRANSP;
    d.border_opa = fill ? LV_OPA_TRANSP : opacity;
    d.border_width = 2;
    lv_area_t a{int32_t(std::lround(cx + x * scale)),
                int32_t(std::lround(cy + y * scale)),
                int32_t(std::lround(cx + (x + w) * scale)),
                int32_t(std::lround(cy + (y + h) * scale))};
    lv_draw_rect(layer, &d, &a);
  };
  auto circle = [&](float x, float y, float r) {
    rect(x - r, y - r, 2 * r, 2 * r, LV_RADIUS_CIRCLE);
  };
  auto arc = [&](int start, int end, int radius) {
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = ink;
    d.opa = opacity;
    d.width = 2;
    d.rounded = 1;
    d.center = {int32_t(std::lround(cx)), int32_t(std::lround(cy))};
    d.radius = std::lround(radius * scale);
    d.start_angle = start;
    d.end_angle = end;
    lv_draw_arc(layer, &d);
  };
  const auto &k = icon->name;
  if(k=="globe") {
    circle(0,0,10);line(-10,0,10,0);line(-8,-5,8,-5);line(-8,5,8,5);
    rect(-4,-10,8,20,LV_RADIUS_CIRCLE);
  } else if(k=="clipboard") {
    rect(-8,-8,16,18,2);rect(-4,-11,8,5,2);line(-4,0,4,0);line(-4,5,2,5);
  } else if(k=="selection-handle") {line(0,-10,0,0);circle(0,4,4);}
  else if(k=="eye"||k=="eye-off") {rect(-10,-6,20,12,LV_RADIUS_CIRCLE);circle(0,0,3);if(k=="eye-off")line(-10,10,10,-10);}
  else if (k == "menu") {
    line(-9, -4, 9, -4);
    line(-9, 4, 3, 4);
  } else if (k == "close") {
    line(-6, -6, 6, 6);
    line(-6, 6, 6, -6);
  } else if (k == "minus") {
    line(-8, 0, 8, 0);
  } else if (k == "plus") {
    line(-8, 0, 8, 0);
    line(0, -8, 0, 8);
  } else if (k == "back" || k == "left" || k == "right") {
    const int s = k == "right" ? 1 : -1;
    line(-s * 8, 0, s * 8, 0);
    line(s * 2, -6, s * 8, 0);
    line(s * 8, 0, s * 2, 6);
  } else if (k == "up" || k == "down") {
    const int s = k == "up" ? -1 : 1;
    line(0, -s * 8, 0, s * 8);
    line(-6, s * 2, 0, s * 8);
    line(0, s * 8, 6, s * 2);
  } else if (k == "chevron-right") {
    line(-2, -5, 3, 0);
    line(3, 0, -2, 5);
  } else if (k == "chevron-down") {
    line(-5, -2, 0, 3);
    line(0, 3, 5, -2);
  } else if (k == "stop")
    rect(-6, -6, 12, 12, 3, true);
  else if (k == "gauge") {
    arc(140, 400, 11);
    circle(0, 1, 2);
    line(-1, -1, -5, -6);
  } else if (k == "history") {
    arc(215, 535, 10);
    line(-9, -6, -9, 0);
    line(-9, 0, -3, 0);
    line(0, -5, 0, 0);
    line(0, 0, 4, 3);
  } else if (k == "settings") {
    static const GearMask regular(26, 1.), small(20, .75);
    const auto &mask = scale < 1 ? small.image : regular.image;
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = &mask;
    d.recolor = ink;
    d.recolor_opa = LV_OPA_COVER;
    d.opa = opacity;
    const int x = bounds.x1 + (lv_obj_get_width(object) - mask.header.w) / 2;
    const int y = bounds.y1 + (lv_obj_get_height(object) - mask.header.h) / 2;
    lv_area_t area{x, y, x + int(mask.header.w) - 1, y + int(mask.header.h) - 1};
    lv_draw_image(layer, &d, &area);
  } else if (k == "camera") {
    rect(-10, -6, 20, 15, 3);
    line(-5, -6, -3, -9);
    line(-3, -9, 3, -9);
    line(3, -9, 5, -6);
    circle(0, 1, 4);
  } else if (k == "album") {
    rect(-10, -9, 20, 18, 3);
    circle(4, -3, 2);
    line(-8, 6, -3, 0);
    line(-3, 0, 2, 5);
    line(2, 5, 5, 2);
    line(5, 2, 8, 5);
  } else if (k == "trash") {
    line(-9, -6, 9, -6);
    rect(-3, -10, 6, 4, 2);
    line(-6, -3, -5, 9);
    line(-5, 9, 5, 9);
    line(5, 9, 6, -3);
    line(-2, -2, -2, 5);
    line(2, -2, 2, 5);
  } else if (k == "pin") {
    line(-5, -8, 5, -8);
    line(-4, -8, -4, -1);
    line(4, -8, 4, -1);
    line(-4, -1, -7, 3);
    line(4, -1, 7, 3);
    line(-7, 3, 7, 3);
    line(0, 3, 0, 10);
  } else if (k == "newchat") {
    line(-8, -8, -1, -8);
    line(-8, -8, -8, 8);
    line(-8, 8, 8, 8);
    line(8, 8, 8, 1);
    line(-2, 3, -1, -1);
    line(-1, -1, 7, -9);
    line(7, -9, 10, -6);
    line(10, -6, 2, 2);
    line(2, 2, -2, 3);
    line(5, -7, 8, -4);
  } else if (k == "shift" || k == "shift-lock") {
    line(-9, 0, 0, -9);
    line(0, -9, 9, 0);
    line(9, 0, 4, 0);
    line(4, 0, 4, 6);
    line(4, 6, -4, 6);
    line(-4, 6, -4, 0);
    line(-4, 0, -9, 0);
    if (k == "shift-lock")
      line(-4, 10, 4, 10);
  } else if (k == "delete") {
    line(-10, 0, -4, -7);
    line(-4, -7, 10, -7);
    line(10, -7, 10, 7);
    line(10, 7, -4, 7);
    line(-4, 7, -10, 0);
    line(0, -3, 6, 3);
    line(0, 3, 6, -3);
  } else if (k == "enter") {
    line(8, -7, 8, 2);
    line(8, 2, -8, 2);
    line(-3, -3, -8, 2);
    line(-8, 2, -3, 7);
  }
}
} // namespace
void attachIcon(lv_obj_t *object, const std::string &name, uint32_t ink) {
  auto data = new Icon{name, ink};
  lv_obj_add_event_cb(object, paint, LV_EVENT_DRAW_MAIN, data);
  lv_obj_add_event_cb(
      object,
      [](lv_event_t *e) { delete static_cast<Icon *>(lv_event_get_user_data(e)); },
      LV_EVENT_DELETE, data);
}
} // namespace penchat
