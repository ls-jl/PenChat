#pragma once
#include "layout.hpp"
namespace penchat {
// Composer edges are hard anchors. Vertical centering is the closest feasible
// position after fitting actual rows; never inflate a short menu to center it.
inline LayoutRect attachmentMenuArea(const AppLayout &layout, LayoutRect composer,
                                      int measuredWidth, int contentHeight) {
  const int top=layout.safety().enabled()?layout.safeTop():layout.dp(8);
  const int height=std::min(contentHeight,std::max(1,layout.height-top));
  const int width=std::min(measuredWidth,std::max(1,layout.width-composer.x));
  const int requiredBottom=std::min(layout.height,composer.y+composer.height+layout.dp(10));
  const int minimumY=std::max(top,requiredBottom-height);
  const int y=std::max(minimumY,std::min(layout.height-height,(layout.height-height)/2));
  return {composer.x,y,width,height};
}
} // namespace penchat
