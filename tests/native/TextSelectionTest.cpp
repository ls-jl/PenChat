#include "editor.hpp"
#include "editor_layout.hpp"
#include "markdown.hpp"
#include "text_gesture.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
int main(int argc, char **argv) {
  TextGesture g;
  g.begin(10, 10, 0, true);
  g.move(23, 10, 100);
  assert(g.mode == TextGesture::Scroll);
  g.move(10, 10, 900);
  assert(g.mode == TextGesture::Scroll);
  g.cancel();
  g.begin(10, 10, 0, true);
  assert(!g.hold(449));
  assert(g.hold(450));
  g.move(22, 10, 460);
  assert(g.mode == TextGesture::Held);
  g.move(23, 10, 470);
  assert(g.mode == TextGesture::Caret);
  g.cancel();
  g.begin(10, 10, 0, false);
  g.move(24, 10, 500);
  assert(g.mode == TextGesture::Range);
  g.end();
  g.begin(10, 10, 0, false, true);
  g.move(12, 10, 10);
  assert(g.mode == TextGesture::Handle);
  g.cancel();
  assert(!g.doubleTap(10, 10, 1000));
  assert(g.doubleTap(22, 10, 1349));
  g.cancel();
  assert(!g.doubleTap(10, 10, 2000));
  assert(!g.doubleTap(10, 10, 2350));
  EditorState e;
  e.text = u8"hello 世界 👩‍💻";
  e.word(2);
  assert(e.selection() == "hello");
  e.word(6);
  assert(e.selection() == u8"世");
  e.word(10);
  assert(e.selection() == u8"👩‍💻");
  e.text = "don't_stop";
  e.word(4);
  assert(e.selection() == "don't_stop");
  for (auto size : {std::make_pair(960, 266), std::make_pair(640, 360), std::make_pair(400, 240),
                    std::make_pair(568, 1210)}) {
    EditorLayout l(size.first, size.second);
    assert(l.selectionHandleWidth() * l.scaleX() / 256. >= 44);
    assert(l.selectionHandleHeight() * l.scaleY() / 256. >= 44);
    assert(l.selectionButtonWidth() * l.scaleX() / 256. >= 44);
    assert(l.selectionButtonHeight() * l.scaleY() / 256. >= 44);
    const auto bubble = l.magnifierPopup(20, 100, 10, 34);
    assert(bubble.width == 180 && bubble.x >= 0 && bubble.x + bubble.width <= l.width);
  }
  EditorClipboard c;
  assert(c.copy("one"));
  assert(c.copy("secret", true));
  assert(c.records.size() == 1 && c.sensitiveCurrent);
  assert(!c.copy(std::string(512 * 1024 + 1, 'x')));
  assert(argc >= 3);
  RichText rich(argv[1], argv[2], argc > 3 ? argv[3] : "");
  auto doc = parseMarkdown("An **example** with $x^2$ and `code`.\n\n| A | B |\n|---|---|\n| C | D "
                           "|\n\n```text\na  b\nc\n```");
  bool atomic = false, table = false;
  rich.prepare(doc, 320, 21);
  for (const auto &block : doc.blocks) {
    const auto text = blockPlainText(block);
    const auto normal = rich.layout(block, 320, 21), mapped = rich.layout(block, 320, 21, true);
    assert(normal.width == mapped.width && normal.height == mapped.height &&
           normal.textHits.empty());
    for (const auto &hit : mapped.textHits) {
      assert(hit.begin < hit.end && hit.end <= text.size());
      if (hit.atomic) {
        atomic = true;
        assert(text.substr(hit.begin, hit.end - hit.begin) == "$x^2$");
      }
    }
    if (block.kind == "table") {
      table = true;
      assert(text.find('\t') != std::string::npos);
    }
  }
  assert(atomic && table);
  std::cout << "Text gestures, grapheme selection, clipboard and rich-text mapping passed\n";
}
