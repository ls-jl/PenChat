#pragma once
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
struct Hit {
  int x, y, w, h;
  std::string target;
};
struct RichLayout {
  int width = 0, height = 0;
  std::vector<Hit> links;
};
class RichText {
  struct Impl;
  std::unique_ptr<Impl> p_;

public:
  RichText(const std::string &fontPath, const std::string &resourcePath);
  ~RichText();
  Bitmap formula(const std::string &text, int size);
  void prepare(Document &document, int width, int size = 21);
  RichLayout layout(const Block &block, int width, int size = 21);
  Bitmap paint(const Block &block, int width, int top, int height,
               int size = 21, int left = 0, int windowWidth = 0,
               bool subdued = false);
  void clearCache();
};
} // namespace penchat
