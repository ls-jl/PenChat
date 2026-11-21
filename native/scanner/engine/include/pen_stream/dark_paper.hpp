#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

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

  bool enabled() const { return target > paper; }

  static DarkPaperTone estimate(const PaperHistogram &hist) {
    DarkPaperTone tone;
    // A hot spot from the pen lamp can fill the top few percent of
    // the histogram. Use the bulk paper instead of that bright tail.
    tone.paper = hist.percentile(80);
    const int span = tone.paper - hist.percentile(10);
    // Leave normal and nearly featureless/clipped images alone. Fade out
    // correction as the paper approaches 120, and cap lift at four times.
    if (hist.count < 128 || span < 10 || tone.paper >= 120) return tone;
    tone.target = std::min({160, 240 - tone.paper, 4 * tone.paper});
    return tone;
  }

  void stabilize(const DarkPaperTone &previous) {
    // Only average neighboring estimates of similar brightness. A bright,
    // clipped or unlit frame breaks the sequence instead of inheriting gain.
    if (!enabled() || !previous.enabled() ||
        std::abs(paper - previous.paper) > std::max(2, paper / 10)) return;
    const int priorTarget = (paper * previous.target + previous.paper / 2) / previous.paper;
    target = std::min({160, 240 - paper, 4 * paper, (3 * target + priorTarget + 2) / 4});
  }

  void apply(const uint8_t *source, uint8_t *output, size_t size) const {
    std::array<uint8_t, 256> table;
    for (int i = 0; i < int(table.size()); ++i) {
      // Keep the black point fixed and compress the highlights above the
      // estimated paper level instead of flattening them at the white limit.
      const int value = i <= paper
        ? (i * target + paper / 2) / paper
        : target + ((i - paper) * (250 - target) + (255 - paper) / 2) / (255 - paper);
      table[i] = uint8_t(value);
    }
    for (size_t i = 0; i < size; ++i)
      output[i] = table[source[i]];
  }
};
}
