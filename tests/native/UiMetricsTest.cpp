#include "ui_metrics.hpp"
#include "layout.hpp"
#include "pull_geometry.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace penchat;
int main() {
  const UiMetrics identity;
  assert(identity.identity() && UiMetrics(200,50).identity());
  assert(!UiMetrics(100,125).identity());
  assert(identity.fontSize(UiFont::FormHint)==17);
  assert(identity.fontSize(UiFont::FormNote)==18);
  assert(identity.fontSize(UiFont::FormCaption)==19);
  assert(identity.fontSize(UiFont::EmptyTitle)==23);
  for (int v : {-123, -2, 0, 1, 21, 44, 960}) assert(identity.dp(v) == v);
  assert(identity.dp(std::numeric_limits<int>::max()) == std::numeric_limits<int>::max());
  assert(identity.dp(std::numeric_limits<int>::min()) == std::numeric_limits<int>::min());
  const UiFont roles[] = {UiFont::Body, UiFont::Button, UiFont::PageTitle, UiFont::FieldTitle,
                         UiFont::FieldValue, UiFont::SettingsTitle, UiFont::Detail};
  const int sizes[] = {21, 20, 26, 15, 19, 20, 16};
  for (int i = 0; i < 7; ++i) assert(identity.fontSize(roles[i]) == sizes[i]);
  for (int percent : {75, 100, 125, 150}) {
    const UiMetrics m(100, percent);
    assert(m.dp(44) == (44 * percent + 50) / 100);
    assert(m.dp(-44) == -m.dp(44));
    assert(m.dp(0) == 0);
    assert(m.form().settingsPitch == (64*percent+50)/100);
    assert(m.form().actionSize == m.dp(44));
    assert(m.form().inset == m.dp(16));
    assert(m.button().captionPadding == (24 * percent + 50) / 100);
    assert(m.field().height == (48 * percent + 50) / 100);
    assert(m.page().headerHeight == m.field().height);
    assert(m.settings().height == (56 * percent + 50) / 100);
    assert(m.fontSize(UiFont::Button) == (20 * percent + 50) / 100);
  }
  assert(UiMetrics(125, 125).dp(1) == 2); // round only after composing both factors
  assert(UiMetrics(100, 150).dp(-1) == -2);
  assert(UiMetrics(1, 1).fontSize(UiFont::Body) == 1);
  assert(identity.fontSize(UiFont::Composer) == 20);
  for (auto size : {std::make_pair(960,266), std::make_pair(640,260), std::make_pair(568,1210)}) {
    const AppLayout original(size.first,size.second);
    assert(original.composerHeight() == (original.portrait()?94:48));
    assert(original.composerX() == (original.portrait()?24:106));
    for (int percent : {75,100,125,150}) {
      const UiMetrics metrics(100,percent);
      const AppLayout layout(size.first,size.second,false,metrics);
      assert(layout.category()==original.category());
      assert(layout.width==size.first && layout.height==size.second);
      assert(layout.composerHeight()==metrics.composer().control+2*layout.composerInset());
      assert(layout.composerY()+layout.composerHeight()+layout.safeBottom()==size.second);
      assert(layout.composerX()>=0 && layout.composerWidth()>0);
      assert(layout.composerX()+layout.composerWidth()<=size.first);
      assert(layout.bodyWidth()+layout.gutter()+metrics.dp(4)==size.first);
      assert(layout.contentWidth()>0 && layout.chatMessageWidth()>0);
      const AppLayout rounded(size.first,size.second,true,metrics);
      const AppLayout unscaledRounded(size.first,size.second,true);
      assert(rounded.safety().radius()==unscaledRounded.safety().radius());
      assert(rounded.safety().margin()==unscaledRounded.safety().margin());
      // Relative pull thresholds consume the resulting reader once; there is
      // deliberately no scale argument or physical-touch multiplier here.
      auto g=pullGeometry({layout.gutter(),layout.chatReaderTop(),size.first,size.second},
                          size.first,size.second,layout.composerY());
      assert(g.reader.bottom==layout.composerY());
      assert(g.start>=1 && g.fill>g.start);
      const auto camera=layout.cameraWorkspace(size.first,size.second,false);
      const auto crop=layout.cropWorkspace(size.first,size.second);
      const auto scan=layout.scanWorkspace();
      for (auto rect : {camera.preview,camera.shutter,crop.viewport,crop.confirm,scan.preview,scan.use})
        assert(rect.width>0 && rect.height>0 && rect.x>=0 && rect.y>=0);
    }
  }
  bool invalid = false, overflow = false;
  try { UiMetrics m(0); } catch (const std::invalid_argument &) { invalid = true; }
  try { UiMetrics(1000, 1000).dp(std::numeric_limits<int>::max()); }
  catch (const std::overflow_error &) { overflow = true; }
  assert(invalid && overflow);
  std::cout << "UI_METRICS_IDENTITY_ROUNDING_ROLES_OK\n";
}
