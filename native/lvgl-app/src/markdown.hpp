#pragma once
#include "ui_metrics.hpp"
#include "common.hpp"
#include <cairo.h>
namespace penchat {
enum Style {
  Bold = 1,
  Italic = 2,
  Strike = 4,
  Mono = 8,
  Math = 16,
  DisplayMath = 32,
  Image = 64
};
struct Run {
  std::string text, link;
  int style = 0;
};
struct Block {
  std::string kind = "p", marker;
  int level = 0, indent = 0, quote = 0;
  std::vector<Run> runs;
  std::vector<std::vector<Run>> cells;
  std::vector<int> columnWidths, cellAlignments;
  int tableId = 0;
  // Stable identities keep hanging indents across loose paragraphs and nesting.
  int listItemId = 0;
  std::vector<int> listGroups;
  std::vector<int> listColumns;
  bool header = false;
};
struct Document {
  std::string source;
  std::vector<Block> blocks;
};
Document parseMarkdown(const std::string &text, bool streaming = false);
struct Bitmap {
  int width = 0, height = 0, baseline = 0;
  std::vector<uint8_t> pixels;
};
// One raster window in block-local coordinates. A batch owns no persistent layout.
struct RichWindow {
  int top = 0, height = 0, left = 0, width = 0;
};
#ifndef PENCHAT_NATIVE_DEVICE
struct RichWorkStats {
  uint64_t layoutCalls = 0, glyphMeasurements = 0;
  uint64_t formulaRenders = 0, formulaHits = 0, formulaCopies = 0;
};
#endif
struct Hit {
  int x, y, w, h;
  std::string target;
};
struct RichLayout {
  int width = 0, height = 0;
  std::vector<Hit> links;
  struct TextHit { int x, y, w, h; size_t begin, end; bool atomic; };
  std::vector<TextHit> textHits;
  const TextHit *selectionEndpoint(size_t byte, bool end) const {
    if (textHits.empty()) return nullptr;
    if (end) {
      for (auto i=textHits.rbegin(); i!=textHits.rend(); ++i)
        if (i->begin<byte) return &*i;
      return &textHits.front();
    }
    for (const auto &hit:textHits) if (hit.end>byte) return &hit;
    return &textHits.back();
  }
};
std::string blockPlainText(const Block &block);
struct RichTypography {
  int fontSize = 21, lineHeight = 34, before = 0, after = 0;
};
class RichText {
  struct Impl;
  std::unique_ptr<Impl> p_;
  std::shared_ptr<const Bitmap> formulaShared(const std::string &text, int size,
                                             uint32_t foreground);
  Bitmap paintPrepared(const Block &block, int width, const RichLayout &info,
                       const RichWindow &window, int size, bool subdued);

public:
  RichText(const std::string &fontPath, const std::string &resourcePath, const std::string &fallbackPath = "", const UiMetrics &metrics = UiMetrics());
  ~RichText();
  Bitmap formula(const std::string &text, int size, uint32_t foreground = 0);
  RichTypography typography(const Block &block, int size = 21) const;
  int blockGap(const Block *previous, const Block &current, int size = 21) const;
  bool hasTrueBold() const;
  void prepare(Document &document, int width, int size = 21);
  RichLayout layout(const Block &block, int width, int size = 21, bool textHits = false, bool subdued = false);
  Bitmap paint(const Block &block, int width, int top, int height,
               int size = 21, int left = 0, int windowWidth = 0,
               bool subdued = false);
  // Layout once, then render windows in order before returning to the caller.
  std::vector<Bitmap> paintBatch(const Block &block, int width,
                                const std::vector<RichWindow> &windows,
                                int size = 21, bool subdued = false);
#ifndef PENCHAT_NATIVE_DEVICE
  RichWorkStats workStats() const;
  void resetWorkStats();
#endif
  void clearCache();
  void setMetrics(const UiMetrics &metrics);
};
} // namespace penchat
