#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace penchat {
// These are design dimensions, not Falcon coordinates, image pixels or LVGL
// encoded sizes. Convert once, then pass the resolved logical pixels unchanged.
enum class UiFont { Body, Button, PageTitle, FieldTitle, FieldValue, SettingsTitle, Detail, Composer, FormHint, FormNote, FormCaption, EmptyTitle, Tiny, Small, Badge, LargeCaption, Heading, Display, Hero, HeroLarge };
class UiMetrics {
public:
  explicit UiMetrics(int densityPercent = 100, int scalePercent = 100)
      : density_(densityPercent), scale_(scalePercent) {
    if (density_ < 1 || density_ > 1000 || scale_ < 1 || scale_ > 1000)
      throw std::invalid_argument("UI metric percentages must be between 1 and 1000");
  }
  int densityPercent() const { return density_; }
  int scalePercent() const { return scale_; }
  bool identity() const { return density_ * scale_ == 10000; }
  int dp(int designPixels) const {
    const int64_t product = int64_t(designPixels) * density_ * scale_;
    const int64_t rounded = product < 0 ? -((-product + 5000) / 10000) : (product + 5000) / 10000;
    if (rounded < std::numeric_limits<int>::min() || rounded > std::numeric_limits<int>::max())
      throw std::overflow_error("UI dimension exceeds logical pixel range");
    return int(rounded);
  }
  int fontSize(UiFont role) const {
    int size = 21;
    switch (role) {
    case UiFont::Body: size = 21; break;
    case UiFont::Button: size = 20; break;
    case UiFont::PageTitle: size = 26; break;
    case UiFont::FieldTitle: size = 15; break;
    case UiFont::FieldValue: size = 19; break;
    case UiFont::SettingsTitle: size = 20; break;
    case UiFont::Detail: size = 16; break;
    case UiFont::Composer: size = 20; break;
    case UiFont::FormHint: size = 17; break;
    case UiFont::FormNote: size = 18; break;
    case UiFont::FormCaption: size = 19; break;
    case UiFont::EmptyTitle: size = 23; break;
    case UiFont::Tiny: size = 12; break;
    case UiFont::Small: size = 13; break;
    case UiFont::Badge: size = 14; break;
    case UiFont::LargeCaption: size = 22; break;
    case UiFont::Heading: size = 24; break;
    case UiFont::Display: size = 28; break;
    case UiFont::Hero: size = 30; break;
    case UiFont::HeroLarge: size = 32; break;
    }
    return std::max(1, dp(size));
  }
  int readingFontSize(int percent) const {
    const int64_t value=int64_t(21)*density_*scale_*percent;
    return std::max(1,int((value+500000)/1000000));
  }
  double factor() const { return double(density_)*scale_/10000.; }
  struct Gesture {
    int tap, drag, key;
    int64_t tapSquared() const { return int64_t(tap)*tap; }
  };
  Gesture gestures() const { return {dp(12),dp(12),dp(12)}; }
  struct Popup { int margin, gap, minimum, row, portraitRow, padding, portraitPadding, width, portraitWidth; };
  Popup popup() const { return {dp(8),dp(8),dp(44),dp(48),dp(64),dp(4),dp(8),dp(224),dp(320)}; }
  struct Button { int radius, captionPadding; };
  Button button() const { return {dp(10), dp(24)}; }
  struct Field { int height, inset, top, captionHeight, valueHeight, valueTop, gap; };
  Field field() const { return {dp(48), dp(12), dp(3), dp(19), dp(23), dp(23), dp(2)}; }
  struct Page {
    int headerHeight, actionTop, actionHeight, navigationGap, titleInset, titleTop;
    int minimumTail, scrollWidth, minimumTitle, portraitActionGap, actionGap;
  };
  Page page() const {
    return {dp(48), dp(2), dp(44), dp(56), dp(10), dp(7), dp(60), dp(3), dp(40), dp(80), dp(24)};
  }
  struct Settings {
    int height, radius, inset, titleTop, detailTop, textTrailing;
    int toggleTrailing, toggleTop, toggleWidth, toggleHeight, knobPadding;
    int arrowTrailing, arrowTop, arrowSize;
  };
  Settings settings() const {
    return {dp(56), dp(12), dp(12), dp(3), dp(31), dp(80),
            dp(56), dp(16), dp(44), dp(24), dp(-2), dp(50), dp(6), dp(44)};
  }
  struct Form {
    int leadingCaptionPadding, captionPadding, top, inset, gap, minimumItemHeight;
    int actionReserve, actionSize, actionTop, bottom;
    int settingsTop, settingsGap, settingsPitch, columnGap, sectionMinimum;
  };
  Form form() const {
    return {dp(52), dp(28), dp(8), dp(16), dp(10), dp(20), dp(56), dp(44),
            dp(6), dp(12), dp(4), dp(8), dp(64), dp(16), dp(20)};
  }
  struct Composer {
    int control, portraitSend, portraitInset, landscapeInset;
    int expandedPad, textInset, textTop, textToolsGap, dockGap, readerGap;
    int messageBottom, returnGap, hiddenOffset, attachmentHitGrow;
    int gaugeTrailing, initialGaugeTrailing, initialSendTrailing, initialTop;
    int border;
    int returnOffset() const { return control + returnGap; }
    int textPadding() const { return 2 * textInset; }
  };
  Composer composer() const {
    return {dp(44), dp(54), dp(25), dp(2), dp(14), dp(12), dp(32), dp(8),
            dp(4), dp(8), dp(16), dp(12), dp(2), dp(8), dp(96), dp(100), dp(50), dp(2), dp(1)};
  }
private:
  int density_, scale_;
};
} // namespace penchat
