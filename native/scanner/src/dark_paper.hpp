#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace penchat_scan {
struct PaperHistogram {
  std::array<unsigned, 256> bins{};
  unsigned count = 0;

  void add(uint8_t pixel) {
    ++bins[pixel];
    ++count;
  }

  int percentile(unsigned percent) const {
    if (!count) return 0;
    const auto limit = (uint64_t(count) * percent + 99) / 100;
    unsigned cumulative = 0;
    for (unsigned i = 0; i < bins.size(); ++i) {
      cumulative += bins[i];
      if (cumulative >= limit) return int(i);
    }
    return 255;
  }
};

struct DarkPaperTone {
  int paper = 0, target = 0;
  std::array<uint8_t, 256> table{};

  bool enabled() const { return target > paper; }

  static DarkPaperTone estimate(const uint8_t *pixels, const uint8_t *mask, size_t size) {
    PaperHistogram hist;
    for (size_t i = 0; i < size; ++i)
      if (mask[i]) hist.add(pixels[i]);

    DarkPaperTone tone;
    tone.paper = hist.percentile(95);
    const int span = tone.paper - hist.percentile(10);
    // Leave normal and nearly featureless/clipped images alone. Fade out
    // correction as the paper approaches 120, and cap lift at four times.
    if (hist.count < 128 || span < 10 || tone.paper >= 120) return tone;
    tone.target = std::min({160, 240 - tone.paper, 4 * tone.paper});
    for (unsigned i = 0; i < tone.table.size(); ++i)
      tone.table[i] = uint8_t(std::min(250, (int(i) * tone.target + tone.paper / 2) / tone.paper));
    return tone;
  }

  void apply(uint8_t *pixels, const uint8_t *mask, size_t size) const {
    if (!enabled()) return;
    for (size_t i = 0; i < size; ++i)
      if (mask[i]) pixels[i] = table[pixels[i]];
  }
};
}
