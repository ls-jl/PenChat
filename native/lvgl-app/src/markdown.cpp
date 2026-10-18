#include "markdown.hpp"
#include "latex.h"
#include "md4c.h"
#include "theme.hpp"
extern "C" {
#include "entity.h"
}
#include "tex_cairo.hpp"
#include <cairo-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cctype>
#include <cmath>
#include <cstring>
#include <list>
#include <stdexcept>
namespace penchat {
namespace {
struct Parser {
  Document doc;
  Block *block = nullptr;
  int indent = 0, quote = 0, style = 0;
  std::vector<int> styles, ordered;
  std::vector<std::string> links;
  bool cell = false;
  std::string marker;
  void begin(std::string kind = "p", int level = 0) {
    Block b;
    b.kind = kind;
    b.level = level;
    b.indent = indent;
    b.quote = quote;
    b.marker = marker;
    marker.clear();
    doc.blocks.push_back(std::move(b));
    block = &doc.blocks.back();
  }
  std::vector<Run> &runs() {
    if (!block)
      begin();
    return cell ? block->cells.back() : block->runs;
  }
};
int enterBlock(MD_BLOCKTYPE type, void *detail, void *ctx) {
  auto &p = *static_cast<Parser *>(ctx);
  switch (type) {
  case MD_BLOCK_QUOTE:
    ++p.quote;
    break;
  case MD_BLOCK_UL:
    p.ordered.push_back(0);
    ++p.indent;
    break;
  case MD_BLOCK_OL:
    p.ordered.push_back(static_cast<MD_BLOCK_OL_DETAIL *>(detail)->start);
    ++p.indent;
    break;
  case MD_BLOCK_LI: {
    auto d = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
    int &n = p.ordered.back();
    p.marker = d->is_task ? (d->task_mark == ' ' ? "☐ " : "☑ ")
                          : (n ? std::to_string(n++) + ". " : "• ");
    p.block = nullptr;
    break;
  }
  case MD_BLOCK_H:
    p.begin("h", static_cast<MD_BLOCK_H_DETAIL *>(detail)->level);
    break;
  case MD_BLOCK_P:
    p.begin();
    break;
  case MD_BLOCK_CODE:
    p.begin("code");
    break;
  case MD_BLOCK_HR:
    p.begin("hr");
    break;
  case MD_BLOCK_TR:
    p.begin("table");
    break;
  case MD_BLOCK_TH:
  case MD_BLOCK_TD:
    if (!p.block || p.block->kind != "table")
      p.begin("table");
    p.block->cells.emplace_back();
    p.block->header = type == MD_BLOCK_TH;
    p.cell = true;
    break;
  default:
    break;
  }
  return 0;
}
int leaveBlock(MD_BLOCKTYPE type, void *, void *ctx) {
  auto &p = *static_cast<Parser *>(ctx);
  if (type == MD_BLOCK_QUOTE)
    --p.quote;
  else if (type == MD_BLOCK_UL || type == MD_BLOCK_OL) {
    --p.indent;
    p.ordered.pop_back();
  } else if (type == MD_BLOCK_TH || type == MD_BLOCK_TD)
    p.cell = false;
  else if (type == MD_BLOCK_P || type == MD_BLOCK_H || type == MD_BLOCK_CODE ||
           type == MD_BLOCK_TR)
    p.block = nullptr;
  return 0;
}
int enterSpan(MD_SPANTYPE type, void *detail, void *ctx) {
  auto &p = *static_cast<Parser *>(ctx);
  p.styles.push_back(p.style);
  p.links.push_back(p.links.empty() ? "" : p.links.back());
  switch (type) {
  case MD_SPAN_EM:
    p.style |= Italic;
    break;
  case MD_SPAN_STRONG:
    p.style |= Bold;
    break;
  case MD_SPAN_DEL:
    p.style |= Strike;
    break;
  case MD_SPAN_CODE:
    p.style |= Mono;
    break;
  case MD_SPAN_LATEXMATH:
    p.style |= Math;
    break;
  case MD_SPAN_LATEXMATH_DISPLAY:
    p.style |= Math | DisplayMath;
    break;
  case MD_SPAN_A: {
    auto d = static_cast<MD_SPAN_A_DETAIL *>(detail);
    p.links.back() = std::string(d->href.text, d->href.size);
    break;
  }
  case MD_SPAN_IMG: {
    auto d = static_cast<MD_SPAN_IMG_DETAIL *>(detail);
    p.style |= Image;
    p.links.back() = std::string(d->src.text, d->src.size);
    break;
  }
  default:
    break;
  }
  return 0;
}
int leaveSpan(MD_SPANTYPE, void *, void *ctx) {
  auto &p = *static_cast<Parser *>(ctx);
  p.style = p.styles.back();
  p.styles.pop_back();
  p.links.pop_back();
  return 0;
}
int textCallback(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size,
                 void *ctx) {
  auto &p = *static_cast<Parser *>(ctx);
  std::string t(text, size);
  if (type == MD_TEXT_ENTITY) {
    if (t.size() > 3 && t[1] == '#') {
      bool hex = t.size() > 4 && (t[2] == 'x' || t[2] == 'X');
      auto cp = strtoul(t.c_str() + (hex ? 3 : 2), nullptr, hex ? 16 : 10);
      t = utf8(cp > 0 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff)
                   ? cp
                   : 0xfffd);
    } else if (auto entity = entity_lookup(t.data(), t.size())) {
      t = utf8(entity->codepoints[0]);
      if (entity->codepoints[1])
        t += utf8(entity->codepoints[1]);
    }
  }
  if (type == MD_TEXT_BR)
    t = "\n";
  if (type == MD_TEXT_SOFTBR)
    t = "\n";
  int style = p.style;
  if (p.block && p.block->kind == "code")
    style |= Mono;
  auto link = p.links.empty() ? "" : p.links.back();
  auto &r = p.runs();
  if (!r.empty() && r.back().style == style && r.back().link == link)
    r.back().text += t;
  else
    r.push_back({t, link, style});
  return 0;
}
// Normalize alternate math delimiters outside fenced and inline code.
std::string normalizeMath(const std::string &input) {
  std::string out;
  size_t i = 0;
  char fence = 0;
  size_t fenceCount = 0;
  while (i < input.size()) {
    bool lineStart = i == 0 || input[i - 1] == '\n';
    if (lineStart) {
      size_t at = i;
      while (at < input.size() && at - i < 3 && input[at] == ' ')
        ++at;
      if (at < input.size() && (input[at] == '`' || input[at] == '~')) {
        size_t end = at;
        while (end < input.size() && input[end] == input[at])
          ++end;
        if (end - at >= 3) {
          if (!fence) {
            fence = input[at];
            fenceCount = end - at;
          } else if (fence == input[at] && end - at >= fenceCount)
            fence = 0;
          auto lineEnd = input.find('\n', end);
          if (lineEnd == std::string::npos)
            lineEnd = input.size();
          else
            ++lineEnd;
          out += input.substr(i, lineEnd - i);
          i = lineEnd;
          continue;
        }
      }
    }
    if (fence) {
      out += input[i++];
      continue;
    }
    if (input[i] == '`') {
      size_t end = i;
      while (end < input.size() && input[end] == '`')
        ++end;
      auto ticks = input.substr(i, end - i);
      auto close = input.find(ticks, end);
      if (close == std::string::npos) {
        out += input.substr(i);
        break;
      }
      out += input.substr(i, close + ticks.size() - i);
      i = close + ticks.size();
      continue;
    }
    if (input[i] == '\\' && i + 1 < input.size()) {
      char open = input[i + 1];
      if (open == '(' || open == '[') {
        char close = open == '(' ? ')' : ']';
        auto at = input.find(std::string("\\") + close, i + 2);
        if (at != std::string::npos) {
          std::string delimiter = open == '(' ? "$" : "$$";
          out += delimiter + input.substr(i + 2, at - i - 2) + delimiter;
          i = at + 2;
          continue;
        }
      }
      out += input.substr(i, 2);
      i += 2;
      continue;
    }
    out += input[i++];
  }
  return out;
}
// An unfinished stream must not hide Markdown delimiters or code fences.
bool incomplete(const std::string &source) {
  std::map<std::string, bool> spans;
  int brackets = 0;
  for (size_t i = 0; i < source.size();) {
    const char c = source[i];
    if (c == '\\') {
      if (i + 1 < source.size() &&
          (source[i + 1] == '(' || source[i + 1] == '[')) {
        auto close = source.find(source[i + 1] == '(' ? "\\)" : "\\]", i + 2);
        if (close == std::string::npos)
          return true;
        i = close + 2;
      } else
        i += std::min(size_t(2), source.size() - i);
      continue;
    }
    if (c == '`' || c == '$' || c == '~') {
      size_t end = i + 1;
      while (end < source.size() && source[end] == c)
        ++end;
      const auto delimiter = source.substr(i, end - i);
      if (c == '~' && delimiter.size() < 3) {
        if (delimiter.size() == 2)
          spans[delimiter] = !spans[delimiter];
        i = end;
        continue;
      }
      auto close = source.find(delimiter, end);
      while (close != std::string::npos && close > 0 &&
             source[close - 1] == '\\')
        close = source.find(delimiter, close + delimiter.size());
      if (close == std::string::npos)
        return true;
      i = close + delimiter.size();
      continue;
    }
    if (c == '*' || c == '_') {
      size_t end = i + 1;
      while (end < source.size() && source[end] == c)
        ++end;
      bool bullet = (i == 0 || source[i - 1] == '\n' || source[i - 1] == ' ') &&
                    end < source.size() && source[end] == ' ';
      bool word = c == '_' && i > 0 && end < source.size() &&
                  std::isalnum(static_cast<unsigned char>(source[i - 1])) &&
                  std::isalnum(static_cast<unsigned char>(source[end]));
      if (!bullet && !word)
        spans[source.substr(i, end - i)] = !spans[source.substr(i, end - i)];
      i = end;
      continue;
    }
    if (c == '[')
      ++brackets;
    if (c == ']' && brackets > 0)
      --brackets;
    ++i;
  }
  if (brackets)
    return true;
  for (const auto &span : spans)
    if (span.second)
      return true;
  return false;
}
} // namespace
Document parseMarkdown(const std::string &source, bool streaming) {
  Parser p;
  p.doc.source = source;
  if (streaming && incomplete(source)) {
    Block raw;
    raw.runs.push_back({source, "", 0});
    p.doc.blocks.push_back(std::move(raw));
    return std::move(p.doc);
  }
  auto input = normalizeMath(source);
  MD_PARSER parser{};
  parser.flags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_NOHTML;
  parser.enter_block = enterBlock;
  parser.leave_block = leaveBlock;
  parser.enter_span = enterSpan;
  parser.leave_span = leaveSpan;
  parser.text = textCallback;
  if (md_parse(input.data(), input.size(), &parser, &p))
    throw std::runtime_error("Markdown parse failed");
  return std::move(p.doc);
}
struct Piece {
  std::string text, target;
  int style = 0;
  float x = 0, y = 0, width = 0, height = 0, baseline = 0;
  std::shared_ptr<Bitmap> math;
};
struct RichText::Impl {
  FT_Library library = nullptr;
  FT_Face face = nullptr, monoFace = nullptr;
  cairo_font_face_t *cairoFace = nullptr, *monoCairo = nullptr;
  std::map<std::string, std::shared_ptr<Bitmap>> cache;
  std::deque<std::string> order;
  size_t cacheBytes = 0;
  std::vector<Piece> pieces;
  void select(cairo_t *c, int size, int style, bool cjk = false) {
    cairo_set_font_face(c, (style & Mono) && !cjk && monoCairo ? monoCairo
                                                               : cairoFace);
    cairo_matrix_t matrix;
    cairo_matrix_init_scale(&matrix, size, size);
    if (style & Italic)
      matrix.xy = -size * .18;
    cairo_set_font_matrix(c, &matrix);
  }
};
RichText::RichText(const std::string &fontPath, const std::string &res)
    : p_(new Impl) {
  if (FT_Init_FreeType(&p_->library) ||
      FT_New_Face(p_->library, fontPath.c_str(), 0, &p_->face))
    throw std::runtime_error("Cannot initialize text renderer");
  p_->cairoFace = cairo_ft_font_face_create_for_ft_face(p_->face, 0);
  if (!FT_New_Face(p_->library,
                   join(res, "fonts/latin/optional/cmtt10.ttf").c_str(), 0,
                   &p_->monoFace))
    p_->monoCairo = cairo_ft_font_face_create_for_ft_face(p_->monoFace, 0);
  setFallbackFont(fontPath);
  tex::LaTeX::init(res);
}
RichText::~RichText() {
  tex::LaTeX::release();
  if (p_->monoCairo)
    cairo_font_face_destroy(p_->monoCairo);
  if (p_->monoFace)
    FT_Done_Face(p_->monoFace);
  cairo_font_face_destroy(p_->cairoFace);
  FT_Done_Face(p_->face);
  FT_Done_FreeType(p_->library);
}
void RichText::clearCache() {
  p_->cache.clear();
  p_->order.clear();
  p_->cacheBytes = 0;
}
Bitmap RichText::formula(const std::string &source, int size) {
  auto key = std::to_string(size) + ":" + source;
  auto it = p_->cache.find(key);
  if (it != p_->cache.end())
    return *it->second;
  if (source.size() > 8192)
    throw std::runtime_error("Formula too large");
  std::unique_ptr<tex::TeXRender> r(
      tex::LaTeX::parse(tex::utf82wide(source), 4096, size, size * .2,
                        (0xff000000u | theme::text)));
  Bitmap b;
  b.width = std::max(1, r->getWidth() + 4);
  b.height = std::max(1, r->getHeight() + 4);
  b.baseline = std::round(r->getBaseline() * r->getHeight()) + 2;
  if (b.width > 4096 || b.height > 2048)
    throw std::runtime_error("Formula dimensions exceed limits");
  b.pixels.resize(size_t(b.width) * b.height * 4);
  auto s = cairo_image_surface_create_for_data(
      b.pixels.data(), CAIRO_FORMAT_ARGB32, b.width, b.height, b.width * 4);
  auto c = cairo_create(s);
  CairoGraphics g(c);
  r->draw(g, 2, 2);
  cairo_surface_flush(s);
  cairo_destroy(c);
  cairo_surface_destroy(s);
  auto n = b.pixels.size();
  while (!p_->order.empty() && p_->cacheBytes + n > 8 * 1024 * 1024) {
    auto old = p_->order.front();
    p_->order.pop_front();
    p_->cacheBytes -= p_->cache[old]->pixels.size();
    p_->cache.erase(old);
  }
  if (n <= 8 * 1024 * 1024) {
    p_->cache[key] = std::make_shared<Bitmap>(b);
    p_->order.push_back(key);
    p_->cacheBytes += n;
  }
  return b;
}
void RichText::prepare(Document &document, int width, int size) {
  for (size_t i = 0; i < document.blocks.size();) {
    if (document.blocks[i].kind != "table") {
      ++i;
      continue;
    }
    size_t end = i;
    std::vector<int> widths;
    while (end < document.blocks.size() &&
           document.blocks[end].kind == "table") {
      auto &row = document.blocks[end];
      widths.resize(std::max(widths.size(), row.cells.size()), 140);
      for (size_t column = 0; column < row.cells.size(); ++column) {
        Block cell;
        cell.kind = "code";
        cell.runs = row.cells[column];
        widths[column] =
            std::max(widths[column], layout(cell, 1, size).width + 14);
      }
      ++end;
    }
    if (!widths.empty())
      for (auto &value : widths)
        value = std::max(value, width / int(widths.size()));
    while (i < end)
      document.blocks[i++].columnWidths = widths;
  }
}
RichLayout RichText::layout(const Block &block, int width, int size) {
  p_->pieces.clear();
  int actualSize =
      block.kind == "h" ? std::max(size, 31 - block.level * 2) : size;
  auto surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto c = cairo_create(surf);
  float left = block.quote * 12 + std::max(0, block.indent - 1) * 20 + 6;
  float x = left, y = 5, lineTop = 0, ascent = actualSize, descent = 5,
        maxWidth = width;
  size_t lineStart = 0;
  auto newline = [&]() {
    for (size_t i = lineStart; i < p_->pieces.size(); ++i)
      p_->pieces[i].y = y + ascent - p_->pieces[i].baseline;
    y += ascent + descent + 5;
    x = left;
    lineStart = p_->pieces.size();
    ascent = actualSize;
    descent = 5;
    lineTop = y;
  };
  (void)lineTop;
  auto add = [&](const Run &run, int forceWidth) {
    if ((run.style & DisplayMath) && x > left)
      newline();
    if (run.style & Math) {
      try {
        auto bitmap = std::make_shared<Bitmap>(formula(run.text, actualSize));
        if (!forceWidth && x + bitmap->width > width && x > left)
          newline();
        Piece p;
        p.math = bitmap;
        p.x = x;
        p.width = bitmap->width;
        p.height = bitmap->height;
        p.baseline = bitmap->baseline;
        p.style = run.style;
        ascent = std::max(ascent, p.baseline);
        descent = std::max(descent, p.height - p.baseline);
        x += p.width;
        maxWidth = std::max(maxWidth, x + 6);
        p_->pieces.push_back(std::move(p));
        if (run.style & DisplayMath)
          newline();
        return;
      } catch (...) {
      }
    }
    p_->select(c, actualSize, run.style);
    for (auto &ch :
         characters(run.style & Image ? ("[图片] " + run.text) : run.text)) {
      if (ch == "\n") {
        newline();
        continue;
      }
      p_->select(c, actualSize, run.style,
                 static_cast<unsigned char>(ch[0]) >= 128);
      cairo_text_extents_t e;
      cairo_text_extents(c, ch.c_str(), &e);
      float advance =
          (run.style & Mono)
              ? float(actualSize * .62) *
                    (static_cast<unsigned char>(ch[0]) >= 128 ? 2 : 1)
              : e.x_advance;
      if (!forceWidth && block.kind != "code" && x + advance > width - 6 &&
          x > left)
        newline();
      Piece p;
      p.text = ch == "\t" ? " " : ch;
      if (ch == "\t")
        advance =
            actualSize * .62 * (4 - int((x - left) / (actualSize * .62)) % 4);
      p.x = x;
      p.baseline = actualSize;
      p.height = actualSize + 5;
      p.width = advance;
      p.style = run.style | (block.header ? Bold : 0);
      p.target = run.link;
      p_->pieces.push_back(std::move(p));
      x += advance;
      maxWidth = std::max(maxWidth, x + 6);
    }
  };
  if (block.kind == "table") {
    float cellX = left;
    int cols = std::max(1, int(block.cells.size()));
    int cellW = std::max(140, (width - int(left)) / cols);
    size_t column = 0;
    for (auto &cell : block.cells) {
      x = cellX;
      for (auto &r : cell)
        add(r, 1);
      cellX += column < block.columnWidths.size()
                   ? block.columnWidths[column]
                   : std::max(float(cellW), x - cellX + 15);
      ++column;
      x = cellX;
    }
    maxWidth = std::max(maxWidth, cellX);
  } else {
    if (!block.marker.empty())
      add({block.marker, "", 0}, 0);
    for (auto &r : block.runs)
      add(r, 0);
  }
  newline();
  RichLayout layout;
  layout.width = std::ceil(maxWidth);
  layout.height = block.kind == "hr" ? 12 : std::max(20, int(std::ceil(y + 4)));
  for (auto &p : p_->pieces)
    if (!p.target.empty())
      layout.links.push_back(
          {int(p.x), int(p.y), int(p.width), int(p.height), p.target});
  cairo_destroy(c);
  cairo_surface_destroy(surf);
  return layout;
}
Bitmap RichText::paint(const Block &block, int width, int top, int height,
                       int size, int left, int windowWidth, bool subdued) {
  auto info = layout(block, width, size);
  Bitmap b;
  b.width =
      windowWidth > 0 ? std::min(windowWidth, info.width - left) : info.width;
  b.width = std::max(1, b.width);
  b.height = height;
  b.pixels.resize(size_t(b.width) * height * 4);
  auto surf = cairo_image_surface_create_for_data(
      b.pixels.data(), CAIRO_FORMAT_ARGB32, b.width, height, b.width * 4);
  auto c = cairo_create(surf);
  cairo_translate(c, -left, -top);
  auto color = [c](uint32_t value) {
    cairo_set_source_rgb(c, ((value >> 16) & 255) / 255.,
                         ((value >> 8) & 255) / 255., (value & 255) / 255.);
  };
  if (block.kind == "code" || block.kind == "table") {
    color(theme::code);
    cairo_rectangle(c, left, top, b.width, height);
    cairo_fill(c);
  }
  if (block.quote) {
    color(theme::muted);
    cairo_rectangle(c, 1, top, 3, height);
    cairo_fill(c);
  }
  if (block.kind == "hr") {
    color(theme::border);
    cairo_rectangle(c, 3, 5, width - 6, 1);
    cairo_fill(c);
  }
  int actual = block.kind == "h" ? std::max(size, 31 - block.level * 2) : size;
  for (auto &p : p_->pieces) {
    if (p.y + p.height < top || p.y > top + height || p.x + p.width < left ||
        p.x > left + b.width)
      continue;
    if (p.math) {
      auto m = cairo_image_surface_create_for_data(
          p.math->pixels.data(), CAIRO_FORMAT_ARGB32, p.math->width,
          p.math->height, p.math->width * 4);
      if (subdued) {
        color(theme::reasoning);
        cairo_mask_surface(c, m, p.x, p.y);
      } else {
        cairo_set_source_surface(c, m, p.x, p.y);
        cairo_paint(c);
      }
      cairo_surface_destroy(m);
      continue;
    }
    p_->select(c, actual, p.style,
               !p.text.empty() && static_cast<unsigned char>(p.text[0]) >= 128);
    if (!p.target.empty())
      color(subdued ? theme::reasoningLink : theme::link);
    else
      color(subdued ? theme::reasoning : theme::text);
    if (p.style & Mono) {
      color(theme::surface);
      cairo_rectangle(c, p.x, p.y, p.width, p.height);
      cairo_fill(c);
      color(subdued ? theme::reasoning : theme::text);
    }
    cairo_move_to(c, p.x, p.y + p.baseline);
    cairo_show_text(c, p.text.c_str());
    if (p.style & Bold) {
      cairo_move_to(c, p.x + .5, p.y + p.baseline);
      cairo_show_text(c, p.text.c_str());
    }
    if ((p.style & Strike) || !p.target.empty()) {
      cairo_rectangle(
          c, p.x, p.y + (p.style & Strike ? p.baseline * .6 : p.baseline + 2),
          p.width, 1);
      cairo_fill(c);
    }
  }
  cairo_surface_flush(surf);
  cairo_destroy(c);
  cairo_surface_destroy(surf);
  return b;
}
} // namespace penchat
