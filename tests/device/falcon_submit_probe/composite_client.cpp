// Isolated test process: shared system frames + real LVGL sample -> shared output.
#include "composite_wire.hpp"
#include "native_touch.hpp"
#include "lvgl.h"
#include <chrono>
#include <cstring>
#include <deque>
#include <poll.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace penchat_compositor;
namespace {
struct Pointer { int x = 0, y = 0; bool down = false; };
struct Sample {
  std::vector<std::uint8_t> pixels;
  std::deque<Pointer> pointers;
  Pointer last;
  bool valid = false;
  unsigned renders = 0, taps = 0;
  lv_display_t* display = nullptr;
  lv_indev_t* input = nullptr;
  lv_obj_t* moving = nullptr;
  lv_obj_t* caption = nullptr;
  Sample(unsigned width, unsigned height) {
    lv_init();
    lv_tick_set_cb([] { return monotonicMs(); });
    pixels.resize(std::size_t(width) * height * 4);
    display = lv_display_create(width, height);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size(), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(display, this);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, std::uint8_t*) {
      auto* self = static_cast<Sample*>(lv_display_get_user_data(d));
      self->valid = true; ++self->renders; lv_display_flush_ready(d);
    });
    auto* root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x172c3d), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(root, false);
    auto* title = lv_label_create(root);
    lv_label_set_text(title, "LVGL / SHARED MEMORY"); lv_obj_set_pos(title, 18, 58);
    lv_obj_set_style_text_color(title, lv_color_hex(0x8ce1c1), 0);
    auto* button = lv_button_create(root);
    lv_obj_set_pos(button, int(width) / 2 - 80, int(height) / 2 - 18);
    lv_obj_set_size(button, 160, 44);
    caption = lv_label_create(button); lv_label_set_text(caption, "Native taps: 0"); lv_obj_center(caption);
    lv_obj_add_event_cb(button, [](lv_event_t* event) {
      auto* self = static_cast<Sample*>(lv_event_get_user_data(event));
      lv_label_set_text_fmt(self->caption, "Native taps: %u", ++self->taps);
      lv_obj_center(self->caption);
    }, LV_EVENT_CLICKED, this);
    moving = lv_obj_create(root); lv_obj_set_size(moving, 44, 24);
    lv_obj_set_pos(moving, 18, int(height) - 84);
    lv_obj_set_style_bg_color(moving, lv_color_hex(0xf5bd64), 0);
    lv_obj_set_style_border_width(moving, 0, 0);
    input = lv_indev_create(); lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(input, this);
    lv_indev_set_read_cb(input, [](lv_indev_t* input, lv_indev_data_t* data) {
      auto* self = static_cast<Sample*>(lv_indev_get_user_data(input));
      if (!self->pointers.empty()) { self->last = self->pointers.front(); self->pointers.pop_front(); }
      data->point = {self->last.x, self->last.y};
      data->state = self->last.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
      data->continue_reading = !self->pointers.empty();
    });
  }
  ~Sample() { lv_indev_delete(input); lv_display_delete(display); lv_deinit(); }
  void cancel() {
    pointers.clear(); last.down = false; lv_indev_reset(input, nullptr);
  }
};
int run() {
  pollfd wait{3, POLLIN, 0};
  Packet hello;
  if (poll(&wait, 1, 3000) <= 0 || receivePacket(3, hello) != 1 || hello.type != Wire::Hello) return 2;
  const auto g = Geometry::make(hello.value, hello.aux, sysconf(_SC_PAGESIZE));
  struct stat st{};
  if (fstat(4, &st) || std::uint64_t(st.st_size) != g.total || hello.value2 != g.slotBytes) return 3;
  const auto cfg = penchat::Json::parse(penchat::fileText("/etc/miniapp/resources/cfg.json", 65536));
  const auto screen = penchat::Screen::fromConfig(cfg, true);
  if (screen.width != int(g.width) || screen.height != int(g.height)) return 4;
  auto* system = static_cast<const std::uint8_t*>(mmap(nullptr, 2 * g.slotBytes, PROT_READ, MAP_SHARED, 4, 0));
  auto* output = static_cast<std::uint8_t*>(mmap(nullptr, 2 * g.slotBytes, PROT_READ | PROT_WRITE, MAP_SHARED, 4, 2 * g.slotBytes));
  if (system == MAP_FAILED || output == MAP_FAILED) return 5;
  Sample sample(g.width, g.height);
  penchat::NativeTouch touch(screen, true);
  if (!sendPacket(3, hello)) return 6;
  bool running = true, focused = false, freeOutput[2] = {true, true};
  bool gesture = false, accepted = false;
  unsigned heartbeat = 0, epoch = 0, systemSequence = 0, outputSequence = 0;
  unsigned outputSlots[2] = {0, 0}, seenSystem = 0, sentSystem = 0, sentRender = 0;
  int heldSystem = -1;
  auto lastTick = monotonicMs();
  while (running) {
    pollfd p{3, POLLIN, 0};
    if (poll(&p, 1, focused ? 4 : 30) < 0 && errno != EINTR) break;
    if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
    for (unsigned batch = 0; batch < 16; ++batch) {
      Packet packet;
      const auto result = receivePacket(3, packet);
      if (!result) break;
      if (result < 0) { running = false; break; }
      if (packet.type == Wire::Stop) { running = false; break; }
      if (packet.type == Wire::Focus) {
        if (packet.value > 1 || !packet.epoch) throw std::runtime_error("Invalid focus packet");
        heartbeat = monotonicMs(); epoch = packet.epoch;
        const bool next = packet.value == 1;
        if (next != focused) { sample.cancel(); gesture = accepted = false; }
        focused = next;
      } else if (packet.type == Wire::System) {
        if (packet.index >= 2 || !packet.sequence || packet.sequence <= seenSystem || int(packet.index) == heldSystem)
          throw std::runtime_error("Invalid shared system frame");
        if (heldSystem >= 0) {
          Packet release; release.type = Wire::ReleaseSystem; release.index = heldSystem; release.sequence = systemSequence;
          if (!sendPacket(3, release)) throw std::runtime_error("Cannot release system frame");
        }
        heldSystem = packet.index; systemSequence = seenSystem = packet.sequence;
      } else if (packet.type == Wire::ReleaseOutput) {
        if (packet.index >= 2 || freeOutput[packet.index] || outputSlots[packet.index] != packet.sequence)
          throw std::runtime_error("Invalid output acknowledgement");
        freeOutput[packet.index] = true;
      } else throw std::runtime_error("Unexpected host packet");
    }
    if (!running) break;
    const auto now = monotonicMs();
    if (focused && std::uint32_t(now - heartbeat) > 300) {
      focused = false; sample.cancel(); gesture = accepted = false;
    }
    for (const auto& event : touch.poll(focused)) {
      const bool down = event.value("pressed", false);
      const bool cancelled = event.value("cancelled", false) || event.at("points").size() > 1;
      if (cancelled) { sample.cancel(); accepted = false; gesture = down; continue; }
      const int x = event.at("x").get<int>(), y = event.at("y").get<int>();
      if (down && !gesture) { accepted = y >= 52 && y < screen.height - 52; gesture = true; }
      if (accepted) {
        if (sample.pointers.size() >= 64) { sample.cancel(); accepted = false; }
        else sample.pointers.push_back({x, y, down});
      }
      if (!down) gesture = accepted = false;
    }
    if (!focused) continue;
    const bool tick = std::uint32_t(now - lastTick) >= 33;
    if (tick) {
      lv_obj_set_pos(sample.moving, 18 + int((now / 12) % std::max(1u, g.width - 80)), screen.height - 84);
      lv_timer_handler(); lastTick = now;
    } else if (!sample.pointers.empty()) lv_timer_handler();
    const int slot = freeOutput[0] ? 0 : freeOutput[1] ? 1 : -1;
    if (slot < 0 || heldSystem < 0 || !sample.valid ||
        (sentSystem == systemSequence && sentRender == sample.renders)) continue;
    const auto begin = std::chrono::steady_clock::now();
    blendOver(system + heldSystem * g.slotBytes, sample.pixels.data(), output + slot * g.slotBytes, g.bytes / 4);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - begin).count();
    Packet frame; frame.type = Wire::Output; frame.index = slot; frame.sequence = ++outputSequence;
    frame.epoch = epoch; frame.value = std::min<std::uint64_t>(micros, UINT32_MAX);
    frame.aux = sample.taps; frame.value2 = sample.renders; frame.value3 = systemSequence;
    if (!frame.sequence || !sendPacket(3, frame)) break;
    freeOutput[slot] = false; outputSlots[slot] = frame.sequence;
    sentSystem = systemSequence; sentRender = sample.renders;
  }
  touch.close(); sample.cancel();
  munmap(const_cast<std::uint8_t*>(system), 2 * g.slotBytes); munmap(output, 2 * g.slotBytes);
  close(3); close(4); return 0;
}
}
int main() {
  try { return run(); }
  catch (const std::exception&) { return 20; } // no screen/input text in diagnostics
}
