#pragma once
#include <cstdint>
#include <string>
namespace penchat { namespace theme {
enum class Mode { Dark, Light };
struct ThemePalette {
  uint32_t scrim;
  uint32_t background;
  uint32_t keyboardCanvas;
  uint32_t primaryText;
  uint32_t blackPage;
  uint32_t keyboardBackground;
  uint32_t sidebar;
  uint32_t surface;
  uint32_t text;
  uint32_t reasoning;
  uint32_t reasoningLink;
  uint32_t muted;
  uint32_t border;
  uint32_t accent;
  uint32_t link;
  uint32_t caret;
  uint32_t danger;
  uint32_t code;
  uint32_t popup;
  uint32_t popupHighlight;
  uint32_t disabledBackground;
  uint32_t disabledText;
  uint32_t richBody;
  uint32_t richHeading;
};
inline const ThemePalette &palette(Mode mode) {
  static const ThemePalette dark{0x000000,0x000000,0x212121,0x212121,0x000000,0x111111,0x171717,0x303030,0xececec,0x9e9e9e,0x7896b4,0xb4b4b4,0x424242,0x3464ef,0x75baff,0x4ade80,0xe79797,0x181818,0x171717,0x303030,0x242424,0x777777,0xd5d7de,0xf4f6fa};
  static const ThemePalette light{0x000000,0xffffff,0xf1f3f6,0xffffff,0xffffff,0xe6e9ef,0xf7f8fa,0xeef1f5,0x20242d,0x626b78,0x315fc3,0x626b78,0xd4dae3,0x3464ef,0x2457c5,0x2457c5,0xb52b3c,0xf0f2f6,0xf7f8fa,0xe4ebf7,0xe4e7ec,0x757d89,0x30343b,0x151922};
  return mode==Mode::Light?light:dark;
}
// The application owns this process-wide appearance on the LVGL thread.
inline Mode &modeStorage() { static Mode value=Mode::Dark;return value; }
inline Mode mode() { return modeStorage(); }
inline void setMode(Mode value) { modeStorage()=value; }
inline const ThemePalette &palette() { return palette(mode()); }
inline const char *name(Mode value) { return value==Mode::Light?"light":"dark"; }
inline Mode parse(const std::string &name) { return name=="light"?Mode::Light:Mode::Dark; }
constexpr int popupBorder = 1;
constexpr int popupRadius = 16;
constexpr int portraitPopupRadius = 24;
constexpr int popupRowHeight = 48;
constexpr int portraitPopupRowHeight = 64;
} } // namespace penchat::theme
