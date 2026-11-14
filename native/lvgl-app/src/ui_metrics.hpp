#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace penchat {
// These are design dimensions, not Falcon coordinates, image pixels or LVGL
// encoded sizes. Convert once, then pass the resolved logical pixels unchanged.
enum class UiFont { Body, Button, PageTitle, FieldTitle, FieldValue, SettingsTitle, Detail };
class UiMetrics {
public:
  explicit UiMetrics(int densityPercent = 100, int scalePercent = 100)
      : density_(densityPercent), scale_(scalePercent) {
    if (density_ < 1 || density_ > 1000 || scale_ < 1 || scale_ > 1000)
      throw std::invalid_argument("UI metric percentages must be between 1 and 1000");
  }
  int densityPercent() const { return density_; }
  int scalePercent() const { return scale_; }
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
    }
    return std::max(1, dp(size));
  }
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
  struct Form { int leadingCaptionPadding, captionPadding; };
  Form form() const { return {dp(52), dp(28)}; }
private:
  int density_, scale_;
};
} // namespace penchat
