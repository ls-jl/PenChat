#pragma once
#include <cstdint>
namespace penchat {
namespace theme {
constexpr uint32_t scrim = 0x000000;
// Page canvas and keyboard canvas deliberately have separate roles.
constexpr uint32_t background = 0x000000;
constexpr uint32_t keyboardCanvas = 0x212121;
constexpr uint32_t primaryText = 0x212121;
constexpr uint32_t blackPage = 0x000000;
constexpr uint32_t keyboardBackground = 0x111111;
constexpr uint32_t sidebar = 0x171717;
constexpr uint32_t surface = 0x303030;
constexpr uint32_t text = 0xececec;
constexpr uint32_t reasoning = 0x9e9e9e;
constexpr uint32_t reasoningLink = 0x7896b4;
constexpr uint32_t muted = 0xb4b4b4;
constexpr uint32_t border = 0x424242;
constexpr uint32_t accent = 0x3464ef;
constexpr uint32_t link = 0x75baff;
constexpr uint32_t caret = 0x4ade80;
constexpr uint32_t danger = 0xe79797;
constexpr uint32_t code = 0x181818;
constexpr uint32_t popup = sidebar;
constexpr uint32_t popupHighlight = surface;
constexpr uint32_t disabledBackground = 0x242424;
constexpr uint32_t disabledText = 0x777777;
constexpr int popupBorder = 1;
constexpr int popupRadius = 16;
constexpr int portraitPopupRadius = 24;
constexpr int popupRowHeight = 48;
constexpr int portraitPopupRowHeight = 64;
} // namespace theme
} // namespace penchat
