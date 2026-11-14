#include "composite_wire.hpp"
#include "display_surface.h"
#include "surface.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
std::atomic<std::uint32_t> clockMs{1000};
}
// The actual worker, transport, mappings and slot logic run unchanged. Linker
// wrapping controls only its monotonic clock, so this checks exact pacing
// boundaries without assuming a minimum host CPU speed or benchmarking FPS.
extern "C" std::uint32_t __wrap__ZN18penchat_compositor11monotonicMsEv() {
  return clockMs.load();
}

namespace {
using namespace penchat_compositor;
class Fixture {
  int host_ = -1, memory_ = -1;
  Geometry geometry_ = Geometry::make(16, 8, sysconf(_SC_PAGESIZE));
  std::uint8_t* mapping_ = nullptr;
  std::unique_ptr<camera::DisplaySurface> surface_;
  std::vector<std::uint8_t> pixels_;
  unsigned outputSequence_ = 0;
  bool receive(Packet& packet, unsigned timeoutMs) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    do {
      const int status = receivePacket(host_, packet);
      assert(status >= 0);
      if (status) return true;
      pollfd wait{host_, POLLIN, 0};
      assert(poll(&wait, 1, 1) >= 0);
    } while (std::chrono::steady_clock::now() < end);
    return false;
  }
public:
  Fixture() {
    int channel[2]; assert(!socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, channel));
    host_ = channel[0];
    memory_ = createShared(geometry_);
    mapping_ = static_cast<std::uint8_t*>(mmap(nullptr, geometry_.total, PROT_READ | PROT_WRITE, MAP_SHARED, memory_, 0));
    assert(mapping_ != MAP_FAILED);
    std::memset(mapping_, 0, geometry_.total);
    Packet hello; hello.type = Wire::Hello; hello.value = geometry_.width;
    hello.aux = geometry_.height; hello.value2 = geometry_.slotBytes;
    assert(sendPacket(host_, hello));
    configureDisplay(channel[1], dup(memory_));
    surface_ = camera::makeDisplaySurface({16, 8, 0, 0, 0, 16, 8});
    Packet ack; assert(receive(ack, 500) && ack.type == Wire::Hello);
    focus(true, 1);
    Packet system; system.type = Wire::System; system.index = 0; system.sequence = 1;
    assert(sendPacket(host_, system));
    pixels_.resize(geometry_.bytes);
  }
  ~Fixture() {
    surface_.reset();
    munmap(mapping_, geometry_.total); close(memory_); close(host_);
  }
  void rate(int hz) { surface_->setRefreshRate(hz); }
  void hide() { surface_->hide(); }
  void focus(bool focused, unsigned epoch) {
    Packet packet; packet.type = Wire::Focus; packet.value = focused; packet.epoch = epoch;
    assert(sendPacket(host_, packet));
  }
  void present(std::uint8_t value) {
    for (std::size_t p = 0; p < pixels_.size(); p += 4) {
      pixels_[p] = value; pixels_[p + 1] = value; pixels_[p + 2] = value; pixels_[p + 3] = 255;
    }
    assert(surface_->present(pixels_.data(), geometry_.width, geometry_.height));
  }
  Packet output(std::uint8_t value, unsigned epoch = 1) {
    Packet packet;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < end) {
      if (!receive(packet, 10)) continue;
      if (packet.type == Wire::Visibility) continue;
      assert(packet.type == Wire::Output && packet.index < 2);
      assert(packet.sequence == ++outputSequence_ && packet.epoch == epoch);
      const auto* pixels = mapping_ + (2 + packet.index) * geometry_.slotBytes;
      assert(pixels[0] == value && pixels[3] == 255);
      surface_->check();
      return packet;
    }
    assert(false && "Compositor did not publish eligible frame");
    return packet;
  }
  void release(const Packet& output) {
    Packet packet; packet.type = Wire::ReleaseOutput; packet.index = output.index; packet.sequence = output.sequence;
    assert(sendPacket(host_, packet));
  }
  void noOutput() {
    // The real worker polls at most 8ms; frozen test time prevents the pending
    // frame from crossing its boundary while we wait for it to inspect state.
    Packet packet;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
    while (std::chrono::steady_clock::now() < end) {
      if (receive(packet, 2)) assert(packet.type == Wire::Visibility);
    }
    surface_->check();
  }
};
}

int main() {
  Fixture fixture;
  fixture.present(0x11);
  const auto first = fixture.output(0x11);
  clockMs = 1032; fixture.present(0x22); fixture.noOutput();
  clockMs = 1033; const auto second = fixture.output(0x22);
  assert(first.index != second.index);

  // Both slots remain owned by Falcon. Only the newest native frame survives.
  clockMs = 1066; fixture.present(0x33); fixture.present(0x44); fixture.noOutput();
  fixture.release(first); const auto third = fixture.output(0x44);
  assert(third.index == first.index);
  fixture.release(second); fixture.release(third);

  // Changing rate retains the previous output's timestamp: 15ms is too early,
  // 16ms is eligible at 60Hz, while changing back requires a full 33ms.
  clockMs = 1081; fixture.rate(60); fixture.present(0x55); fixture.noOutput();
  clockMs = 1082; const auto fourth = fixture.output(0x55); fixture.release(fourth);
  clockMs = 1098; fixture.rate(30); fixture.present(0x66); fixture.noOutput();
  clockMs = 1114; fixture.noOutput();
  clockMs = 1115; const auto fifth = fixture.output(0x66); fixture.release(fifth);

  // A setting change cannot override hide, focus epoch or an expired lease.
  clockMs = 1120; fixture.present(0x77); fixture.hide(); fixture.noOutput();
  clockMs = 1148; fixture.rate(60); fixture.noOutput();
  fixture.present(0x88); const auto sixth = fixture.output(0x88); fixture.release(sixth);
  clockMs = 1164; fixture.focus(false, 2); fixture.present(0x99); fixture.noOutput();
  fixture.focus(true, 3); const auto seventh = fixture.output(0x99, 3); fixture.release(seventh);
  clockMs = 1164 + kDisplayLeaseMs + 1; fixture.present(0xaa); fixture.rate(30); fixture.noOutput();
  fixture.focus(true, 4); const auto eighth = fixture.output(0xaa, 4); fixture.release(eighth);
  std::cout << "COMPOSITOR_REFRESH_33_16MS_LATEST_SLOTS_FOCUS_AND_LEASE_OK\n";
}
