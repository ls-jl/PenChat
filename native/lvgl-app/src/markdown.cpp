#include "markdown.hpp"
#include "latex.h"
#include "core/formula.h"
#include "core/parser.h"
#include "md4c.h"
#include "markdown_tables.hpp"
#include "theme.hpp"
#include "chemistry.hpp"
#include "latex_extensions.hpp"
#include "latex_native.hpp"
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
#include <functional>
#include <unordered_map>
#include <list>
#include <stdexcept>
namespace penchat {
namespace {
struct Parser {
  Document doc;
  std::map<std::string,std::string> mathSources;
  Block *block = nullptr;
  int indent = 0, quote = 0, style = 0, tableId = 0;
  std::vector<int> styles, ordered, listGroups, listItems;
  int nextListGroup = 0, nextListItem = 0;
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
    b.listGroups = listGroups;
    b.listItemId = listItems.empty() ? 0 : listItems.back();
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
    p.listGroups.push_back(++p.nextListGroup);
    p.listItems.push_back(0);
    ++p.indent;
    break;
  case MD_BLOCK_OL:
    p.ordered.push_back(static_cast<MD_BLOCK_OL_DETAIL *>(detail)->start);
    p.listGroups.push_back(++p.nextListGroup);
    p.listItems.push_back(0);
    ++p.indent;
    break;
  case MD_BLOCK_LI: {
    auto d = static_cast<MD_BLOCK_LI_DETAIL *>(detail);
    p.listItems.back() = ++p.nextListItem;
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
  case MD_BLOCK_TABLE:
    ++p.tableId;
    break;
  case MD_BLOCK_TR:
    p.begin("table");
    p.block->tableId = p.tableId;
    break;
  case MD_BLOCK_TH:
  case MD_BLOCK_TD:
    if (!p.block || p.block->kind != "table")
      p.begin("table");
    p.block->cells.emplace_back();
    p.block->cellAlignments.push_back(
        static_cast<MD_BLOCK_TD_DETAIL *>(detail)->align);
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
    p.listGroups.pop_back();
    p.listItems.pop_back();
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
  if(p.style&Math){auto saved=p.mathSources.find(t);if(saved!=p.mathSources.end())t=saved->second;}
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
std::string normalizeMath(const std::string &input,std::map<std::string,std::string> &sources) {
  std::string prefix="PCMATHTOKEN";while(input.find(prefix)!=std::string::npos)prefix+='X';
  auto protectedMath=[&](const std::string &body,size_t delimiters){const auto token=prefix+std::to_string(sources.size())+"END";sources.emplace(token,body);const std::string delimiter(delimiters,'$');return delimiter+token+delimiter;};
  struct CodeRanges {const std::string *source;std::vector<std::pair<size_t,size_t>> ranges;};
  CodeRanges code{&input,{}};
  // Only bare-extension normalization needs this structural check. MD4C knows
  // whether indentation belongs to code or to a nested list; a four-space
  // shortcut would break one of those cases.
  if(input.find("\\begin")!=std::string::npos||input.find("\\ce")!=std::string::npos) {
    MD_PARSER probe{};probe.flags=MD_DIALECT_GITHUB|MD_FLAG_LATEXMATHSPANS|MD_FLAG_NOHTML;
    probe.enter_block=[](MD_BLOCKTYPE,void *,void *){return 0;};probe.leave_block=probe.enter_block;
    probe.enter_span=[](MD_SPANTYPE,void *,void *){return 0;};probe.leave_span=probe.enter_span;
    probe.text=[](MD_TEXTTYPE type,const MD_CHAR *text,MD_SIZE size,void *ctx){
      auto &c=*static_cast<CodeRanges *>(ctx);const auto base=reinterpret_cast<uintptr_t>(c.source->data()),at=reinterpret_cast<uintptr_t>(text);
      if(type==MD_TEXT_CODE&&at>=base&&at-base<=c.source->size()&&size<=c.source->size()-(at-base))c.ranges.push_back({size_t(at-base),size_t(at-base)+size});return 0;
    };
    md_parse(input.data(),input.size(),&probe,&code);
    std::sort(code.ranges.begin(),code.ranges.end());
  }
  std::string out;
  size_t i = 0;
  size_t codeIndex=0;
  char fence = 0;
  size_t fenceCount = 0;
  size_t mathDelimiter = 0;
  while (i < input.size()) {
    while(codeIndex<code.ranges.size()&&code.ranges[codeIndex].second<=i)++codeIndex;
    if(codeIndex<code.ranges.size()&&i>=code.ranges[codeIndex].first) {
      const auto end=code.ranges[codeIndex].second;out+=input.substr(i,end-i);i=end;continue;
    }
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
    if (input[i] == '$') {
      const size_t count = i + 1 < input.size() && input[i + 1] == '$' ? 2 : 1;
      if(!mathDelimiter){
        size_t end=i+count;unsigned braces=0;
        for(;end<input.size();++end){if(input[end]=='\\'){if(end+1<input.size())++end;continue;}if(input[end]=='{')++braces;else if(input[end]=='}'&&braces)--braces;
          if(!braces&&input[end]=='$'&&(count==1||(end+1<input.size()&&input[end+1]=='$')))break;}
        if(end<input.size()){out+=protectedMath(input.substr(i+count,end-i-count),count);i=end+count;continue;}
      }
      if (!mathDelimiter) mathDelimiter = count;
      else if (mathDelimiter == count) mathDelimiter = 0;
      out += input.substr(i, count); i += count; continue;
    }
    if(!mathDelimiter&&input.compare(i,6,"\\begin")==0) {
      auto end=latex::environmentEnd(input,i);
      if(end!=std::string::npos){out+=protectedMath(input.substr(i,end-i),2);i=end;continue;}
    }
    if (!mathDelimiter && input.compare(i, 3, "\\ce") == 0) {
      size_t at = i + 3;
      while (at < input.size() && std::isspace(static_cast<unsigned char>(input[at]))) ++at;
      if (at < input.size() && input[at] == '{') {
        try {
          std::string body;
          if (chemistry::group(input, at, '{', '}', body)) {
            out += protectedMath(input.substr(i,at-i),1); i = at; continue;
          }
        } catch (const std::exception &) { /* Preserve unfinished streaming text. */ }
      }
    }
    if (input[i] == '\\' && i + 1 < input.size()) {
      char open = input[i + 1];
      if (open == '(' || open == '[') {
        char close = open == '(' ? ')' : ']';
        auto at = input.find(std::string("\\") + close, i + 2);
        if (at != std::string::npos) {
          std::string delimiter = open == '(' ? "$" : "$$";
          out += protectedMath(input.substr(i+2,at-i-2),delimiter.size());
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
  const bool unfinished=streaming&&incomplete(source);
  if (unfinished&&source.find('$')==std::string::npos&&source.find("\\end{")==std::string::npos&&source.find("\\)")==std::string::npos&&source.find("\\]")==std::string::npos) {
    Block raw;
    raw.runs.push_back({source, "", 0});
    p.doc.blocks.push_back(std::move(raw));
    return std::move(p.doc);
  }
  auto input = detail::normalizeTableBoundaries(normalizeMath(source,p.mathSources));
  MD_PARSER parser{};
  parser.flags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_NOHTML;
  parser.enter_block = enterBlock;
  parser.leave_block = leaveBlock;
  parser.enter_span = enterSpan;
  parser.leave_span = leaveSpan;
  parser.text = textCallback;
  if (md_parse(input.data(), input.size(), &parser, &p))
    throw std::runtime_error("Markdown parse failed");
  if(unfinished) {
    bool completedMath=false;
    for(const auto &block:p.doc.blocks){for(const auto &run:block.runs)completedMath|=bool(run.style&Math);for(const auto &cell:block.cells)for(const auto &run:cell)completedMath|=bool(run.style&Math);}
    if(!completedMath){p.doc.blocks.clear();Block raw;raw.runs.push_back({source,"",0});p.doc.blocks.push_back(std::move(raw));return std::move(p.doc);}
  }
  latex::prepare(p.doc);
  return std::move(p.doc);
}
static uint32_t codepoint(const std::string &text) {
  if (text.empty()) return 0;
  const auto lead = static_cast<unsigned char>(text[0]);
  if (lead < 128) return lead;
  uint32_t value = lead & (text.size() == 2 ? 31 : text.size() == 3 ? 15 : 7);
  for (size_t i = 1; i < text.size(); ++i) value = (value << 6) | (static_cast<unsigned char>(text[i]) & 63);
  return value;
}
struct Piece {
  std::string text, target;
  int style = 0;
  float x = 0, y = 0, width = 0, height = 0, baseline = 0;
  std::shared_ptr<const Bitmap> math;
  size_t begin = 0, end = 0;
};
struct RichText::Impl {
  UiMetrics metrics;
  std::string cacheIdentity;
  FT_Library library = nullptr;
  FT_Face face = nullptr, boldFace = nullptr, monoFace = nullptr, fallbackFace = nullptr;
  cairo_font_face_t *cairoFace = nullptr, *boldCairo = nullptr, *monoCairo = nullptr, *fallbackCairo = nullptr;
  std::map<std::string, std::shared_ptr<const Bitmap>> cache;
  std::deque<std::string> order;
  size_t cacheBytes = 0;
  uint32_t proseForeground=0;
  static size_t cost(const std::string &key,const Bitmap &bitmap) {return bitmap.pixels.size()+key.size()*2+128;}
  void remember(const std::string &key,const std::shared_ptr<const Bitmap> &bitmap) {
    if(cache.count(key))return;
    const auto n=cost(key,*bitmap);
    while(!order.empty()&&(cacheBytes+n>8*1024*1024||cache.size()>=512)) {
      const auto old=order.front();order.pop_front();auto entry=cache.find(old);
      if(entry!=cache.end()){cacheBytes-=cost(old,*entry->second);cache.erase(entry);}
    }
    if(n<=8*1024*1024){cache.emplace(key,bitmap);order.push_back(key);cacheBytes+=n;}
  }
#ifndef PENCHAT_NATIVE_DEVICE
  RichWorkStats work;
#endif
  std::vector<Piece> pieces;
  void select(cairo_t *c, int size, int style, uint32_t cp = 0) {
    const bool mono = (style & Mono) && !(style & Bold) && cp < 128 && monoCairo;
    const bool bold = (style & Bold) && boldCairo && !mono;
    auto selected = mono ? monoFace : bold ? boldFace : face;
    auto cairoSelected = mono ? monoCairo : bold ? boldCairo : cairoFace;
    if (cp && !FT_Get_Char_Index(selected, cp) && fallbackCairo)
      cairoSelected = fallbackCairo;
    cairo_set_font_face(c, cairoSelected);
    cairo_matrix_t matrix;
    cairo_matrix_init_scale(&matrix, size, size);
    if (style & Italic)
      matrix.xy = -size * .18;
    cairo_set_font_matrix(c, &matrix);
  }
};
RichText::RichText(const std::string &fontPath, const std::string &res, const std::string &fallbackPath, const UiMetrics &metrics)
    : p_(new Impl) {
  p_->metrics=metrics;
  p_->cacheIdentity=std::to_string(metrics.densityPercent())+":"+std::to_string(metrics.scalePercent())+":"+std::to_string(metrics.textPercent())+":";
  if (FT_Init_FreeType(&p_->library) ||
      FT_New_Face(p_->library, fontPath.c_str(), 0, &p_->face))
    throw std::runtime_error("Cannot initialize text renderer");
  p_->cairoFace = cairo_ft_font_face_create_for_ft_face(p_->face, 0);
  // Use the sibling system face; device fonts remain system-owned resources.
  const auto slash = fontPath.find_last_of('/');
  const auto directory = slash == std::string::npos ? std::string() : fontPath.substr(0, slash + 1);
  const std::vector<std::string> boldPaths = {
      directory + "HarmonyOS_Sans_SC_Bold.ttf", directory + "chinese-bold.ttf"};
  for (const auto &path : boldPaths) {
    if (!FT_New_Face(p_->library, path.c_str(), 0, &p_->boldFace)) {
      if ((p_->boldFace->style_flags & FT_STYLE_FLAG_BOLD) &&
          p_->face->family_name && p_->boldFace->family_name &&
          !std::strcmp(p_->face->family_name, p_->boldFace->family_name)) {
        p_->boldCairo = cairo_ft_font_face_create_for_ft_face(p_->boldFace, 0);
        break;
      }
      FT_Done_Face(p_->boldFace);
      p_->boldFace = nullptr;
    }
  }
  if (!FT_New_Face(p_->library,
                   join(res, "fonts/latin/optional/cmtt10.ttf").c_str(), 0,
                   &p_->monoFace))
    p_->monoCairo = cairo_ft_font_face_create_for_ft_face(p_->monoFace, 0);
  if (!fallbackPath.empty()) {
    if (FT_New_Face(p_->library, fallbackPath.c_str(), 0, &p_->fallbackFace))
      throw std::runtime_error("Cannot load symbol fallback font");
    p_->fallbackCairo = cairo_ft_font_face_create_for_ft_face(p_->fallbackFace, 0);
  }
  setFallbackFont(fontPath);
  tex::LaTeX::init(res);
  latex::registerNative();
}
RichText::~RichText() {
  tex::LaTeX::release();
  if (p_->boldCairo) cairo_font_face_destroy(p_->boldCairo);
  if (p_->boldFace) FT_Done_Face(p_->boldFace);
  if (p_->monoCairo)
    cairo_font_face_destroy(p_->monoCairo);
  if (p_->monoFace)
    FT_Done_Face(p_->monoFace);
  if (p_->fallbackCairo) cairo_font_face_destroy(p_->fallbackCairo);
  if (p_->fallbackFace) FT_Done_Face(p_->fallbackFace);
  cairo_font_face_destroy(p_->cairoFace);
  FT_Done_Face(p_->face);
  FT_Done_FreeType(p_->library);
}
void RichText::setMetrics(const UiMetrics &metrics) {
  if(p_->metrics.densityPercent()==metrics.densityPercent() && p_->metrics.scalePercent()==metrics.scalePercent() &&
      p_->metrics.textPercent()==metrics.textPercent())return;
  clearCache();p_->pieces.clear();p_->metrics=metrics;
  p_->cacheIdentity=std::to_string(metrics.densityPercent())+":"+std::to_string(metrics.scalePercent())+":"+std::to_string(metrics.textPercent())+":";
}
void RichText::clearCache() {
  p_->cache.clear();
  p_->order.clear();
  p_->cacheBytes = 0;
  p_->pieces.clear();
}
bool RichText::hasTrueBold() const { return p_->boldCairo != nullptr; }
static std::string formulaKey(const std::string &identity, const std::string &source,
                              int size, uint32_t foreground) {
  return identity + "typography3:latex2:" + theme::name(theme::mode()) + ":" +
      std::to_string(foreground) + ":" + std::to_string(size) + ":" + source;
}
#ifndef PENCHAT_NATIVE_DEVICE
RichWorkStats RichText::workStats() const { return p_->work; }
void RichText::resetWorkStats() { p_->work = {}; }
#endif
Bitmap RichText::formula(const std::string &source, int size, uint32_t foreground) {
  if(source.size()>8192)throw std::runtime_error("Formula too large");
  Document document;Block block;block.runs.push_back({source,"",Math|DisplayMath});document.blocks.push_back(std::move(block));latex::prepare(document);
  const auto bitmap = formulaRunShared(document.blocks[0].runs[0],size,foreground,0);
#ifndef PENCHAT_NATIVE_DEVICE
  ++p_->work.formulaCopies;
#endif
  return *bitmap;
}
std::shared_ptr<const Bitmap> RichText::formulaShared(const std::string &source,
                                                     int size, uint32_t foreground) {
  if (!foreground) foreground = theme::palette().richBody;
  auto key = formulaKey(p_->cacheIdentity, source, size, foreground);
  auto it = p_->cache.find(key);
  if (it != p_->cache.end()) {
#ifndef PENCHAT_NATIVE_DEVICE
    ++p_->work.formulaHits;
#endif
    return it->second;
  }
  if (source.size() > 65536)
    throw std::runtime_error("Formula too large");
#ifndef PENCHAT_NATIVE_DEVICE
  ++p_->work.formulaRenders;
#endif
  const float formulaSize = size * 1.5f;
  const auto normalized=latex::expand(chemistryTex(source,65536));
  tex::Formula formula;
  tex::TeXParser parser(false,tex::utf82wide(normalized),&formula,true);
  parser.parse();
  const bool lined=normalized.compare(0,2,"$$")!=0&&normalized.compare(0,2,"\\[")!=0;
  tex::TeXRenderBuilder builder;
  std::unique_ptr<tex::TeXRender> r(builder.setStyle(tex::TexStyle::display).setTextSize(formulaSize)
      .setWidth(tex::UnitType::pixel,4096,lined?tex::Alignment::left:tex::Alignment::center)
      .setIsMaxWidth(lined).setLineSpace(tex::UnitType::pixel,formulaSize*.2)
      .setForeground(0xff000000u|foreground).build(formula));
  auto bitmap = std::make_shared<Bitmap>();
  auto &b = *bitmap;
  b.width = std::max(1, r->getWidth() + p_->metrics.dp(4));
  b.height = std::max(1, r->getHeight() + p_->metrics.dp(4));
  b.baseline = std::round(r->getBaseline() * r->getHeight()) + p_->metrics.dp(2);
  if (b.width > 4096 || b.height > 2048)
    throw std::runtime_error("Formula dimensions exceed limits");
  b.pixels.resize(size_t(b.width) * b.height * 4);
  auto s = cairo_image_surface_create_for_data(
      b.pixels.data(), CAIRO_FORMAT_ARGB32, b.width, b.height, b.width * 4);
  auto c = cairo_create(s);
  CairoGraphics g(c);
  r->draw(g, p_->metrics.dp(2), p_->metrics.dp(2));
  cairo_surface_flush(s);
  cairo_destroy(c);
  cairo_surface_destroy(s);
  p_->remember(key,bitmap);
  return bitmap;
}
std::shared_ptr<const Bitmap> RichText::formulaRunShared(const Run &run,int size,uint32_t foreground,int available) {
  if(run.mathInvalid)throw std::runtime_error("Invalid LaTeX structure");
  if(run.text.size()>8192)throw std::runtime_error("Formula too large");
  if(run.formulaRows.empty())return formulaShared(run.text,size,foreground);
  const auto &rows=run.formulaRows;
  if(rows.size()==1&&rows[0].cells.size()==1&&!rows[0].showTag&&!rows[0].prose)return formulaShared(rows[0].cells[0],size,foreground);
  if(!foreground)foreground=theme::palette().richBody;
  std::string identity="formula-layout3:"+run.mathKind+":"+std::to_string(available)+":";
  for(const auto &row:rows) {
    identity+=(row.first?"first:":"middle:")+std::string(row.last?"last:":"more:")+row.kind+(row.prose?":prose:":":math:")+(row.compact?"compact:":"normal:");
    identity+="gap:"+std::to_string(row.gapBefore.size())+":"+row.gapBefore;
    identity+=row.showTag?(row.tagStar?"star:":"tag:"):"none:";identity+=std::to_string(row.tag.size())+":"+row.tag;
    for(const auto &cell:row.cells)identity+=":"+std::to_string(cell.size())+":"+cell;
    identity+=';';
  }
  const auto key=formulaKey(p_->cacheIdentity+"composite:",identity,size,foreground);
  auto found=p_->cache.find(key);if(found!=p_->cache.end())return found->second;
  struct Row {std::vector<std::shared_ptr<const Bitmap>> cells;std::shared_ptr<const Bitmap> tag;int ascent=0,descent=0,y=0,gap=0;bool below=false,prose=false;std::string kind;};
  std::vector<Row> measured;std::vector<int> columns;
  const int gap=p_->metrics.dp(12),lineGap=p_->metrics.dp(6);int height=0,totalWidth=0,tagWidth=0;
  for(const auto &row:rows) {
    Row r;r.prose=row.prose;r.kind=row.kind;
    if(row.prose)r.gap=row.compact?0:p_->metrics.dp(6);
    if(!row.gapBefore.empty()) {
      auto gap=row.gapBefore;const bool negative=gap[0]=='-';if(negative||gap[0]=='+')gap.erase(0,1);
      auto spacer=formulaShared("\\rule{0pt}{"+gap+"}",size,foreground);
      r.gap=std::max(0,spacer->height-p_->metrics.dp(4))*(negative?-1:1);
    }
    if(row.prose){
      struct PieceScope {std::vector<Piece> &current;std::vector<Piece> saved;uint32_t &ink;uint32_t old;PieceScope(std::vector<Piece> &value,uint32_t &color,uint32_t next):current(value),saved(std::move(value)),ink(color),old(color){ink=next;}~PieceScope(){current.swap(saved);ink=old;}} preserve(p_->pieces,p_->proseForeground,foreground);
      const auto prose=parseMarkdown(row.cells[0]);std::vector<Bitmap> pieces;int w=0,h=0;
      for(const auto &block:prose.blocks){auto info=layout(block,std::max(1,available>0?available:600),size);if(info.height){pieces.push_back(paint(block,std::max(1,available>0?available:600),0,info.height,size));w=std::max(w,pieces.back().width);h+=pieces.back().height;}}
      if(w>4096||h>2048)throw std::runtime_error("Formula explanation exceeds limit");
      auto b=std::make_shared<Bitmap>();b->width=std::max(1,w);b->height=std::max(1,h);b->baseline=b->height;b->pixels.resize(size_t(b->width)*b->height*4);
      int y=0;for(const auto &piece:pieces){for(int line=0;line<piece.height;++line)std::copy_n(piece.pixels.data()+size_t(line)*piece.width*4,size_t(piece.width)*4,b->pixels.data()+(size_t(y+line)*b->width)*4);y+=piece.height;}
      r.cells.push_back(b);r.ascent=b->height;r.descent=0;
    }
    for(size_t i=0;!row.prose&&i<row.cells.size();++i) {
      auto bitmap=formulaShared(row.cells[i].empty()?"\\vphantom{x}":row.cells[i],size,foreground);
      if(columns.size()<=i)columns.resize(i+1);
      columns[i]=std::max(columns[i],bitmap->width);int extent=0;for(auto w:columns)extent+=w;
      if(extent+int(columns.size()-1)*gap>4096)throw std::runtime_error("Formula width exceeds limit");
      r.ascent=std::max(r.ascent,bitmap->baseline);r.descent=std::max(r.descent,bitmap->height-bitmap->baseline);r.cells.push_back(std::move(bitmap));
    }
    if(row.showTag&&(!row.tagStar||!row.tag.empty())) {
      auto text=row.tagStar?row.tag:"("+row.tag+")";
      r.tag=formulaShared("\\text{"+text+"}",size,foreground);tagWidth=std::max(tagWidth,r.tag->width);
      r.ascent=std::max(r.ascent,r.tag->baseline);r.descent=std::max(r.descent,r.tag->height-r.tag->baseline);
    }
    height+=r.ascent+r.descent+lineGap+std::max(0,r.gap);if(height>2048)throw std::runtime_error("Formula height exceeds limit");
    measured.push_back(std::move(r));
  }
  for(auto w:columns)totalWidth+=w;
  totalWidth+=int(columns.size()-1)*gap;
  const int viewport=available>0?available:totalWidth+(tagWidth?tagWidth+gap:0);
  const bool tagsBelow=tagWidth&&totalWidth+gap+tagWidth>viewport;
  int proseWidth=0;for(const auto &r:measured)if(r.prose)proseWidth=std::max(proseWidth,r.cells[0]->width);
  const int width=std::max(proseWidth,std::max(std::max(totalWidth,tagWidth),tagWidth?viewport:totalWidth));
  height=0;
  bool firstRow=true;
  for(auto &r:measured){if(!firstRow)height+=std::max(0,lineGap+r.gap);firstRow=false;r.y=height;r.below=tagsBelow&&bool(r.tag);height+=r.ascent+r.descent+(r.below?r.tag->height+lineGap:0);}
  height=std::max(1,height);
  if(width<1||width>4096||height>2048)throw std::runtime_error("Formula dimensions exceed limits");
  auto bitmap=std::make_shared<Bitmap>();bitmap->width=width;bitmap->height=height;bitmap->baseline=measured.front().ascent;
  bitmap->pixels.resize(size_t(width)*height*4);
  auto surface=cairo_image_surface_create_for_data(bitmap->pixels.data(),CAIRO_FORMAT_ARGB32,width,height,width*4);
  auto context=cairo_create(surface);
  auto paint=[&](const Bitmap &b,int x,int y){
    auto source=cairo_image_surface_create_for_data(const_cast<unsigned char *>(b.pixels.data()),CAIRO_FORMAT_ARGB32,b.width,b.height,b.width*4);
    cairo_set_source_surface(context,source,x,y);cairo_paint(context);cairo_surface_destroy(source);
  };
  for(size_t i=0;i<measured.size();++i) {
    const auto &r=measured[i];const int content=tagWidth&&!tagsBelow?std::max(totalWidth,viewport-tagWidth-gap):width;
    int x=std::max(0,(content-totalWidth)/2);
    if(r.prose||r.kind=="flalign")x=0;
    if(r.kind=="gather"&&r.cells.size()==1)x=std::max(0,(content-r.cells[0]->width)/2);
    if(r.kind=="multline") {
      int rowWidth=0;for(const auto &b:r.cells)rowWidth+=b->width;
      x=rows[i].first?0:(rows[i].last?std::max(0,content-rowWidth):std::max(0,(content-rowWidth)/2));
    }
    for(size_t j=0;j<r.cells.size();++j) {
      const bool align=!r.prose&&(r.kind=="align"||r.kind=="alignat"||r.kind=="flalign");
      if(r.kind=="flalign"&&r.cells.size()>2&&j&&j%2==0)x+=std::max(0,(content-totalWidth)/int((r.cells.size()-1)/2));
      const int offset=align&&j%2==0?columns[j]-r.cells[j]->width:0;
      paint(*r.cells[j],x+offset,r.y+r.ascent-r.cells[j]->baseline);x+=(r.prose?r.cells[j]->width:columns[j])+gap;
    }
    if(r.tag)paint(*r.tag,std::max(0,viewport-r.tag->width),r.below?r.y+r.ascent+r.descent+lineGap:r.y+r.ascent-r.tag->baseline);
  }
  cairo_surface_flush(surface);cairo_destroy(context);cairo_surface_destroy(surface);
  p_->remember(key,bitmap);
  return bitmap;
}
void RichText::prepare(Document &document, int width, int size) {
  std::map<int, int> listColumns;
  auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto context = cairo_create(surface);
  p_->select(context, size, 0);
  for (const auto &block : document.blocks) {
    if (block.marker.empty() || block.listGroups.empty()) continue;
    double advance = 0;
    for (const auto &ch : characters(block.marker)) {
      p_->select(context, size, 0, codepoint(ch));
      cairo_text_extents_t extents;
      cairo_text_extents(context, ch.c_str(), &extents);
      advance += extents.x_advance;
    }
    const int group = block.listGroups.back();
    listColumns[group] = std::max(listColumns[group], int(std::ceil(advance)) + std::max(1, size / 4));
  }
  cairo_destroy(context);
  cairo_surface_destroy(surface);
  for (auto &block : document.blocks) {
    block.listColumns.clear();
    for (int group : block.listGroups)
      block.listColumns.push_back(std::max(size, listColumns[group]));
  }
  for (size_t i = 0; i < document.blocks.size();) {
    if (document.blocks[i].kind != "table") {
      ++i;
      continue;
    }
    size_t end = i;
    std::vector<int> widths;
    while (end < document.blocks.size() &&
           document.blocks[end].kind == "table" &&
           document.blocks[end].tableId == document.blocks[i].tableId) {
      auto &row = document.blocks[end];
      widths.resize(std::max(widths.size(), row.cells.size()), p_->metrics.dp(140));
      for (size_t column = 0; column < row.cells.size(); ++column) {
        Block cell;
        cell.kind = "code";
        cell.runs = row.cells[column];
        if (row.header)
          for (auto &run : cell.runs)
            run.style |= Bold;
        widths[column] =
            std::max(widths[column], layout(cell, 1, size).width + p_->metrics.dp(14));
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
static std::string runPlainText(const Run &run) {
  if (run.style & Math) return run.bareMath?run.text:(run.style & DisplayMath ? "$$" : "$") + run.text +
                               (run.style & DisplayMath ? "$$" : "$");
  return run.style & Image ? "[图片] " + run.text : run.text;
}
std::string blockPlainText(const Block &block) {
  std::string text = block.marker;
  if (block.kind == "table") {
    for (size_t i = 0; i < std::max(block.cells.size(),block.columnWidths.size()); ++i) {
      if (i) text += '\t';
      if(i<block.cells.size())for (const auto &run : block.cells[i]) text += runPlainText(run);
    }
  } else for (const auto &run : block.runs) text += runPlainText(run);
  return text;
}
RichTypography RichText::typography(const Block &block, int size) const {
  RichTypography result;
  result.fontSize = size;
  if (block.kind == "h") {
    static const int sizes[] = {29, 27, 25, 23, 22, 21};
    static const double spaces[] = {1.2, 1.0, .9, .8, .7, .6};
    const int index = std::max(0, std::min(5, block.level - 1));
    result.fontSize = std::max(1, int(std::lround(sizes[index] * size / 21.0)));
    result.lineHeight = std::ceil(result.fontSize * 1.3);
    result.before = std::lround(result.fontSize * spaces[index]);
    result.after = std::lround(result.fontSize * .4);
  } else {
    result.lineHeight = std::ceil(size * (block.kind == "code" || block.kind == "table" ? 1.45 : 1.6));
    result.before = result.after = std::lround(size * (block.listItemId ? .6 : .7));
  }
  return result;
}
int RichText::blockGap(const Block *previous, const Block &current, int size) const {
  if (!previous) return 0;
  if (previous->tableId && previous->tableId == current.tableId) return 0;
  int gap = std::max(typography(*previous, size).after, typography(current, size).before);
  if (previous->listItemId && previous->listItemId == current.listItemId)
    gap = std::max(gap, int(std::lround(size * .7)));
  return gap;
}
RichLayout RichText::layout(const Block &block, int width, int size, bool textHits, bool subdued) {
#ifndef PENCHAT_NATIVE_DEVICE
  ++p_->work.layoutCalls;
#endif
  p_->pieces.clear();
  if (block.kind == "table") {
    std::vector<Piece> pieces;
    struct CellSpan {
      size_t begin, end;
      int height;
    };
    std::vector<CellSpan> spans;
    int cellX = block.quote * p_->metrics.dp(12) + std::max(0, block.indent - 1) * p_->metrics.dp(20),
        rowHeight = p_->metrics.dp(20);
    const size_t count =
        std::max(block.cells.size(), block.columnWidths.size());
    size_t textOffset = 0;
    for (size_t column = 0; column < count; ++column) {
      Block cell;
      cell.kind = "code";
      if (column < block.cells.size())
        cell.runs = block.cells[column];
      if (block.header)
        for (auto &run : cell.runs)
          run.style |= Bold;
      auto cellLayout = layout(cell, 1, size, false, subdued);
      const int cellWidth = column < block.columnWidths.size()
                                ? block.columnWidths[column]
                                : std::max(p_->metrics.dp(140), cellLayout.width + p_->metrics.dp(14));
      const int alignment = column < block.cellAlignments.size()
                                ? block.cellAlignments[column]
                                : MD_ALIGN_DEFAULT;
      const int spare = std::max(0, cellWidth - cellLayout.width);
      const int offset = alignment == MD_ALIGN_RIGHT    ? spare
                         : alignment == MD_ALIGN_CENTER ? spare / 2
                                                        : 0;
      const size_t begin = pieces.size();
      for (auto piece : p_->pieces) {
        piece.x += cellX + offset;
        piece.begin += textOffset; piece.end += textOffset;
        pieces.push_back(std::move(piece));
      }
      textOffset += blockPlainText(cell).size() + (column + 1 < count ? 1 : 0);
      spans.push_back({begin, pieces.size(), cellLayout.height});
      rowHeight = std::max(rowHeight, cellLayout.height);
      cellX += cellWidth;
    }
    for (const auto &span : spans)
      for (size_t i = span.begin; i < span.end; ++i)
        pieces[i].y += (rowHeight - span.height) / 2;
    p_->pieces = std::move(pieces);
    RichLayout result;
    result.width = std::max(width, cellX);
    result.height = rowHeight;
    for (const auto &piece : p_->pieces) {
      if (textHits && piece.end > piece.begin)
        result.textHits.push_back({int(piece.x),int(piece.y),std::max(1,int(std::ceil(piece.width))),
                                  std::max(1,int(std::ceil(piece.height))),piece.begin,piece.end,bool(piece.style & Math)});
      if (!piece.target.empty())
        result.links.push_back({int(piece.x), int(piece.y), int(piece.width),
                                int(piece.height), piece.target});
    }
    return result;
  }
  const auto type = typography(block, size);
  const int actualSize = type.fontSize;
  const int baseStyle = block.kind == "h" || block.header ? Bold : 0;
  const uint32_t foreground = p_->proseForeground?p_->proseForeground:subdued ? theme::palette().reasoning :
      block.kind == "h" ? theme::palette().richHeading : theme::palette().richBody;
  auto surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
  auto c = cairo_create(surf);
  const float inset = block.quote * p_->metrics.dp(12) + p_->metrics.dp(6);
  float markerWidth = 0;
  for (const auto &ch : characters(block.marker)) {
    p_->select(c, actualSize, 0, codepoint(ch));
    cairo_text_extents_t extents;
    cairo_text_extents(c, ch.c_str(), &extents);
    markerWidth += extents.x_advance;
  }
  float ownColumn = 0, parents = 0;
  if (!block.listGroups.empty()) {
    for (size_t i = 0; i < block.listGroups.size(); ++i) {
      const float column = i < block.listColumns.size() ? block.listColumns[i] : actualSize * 1.25f;
      if (i + 1 == block.listGroups.size()) ownColumn = std::max(column, markerWidth + actualSize * .25f);
      else parents += column + actualSize * .35f;
    }
    // Compress ancestral indentation before sacrificing readable body width.
    parents = std::min(parents, std::max(0.f, width - inset - p_->metrics.dp(6) - 6.f * size - ownColumn));
  } else if (block.indent || !block.marker.empty()) {
    parents = std::min(float(std::max(0, block.indent - 1) * p_->metrics.dp(20)),
                       std::max(0.f, width - inset - 6.f * size - markerWidth));
    ownColumn = markerWidth;
  }
  const float left = inset + parents + ownColumn;
  p_->select(c, actualSize, baseStyle);
  cairo_font_extents_t defaultFont;
  cairo_font_extents(c, &defaultFont);
  const float defaultAscent = defaultFont.ascent, defaultDescent = defaultFont.descent;
  const float padding = block.kind == "code" ? p_->metrics.dp(4) : 0;
  float x = left, y = padding, ascent = defaultAscent, descent = defaultDescent,
        maxWidth = width;
  size_t lineStart = 0;
  auto newline = [&]() {
    const float lineHeight = std::max(float(type.lineHeight), ascent + descent);
    const float leading = (lineHeight - ascent - descent) / 2;
    for (size_t i = lineStart; i < p_->pieces.size(); ++i)
      p_->pieces[i].y = y + leading + ascent - p_->pieces[i].baseline;
    y += lineHeight;
    x = left;
    lineStart = p_->pieces.size();
    ascent = defaultAscent;
    descent = defaultDescent;
  };
  // The lookahead and placement passes share glyph metrics. Bound this local
  // cache so long replies do not retain an additional unbounded text index.
  struct GlyphMetrics {
    float advance, baseline, below;
  };
  std::unordered_map<uint64_t, GlyphMetrics> glyphs;
  auto metricsOf = [&](const std::string &ch, uint32_t cp, int style) {
    const uint64_t key = (uint64_t(uint32_t(style)) << 32) | cp;
    const auto found = glyphs.find(key);
    if (found != glyphs.end()) return found->second;
    p_->select(c, actualSize, style, cp);
    cairo_text_extents_t extents;
    cairo_text_extents(c, ch.c_str(), &extents);
    const float advance = (style & Mono) && !(style & Bold)
        ? float(actualSize * .62) * (cp >= 128 ? 2 : 1) : extents.x_advance;
    // Tabs retain their dynamic advance but are painted as spaces.
    if (ch == "\t") cairo_text_extents(c, " ", &extents);
    cairo_font_extents_t font;
    cairo_font_extents(c, &font);
    const GlyphMetrics result{advance, float(std::max(font.ascent, -extents.y_bearing)),
                              float(std::max(font.descent, extents.y_bearing + extents.height))};
#ifndef PENCHAT_NATIVE_DEVICE
    ++p_->work.glyphMeasurements;
#endif
    if (glyphs.size() < 256) glyphs.emplace(key, result);
    return result;
  };
  size_t textOffset = 0;
  auto add = [&](const Run &run, int forceWidth) {
    if ((run.style & DisplayMath) && x > left)
      newline();
    if (run.style & Math) {
      try {
        auto bitmap = formulaRunShared(run, actualSize, foreground, width-left);
        if (!forceWidth && block.kind != "code" && x + bitmap->width > width && x > left)
          newline();
        Piece p;
        p.begin = textOffset; textOffset += runPlainText(run).size(); p.end = textOffset;
        p.math = bitmap;
        p.x = x;
        p.width = bitmap->width;
        p.height = bitmap->height;
        p.baseline = bitmap->baseline;
        p.style = run.style;
        ascent = std::max(ascent, p.baseline);
        descent = std::max(descent, p.height - p.baseline);
        x += p.width;
        maxWidth = std::max(maxWidth, x + p_->metrics.dp(6));
        p_->pieces.push_back(std::move(p));
        if (run.style & DisplayMath)
          newline();
        return;
      } catch (...) {
      }
    }
    p_->select(c, actualSize, run.style | baseStyle);
    const size_t formulaBegin=textOffset,formulaEnd=textOffset+runPlainText(run).size();
    if(run.style&Math)textOffset+=(run.style&DisplayMath)?2:1;
    for (auto &ch :
         characters(run.style & Image ? ("[图片] " + run.text) : run.text)) {
      const size_t textBegin = textOffset;
      textOffset += ch.size();
      if (ch == "\n") {
        newline();
        continue;
      }
      const auto cp = codepoint(ch);
      if ((cp >= 0xfe00 && cp <= 0xfe0f) || (cp >= 0xe0100 && cp <= 0xe01ef) || cp == 0x200d || cp == 0x200c || cp == 0x200b) continue;
      const auto metrics = metricsOf(ch, cp, run.style | baseStyle);
      float advance = metrics.advance;
      if (block.kind != "code" && (ch == " " || ch == "\t") &&
          (x == left || x + advance > width - p_->metrics.dp(6))) {
        if (x > left) newline();
        continue;
      }
      if (!forceWidth && block.kind != "code" && x + advance > width - p_->metrics.dp(6) &&
          x > left)
        newline();
      Piece p;
      p.begin = textBegin; p.end = textOffset;
      if(run.style&Math){p.begin=formulaBegin;p.end=formulaEnd;}
      p.text = ch == "\t" ? " " : ch;
      if (ch == "\t")
        advance =
            actualSize * .62 * (4 - int((x - left) / (actualSize * .62)) % 4);
      p.x = x;
      p.baseline = metrics.baseline;
      p.height = p.baseline + metrics.below;
      ascent = std::max(ascent, p.baseline);
      descent = std::max(descent, metrics.below);
      p.width = advance;
      p.style = run.style | baseStyle;
      p.target = run.link;
      p_->pieces.push_back(std::move(p));
      x += advance;
      maxWidth = std::max(maxWidth, x + p_->metrics.dp(6));
    }
    if(run.style&Math)textOffset=formulaEnd;
  };
  if (!block.marker.empty()) {
    x = left - markerWidth - (block.listGroups.empty() ? 0 : actualSize * .25f);
    add({block.marker, "", 0}, 1);
    x = left;
  }
  auto wordCharacter = [](uint32_t cp) {
    return (cp < 128 && (std::isalnum(static_cast<unsigned char>(cp)) ||
                         cp == '_' || cp == '\'')) ||
           (cp >= 0xc0 && cp <= 0x52f && cp != 0xd7 && cp != 0xf7) ||
           (cp >= 0x1e00 && cp <= 0x1eff) || cp == 0x2019;
  };
  auto closingPunctuation = [](uint32_t cp) {
    switch (cp) {
    case ',': case '.': case '!': case '?': case ':': case ';':
    case ')': case ']': case '}': case '%':
    case 0x00b0: case 0x2030: case 0x2031: case 0x2103: case 0x2109:
    case 0x2019: case 0x201d: case 0x2026:
    case 0x3001: case 0x3002: case 0x3009: case 0x300b: case 0x300d:
    case 0x300f: case 0x3011: case 0x3015: case 0x3017: case 0x3019:
    case 0x301b: case 0xff01: case 0xff05: case 0xff09: case 0xff0c:
    case 0xff0e: case 0xff1a: case 0xff1b: case 0xff1f: case 0xff3d:
    case 0xff5d: case 0xff60: case 0xff63: case 0xff64:
      return true;
    default: return false;
    }
  };
  auto openingPunctuation = [](uint32_t cp) {
    switch (cp) {
    case '(': case '[': case '{': case 0x2018: case 0x201c:
    case 0x3008: case 0x300a: case 0x300c: case 0x300e: case 0x3010:
    case 0x3014: case 0x3016: case 0x3018: case 0x301a: case 0xff08:
    case 0xff3b: case 0xff5b: case 0xff5f: case 0xff62:
      return true;
    default: return false;
    }
  };
  // Delay one wrapping unit: a word (across Markdown spans) or CJK character,
  // its opening brackets and its trailing punctuation. Never break those at a
  // soft line boundary. Explicit newlines and code retain their own semantics.
  // Oversized words are reprocessed as characters, still carrying punctuation;
  // genuinely indivisible units may overflow into the existing horizontal reader.
  std::function<void(const std::vector<Run> &, bool)> flow;
  flow = [&](const std::vector<Run> &runs, bool keepWords) {
    std::vector<Run> group;
    float groupWidth = 0;
    bool onlyOpening = false, lastWord = false, joinNext = false;
    auto flush = [&]() {
      if (group.empty()) return;
      if (keepWords && groupWidth > width - p_->metrics.dp(6) - left) {
        flow(group, false);
      } else {
        if (x > left && x + groupWidth > width - p_->metrics.dp(6)) newline();
        for (const auto &part : group) add(part, 1);
      }
      group.clear(); groupWidth = 0;
      onlyOpening = lastWord = joinNext = false;
    };
    auto append = [&](const Run &part, float advance) {
      groupWidth += advance;
      if (!group.empty() && !(part.style & Math) &&
          !(group.back().style & Math) && group.back().style == part.style &&
          group.back().link == part.link) group.back().text += part.text;
      else group.push_back(part);
    };
    for (const auto &run : runs) {
      if (run.style & (DisplayMath | Image)) {
        flush(); add(run, 0); continue;
      }
      if (run.style & Math) {
        if (!onlyOpening) flush();
        try {
          const bool simple=!run.mathInvalid&&(run.formulaRows.empty()||
              (run.formulaRows.size()==1&&run.formulaRows[0].cells.size()==1&&!run.formulaRows[0].showTag));
          int advance;
          if(simple) {
            const auto &source=run.formulaRows.empty()?run.text:run.formulaRows[0].cells[0];
            const auto key=formulaKey(p_->cacheIdentity,source,actualSize,foreground);
            const auto cached=p_->cache.find(key);
            advance=cached==p_->cache.end()?formulaRunShared(run,actualSize,foreground,width-left)->width:cached->second->width;
          } else advance=formulaRunShared(run,actualSize,foreground,width-left)->width;
          append(run, advance);
          onlyOpening = lastWord = joinNext = false;
        } catch (...) {
          flush(); add(run, 0);
        }
        continue;
      }
      for (const auto &ch : characters(run.text)) {
        if (ch == "\n" || ch == " " || ch == "\t") {
          flush(); add({ch, run.link, run.style}, 0); continue;
        }
        const auto cp = codepoint(ch);
        const bool word = wordCharacter(cp);
        const bool extending = (cp >= 0x300 && cp <= 0x36f) ||
            (cp >= 0xfe00 && cp <= 0xfe0f) ||
            (cp >= 0xe0100 && cp <= 0xe01ef) || cp == 0x200d || cp == 0x200c;
        const bool close = closingPunctuation(cp) || extending ||
            (cp == '"' && !group.empty() && !onlyOpening);
        const bool open = openingPunctuation(cp) || (cp == '"' && !close);
        if (!group.empty() && !onlyOpening && !close && !joinNext &&
            !(keepWords && lastWord && word && !open)) flush();
        const bool wasEmpty = group.empty();
        const bool invisible = (cp >= 0xfe00 && cp <= 0xfe0f) ||
            (cp >= 0xe0100 && cp <= 0xe01ef) || cp == 0x200d ||
            cp == 0x200c || cp == 0x200b;
        const float advance = invisible ? 0 : metricsOf(ch, cp, run.style | baseStyle).advance;
        append({ch, run.link, run.style}, advance);
        onlyOpening = open && (wasEmpty || onlyOpening);
        lastWord = word && (!close || cp == 0x2019);
        joinNext = cp == 0x200d;
      }
    }
    flush();
  };
  if (block.kind == "code") {
    for (const auto &run : block.runs) add(run, 0);
  } else {
    flow(block.runs, true);
  }
  if (lineStart < p_->pieces.size() || p_->pieces.empty()) newline();
  RichLayout layout;
  layout.width = std::ceil(maxWidth);
  layout.height = block.kind == "hr" ? p_->metrics.dp(12) : std::max(1, int(std::ceil(y + padding)));
  for (auto &p : p_->pieces) {
    if (textHits && p.end > p.begin)
      layout.textHits.push_back({int(p.x),int(p.y),std::max(1,int(std::ceil(p.width))),
                                std::max(1,int(std::ceil(p.height))),p.begin,p.end,bool(p.style & Math)});
    if (!p.target.empty())
      layout.links.push_back(
          {int(p.x), int(p.y), int(p.width), int(p.height), p.target});
  }
  cairo_destroy(c);
  cairo_surface_destroy(surf);
  return layout;
}
Bitmap RichText::paint(const Block &block, int width, int top, int height,
                       int size, int left, int windowWidth, bool subdued) {
  const auto info = layout(block, width, size, false, subdued);
  return paintPrepared(block, width, info, {top, height, left, windowWidth}, size, subdued);
}
std::vector<Bitmap> RichText::paintBatch(const Block &block, int width,
                                       const std::vector<RichWindow> &windows,
                                       int size, bool subdued) {
  std::vector<Bitmap> bitmaps;
  if (windows.empty()) return bitmaps;
  bitmaps.reserve(windows.size());
  const auto info = layout(block, width, size, false, subdued);
  for (const auto &window : windows)
    bitmaps.push_back(paintPrepared(block, width, info, window, size, subdued));
  return bitmaps;
}
Bitmap RichText::paintPrepared(const Block &block, int width, const RichLayout &info,
                               const RichWindow &window, int size, bool subdued) {
  const int top = window.top, height = window.height;
  const int left = window.left, windowWidth = window.width;
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
    color(block.kind == "table" && block.header ? theme::palette().surface : theme::palette().code);
    cairo_rectangle(c, left, top, b.width, height);
    cairo_fill(c);
  }
  if (block.kind == "table") {
    color(theme::palette().border);
    cairo_set_line_width(c, p_->metrics.dp(1));
    const float tableLeft =
        block.quote * p_->metrics.dp(12) + std::max(0, block.indent - 1) * p_->metrics.dp(20);
    float edge = tableLeft + .5f;
    cairo_move_to(c, edge, .5);
    cairo_line_to(c, edge, info.height - .5);
    for (int cellWidth : block.columnWidths) {
      edge += cellWidth;
      cairo_move_to(c, edge - 1, .5);
      cairo_line_to(c, edge - 1, info.height - .5);
    }
    cairo_move_to(c, tableLeft, .5);
    cairo_line_to(c, info.width, .5);
    cairo_move_to(c, tableLeft, info.height - .5);
    cairo_line_to(c, info.width, info.height - .5);
    cairo_stroke(c);
  }
  if (block.quote) {
    color(theme::palette().muted);
    cairo_rectangle(c, p_->metrics.dp(1), top, p_->metrics.dp(3), height);
    cairo_fill(c);
  }
  if (block.kind == "hr") {
    color(theme::palette().border);
    cairo_rectangle(c, p_->metrics.dp(3), p_->metrics.dp(5), width - p_->metrics.dp(6), p_->metrics.dp(1));
    cairo_fill(c);
  }
  int actual = typography(block, size).fontSize;
  for (auto &p : p_->pieces) {
    if (p.y + p.height < top || p.y > top + height || p.x + p.width < left ||
        p.x > left + b.width)
      continue;
    if (p.math) {
      auto m = cairo_image_surface_create_for_data(
          // Cairo's source-surface API takes mutable data but only reads it here.
          const_cast<uint8_t *>(p.math->pixels.data()), CAIRO_FORMAT_ARGB32, p.math->width,
          p.math->height, p.math->width * 4);
      cairo_set_source_surface(c, m, p.x, p.y);
      cairo_paint(c);
      cairo_surface_destroy(m);
      continue;
    }
    p_->select(c, actual, p.style, codepoint(p.text));
    if (!p.target.empty())
      color(subdued ? theme::palette().reasoningLink : theme::palette().link);
    else
      color(p_->proseForeground?p_->proseForeground:subdued ? theme::palette().reasoning : block.kind == "h" ? theme::palette().richHeading : theme::palette().richBody);
    if ((p.style & Mono) && block.kind != "code") {
      color(theme::palette().surface);
      cairo_rectangle(c, p.x, p.y, p.width, p.height);
      cairo_fill(c);
      color(p_->proseForeground?p_->proseForeground:subdued ? theme::palette().reasoning : block.kind == "h" ? theme::palette().richHeading : theme::palette().richBody);
    }
    cairo_move_to(c, p.x, p.y + p.baseline);
    cairo_show_text(c, p.text.c_str());
    if ((p.style & Bold) && !p_->boldCairo) {
      cairo_move_to(c, p.x + .5*p_->metrics.factor(), p.y + p.baseline);
      cairo_show_text(c, p.text.c_str());
    }
    if ((p.style & Strike) || !p.target.empty()) {
      cairo_rectangle(
          c, p.x, p.y + (p.style & Strike ? p.baseline * .6 : p.baseline + p_->metrics.dp(2)),
          p.width, p_->metrics.dp(1));
      cairo_fill(c);
    }
  }
  cairo_surface_flush(surf);
  cairo_destroy(c);
  cairo_surface_destroy(surf);
  return b;
}
} // namespace penchat
