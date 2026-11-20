#include "platform.hpp"
#include "camera_logic.h"
#include "refresh_policy.hpp"
#include "stb_image_write.h"
#include "src/misc/lv_timer_private.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fcntl.h>
#include <cerrno>
#include <map>
#include <array>
#include <chrono>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace penchat {
// LVGL has one global animation timer. The native app owns one Display; newly
// created inputs must inherit its setting as well as existing inputs.
static int activeRefreshRate = 30;
namespace {
uint64_t monotonicUs() {return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct RenderTiming {
  uint64_t frames=0,start=0,total=0,maximum=0,lastStart=0,intervalTotal=0,intervalMax=0;
  uint64_t presentCalls=0,presented=0,presentTotalUs=0,presentMaxUs=0;
  std::array<uint64_t,6> buckets{};
} renderTiming;
bool presentTimed(camera::DisplaySurface &surface,const uint8_t *pixels,int width,int height) {
  const auto start=monotonicUs();const bool accepted=surface.present(pixels,width,height);
  const auto duration=monotonicUs()-start;
  ++renderTiming.presentCalls;renderTiming.presented+=accepted;
  renderTiming.presentTotalUs+=duration;renderTiming.presentMaxUs=std::max(renderTiming.presentMaxUs,duration);
  return accepted;
}
}
Json displayTimingInfo() {
  return {{"frames",renderTiming.frames},{"render_total_us",renderTiming.total},
    {"render_max_us",renderTiming.maximum},{"interval_total_us",renderTiming.intervalTotal},
    {"interval_max_us",renderTiming.intervalMax},{"render_buckets",renderTiming.buckets},
    {"present_calls",renderTiming.presentCalls},{"presented",renderTiming.presented},
    {"present_total_us",renderTiming.presentTotalUs},{"present_max_us",renderTiming.presentMaxUs},
    {"sample_time_us",monotonicUs()}};
}
Json displayInformationJson(const camera::DisplayInfo &info) {
  return {{"mode",camera::compositionName(info.mode)},{"stage",info.stage},{"reason",info.reason},
    {"primary",info.primary},{"crtc",info.crtc},{"framebuffer",info.framebuffer},{"format",info.format},
    {"plane",info.plane},{"native_format",info.nativeFormat},{"query_rc",info.queryResult},{"query_errno",info.queryError},
    {"attempts",info.attempts},{"atomic",info.atomic},{"completion",info.completion},
    {"native_z",info.nativeRange},{"host_z",info.hostRange}};
}
Display::Display(const Screen &s, bool headless)
    : screen_(s), headless_(headless) {
  buffer_.resize(size_t(s.width) * s.height * 4);
  if (!headless_) {
    surface_=camera::makeDisplaySurface({s.width,s.height,s.rotation,s.xoffset,s.yoffset,s.panelWidth,s.panelHeight});
    // An upper overlay may cover system UI. Wait for the foreground display
    // lease before first upload; touch still waits for nativeReady separately.
    if(!surface_->systemOverlaySafe()){shown_=false;available_=false;}
  }
  display_ = lv_display_create(s.width, s.height);
  renderTiming={};
  lv_display_add_event_cb(display_,[](lv_event_t *event){
    const auto now=monotonicUs();
    if(lv_event_get_code(event)==LV_EVENT_RENDER_START) {
      if(renderTiming.lastStart) {
        const auto interval=now-renderTiming.lastStart;
        renderTiming.intervalTotal+=interval;renderTiming.intervalMax=std::max(renderTiming.intervalMax,interval);
      }
      renderTiming.start=renderTiming.lastStart=now;
    }
  },LV_EVENT_RENDER_START,nullptr);
  lv_display_add_event_cb(display_,[](lv_event_t *){
    if(!renderTiming.start)return;
    const auto duration=monotonicUs()-renderTiming.start;renderTiming.start=0;
    ++renderTiming.frames;renderTiming.total+=duration;renderTiming.maximum=std::max(renderTiming.maximum,duration);
    const uint64_t limits[]={8000,16667,33334,50000,100000};size_t bucket=0;
    while(bucket<5&&duration>limits[bucket])++bucket;
    ++renderTiming.buckets[bucket];
  },LV_EVENT_RENDER_READY,nullptr);
  lv_display_set_color_format(display_, LV_COLOR_FORMAT_ARGB8888);
  // Keep one complete CPU framebuffer, but redraw only LVGL's invalid areas.
  // Static headers, composer and keys need not be repainted on every scroll.
  lv_display_set_buffers(display_, buffer_.data(), nullptr, buffer_.size(),
                         LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_user_data(display_, this);
  lv_display_set_flush_cb(
      display_, [](lv_display_t *d, const lv_area_t *a, uint8_t *p) {
        static_cast<Display *>(lv_display_get_user_data(d))->flush(d, a, p);
      });
  setRefreshRate(refresh::defaultRate(s.width, s.height));
}
Display::~Display() {
  surface_.reset();
  if (display_)
    lv_display_delete(display_);
}
void Display::setRefreshRate(int hz) {
  refreshRate_ = refresh::resolveRate(hz, screen_.width, screen_.height);
  activeRefreshRate = refreshRate_;
  const auto period = refresh::periodMs(refreshRate_);
  if (auto timer = lv_display_get_refr_timer(display_)) lv_timer_set_period(timer, period);
  if (auto timer = lv_anim_get_timer()) lv_timer_set_period(timer, period);
  for (auto input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input))
    if (auto timer = lv_indev_get_read_timer(input)) lv_timer_set_period(timer, period);
  if (surface_) surface_->setRefreshRate(refreshRate_);
}
uint32_t Display::refreshPeriod() const {
  const auto timer = display_ ? lv_display_get_refr_timer(display_) : nullptr;
  return timer ? timer->period : 0;
}
uint32_t Display::animationPeriod() const {
  const auto timer = lv_anim_get_timer();
  return timer ? timer->period : 0;
}
uint32_t Display::inputPeriod() const {
  const auto input = lv_indev_get_next(nullptr);
  const auto timer = input ? lv_indev_get_read_timer(input) : nullptr;
  return timer ? timer->period : 0;
}
void Display::flush(lv_display_t *d, const lv_area_t *, uint8_t *) {
  // DIRECT may flush several dirty rectangles in one refresh. Backends consume
  // the complete framebuffer, so submit once after its last rectangle is drawn.
  if (!lv_display_flush_is_last(d)) { lv_display_flush_ready(d); return; }
  if (shown_ && !headless_ && surface_) {
    try {retryPresent();}
    catch(const camera::DisplayUnavailable &) {available_=false;pending_=true;retryPresentAt_=monotonicUs()+100000;}
    catch(const std::exception &e) {
      error_=e.what();shown_=false;pending_=false;
      try{surface_->hide();}catch(...){}
    }
  }
  if(headless_)presented_=true;
  if(lv_display_flush_is_last(d)) trimFontCache();
  lv_display_flush_ready(d);
}
void Display::check() {
  if(!error_.empty())throw std::runtime_error(error_);
  if(surface_)surface_->check();
  if(pending_&&shown_&&!surface_&&!mediaPaused_&&!headless_&&monotonicUs()>=retryPresentAt_)mediaHandoff(false);
  // The full LVGL CPU buffer already contains the newest frame. Retry that
  // buffer directly; no extra redraw, queue, copied snapshot or UI wait.
  if(pending_&&shown_&&surface_) {
    try {retryPresent();}
    catch(const camera::DisplayUnavailable &) {available_=false;retryPresentAt_=monotonicUs()+100000;}
  }
}
void Display::retryPresent() {
  if(monotonicUs()<retryPresentAt_)return;
  pending_=!presentTimed(*surface_,buffer_.data(),screen_.width,screen_.height);
  if(!pending_) {presented_=true;available_=true;retryPresentAt_=0;}
}
void Display::visible(bool value) {
  shown_ = value;
  if(!value)pending_=false;
  if (!value && !headless_ && surface_) {
    if(surface_->releaseOnHide()){surface_.reset();available_=false;}
    else surface_->hide();
  }
  if (value) {
    retryPresentAt_=0;
    if(!surface_&&!mediaPaused_&&!headless_)mediaHandoff(false);
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display_);
  }
}
void Display::topHeight(int height) {
  height=std::max(0,std::min(screen_.height,height));
  if(topHeight_==height)return;
  topHeight_=height;
  if(surface_)surface_->topRegion(height);
  if(shown_) {lv_obj_invalidate(lv_screen_active());lv_refr_now(display_);}
}
void Display::mediaHandoff(bool release) {
  if(headless_)return;
  if(surface_&&surface_->sharedCaptureSafe())return;
  mediaPaused_=release;
  if(release){pending_=false;surface_.reset();}
  else if(!surface_&&shown_) {
    try {
      surface_=camera::makeDisplaySurface({screen_.width,screen_.height,screen_.rotation,
        screen_.xoffset,screen_.yoffset,screen_.panelWidth,screen_.panelHeight});
    }catch(const camera::DisplayInitializationError &failure) {
      if(failure.info.stage!="primary_not_ready"&&failure.info.stage!="primary_changed"&&
         failure.info.queryError!=EACCES&&failure.info.queryError!=EPERM)throw;
      available_=false;pending_=true;retryPresentAt_=monotonicUs()+100000;return;
    }
    surface_->setRefreshRate(refreshRate_);
    surface_->topRegion(topHeight_);
    if(shown_)lv_obj_invalidate(lv_screen_active());
  }
}
void Display::screenshot(const std::string &path) {
  lv_refr_now(display_);
  std::vector<uint8_t> rgba(buffer_.size());
  camera::rgbaToBgra(buffer_.data(), rgba.data(), rgba.size() / 4);
  if (!stbi_write_png(path.c_str(), screen_.width, screen_.height, 4,
                      rgba.data(), screen_.width * 4))
    throw std::runtime_error("Screenshot failed");
}
Input::Input() {
  input_ = lv_indev_create();
  if (auto timer = lv_indev_get_read_timer(input_))
    lv_timer_set_period(timer, refresh::periodMs(activeRefreshRate));
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
Input::~Input() {
  if (input_) {
    // LVGL 9.6's inertial-scroll animation retains the indev as its variable.
    // Its deletion callback must run while the input device is still alive.
    lv_anim_delete(input_, nullptr);
    lv_indev_delete(input_);
  }
}
Bridge::~Bridge() { if(fd_>=0) close(fd_); }
void Input::cancel() {
  pressed_ = false;
  lv_indev_reset(input_, nullptr);
  lv_indev_read(input_);
}
void Input::interrupt() {
  auto scroll = lv_indev_get_scroll_obj(input_);
  lv_anim_delete(input_, nullptr);
  if (scroll) {
    lv_obj_stop_scroll_anim(scroll);
    // A reset alone drops LVGL's scroll owner before its elastic return runs.
    // Stop at the nearest valid offset, without moving a valid reading position.
    const int x = lv_obj_get_scroll_x(scroll), y = lv_obj_get_scroll_y(scroll);
    const int maxX = std::max(0, int(lv_obj_get_scroll_left(scroll) + lv_obj_get_scroll_right(scroll)));
    const int maxY = std::max(0, int(lv_obj_get_scroll_top(scroll) + lv_obj_get_scroll_bottom(scroll)));
    const bool rtl = lv_obj_get_style_base_dir(scroll, LV_PART_MAIN) == LV_BASE_DIR_RTL;
    const int targetX = rtl ? std::max(-maxX, std::min(0, x)) : std::max(0, std::min(maxX, x));
    const int targetY = std::max(0, std::min(maxY, y));
    lv_obj_scroll_by_bounded(scroll, x - targetX, y - targetY, LV_ANIM_OFF);
  }
  cancel();
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
