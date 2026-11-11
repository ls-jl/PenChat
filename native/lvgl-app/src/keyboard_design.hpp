#pragma once
#include "theme.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>

namespace penchat {
// Compile-time authoring surface. Pixels are Falcon logical pixels, except the
// explicitly named compact split canvas. No device resolution belongs here.
enum class KeyboardView { Wide, Compact, CompactExpanded, Portrait };
enum class KeyboardRole { Character, Function, Primary, Auxiliary, Input, Composition, Candidates, Candidate, Menu, Count };
enum class KeyboardVariant { Default, UtilityIcon, FinishIcon, SolidControl, FilterSelected, DeleteKey, Count };
enum class KeyboardFont { Character, KeyFunction, Function, Candidate, Composition, Menu, Editor, InlineField, Filter, Heading, Hint, Count };

struct KeyboardStyle {
  uint32_t background=theme::surface, foreground=theme::text;
  uint32_t pressedBackground=theme::border, selectedBackground=theme::surface;
  int radius=9, opacity=153, pressedOpacity=153, disabledOpacity=255;
};
// -1 means inherit. Patches only contain visual properties; actions and layout
// geometry cannot be overridden by a style.
struct KeyboardStylePatch {
  int64_t background=-1, foreground=-1, pressedBackground=-1, selectedBackground=-1;
  int radius=-1, opacity=-1, pressedOpacity=-1, disabledOpacity=-1;
  void apply(KeyboardStyle &s) const {
    if(background>=0)s.background=uint32_t(background);
    if(foreground>=0)s.foreground=uint32_t(foreground);
    if(pressedBackground>=0)s.pressedBackground=uint32_t(pressedBackground);
    if(selectedBackground>=0)s.selectedBackground=uint32_t(selectedBackground);
    if(radius>=0)s.radius=radius;
    if(opacity>=0)s.opacity=opacity;
    if(pressedOpacity>=0)s.pressedOpacity=pressedOpacity;
    if(disabledOpacity>=0)s.disabledOpacity=disabledOpacity;
  }
};
struct KeyboardVisualState { bool pressed=false, disabled=false, selected=false; };
struct KeyboardResolvedStyle {
  uint32_t background, foreground;
  int radius, backgroundOpacity, opacity;
};
struct KeyboardTypography {
  std::array<int,size_t(KeyboardFont::Count)> sizes{{23,23,17,20,16,20,20,19,17,19,18}};
};
struct KeyboardProfile {
  // Zero inherits the common font role. Geometry is defined separately below.
  std::array<int,size_t(KeyboardFont::Count)> fonts{};
};
struct KeyboardCandidateMetrics {
  int height=44, rowGap=4, minimumWidth=44, naturalMinimum=52;
  int textPadding=24, correctionPadding=38, captionInset=8, correctedCaptionInset=22;
  int separatorInset=9, separatorHeight=26;
  int correctionSize=5, correctionRight=13, correctionTop=7;
};
struct KeyboardDesign {
  KeyboardStyle base;
  std::array<KeyboardStylePatch,size_t(KeyboardRole::Count)> roles{};
  std::array<KeyboardStylePatch,size_t(KeyboardVariant::Count)> variants{};
  KeyboardTypography typography;
  std::array<KeyboardProfile,4> profiles{};
  uint32_t background=theme::keyboardBackground, popupBackground=theme::sidebar;
  uint32_t compositionText=theme::muted, caret=theme::caret, selection=theme::border;
  struct Keys {
    int gap=4, rowGap=6, margin=4, touchX=4, touchY=3;
    int filterWidth=88, utilitySize=48, utilityGrow=24;
    int utilityShiftX=14, utilityShiftY=3;
  } keys;
  struct Composition {
    int gap=4, minimum=12, maximum=144, widthDivisor=3, padding=8;
    int labelX=2, labelY=11;
    int portraitExpandWidth=64, portraitExpandHeight=48, portraitRight=2;
  } composition;
  KeyboardCandidateMetrics candidates;
  struct Controls {
    int height=44, gap=4, doneWidth=104, clipboardWidth=44, ocrWidth=48;
    int portraitCancelWidth=84, portraitOcrWidth=112, portraitClipboardWidth=48;
    int portraitDoneWidth=64;
    int captionInset=4, popupCaptionInset=24, inputPadding=6, caretOffset=8;
    int finishTouchGrow=8;
  } controls;
  struct Wide {
    int editorMinimum=352, inset=8, textTop=56, textActionGap=6;
    int keyTop=6, bottomInset=6;
    double editorFraction=.4;
  } wide;
  struct Compact {
    int headerHeight=44, headerGap=4, textLeading=100, textTrailing=52;
    int doneWidth=64, bottomInset=4;
    int splitCanvasWidth=960, splitCanvasHeight=266;
  } compact;
  struct Portrait {
    int rowMin=52, rowMax=72, bottomMin=44, bottomMax=60, liftStep=10;
    double rowRatio=.12, bottomRatio=.10, keyHeightScale=1.5;
    int actionGap=8, panelInset=4, editorGap=8, overlayGap=10;
  } portrait;
  struct Icons { float keyScale=1, utilityScale=1, controlScale=1; } icons;
  struct Popups {
    int safeMargin=4, radius=12, menuRadius=16, portraitMenuRadius=24;
    int menuWidth=224, portraitMenuWidth=320, menuRow=48, portraitMenuRow=64;
    int menuPadding=4, portraitMenuPadding=8, menuGap=12;
    int lensWidth=180, lensHeight=58, lensOffset=96, portraitLensOffset=58;
    int lensScale=448, selectionRadius=10;
  } popups;

  KeyboardDesign() {
    auto &primary=roles[size_t(KeyboardRole::Primary)];
    primary.background=theme::text;primary.foreground=theme::background;
    primary.pressedBackground=theme::muted;primary.radius=10;
    primary.opacity=primary.pressedOpacity=255;primary.disabledOpacity=127;
    auto &auxiliary=roles[size_t(KeyboardRole::Auxiliary)];
    auxiliary.radius=10;auxiliary.opacity=51;auxiliary.pressedOpacity=204;
    auto &input=roles[size_t(KeyboardRole::Input)];
    input.radius=8;input.opacity=51;
    auto &composition=roles[size_t(KeyboardRole::Composition)];
    composition.background=theme::background;composition.radius=8;composition.opacity=51;
    auto &candidates=roles[size_t(KeyboardRole::Candidates)];
    candidates.background=theme::background;candidates.radius=8;candidates.opacity=0;
    auto &candidate=roles[size_t(KeyboardRole::Candidate)];
    candidate.radius=6;candidate.opacity=0;candidate.pressedOpacity=255;
    auto &menu=roles[size_t(KeyboardRole::Menu)];
    menu.background=theme::sidebar;menu.radius=10;menu.opacity=menu.pressedOpacity=255;
    auto &utility=variants[size_t(KeyboardVariant::UtilityIcon)];
    utility.opacity=0;
    auto &finish=variants[size_t(KeyboardVariant::FinishIcon)];
    finish.opacity=finish.pressedOpacity=0;finish.foreground=theme::text;
    auto &solid=variants[size_t(KeyboardVariant::SolidControl)];
    solid.opacity=solid.pressedOpacity=255;
    auto &filter=variants[size_t(KeyboardVariant::FilterSelected)];
    filter.background=theme::border;filter.opacity=filter.pressedOpacity=255;
    profiles[size_t(KeyboardView::CompactExpanded)].fonts[size_t(KeyboardFont::Function)]=22;
    profiles[size_t(KeyboardView::Portrait)].fonts[size_t(KeyboardFont::Menu)]=22;
  }
  int font(KeyboardFont role,KeyboardView view) const {
    const int override=profiles[size_t(view)].fonts[size_t(role)];
    return override>0?override:typography.sizes[size_t(role)];
  }
  KeyboardStyle style(KeyboardRole role,KeyboardVariant variant=KeyboardVariant::Default) const {
    auto value=base;roles[size_t(role)].apply(value);variants[size_t(variant)].apply(value);return value;
  }
  KeyboardResolvedStyle resolve(KeyboardRole role,KeyboardVariant variant={},KeyboardVisualState state={}) const {
    const auto s=style(role,variant);
    const bool pressed=state.pressed&&!state.disabled;
    return {pressed?s.pressedBackground:state.selected?s.selectedBackground:s.background,s.foreground,
            s.radius,pressed?s.pressedOpacity:s.opacity,state.disabled?s.disabledOpacity:255};
  }
};
inline const KeyboardDesign &keyboardDesign() {
  static const KeyboardDesign value;
  return value;
}
} // namespace penchat
