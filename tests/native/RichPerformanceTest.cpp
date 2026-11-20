#include "markdown.hpp"
#include "theme.hpp"
#include <cassert>
#include <chrono>
#include <iomanip>
#include <iostream>

using namespace penchat;
namespace {
uint64_t digest = 1469598103934665603ULL;
void hash(size_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    digest ^= (uint64_t(value) >> shift) & 255;
    digest *= 1099511628211ULL;
  }
}
void hash(const Bitmap &bitmap) {
  hash(bitmap.width); hash(bitmap.height); hash(bitmap.baseline);
  for (auto byte : bitmap.pixels) {
    digest ^= byte;
    digest *= 1099511628211ULL;
  }
}
std::vector<RichWindow> windows(int width, int height) {
  std::vector<RichWindow> result;
  for (int y = 0; y < std::min(height, 512); y += 128)
    for (int x = 0; x < std::min(width, 1536); x += 512)
      result.push_back({y, std::min(128, height - y), x, 512});
  return result;
}
std::vector<std::string> fixtures() {
  std::string paragraph, code = "```text\n";
  for (int i = 0; i < 50; ++i)
    paragraph += u8"重复中英文（标点），a **styled** word, [linked](target)! ";
  for (int i = 0; i < 15; ++i)
    code += u8"\t中文\tABCD "+std::string(110, 'W')+"\n";
  code += "```";
  return {paragraph, code,
    u8"| 左 | 中 | 右 | 公式 |\n|:---|:---:|---:|---|\n"
    "| [left](a) | **middle** | `right` | $x^2$ |\n"
    u8"| 中文\t测试 | $\\frac{1}{2}$ | [✓∞](symbols) | $x^2$，结束。 |",
    u8"# 主题字号\n\n9. 第一段 $x^2$。\n\n   第二段。\n\n"
    u8"   - 内层 [link](b)，*斜体*。\n\n> 引用 ~~删除~~ `inline` ✓∞ 👨‍👩‍👧‍👦 á。\n\n"
    "$$\\frac{1}{2}$$\n\n$\\definitelyUnknownCommand{x}$"};
}
#ifndef PENCHAT_RICH_BASELINE
void same(const Bitmap &a, const Bitmap &b) {
  assert(a.width == b.width && a.height == b.height && a.baseline == b.baseline);
  assert(a.pixels == b.pixels);
}
#endif
} // namespace
int main(int argc, char **argv) {
  assert(argc >= 3);
  RichText rich(argv[1], argv[2], argc > 3 ? argv[3] : "");
  size_t batches = 0, tiles = 0;
  for (auto mode : {theme::Mode::Dark, theme::Mode::Light}) {
    theme::setMode(mode);
    for (int scale : {100, 150}) {
      rich.setMetrics(UiMetrics(100, scale));
      for (int size : {16, 21, 32}) {
        for (int width : {160, 548, 824}) {
          for (const auto &source : fixtures()) {
            auto document = parseMarkdown(source);
            rich.prepare(document, width, size);
            for (const auto &block : document.blocks) {
              const auto layout = rich.layout(block, width, size, true);
              hash(layout.width); hash(layout.height);
              for (const auto &hit : layout.textHits) {
                hash(hit.x); hash(hit.y); hash(hit.w); hash(hit.h);
                hash(hit.begin); hash(hit.end); hash(hit.atomic);
              }
              const auto regions = windows(layout.width, layout.height);
              std::vector<Bitmap> reference;
              for (const auto &window : regions) {
                reference.push_back(rich.paint(block, width, window.top, window.height,
                                               size, window.left, window.width));
                hash(reference.back());
              }
#ifndef PENCHAT_RICH_BASELINE
              rich.resetWorkStats();
              const auto batched = rich.paintBatch(block, width, regions, size);
              assert(batched.size() == reference.size());
              const auto work = rich.workStats();
              const uint64_t expectedLayouts = block.kind == "table"
                  ? 1 + std::max(block.cells.size(), block.columnWidths.size()) : 1;
              assert(work.layoutCalls == expectedLayouts);
              assert(work.formulaCopies == 0);
              for (size_t i = 0; i < batched.size(); ++i) same(batched[i], reference[i]);
              // Reasoning uses the same layout with its own formula/link colours.
              auto subdued = rich.paintBatch(block, width, regions, size, true);
              for (size_t i = 0; i < regions.size(); ++i) {
                const auto &w = regions[i];
                const auto single = rich.paint(block, width, w.top, w.height, size, w.left, w.width, true);
                same(subdued[i], single); hash(single);
              }
#else
              for (const auto &w : regions)
                hash(rich.paint(block, width, w.top, w.height, size, w.left, w.width, true));
#endif
              ++batches; tiles += regions.size();
            }
          }
        }
      }
    }
  }
  std::cout << "Equivalent raster fixtures: " << batches << " blocks, " << tiles
            << " tiles, digest " << std::hex << digest << std::dec << "\n";
#ifndef PENCHAT_RICH_BASELINE
  theme::setMode(theme::Mode::Dark); rich.setMetrics(UiMetrics());
  Block repeated;
  for (int i = 0; i < 1000; ++i) repeated.runs.push_back({u8"a中", "", 0});
  rich.resetWorkStats(); rich.layout(repeated, 548);
  assert(rich.workStats().glyphMeasurements == 2);
  std::cout << "Repeated 2000 glyphs: " << rich.workStats().glyphMeasurements << " metric measurements\n";
  rich.resetWorkStats();
  assert(rich.paintBatch(repeated, 548, {}).empty());
  assert(rich.workStats().layoutCalls == 0);
  Block math; math.runs = {{"x^2", "", Math}, {"x^2", "", Math}};
  rich.clearCache(); rich.resetWorkStats(); rich.layout(math, 548);
  assert(rich.workStats().formulaRenders == 1 && rich.workStats().formulaHits == 2);
  assert(rich.workStats().formulaCopies == 0);
  const auto first = rich.formula("x^2", 21);
  const auto second = rich.formula("x^2", 21);
  same(first, second); assert(rich.workStats().formulaCopies == 2);
  std::cout << "Two repeated formulas: 1 render, 2 internal hits, 0 internal pixel copies\n";
  const std::vector<RichWindow> fourTiles{{0,128,0,512},{128,128,0,512},
                                        {256,128,0,512},{384,128,0,512}};
  rich.resetWorkStats();
  for (const auto &w : fourTiles) rich.paint(repeated,548,w.top,w.height,21,w.left,w.width);
  const auto separateLayouts = rich.workStats().layoutCalls;
  rich.resetWorkStats(); rich.paintBatch(repeated,548,fourTiles);
  assert(separateLayouts == 4 && rich.workStats().layoutCalls == 1);
  std::cout << "Four new tiles: " << separateLayouts << " separate layouts, "
            << rich.workStats().layoutCalls << " batched layout\n";
#endif
  // A fixed host workload reports time without asserting a machine-specific limit.
  const auto source = fixtures().front();
  auto document = parseMarkdown(source);
  const auto &block = document.blocks.front();
  std::vector<double> elapsed;
  const std::vector<RichWindow> regions{{0,128,0,512},{128,128,0,512},
                                      {256,128,0,512},{384,128,0,512}};
  theme::setMode(theme::Mode::Dark); rich.setMetrics(UiMetrics());
  for (int run = 0; run < 7; ++run) {
    const auto start = std::chrono::steady_clock::now();
    for (int repeat = 0; repeat < 8; ++repeat) {
#ifndef PENCHAT_RICH_BASELINE
      auto pixels = rich.paintBatch(block, 548, regions);
#else
      std::vector<Bitmap> pixels;
      for (const auto &w : regions)
        pixels.push_back(rich.paint(block,548,w.top,w.height,21,w.left,w.width));
#endif
      assert(pixels.size() == regions.size());
    }
    elapsed.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  }
  std::sort(elapsed.begin(), elapsed.end());
  std::cout << "Host long-paragraph 8x4-tile median: " << std::fixed << std::setprecision(3)
            << elapsed[elapsed.size()/2] << " ms\n";
}
