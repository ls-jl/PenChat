#include "preview.hpp"
#include <atomic>
#include <chrono>
#include <stdexcept>
using namespace penchat_scan;
namespace {
void require(bool okay, const char *reason) {
  if (!okay)
    throw std::runtime_error(reason);
}
template <class F> void wait(F predicate) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < end, "preview transition timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
std::unique_ptr<PreviewFrame> frame(uint64_t generation) {
  std::unique_ptr<PreviewFrame> f(new PreviewFrame);
  f->generation = generation;
  f->scan = generation;
  f->width = f->height = 1;
  f->pixels = {uint8_t(generation)};
  return f;
}
} // namespace
int main() {
  PreviewWorker worker;
  std::mutex mutex;
  std::condition_variable cv;
  bool started = false, released = false;
  auto reference = std::make_shared<int>(1);
  std::weak_ptr<int> inFlight = reference;
  require(worker.request([&, hold = std::move(reference)] {
    std::unique_lock<std::mutex> lock(mutex);
    started = true;
    cv.notify_all();
    cv.wait(lock, [&] { return released; });
    return frame(1);
  }), "first reader accepted");
  {
    std::unique_lock<std::mutex> lock(mutex);
    require(cv.wait_for(lock, std::chrono::seconds(3), [&] { return started; }),
            "reader starts");
  }
  // A blocked canvas reader cannot block the capture-side poll/request calls.
  require(!worker.take(), "no unfinished image");
  require(!worker.request([] { return frame(2); }), "ordinary reads coalesce");
  auto pending = std::make_shared<int>(2);
  std::weak_ptr<int> dropped = pending;
  require(worker.request([hold = std::move(pending)] { return frame(2); }, true),
          "one final request queued");
  require(worker.request([] { return frame(3); }, true), "replace final request");
  require(dropped.expired(), "replacement releases pending references");
  pending = std::make_shared<int>(3);
  dropped = pending;
  require(worker.request([hold = std::move(pending)] { return frame(3); }, true),
          "new pending final");
  std::atomic<bool> cancelled{false};
  std::thread cancel([&] { worker.cancelAndWait(); cancelled = true; });
  wait([&] { return dropped.expired(); });
  require(!cancelled, "cancellation waits for the active result reference");
  {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    cv.notify_all();
  }
  cancel.join();
  require(inFlight.expired() && !worker.take(), "cancel excludes stale output");
  require(worker.request([] { return frame(4); }), "reader remains reusable");
  std::unique_ptr<PreviewFrame> output;
  wait([&] { output = worker.take(); return bool(output); });
  require(output->generation == 4 && output->pixels == std::vector<uint8_t>{4},
          "only current generation published");
  worker.cancelAndWait();
}
