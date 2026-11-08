#pragma once
#include "presentation.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>
#include "composite_wire.hpp"
#include <array>
namespace penchat_compositor {
struct CompositeStats {
  bool running = false, originalRestored = true, focused = false;
  int child = -1;
  unsigned systemFrames = 0, outputFrames = 0, coalesced = 0, rejected = 0;
  unsigned nativeFrames = 0, taps = 0, epoch = 0, elapsedMs = 0;
  std::size_t sharedBytes = 0;
  double copyMedianMs = 0, blendMedianMs = 0, presentMedianMs = 0;
  std::string reason;
};
class Composite {
public:
  static Composite& instance();
  void start(const Slot&, int width, int height, const std::string& executable, unsigned seconds = 25);
  // Runtime caller owns/reaps its application process; a zero duration uses focus/IPC lifetime.
  void start(const Slot&, int width, int height, const std::function<pid_t(int,int)>& spawn, unsigned seconds = 0, bool ownChild = false);
  // Renewable display lease, independent of the native input/window focus.
  // Attached system windows are always the upper layer in Surface::blendOver.
  void focus(bool active);
  void disconnect();
  void stop();
  CompositeStats stats();
  static void relay(void*, const Rect&, int, int);
private:
  enum class Mode { Detached, Waiting, Active, Draining };
  void submit(void*, const Rect&, int, int);
  void loop();
  void restore();
  void releaseResources();
  bool freshFocus() const;
  void postLatestSystem();
  bool send(const Packet&);
  bool queueSystem(const std::uint8_t*, unsigned sequence);
  std::mutex channelMutex_;
  std::mutex systemMutex_, outputMutex_, slotMutex_, statsMutex_;
  Slot slot_;
  std::atomic<Present> original_{nullptr};
  std::atomic<Mode> mode_{Mode::Detached};
  std::atomic<bool> quit_{false}, restored_{true}, focused_{false}, passthrough_{false};
  std::atomic<unsigned> epoch_{1}, focusAt_{0}, callbacks_{0}, systemFrames_{0}, outputFrames_{0};
  std::atomic<unsigned> coalesced_{0}, rejected_{0}, nativeFrames_{0}, taps_{0};
  std::atomic<int> child_{-1};
  std::atomic<bool> nativeVisible_{true};
  bool ownChild_ = true;
  std::thread worker_;
  int socket_ = -1, shared_ = -1;
  Geometry geometry_;
  std::uint8_t* systems_ = nullptr;
  const std::uint8_t* outputs_ = nullptr;
  std::vector<std::uint8_t> pending_;
  bool freeSystem_[2] = {true, true}, pendingValid_ = false;
  unsigned pendingSequence_ = 0, systemSequence_ = 0, slotSequence_[2] = {0, 0};
  int latestSystem_ = -1;
  unsigned started_ = 0, deadline_ = 0, ended_ = 0;
  std::array<double, 64> copyTimes_{}, blendTimes_{}, presentTimes_{};
  unsigned copyCount_ = 0, frameCount_ = 0;
  std::string reason_;
};
}
