#include "markdown.hpp"
#include "theme.hpp"
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cassert>
#include <cmath>
#include <iostream>
#include <map>

using namespace penchat;

static uint32_t opaqueColor(const Bitmap &bitmap) {
  for (size_t i = 0; i + 3 < bitmap.pixels.size(); i += 4)
    if (bitmap.pixels[i + 3] == 255)
      return uint32_t(bitmap.pixels[i + 2]) << 16 |
          uint32_t(bitmap.pixels[i + 1]) << 8 | bitmap.pixels[i];
  assert(false && "expected an opaque glyph interior");
  return 0;
}
static const RichLayout::TextHit &hitAt(const RichLayout &layout, size_t offset) {
  for (const auto &hit : layout.textHits) if (hit.begin == offset) return hit;
  assert(false && "missing text offset");
  return layout.textHits.front();
}
static void checkOffsets(const Block &block, const RichLayout &layout) {
  const auto text = blockPlainText(block);
  size_t previous = 0;
  for (const auto &hit : layout.textHits) {
    assert(hit.begin >= previous && hit.begin < hit.end && hit.end <= text.size());
    assert((static_cast<unsigned char>(text[hit.begin]) & 0xc0) != 0x80);
    assert(hit.x >= 0 && hit.y >= 0 && hit.y + hit.h <= layout.height);
    assert(layout.selectionEndpoint(hit.begin, false));
    assert(layout.selectionEndpoint(hit.end, true));
    previous = hit.end;
  }
}
static void compareReference(const Bitmap &bitmap, cairo_font_face_t *face, int lineHeight) {
  std::vector<uint8_t> pixels(bitmap.pixels.size());
  auto surface = cairo_image_surface_create_for_data(pixels.data(), CAIRO_FORMAT_ARGB32,
      bitmap.width, bitmap.height, bitmap.width * 4);
  auto c = cairo_create(surface);
  cairo_set_font_face(c, face);
  cairo_set_font_size(c, 21);
  cairo_font_extents_t font;
  cairo_font_extents(c, &font);
  const float ascent = font.ascent, descent = font.descent;
  const float height = std::max(float(lineHeight), ascent + descent);
  cairo_set_source_rgb(c, 1, 1, 1);
  cairo_move_to(c, 6, (height - ascent - descent) / 2 + ascent);
  cairo_show_text(c, "W");
  cairo_surface_flush(surface);
  // Alpha must match a single drawing with the actual face, pixel for pixel.
  for (size_t i = 3; i < pixels.size(); i += 4) assert(pixels[i] == bitmap.pixels[i]);
  cairo_destroy(c); cairo_surface_destroy(surface);
}
static void fontWeights(RichText &rich, const std::string &regular) {
  assert(rich.hasTrueBold());
  const auto slash = regular.find_last_of('/');
  const auto path = regular.substr(0, slash + 1) + "chinese-bold.ttf";
  FT_Library library;
  FT_Face bold;
  assert(!FT_Init_FreeType(&library));
  assert(!FT_New_Face(library, path.c_str(), 0, &bold));
  assert(bold->style_flags & FT_STYLE_FLAG_BOLD);
  auto face = cairo_ft_font_face_create_for_ft_face(bold, 0);
  auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto c = cairo_create(surface);
  cairo_set_font_face(c, face);
  cairo_set_font_size(c, 21);
  cairo_text_extents_t extents;
  cairo_text_extents(c, "W", &extents);
  Block strong; strong.runs = {{"W", "", Bold}};
  const auto boldLayout = rich.layout(strong, 100, 21, true);
  assert(boldLayout.textHits.front().w == int(std::ceil(extents.x_advance)));
  Block normal; normal.runs = {{"W", "", 0}};
  auto regularBitmap = rich.paint(normal, 100, 0, rich.layout(normal, 100).height);
  auto boldBitmap = rich.paint(strong, 100, 0, boldLayout.height);
  assert(regularBitmap.pixels != boldBitmap.pixels);
  compareReference(boldBitmap, face, 34);
  FT_Face regularFace;
  assert(!FT_New_Face(library, regular.c_str(), 0, &regularFace));
  auto regularCairo = cairo_ft_font_face_create_for_ft_face(regularFace, 0);
  compareReference(regularBitmap, regularCairo, 34);
  cairo_font_face_destroy(regularCairo); FT_Done_Face(regularFace);
  // Headings must render their regular text through the same true Bold face.
  Block heading = normal; heading.kind = "h"; heading.level = 6;
  const auto headingBitmap = rich.paint(heading, 100, 0, rich.layout(heading, 100).height);
  assert(headingBitmap.pixels != regularBitmap.pixels);
  compareReference(headingBitmap, face, 28);
  cairo_destroy(c); cairo_surface_destroy(surface); cairo_font_face_destroy(face);
  FT_Done_Face(bold); FT_Done_FreeType(library);
}
static void typography(RichText &rich) {
  Block body; body.runs = {{"Alpha\nBeta", "", 0}};
  auto type = rich.typography(body);
  assert(type.fontSize == 21 && type.lineHeight == 34);
  assert(type.before == 15 && type.after == 15);
  assert(rich.blockGap(nullptr, body) == 0);
  auto layout = rich.layout(body, 400, 21, true);
  assert(hitAt(layout, 6).y - hitAt(layout, 0).y == 34);
  assert(layout.height == 68);
  checkOffsets(body, layout);
  const int sizes[] = {29, 27, 25, 23, 22, 21};
  for (int level = 1; level <= 6; ++level) {
    Block heading = body; heading.kind = "h"; heading.level = level;
    const auto h = rich.typography(heading);
    assert(h.fontSize == sizes[level - 1]);
    assert(h.lineHeight == int(std::ceil(h.fontSize * 1.3)));
    assert(rich.blockGap(&body, heading) == std::max(type.after, h.before));
    assert(rich.blockGap(&heading, body) == std::max(h.after, type.before));
    checkOffsets(heading, rich.layout(heading, 400, 21, true));
  }
  Block code = body; code.kind = "code";
  assert(rich.typography(code).lineHeight == 31);
  const auto codeLayout = rich.layout(code, 30, 21, true);
  assert(codeLayout.width > 30);
  assert(hitAt(codeLayout, 6).y - hitAt(codeLayout, 0).y == 31);
  auto large = rich.typography(body, 42);
  assert(large.fontSize == 42 && large.lineHeight == 68 && large.after == 29);
  Block math; math.runs = {{"before ", "", 0}, {"\\frac{a}{\\frac{b}{c}}", "", Math}, {" after", "", 0}};
  checkOffsets(math, rich.layout(math, 400, 21, true));
}
static void lists(RichText &rich) {
  auto doc = parseMarkdown(
      "999. **First** paragraph with a [link](https://example.test) and enough words to wrap repeatedly.\n\n"
      "     Second paragraph in the same item, also wrapping onto several lines.\n\n"
      "1000. Next item remains aligned with its sibling.\n\n"
      "      - Nested item with enough words for wrapping.\n"
      "        - Deeper item with enough words for wrapping.\n"
      "          - Third nested item with enough words for wrapping.\n"
      "            - Fourth nested item with enough words for wrapping.\n"
      "              - Fifth nested item with enough words for wrapping.\n\n"
      "Outside list.\n");
  rich.prepare(doc, 300);
  assert(doc.blocks.size() >= 9);
  assert(doc.blocks[0].listItemId && doc.blocks[0].listItemId == doc.blocks[1].listItemId);
  assert(doc.blocks[1].marker.empty());
  assert(blockPlainText(doc.blocks[0]) == "999. First paragraph with a link and enough words to wrap repeatedly.");
  const auto firstLayout = rich.layout(doc.blocks[0], 300, 21, true);
  const auto firstPlain = blockPlainText(doc.blocks[0]);
  const size_t selection = firstPlain.find("First");
  const auto begin = firstLayout.selectionEndpoint(selection, false);
  const auto end = firstLayout.selectionEndpoint(selection + 5, true);
  assert(begin && end && firstPlain.substr(begin->begin, end->end - begin->begin) == "First");
  assert(!firstLayout.links.empty() && firstLayout.links.front().target == "https://example.test");
  assert(doc.blocks[0].listGroups == doc.blocks[2].listGroups);
  std::map<int, int> bodyStarts;
  int firstBodyX = -1;
  for (const auto &block : doc.blocks) {
    auto layout = rich.layout(block, 300, 21, true);
    checkOffsets(block, layout);
    if (block.listGroups.empty()) continue;
    const auto bodyOffset = block.marker.size();
    const int bodyX = hitAt(layout, bodyOffset).x;
    if (firstBodyX < 0) firstBodyX = bodyX;
    if (block.listGroups.size() == 1) assert(bodyX == firstBodyX);
    if (block.listGroups.size() > 1) assert(300 - 6 - bodyX >= 125);
    if (!bodyStarts.count(block.listItemId)) bodyStarts[block.listItemId] = bodyX;
    assert(bodyStarts[block.listItemId] == bodyX);
    std::map<int, int> rowStarts;
    for (const auto &hit : layout.textHits) {
      if (hit.begin < bodyOffset) continue;
      if (!rowStarts.count(hit.y)) rowStarts[hit.y] = hit.x;
    }
    for (const auto &row : rowStarts) assert(row.second == bodyX);
  }
  assert(rich.blockGap(&doc.blocks[0], doc.blocks[2]) == 13);
  assert(rich.blockGap(&doc.blocks[0], doc.blocks[1]) == 15);
  auto incomplete = parseMarkdown("1. **unfinished", true);
  assert(incomplete.blocks.size() == 1 && blockPlainText(incomplete.blocks.front()) == "1. **unfinished");
}
static void colors(RichText &rich) {
  Block body; body.runs = {{"M", "", 0}};
  Block heading = body; heading.kind = "h"; heading.level = 1;
  Bitmap darkFormula;
  for (auto mode : {theme::Mode::Dark, theme::Mode::Light}) {
    theme::setMode(mode);
    assert(opaqueColor(rich.paint(body, 100, 0, rich.layout(body, 100).height)) == theme::palette().richBody);
    assert(opaqueColor(rich.paint(heading, 100, 0, rich.layout(heading, 100).height)) == theme::palette().richHeading);
    const auto formula = rich.formula("x^2", 21);
    assert(opaqueColor(formula) == theme::palette().richBody);
    if (mode == theme::Mode::Dark) darkFormula = formula;
    else assert(formula.pixels != darkFormula.pixels);
    assert(opaqueColor(rich.formula("\\textcolor{red}{M}", 21)) == 0xff0000);
    Block math; math.runs = {{"\\textcolor{red}{M}", "", Math}};
    assert(opaqueColor(rich.paint(math, 150, 0, rich.layout(math, 150).height, 21, 0, 0, true)) == 0xff0000);
    const auto before = rich.formula("x^2", 21);
    rich.clearCache();
    assert(rich.formula("x^2", 21).pixels == before.pixels);
  }
  theme::setMode(theme::Mode::Dark);
}
int main(int argc, char **argv) {
  assert(argc >= 3);
  RichText rich(argv[1], argv[2], argc > 3 ? argv[3] : "");
  fontWeights(rich, argv[1]);
  typography(rich);
  lists(rich);
  colors(rich);
  std::cout << "Rich typography: real weights, spacing, hanging lists, offsets and themed formulas passed\n";
}
