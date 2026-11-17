#pragma once
#include "theme.hpp"
#include "ui_metrics.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>

namespace penchat {
// Compile-time authoring surface. Pixels are Falcon logical pixels, except the
// explicitly named compact split canvas. No device resolution belongs here.
enum class KeyboardView { Wide, Compact, CompactExpanded, Portrait };
enum class KeyboardRole { Character, Function, Primary, Auxiliary, Input, Composition, Candidates, Candidate, Menu, Count };
enum class KeyboardVariant { Default, UtilityIcon, FinishIcon, SolidControl, FilterSelected, DeleteKey, Count };
enum class KeyboardFont { Character, KeyFunction, Function, Candidate, Composition, Menu, Editor, InlineField, Filter, Heading, Hint, Count };

struct KeyboardStyle {
  uint32_t background=theme::palette().surface, foreground=theme::palette().text;
  uint32_t pressedBackground=theme::palette().border, selectedBackground=theme::palette().surface;
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
  uint32_t background=theme::palette().keyboardBackground, popupBackground=theme::palette().popup;
  uint32_t compositionText=theme::palette().muted, caret=theme::palette().caret, selection=theme::palette().border;
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
    int height=44, gap=4, doneWidth=104, clipboardWidth=44, ocrWidth=48, officialWidth=64;
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
    int safeMargin=4, radius=12, menuRadius=theme::popupRadius, portraitMenuRadius=theme::portraitPopupRadius;
    int menuWidth=224, portraitMenuWidth=320, menuRow=theme::popupRowHeight, portraitMenuRow=theme::portraitPopupRowHeight;
    int menuPadding=4, portraitMenuPadding=8, menuGap=12;
    int lensWidth=180, lensHeight=58, lensOffset=96, portraitLensOffset=58;
    int lensScale=448, selectionRadius=10;
  } popups;

  KeyboardDesign() {
    if(theme::mode()==theme::Mode::Light) {
      // Opaque bright caps stay distinct from the light keyboard background.
      for(auto role:{KeyboardRole::Character,KeyboardRole::Function}) {
        auto &cap=roles[size_t(role)];
        cap.background=theme::palette().background;
        cap.opacity=cap.pressedOpacity=255;
      }
    }
    auto &primary=roles[size_t(KeyboardRole::Primary)];
    primary.background=theme::palette().text;primary.foreground=theme::palette().primaryText;
    primary.pressedBackground=theme::palette().muted;primary.radius=10;
    primary.opacity=primary.pressedOpacity=255;primary.disabledOpacity=127;
    auto &auxiliary=roles[size_t(KeyboardRole::Auxiliary)];
    auxiliary.radius=10;auxiliary.opacity=51;auxiliary.pressedOpacity=204;
    auto &input=roles[size_t(KeyboardRole::Input)];
    input.radius=8;input.opacity=theme::mode()==theme::Mode::Light?255:51;
    if(theme::mode()==theme::Mode::Light)input.background=theme::palette().background;
    auto &composition=roles[size_t(KeyboardRole::Composition)];
    composition.background=theme::palette().keyboardCanvas;composition.radius=8;composition.opacity=51;
    auto &candidates=roles[size_t(KeyboardRole::Candidates)];
    candidates.background=theme::palette().keyboardCanvas;candidates.radius=8;candidates.opacity=0;
    auto &candidate=roles[size_t(KeyboardRole::Candidate)];
    candidate.radius=6;candidate.opacity=0;candidate.pressedOpacity=255;
    auto &menu=roles[size_t(KeyboardRole::Menu)];
    menu.background=theme::palette().sidebar;menu.radius=10;menu.opacity=menu.pressedOpacity=255;
    auto &utility=variants[size_t(KeyboardVariant::UtilityIcon)];
    utility.opacity=0;
    auto &finish=variants[size_t(KeyboardVariant::FinishIcon)];
    finish.opacity=finish.pressedOpacity=0;finish.foreground=theme::palette().text;
    auto &solid=variants[size_t(KeyboardVariant::SolidControl)];
    solid.opacity=solid.pressedOpacity=255;
    auto &filter=variants[size_t(KeyboardVariant::FilterSelected)];
    filter.background=theme::palette().border;filter.opacity=filter.pressedOpacity=255;
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
  static std::unique_ptr<KeyboardDesign> values[2];
  auto &value=values[theme::mode()==theme::Mode::Light?1:0];
  if(!value)value.reset(new KeyboardDesign);
  return *value;
}

// Authoring values remain in design units. Ratios, the legacy compact canvas,
// and inheritance sentinels are not dimensions and never pass through dp().
inline KeyboardDesign resolveKeyboardDesign(const KeyboardDesign &base,const UiMetrics &metrics) {
  auto result=base;
  result.base.radius=metrics.dp(base.base.radius);
  for(auto &patch:result.roles)if(patch.radius>=0)patch.radius=metrics.dp(patch.radius);
  for(auto &patch:result.variants)if(patch.radius>=0)patch.radius=metrics.dp(patch.radius);
  for(auto &size:result.typography.sizes)size=std::max(1,metrics.sp(size));
  for(auto &profile:result.profiles)for(auto &size:profile.fonts)if(size>0)size=std::max(1,metrics.sp(size));
  result.keys.gap=metrics.dp(base.keys.gap);
  result.keys.rowGap=metrics.dp(base.keys.rowGap);
  result.keys.margin=metrics.dp(base.keys.margin);
  result.keys.touchX=metrics.dp(base.keys.touchX);
  result.keys.touchY=metrics.dp(base.keys.touchY);
  result.keys.filterWidth=metrics.dp(base.keys.filterWidth);
  result.keys.utilitySize=metrics.touchTarget(base.keys.utilitySize);
  result.keys.utilityGrow=metrics.dp(base.keys.utilityGrow);
  result.keys.utilityShiftX=metrics.dp(base.keys.utilityShiftX);
  result.keys.utilityShiftY=metrics.dp(base.keys.utilityShiftY);
  result.composition.gap=metrics.dp(base.composition.gap);
  result.composition.minimum=metrics.dp(base.composition.minimum);
  result.composition.maximum=metrics.dp(base.composition.maximum);
  result.composition.padding=metrics.dp(base.composition.padding);
  result.composition.labelX=metrics.dp(base.composition.labelX);
  result.composition.labelY=metrics.dp(base.composition.labelY);
  result.composition.portraitExpandWidth=metrics.dp(base.composition.portraitExpandWidth);
  result.composition.portraitExpandHeight=metrics.dp(base.composition.portraitExpandHeight);
  result.composition.portraitRight=metrics.dp(base.composition.portraitRight);
  result.controls.height=metrics.touchTarget(base.controls.height);
  result.controls.gap=metrics.dp(base.controls.gap);
  result.controls.doneWidth=metrics.dp(base.controls.doneWidth);
  result.controls.clipboardWidth=metrics.dp(base.controls.clipboardWidth);
  result.controls.ocrWidth=metrics.dp(base.controls.ocrWidth);
  result.controls.officialWidth=metrics.dp(base.controls.officialWidth);
  result.controls.portraitCancelWidth=metrics.dp(base.controls.portraitCancelWidth);
  result.controls.portraitOcrWidth=metrics.dp(base.controls.portraitOcrWidth);
  result.controls.portraitClipboardWidth=metrics.dp(base.controls.portraitClipboardWidth);
  result.controls.portraitDoneWidth=metrics.dp(base.controls.portraitDoneWidth);
  result.controls.captionInset=metrics.dp(base.controls.captionInset);
  result.controls.popupCaptionInset=metrics.dp(base.controls.popupCaptionInset);
  result.controls.inputPadding=metrics.dp(base.controls.inputPadding);
  result.controls.caretOffset=metrics.dp(base.controls.caretOffset);
  result.controls.finishTouchGrow=metrics.dp(base.controls.finishTouchGrow);
  result.wide.editorMinimum=metrics.dp(base.wide.editorMinimum);
  result.wide.inset=metrics.dp(base.wide.inset);
  result.wide.textTop=metrics.dp(base.wide.textTop);
  result.wide.textActionGap=metrics.dp(base.wide.textActionGap);
  result.wide.keyTop=metrics.dp(base.wide.keyTop);
  result.wide.bottomInset=metrics.dp(base.wide.bottomInset);
  result.compact.headerHeight=std::max(result.controls.height,metrics.dp(base.compact.headerHeight));
  result.compact.headerGap=metrics.dp(base.compact.headerGap);
  result.compact.textLeading=std::max(metrics.dp(base.compact.textLeading),2*result.controls.height+3*result.controls.gap);
  result.compact.textTrailing=std::max(metrics.dp(base.compact.textTrailing),result.controls.height+2*result.controls.gap);
  result.compact.doneWidth=metrics.dp(base.compact.doneWidth);
  result.compact.bottomInset=metrics.dp(base.compact.bottomInset);
  result.portrait.rowMin=metrics.dp(base.portrait.rowMin);
  result.portrait.rowMax=metrics.dp(base.portrait.rowMax);
  result.portrait.bottomMin=metrics.dp(base.portrait.bottomMin);
  result.portrait.bottomMax=metrics.dp(base.portrait.bottomMax);
  result.portrait.liftStep=metrics.dp(base.portrait.liftStep);
  result.portrait.actionGap=metrics.dp(base.portrait.actionGap);
  result.portrait.panelInset=metrics.dp(base.portrait.panelInset);
  result.portrait.editorGap=metrics.dp(base.portrait.editorGap);
  result.portrait.overlayGap=metrics.dp(base.portrait.overlayGap);
  result.popups.safeMargin=metrics.dp(base.popups.safeMargin);
  result.popups.radius=metrics.dp(base.popups.radius);
  result.popups.menuRadius=metrics.dp(base.popups.menuRadius);
  result.popups.portraitMenuRadius=metrics.dp(base.popups.portraitMenuRadius);
  result.popups.menuWidth=metrics.dp(base.popups.menuWidth);
  result.popups.portraitMenuWidth=metrics.dp(base.popups.portraitMenuWidth);
  result.popups.menuRow=metrics.dp(base.popups.menuRow);
  result.popups.portraitMenuRow=metrics.dp(base.popups.portraitMenuRow);
  result.popups.menuPadding=metrics.dp(base.popups.menuPadding);
  result.popups.portraitMenuPadding=metrics.dp(base.popups.portraitMenuPadding);
  result.popups.menuGap=metrics.dp(base.popups.menuGap);
  result.popups.lensWidth=metrics.dp(base.popups.lensWidth);
  result.popups.lensHeight=metrics.dp(base.popups.lensHeight);
  result.popups.lensOffset=metrics.dp(base.popups.lensOffset);
  result.popups.portraitLensOffset=metrics.dp(base.popups.portraitLensOffset);
  result.popups.selectionRadius=metrics.dp(base.popups.selectionRadius);
  result.candidates.height=metrics.dp(base.candidates.height);
  result.candidates.rowGap=metrics.dp(base.candidates.rowGap);
  result.candidates.minimumWidth=metrics.dp(base.candidates.minimumWidth);
  result.candidates.naturalMinimum=metrics.dp(base.candidates.naturalMinimum);
  result.candidates.textPadding=metrics.dp(base.candidates.textPadding);
  result.candidates.correctionPadding=metrics.dp(base.candidates.correctionPadding);
  result.candidates.captionInset=metrics.dp(base.candidates.captionInset);
  result.candidates.correctedCaptionInset=metrics.dp(base.candidates.correctedCaptionInset);
  result.candidates.separatorInset=metrics.dp(base.candidates.separatorInset);
  result.candidates.separatorHeight=metrics.dp(base.candidates.separatorHeight);
  result.candidates.correctionSize=metrics.dp(base.candidates.correctionSize);
  result.candidates.correctionRight=metrics.dp(base.candidates.correctionRight);
  result.candidates.correctionTop=metrics.dp(base.candidates.correctionTop);
  return result;
}
} // namespace penchat
