#include "ui_metrics.hpp"
#include "ui_scale_settings.hpp"
#include "layout.hpp"
#include "pull_geometry.hpp"
#include "back_geometry.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace penchat;
int main() {
  assert(savedUiScale(Json::object())==100);
  for(const auto &bad:{Json(),Json(true),Json("125"),Json(125.5)})assert(savedUiScale({{"ui_scale_percent",bad}})==100);
  assert(savedUiScale({{"ui_scale_percent",-300}})==75);
  assert(savedUiScale({{"ui_scale_percent",uint64_t(-1)}})==150);
  for(int percent=75;percent<=150;++percent) {
    assert(savedUiScale({{"ui_scale_percent",percent}})==percent);
    const UiMetrics ui(100,percent);
    assert(ui.page().actionHeight>=44 && ui.page().headerHeight>=ui.page().actionHeight+ui.page().actionTop);
    assert(ui.popup().row-ui.dp(4)>=44 && ui.popup().portraitRow-ui.dp(4)>=44);
  }
  const UiMetrics identity;
  for(int width:{568,640,936,960})for(int scale:{75,100,125,150}) {
    const UiMetrics metrics(100,scale);
    const auto back=backGeometry(width,metrics);
    assert(back.edge>=44);
    assert(back.edge==std::max(metrics.touchTarget(20),int(std::lround(width*.046))));
    assert(back.slop==std::max(metrics.dp(8),int(std::lround(width*.0125))));
    assert(back.distance==std::max(metrics.dp(44),int(std::lround(width*.10))));
    const int oldEdge=std::max(metrics.dp(20),int(std::lround(width*.046)));
    if(oldEdge>=44)assert(back.edge==oldEdge);
    else assert(back.edge==44);
  }
  assert(savedTextScale(Json::object())==100);
  for(const auto &bad:{Json(),Json(true),Json("125"),Json(125.5)})assert(savedTextScale({{"reading_font_percent",bad}})==100);
  assert(savedTextScale({{"reading_font_percent",-300}})==75);
  assert(savedTextScale({{"reading_font_percent",uint64_t(-1)}})==150);
  const UiMetrics enlargedText(100,100,150);
  assert(enlargedText.identity() && enlargedText.dp(44)==44);
  assert(enlargedText.textPercent()==150 && enlargedText.fontSize(UiFont::Body)==32);
  assert(enlargedText.fontSize(UiFont::PageTitle)==39);
  assert(enlargedText.readingFontSize(100)==21); // preview replaces the saved text factor
  assert(UiMetrics(125,125,125).sp(1)==2); // compose three factors before rounding
  assert(UiMetrics(100,150,150).fontSize(UiFont::Body)==47);
  assert(enlargedText.sp(-21)==-32);
  for(int text:{75,100,125,150})for(int scale:{75,100,125,150}) {
    const UiMetrics combined(100,scale,text);
    assert(combined.dp(44)==UiMetrics(100,scale).dp(44));
    for(auto role:{UiFont::Body,UiFont::Button,UiFont::PageTitle,UiFont::FieldTitle,
        UiFont::FieldValue,UiFont::Composer,UiFont::FormNote,UiFont::Tiny})
      assert(combined.fontSize(role)==(int64_t(identity.fontSize(role))*scale*text+5000)/10000);
  }
  assert(UiMetrics(100,125).readingFontSize(80)==21);
  assert(UiMetrics(100,150).readingFontSize(150)==47);
  assert(UiMetrics(100,75).gestures().tap==9);
  assert(UiMetrics(100,150).gestures().tapSquared()==324);
  assert(UiMetrics(100,125).popup().row==60);
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
    assert(m.page().headerHeight >= m.field().height);
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
    assert(original.composerX() == (original.portrait()?20:106));
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
      if(rounded.portrait()) {
        const auto scan=rounded.scanWorkspace();
        const auto crop=rounded.cropWorkspace(size.first,size.second);
        assert(scan.use.height>=44 && scan.cancel.height>=44);
        assert(scan.use.y==scan.cancel.y);
        assert(scan.use.x+scan.use.width<scan.cancel.x);
        assert(std::abs(scan.use.x+scan.cancel.x+scan.cancel.width-size.first)<=1);
        assert(crop.confirm.height>=44 && crop.album.height>=44);
        assert(crop.album.y+crop.album.height<=crop.confirm.y);
      }
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
