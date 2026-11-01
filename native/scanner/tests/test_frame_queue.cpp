#ifdef NDEBUG
#undef NDEBUG
#endif
#include "frame_queue.hpp"
#include <cassert>
#include <chrono>
#include <dirent.h>
#include <iostream>
#include <utility>
using namespace penchat_scan;
int main() {
  char name[] = "/tmp/penchat-frame-queue-XXXXXX";
  assert(mkdtemp(name));
  for (const auto size : {std::make_pair(496,360), std::make_pair(640,480)}) {
    const int width=size.first, height=size.second, stride=width+4;
    FrameQueue queue(name,width,height);
    assert(queue.storageLimit()<=110*1024*1024 && queue.capacity()<=640);
    if(width==640)assert(queue.capacity()==375);
    std::vector<uint8_t> pixels(size_t(stride)*height, 0);
    for (unsigned round = 0; round < 2; ++round) {
      for (unsigned i = 0; i < queue.capacity(); ++i) {
        std::fill(pixels.begin(), pixels.end(), uint8_t(i));
        queue.push(pixels.data(), stride, round * queue.capacity() + i,
                   1000 + i);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      bool full = false;
      try {
        queue.push(pixels.data(), stride, 9999, 0);
      } catch (const std::runtime_error &) {
        full = true;
      }
      assert(full && queue.pending() == queue.capacity());
      for (unsigned i = 0; i < queue.capacity(); ++i) {
        FrameQueue::Frame frame;
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!queue.pop(frame)) {
          assert(std::chrono::steady_clock::now() < end);
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(frame.sequence == round * queue.capacity() + i &&
               frame.timestamp == 1000 + i);
        assert(frame.pixels.size() == size_t(width)*height);
        for (auto pixel : frame.pixels)
          assert(pixel == uint8_t(i));
      }
      assert(queue.pending() == 0 && queue.spilled() > 0);
    }
    // Cancellation while work is queued joins its I/O worker and unlinks
    // storage.
    queue.push(pixels.data(), stride, 2000, 0);
  }
  bool rejected=false;
  try { FrameQueue invalid(name,-16,1); } catch(const std::invalid_argument &) { rejected=true; }
  assert(rejected);
  auto dir = opendir(name);
  assert(dir);
  while (auto entry = readdir(dir))
    assert(entry->d_name[0] == '.');
  closedir(dir);
  assert(rmdir(name) == 0);
  std::cout << "Frame queue: ordered pixel-exact spill, wraparound, bounded "
               "capacity and cancellation passed\n";
}
