#include "platform.hpp"
#include "stb_image_write.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fcntl.h>
#include <map>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace penchat {
Display::Display(const Screen &s, bool headless)
    : screen_(s), headless_(headless) {
  buffer_.resize(size_t(s.width) * s.height * 4);
  rgba_.resize(buffer_.size());
  if (!headless_) {
    camera::HoleRect r(0, 0, s.width, s.height, s.width, s.height, s.rotation,
                       0, 0, s.xoffset, s.yoffset);
    overlay_.setHole(r);
    if (!overlay_.open())
      throw std::runtime_error(overlay_.lastError());
  }
  display_ = lv_display_create(s.width, s.height);
  lv_display_set_color_format(display_, LV_COLOR_FORMAT_ARGB8888);
  lv_display_set_buffers(display_, buffer_.data(), nullptr, buffer_.size(),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_user_data(display_, this);
  lv_display_set_flush_cb(
      display_, [](lv_display_t *d, const lv_area_t *a, uint8_t *p) {
        static_cast<Display *>(lv_display_get_user_data(d))->flush(d, a, p);
      });
}
Display::~Display() {
  overlay_.close();
  if (display_)
    lv_display_delete(display_);
}
void Display::flush(lv_display_t *d, const lv_area_t *, uint8_t *) {
  for (size_t i = 0; i < buffer_.size(); i += 4) {
    rgba_[i] = buffer_[i + 2];
    rgba_[i + 1] = buffer_[i + 1];
    rgba_[i + 2] = buffer_[i];
    rgba_[i + 3] = buffer_[i + 3];
  }
  if (shown_ && !headless_)
    overlay_.presentRgba(rgba_.data(), screen_.width, screen_.height);
  if(lv_display_flush_is_last(d)) trimFontCache();
  lv_display_flush_ready(d);
}
void Display::visible(bool value) {
  shown_ = value;
  if (!value && !headless_)
    overlay_.disable();
  if (value) {
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display_);
  }
}
void Display::screenshot(const std::string &path) {
  lv_refr_now(display_);
  if (!stbi_write_png(path.c_str(), screen_.width, screen_.height, 4,
                      rgba_.data(), screen_.width * 4))
    throw std::runtime_error("Screenshot failed");
}
Input::Input() {
  input_ = lv_indev_create();
  lv_indev_set_type(input_, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(input_, this);
  lv_indev_set_read_cb(input_, [](lv_indev_t *d, lv_indev_data_t *out) {
    auto p = static_cast<Input *>(lv_indev_get_user_data(d));
    out->point = p->point_;
    out->state = p->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  });
}
void Input::touch(int x, int y, bool down) {
  point_.x = x;
  point_.y = y;
  pressed_ = down;
  lv_indev_read(input_);
}
Input::~Input() { if(input_) lv_indev_delete(input_); }
Bridge::~Bridge() { if(fd_>=0) close(fd_); }
void Input::cancel() {
  pressed_ = false;
  lv_indev_reset(input_, nullptr);
  lv_indev_read(input_);
}
void Input::release() {
  pressed_ = false;
  lv_indev_read(input_);
  lv_indev_reset(input_, nullptr);
}
bool Bridge::send(const std::string &type, Json data) {
  if (fd_ < 0)
    return false;
  if (!data.contains("requestId"))
    data["requestId"] = ++requestSequence_;
  data["v"] = 1;
  data["type"] = type;
  data["generation"] = generation_;
  auto p = data.dump();
  return ::send(fd_, p.data(), p.size(), MSG_DONTWAIT | MSG_NOSIGNAL) ==
         ssize_t(p.size());
}
std::vector<Json> Bridge::receive() {
  std::vector<Json> out;
  if (fd_ < 0)
    return out;
  for (int i = 0; i < 128; ++i) {
    char b[65537];
    auto n = recv(fd_, b, 65536, MSG_DONTWAIT);
    if (n > 0) {
      auto j = Json::parse(std::string(b, n), nullptr, false);
      if (!j.is_discarded() && j.value("v", 0) == 1)
        out.push_back(j);
    } else {
      if (n == 0 ||
          (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        close(fd_);
        fd_ = -1;
      }
      break;
    }
  }
  return out;
}
struct Glyph {
  lv_font_glyph_dsc_t metrics{};
  std::vector<uint8_t> pixels;
  uint64_t used=0;
};
struct Typeface {
  lv_font_t font{};
  FT_Face face = nullptr, fallbackFace = nullptr;
  std::map<uint32_t, Glyph> glyphs;
  ~Typeface() {
    if(fallbackFace) FT_Done_Face(fallbackFace);
    if(face) FT_Done_Face(face);
  }
};
static FT_Library ft;
static std::string fontPath, symbolFontPath;
static std::map<int, std::unique_ptr<Typeface>> fonts;
static size_t glyphBytes=0, glyphCount=0;
static uint64_t glyphUse=0;
Json fontCacheInfo() { return {{"bytes",glyphBytes},{"entries",glyphCount},{"faces",fonts.size()}}; }
void trimFontCache() {
  if(glyphBytes<=4*1024*1024 && glyphCount<=8192) return;
  struct Item {Typeface *face; uint32_t cp; uint64_t used;};
  std::vector<Item> items;items.reserve(glyphCount);
  for(auto &font:fonts)for(const auto &g:font.second->glyphs)items.push_back({font.second.get(),g.first,g.second.used});
  std::sort(items.begin(),items.end(),[](const Item&a,const Item&b){return a.used<b.used;});
  for(const auto &item:items){
    if(glyphBytes<=4*1024*1024 && glyphCount<=8192)break;
    auto it=item.face->glyphs.find(item.cp);glyphBytes-=it->second.pixels.size();--glyphCount;item.face->glyphs.erase(it);
  }
}
void shutdownFonts() {
  fonts.clear();
  glyphBytes=glyphCount=0;
  if(ft) { FT_Done_FreeType(ft); ft=nullptr; }
}
void initializeFonts(const std::string &path, const std::string &fallbackPath) {
  symbolFontPath = fallbackPath;
  fontPath = path;
  if (FT_Init_FreeType(&ft))
    throw std::runtime_error("FreeType initialization failed");
}
static Glyph &glyph(Typeface *t, uint32_t cp) {
  auto found = t->glyphs.find(cp);
  if (found != t->glyphs.end()) {
    found->second.used=++glyphUse;
    return found->second;
  }
  Glyph g;
  const auto face = !FT_Get_Char_Index(t->face, cp) && t->fallbackFace ? t->fallbackFace : t->face;
  if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) {
    g.used=++glyphUse; ++glyphCount;
    return t->glyphs.emplace(cp,std::move(g)).first->second;
  }
  auto slot = face->glyph;
  auto &d = g.metrics;
  d.adv_w = std::max(0L, slot->advance.x / 64);
  d.box_w = slot->bitmap.width;
  d.box_h = slot->bitmap.rows;
  d.ofs_x = slot->bitmap_left;
  d.ofs_y = slot->bitmap_top - int(d.box_h);
  d.stride = d.box_w;
  d.format = LV_FONT_GLYPH_FORMAT_A8;
  g.pixels.resize(size_t(d.box_w) * d.box_h);
  for (int y = 0; y < d.box_h; ++y)
    std::copy_n(slot->bitmap.buffer + y * slot->bitmap.pitch, d.box_w,
                g.pixels.data() + size_t(y) * d.box_w);
  g.used=++glyphUse;glyphBytes+=g.pixels.size();++glyphCount;
  return t->glyphs.emplace(cp, std::move(g)).first->second;
}
lv_font_t *font(int size, bool bold) {
  int key = size * 2 + (bold ? 1 : 0);
  auto found = fonts.find(key);
  if (found != fonts.end())
    return &found->second->font;
  auto t = std::unique_ptr<Typeface>(new Typeface);
  if (FT_New_Face(ft, fontPath.c_str(), 0, &t->face))
    throw std::runtime_error("Cannot load Chinese font: " + fontPath);
  FT_Set_Pixel_Sizes(t->face, 0, size);
  if (FT_New_Face(ft, symbolFontPath.c_str(), 0, &t->fallbackFace))
    throw std::runtime_error("Cannot load symbol fallback font");
  FT_Set_Pixel_Sizes(t->fallbackFace, 0, size);
  auto &f = t->font;
  f.user_data = t.get();
  f.line_height = (t->face->size->metrics.height + 63) / 64;
  f.base_line = -(t->face->size->metrics.descender >> 6);
  f.static_bitmap = 1;
  f.get_glyph_dsc = [](const lv_font_t *f, lv_font_glyph_dsc_t *d, uint32_t c,
                       uint32_t) {
    auto t = static_cast<Typeface *>(f->user_data);
    *d = glyph(t, c).metrics;
    d->resolved_font = f;
    d->gid.index = c;
    return true;
  };
  f.get_glyph_bitmap = [](lv_font_glyph_dsc_t *d,
                          lv_draw_buf_t *) -> const void * {
    auto t = static_cast<Typeface *>(d->resolved_font->user_data);
    auto &g = glyph(t, d->gid.index);
    return g.pixels.empty() ? nullptr : g.pixels.data();
  };
  auto raw = t.get();
  fonts.emplace(key, std::move(t));
  return &raw->font;
}
} // namespace penchat
