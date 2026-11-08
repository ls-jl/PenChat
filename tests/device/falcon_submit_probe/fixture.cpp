#include "probe.hpp"
#include <atomic>
namespace { std::atomic<unsigned> calls{0}; std::atomic<const void*> lastSource{nullptr}, lastRect{nullptr}; std::atomic<int> lastWidth{0}, lastHeight{0}; std::atomic<unsigned> lastPixel{0}; }
extern "C" __attribute__((noinline)) void fixture_present(void* source, const submit_probe::Rect& dirty, int width, int height) {
  lastSource = source; lastRect = &dirty; lastWidth = width; lastHeight = height;
  unsigned pixel = 0;
  if (source && width > 0 && height > 0) __builtin_memcpy(&pixel, source, 4);
  lastPixel = pixel;
  calls.fetch_add(1);
}
extern "C" void fixture_submit(void* source, const submit_probe::Rect& dirty, int width, int height) {
  fixture_present(source, dirty, width, height);
}
extern "C" unsigned fixture_count() { return calls.load(); }
extern "C" bool fixture_arguments(void* source, const submit_probe::Rect& dirty, int width, int height) {
  return lastSource == source && lastRect == &dirty && lastWidth == width && lastHeight == height;
}

extern "C" unsigned fixture_pixel() { return lastPixel.load(); }
