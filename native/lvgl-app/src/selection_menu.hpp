#pragma once
#include "layout.hpp"
namespace penchat {
struct SelectionMenu {
  enum Position { Above, Below, LowerLeft } position = Above;
  LayoutRect bounds{};
};
inline LayoutRect selectionMenuSafeArea(int width, int height) {
  const AppLayout layout{width, height};
  const int side = std::max(8, layout.safeSide()), top = std::max(4, layout.safeTop());
  return {side, top, std::max(1, width - 2 * side),
          std::max(1, height - top - std::max(4, layout.safeBottom()))};
}
inline SelectionMenu placeSelectionMenu(LayoutRect selected, LayoutRect safe, int width,
                                        int height, int belowClearance=0) {
  width = std::min(width, safe.width);
  height = std::min(height, safe.height);
  const int x = std::max(
      safe.x, std::min(safe.x + safe.width - width, selected.x + (selected.width - width) / 2));
  if (selected.y - 8 - height >= safe.y)
    return {SelectionMenu::Above, {x, selected.y - 8 - height, width, height}};
  if (selected.y + selected.height + belowClearance + 8 + height <= safe.y + safe.height)
    return {SelectionMenu::Below, {x, selected.y + selected.height + belowClearance + 8, width, height}};
  return {SelectionMenu::LowerLeft, {safe.x, safe.y + safe.height - height, width, height}};
}
inline int selectionMenuClearance(LayoutRect selected,const SelectionMenu &menu,LayoutRect grip) {
  if(menu.position!=SelectionMenu::Below||grip.width<=0||grip.x>=menu.bounds.x+menu.bounds.width||
    grip.x+grip.width<=menu.bounds.x)return 0;
  return std::max(0,grip.y+grip.height-selected.y-selected.height);
}
inline void includeSelectionRect(LayoutRect &bounds, LayoutRect rect) {
  if (rect.width <= 0 || rect.height <= 0)
    return;
  if (bounds.width <= 0 || bounds.height <= 0) {
    bounds = rect;
    return;
  }
  const int right = std::max(bounds.x + bounds.width, rect.x + rect.width);
  const int bottom = std::max(bounds.y + bounds.height, rect.y + rect.height);
  bounds.x = std::min(bounds.x, rect.x);
  bounds.y = std::min(bounds.y, rect.y);
  bounds.width = right - bounds.x;
  bounds.height = bottom - bounds.y;
}
} // namespace penchat
