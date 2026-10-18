#ifdef NDEBUG
#undef NDEBUG
#endif
#include "frame_queue.hpp"
#include <cassert>
#include <chrono>
#include <dirent.h>
#include <iostream>
using namespace penchat_scan;
int main() {
  char name[] = "/tmp/penchat-frame-queue-XXXXXX";
  assert(mkdtemp(name));
  {
    FrameQueue queue(name);
    std::vector<uint8_t> pixels(500 * 360, 0);
    for (unsigned round = 0; round < 2; ++round) {
      for (unsigned i = 0; i < FrameQueue::Capacity; ++i) {
        std::fill(pixels.begin(), pixels.end(), uint8_t(i));
        queue.push(pixels.data(), 500, round * FrameQueue::Capacity + i,
                   1000 + i);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      bool full = false;
      try {
        queue.push(pixels.data(), 500, 9999, 0);
      } catch (const std::runtime_error &) {
        full = true;
      }
      assert(full && queue.pending() == FrameQueue::Capacity);
      for (unsigned i = 0; i < FrameQueue::Capacity; ++i) {
        FrameQueue::Frame frame;
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!queue.pop(frame)) {
          assert(std::chrono::steady_clock::now() < end);
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(frame.sequence == round * FrameQueue::Capacity + i &&
               frame.timestamp == 1000 + i);
        assert(frame.pixels.size() == FrameQueue::Pixels);
        for (auto pixel : frame.pixels)
          assert(pixel == uint8_t(i));
      }
      assert(queue.pending() == 0 && queue.spilled() > 0);
    }
    // Cancellation while work is queued joins its I/O worker and unlinks
    // storage.
    queue.push(pixels.data(), 500, 2000, 0);
  }
  auto dir = opendir(name);
  assert(dir);
  while (auto entry = readdir(dir))
    assert(entry->d_name[0] == '.');
  closedir(dir);
  assert(rmdir(name) == 0);
  std::cout << "Frame queue: ordered pixel-exact spill, wraparound, bounded "
               "capacity and cancellation passed\n";
}
