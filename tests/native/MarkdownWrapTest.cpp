#include "markdown.hpp"
#include "stb_image_write.h"
#include <cassert>
#include <iostream>
using namespace penchat;

static bool closing(const std::string &ch) {
  const std::string punctuation = u8"，。！？；：、）】》〉」』〕〗〙〛’”…,.!?;:%)]}℃°";
  return punctuation.find(ch) != std::string::npos;
}
static bool opening(const std::string &ch) {
  const std::string punctuation = u8"（【《〈「『〔〖〘〚“‘([{“";
  return punctuation.find(ch) != std::string::npos;
}
static void check(RichText &rich, const Block &block, int width, int size) {
  const auto text = blockPlainText(block);
  const auto layout = rich.layout(block, width, size, true);
  const auto normal = rich.layout(block, width, size);
  assert(layout.width == normal.width && layout.height == normal.height);
  for (size_t i = 0; i < layout.textHits.size(); ++i) {
    const auto &hit = layout.textHits[i];
    assert(hit.begin < hit.end && hit.end <= text.size());
    if (!i) continue;
    const auto &previous = layout.textHits[i - 1];
    const auto between = text.substr(previous.end, hit.begin - previous.end);
    if (between.find('\n') != std::string::npos || between.find(' ') != std::string::npos)
      continue; // Explicit breaks and spaces remain authored boundaries.
    const auto ch = text.substr(hit.begin, hit.end - hit.begin);
    const auto before = text.substr(previous.begin, previous.end - previous.begin);
    if (closing(ch) || opening(before)) {
      // Formula and text baselines differ, but must share a physical line.
      assert(hit.y < previous.y + previous.h && previous.y < hit.y + hit.h);
    }
  }
}
int main(int argc, char **argv) {
  assert(argc >= 3);
  RichText rich(argv[1], argv[2]);
  const auto doc = parseMarkdown(
      u8"中文标点应随前文，句号也一样。测试（括号）与【引号】《书名》：“结束！”\n\n"
      "Words **hel**lo, *world*! don't don’t stop. A [linked](https://example.test) word.\n\n"
      u8"中文**加粗**，*斜体*。`内联代码`；公式 $x^2$，结束。\n\n"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ.\n\n"
      u8"| 说明 | 结果 |\n|---|---|\n| （中文），以及完整的收尾。 | hello, world! |\n");
  size_t cases = 0;
  for (const int size : {16, 21, 32}) {
    for (int width = 36; width <= 600; width += 7) {
      for (auto block : doc.blocks) {
        if (block.kind == "table") {
          // Table cells use the same paragraph flow, including style boundaries.
          for (const auto &cell : block.cells) {
            Block p; p.runs = cell; check(rich, p, width, size); ++cases;
          }
        } else { check(rich, block, width, size); ++cases; }
      }
    }
  }
  Block split;
  split.runs = {{u8"中文中文中 ", "", 0}, {"hel", "word", Bold},
                {"lo", "word", Italic}, {",", "word", 0}, {" world!", "next", 0}};
  const auto words = rich.layout(split, 150);
  int y = -1;
  for (const auto &hit : words.links) if (hit.target == "word") {
    if (y < 0) y = hit.y;
    assert(y == hit.y);
  }
  split.runs = {{std::string(90, 'W') + ".", "long", 0}};
  const auto longWord = rich.layout(split, 100);
  assert(longWord.links.size() == 91);
  assert(longWord.links.back().y > longWord.links.front().y);
  for (const auto &hit : longWord.links) assert(hit.x + hit.w <= 95);
  split.kind = "code";
  const auto code = rich.layout(split, 100);
  assert(code.width > 100 && code.links.front().y == code.links.back().y);
  split.kind = "p"; split.runs = {{u8"字\n，主动换行", "explicit", 0}};
  const auto hardBreak = rich.layout(split, 200);
  assert(hardBreak.links[1].y > hardBreak.links[0].y);
  // A punctuation pair wider than the viewport stays intact and scrollable.
  split.runs = {{u8"字。", "narrow", 0}};
  const auto narrow = rich.layout(split, 20);
  assert(narrow.width > 20 && narrow.links[0].y == narrow.links[1].y);
  if (argc > 3) {
    for (const int width : {160, 528, 548, 824}) {
      auto demo = parseMarkdown(u8"标点跟随前文，句号不单独换行。\n（括号内的文字）与“引用内容”，都有完整的收尾。\n\n"
                                "A **styled** word, an English sentence!\n\n"
                                u8"公式 $x^2$，以及 `内联代码`。", false);
      int height = 12;
      for (const auto &block : demo.blocks) height += rich.layout(block, width, 21).height + 8;
      auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
      auto c = cairo_create(surface);
      cairo_set_source_rgb(c, 0, 0, 0); cairo_paint(c);
      int top = 6;
      for (const auto &block : demo.blocks) {
        auto layout = rich.layout(block, width, 21);
        auto bitmap = rich.paint(block, width, 0, layout.height, 21);
        auto image = cairo_image_surface_create_for_data(bitmap.pixels.data(), CAIRO_FORMAT_ARGB32,
                                                        bitmap.width, bitmap.height, bitmap.width * 4);
        cairo_set_source_surface(c, image, 0, top); cairo_paint(c);
        cairo_surface_destroy(image); top += layout.height + 8;
      }
      const auto path = std::string(argv[3]) + "/wrap-" + std::to_string(width) + ".png";
      cairo_surface_flush(surface);
      const auto *pixels = cairo_image_surface_get_data(surface);
      std::vector<uint8_t> rgba(size_t(width) * height * 4);
      for (size_t pixel = 0; pixel < rgba.size(); pixel += 4) {
        rgba[pixel] = pixels[pixel + 2]; rgba[pixel + 1] = pixels[pixel + 1];
        rgba[pixel + 2] = pixels[pixel]; rgba[pixel + 3] = pixels[pixel + 3];
      }
      assert(stbi_write_png(path.c_str(), width, height, 4, rgba.data(), width * 4));
      cairo_destroy(c); cairo_surface_destroy(surface);
    }
  }
  std::cout << "Markdown punctuation wrapping: " << cases << " cases passed\n";
}
