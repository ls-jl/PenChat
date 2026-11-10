#include "app.hpp"
#include "icons.hpp"
#include "text_selection_geometry.hpp"
#include <algorithm>
#include <limits>
namespace penchat {
namespace {
bool contains(const lv_area_t &a, int x, int y) {
  return x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2;
}
bool inside(lv_obj_t *object, lv_obj_t *root) {
  for (auto p = object; p; p = lv_obj_get_parent(p))
    if (p == root)
      return true;
  return false;
}
size_t scalar(const std::vector<size_t> &bytes, size_t at) {
  return std::lower_bound(bytes.begin(), bytes.end(), at) - bytes.begin();
}
} // namespace
App::ReadRegion *App::readRegion(uint64_t id) {
  for (auto &entry : readRegions_)
    if (entry.second.id == id)
      return &entry.second;
  return nullptr;
}
uint64_t App::readBegin(lv_obj_t *root, const std::string &key) {
  auto old = readRegions_.find(root);
  const bool fresh = old == readRegions_.end();
  if (!fresh && old->second.id == readActive_)
    readReset();
  ReadRegion region;
  region.id = ++readSerial_;
  region.root = root;
  region.key = key;
  readRegions_[root] = std::move(region);
  if (fresh)
    lv_obj_add_event_cb(
        root,
        [](lv_event_t *e) {
          if (lv_event_get_target_obj(e) != lv_event_get_current_target_obj(e))
            return;
          auto app = static_cast<App *>(lv_event_get_user_data(e));
          auto found = app->readRegions_.find(lv_event_get_target_obj(e));
          if (found != app->readRegions_.end()) {
            if (found->second.id == app->readActive_) {
              if (app->readGesture_.active())
                app->readSuppressed_ = app->readDown_;
              app->readActive_ = 0;
              app->readGesture_.cancel();
            }
            app->readRegions_.erase(found);
          }
        },
        LV_EVENT_DELETE, this);
  return readRegions_.at(root).id;
}
void App::readRich(lv_obj_t *root, lv_obj_t *object, const Block &block, int width, int size,
                   const RichLayout &metrics) {
  auto found = readRegions_.find(root);
  if (found == readRegions_.end())
    return;
  auto &region = found->second;
  auto text = blockPlainText(block);
  if (text.empty())
    return;
  if (!region.text.empty()) {
    const std::string separator = block.kind == "table" ? "\n" : "\n\n";
    region.text += separator;
    region.length += separator.size();
  }
  ReadPart part;
  part.object = object;
  part.begin = region.length;
  part.length = editLength(text);
  region.length += part.length;
  part.width = width;
  part.size = size;
  part.rich = true;
  part.links = metrics.links;
  region.text += text;
  region.parts.push_back(std::move(part));
  struct Owner {
    App *app;
    lv_obj_t *root;
    uint64_t id;
  };
  auto owner = new Owner{this, root, region.id};
  lv_obj_add_event_cb(
      object,
      [](lv_event_t *e) {
        if (lv_event_get_target_obj(e) != lv_event_get_current_target_obj(e))
          return;
        auto owner = static_cast<Owner *>(lv_event_get_user_data(e));
        auto found = owner->app->readRegions_.find(owner->root);
        if (found != owner->app->readRegions_.end() && found->second.id == owner->id) {
          auto &parts = found->second.parts;
          parts.erase(std::remove_if(parts.begin(), parts.end(),
                                     [&](const ReadPart &p) {
                                       return p.object == lv_event_get_target_obj(e);
                                     }),
                      parts.end());
          if (owner->app->readActive_ == owner->id) {
            if (owner->app->readGesture_.active())
              owner->app->readSuppressed_ = owner->app->readDown_;
            owner->app->readActive_ = 0;
            owner->app->readGesture_.cancel();
          }
        }
        delete owner;
      },
      LV_EVENT_DELETE, owner);
}
void App::readLabel(lv_obj_t *object, const std::string &text) {
  readBegin(object, "label:" + std::to_string(readSerial_ + 1));
  auto &region = readRegions_.at(object);
  region.text = text;
  region.length = editLength(text);
  ReadPart part;
  part.object = object;
  part.text = text;
  region.parts.push_back(std::move(part));
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::accent), LV_PART_SELECTED);
  lv_obj_set_style_bg_opa(object, LV_OPA_40, LV_PART_SELECTED);
  lv_obj_set_style_text_color(object, lv_color_hex(theme::text), LV_PART_SELECTED);
}
void App::readLinkText(lv_obj_t *object, const std::string &text, const std::string &target) {
  readLabel(object, text);
  readRegions_.at(object).parts[0].target = target;
}
void App::readSlice(lv_obj_t *object, const std::string &text, size_t byteOffset) {
  auto &region = readRegions_.at(object);
  region.text = text;
  region.length = editLength(text);
  auto &part = region.parts[0];
  part.begin = editLength(text.substr(0, byteOffset));
  part.slice = true;
}
lv_obj_t *App::readableLabel(lv_obj_t *parent, const std::string &text, int x, int y, int width,
                             int size, uint32_t color) {
  auto object = label(parent, text, x, y, width, size, color);
  readLabel(object, text);
  return object;
}
lv_area_t App::readClip(lv_obj_t *object) const {
  lv_area_t area;
  lv_obj_get_coords(object, &area);
  area.x1 = std::max(int(area.x1), 0);
  area.x2 = std::min(int(area.x2), screen_.width - 1);
  area.y1 = std::max(int(area.y1), 0);
  area.y2 = std::min(int(area.y2), screen_.height - 1);
  for (auto p = lv_obj_get_parent(object); p; p = lv_obj_get_parent(p)) {
    if (lv_obj_is_hidden(p))
      return {0, 0, -1, -1};
    lv_area_t clip;
    lv_obj_get_coords(p, &clip);
    area.x1 = std::max(area.x1, clip.x1);
    area.x2 = std::min(area.x2, clip.x2);
    area.y1 = std::max(area.y1, clip.y1);
    area.y2 = std::min(area.y2, clip.y2);
  }
  if (view_ == "chat" && body_ && inside(object, body_) && chatControls_ &&
      !lv_obj_is_hidden(chatControls_)) {
    lv_area_t controls;
    lv_obj_get_coords(chatControls_, &controls);
    area.y2 = std::min(area.y2, controls.y1 - 1);
  }
  return area;
}
void App::readMap(ReadPart &part) {
  if (!part.rich || !part.scalarBytes.empty())
    return;
  const auto block = readBlock(part);
  if (!block)
    return;
  const auto text = blockPlainText(*block);
  part.hits = rich_.layout(*block, part.width, part.size, true);
  for (size_t i = 0; i < text.size(); ++i)
    if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
      part.scalarBytes.push_back(i);
  part.scalarBytes.push_back(text.size());
}
const Block *App::readBlock(const ReadPart &part) const {
  for (const auto &r : rasters_)
    if (r.holder == part.object)
      return &r.block;
  return nullptr;
}
size_t App::readPosition(ReadRegion &region, int x, int y, bool word,
                         std::pair<size_t, size_t> *atom) {
  int64_t distance = std::numeric_limits<int64_t>::max();
  size_t result = 0;
  for (auto &part : region.parts) {
    const auto clip = readClip(part.object);
    if (clip.x2 < clip.x1 || clip.y2 < clip.y1)
      continue;
    const int dx = std::max({0, int(clip.x1) - x, x - int(clip.x2)}),
              dy = std::max({0, int(clip.y1) - y, y - int(clip.y2)});
    if (int64_t(dy) * dy * 16 + int64_t(dx) * dx > distance)
      continue;
    lv_area_t area;
    lv_obj_get_coords(part.object, &area);
    if (!part.rich) {
      lv_point_t point{std::max(0, x - int(area.x1)), std::max(0, y - int(area.y1))};
      auto at = lv_label_get_letter_on(part.object, &point, false);
      result = part.begin + std::min(size_t(at), editLength(part.text));
      distance = int64_t(dy) * dy * 16 + int64_t(dx) * dx;
      if (atom)
        *atom = {0, 0};
      continue;
    }
    readMap(part);
    const int ox = area.x1 - lv_obj_get_scroll_x(part.object),
              oy = area.y1 - lv_obj_get_scroll_y(part.object);
    for (const auto &hit : part.hits.textHits) {
      const int gx = ox + hit.x, gy = oy + hit.y;
      if (gx + hit.w < clip.x1 || gx > clip.x2 || gy + hit.h < clip.y1 || gy > clip.y2)
        continue;
      const int hx = std::max({0, gx - x, x - gx - hit.w}),
                hy = std::max({0, gy - y, y - gy - hit.h});
      const int64_t score = int64_t(hy) * hy * 16 + int64_t(hx) * hx;
      if (score > distance)
        continue;
      distance = score;
      const auto begin = part.begin + scalar(part.scalarBytes, hit.begin),
                 end = part.begin + scalar(part.scalarBytes, hit.end);
      result = word || x < gx + hit.w / 2 ? begin : end;
      if (atom)
        *atom = hit.atomic ? std::make_pair(begin, end) : std::make_pair(size_t(0), size_t(0));
    }
  }
  return std::min(result, region.length);
}
void App::readWord(ReadRegion &region, int x, int y) {
  std::pair<size_t, size_t> atom;
  const auto at = readPosition(region, x, y, true, &atom);
  auto range = readSelection_.range();
  if (!(readSelection_.selected() && at >= range.first && at < range.second)) {
    if (atom.second > atom.first)
      readSelection_.select(atom.first, atom.second);
    else
      readSelection_.word(at);
  }
  range = readSelection_.range();
  readWordStart_ = range.first;
  readWordEnd_ = range.second;
  readPreview_ = true;
  if (view_ == "chat")
    chatFollowing_ = false;
  resetPull();
  input_.cancel();
  readDraw(false);
}
void App::readReset() {
  if (view_ == "chat" && readActive_ && readSelection_.selected())
    readerSettle_ = false;
  for (auto &entry : readRegions_)
    for (auto &part : entry.second.parts) {
      if (!part.rich) {
        if (part.hasDeferred) {
          lv_label_set_text(part.object, part.deferred.c_str());
          part.hasDeferred = false;
          part.deferred.clear();
        }
        lv_label_set_text_selection_start(part.object, LV_LABEL_TEXT_SELECTION_OFF);
        lv_label_set_text_selection_end(part.object, LV_LABEL_TEXT_SELECTION_OFF);
      }
      part.hits = RichLayout{};
      std::vector<size_t>().swap(part.scalarBytes);
    }
  readActive_ = 0;
  readSelectionBounds_ = {};
  readGesture_.cancel();
  readPreview_ = readMenuPointer_ = readMenuWanted_ = readScrollPassthrough_ = false;
  readHandle_ = -1;
  readLinkDue_ = 0;
  readLink_.clear();
  readLinkRegion_ = readScrollRegion_ = 0;
  readSelection_ = EditorState{};
  if (readOverlay_)
    lv_obj_delete(readOverlay_);
  readOverlay_ = readMenu_ = readHandles_[0] = readHandles_[1] = nullptr;
  readMarks_.clear();
}
void App::readDraw(bool menu) {
  readMenuWanted_ = menu;
  auto region = readRegion(readActive_);
  if (!region)
    return;
  if (!readOverlay_) {
    readOverlay_ = box(lv_layer_top(), 0, 0, screen_.width, screen_.height, theme::background);
    lv_obj_set_style_bg_opa(readOverlay_, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollable(readOverlay_, false);
    lv_obj_set_clickable(readOverlay_, false);
    lv_obj_add_event_cb(
        readOverlay_,
        [](lv_event_t *e) {
          if (lv_event_get_target_obj(e) != lv_event_get_current_target_obj(e))
            return;
          auto app = static_cast<App *>(lv_event_get_user_data(e));
          app->readOverlay_ = app->readMenu_ = app->readHandles_[0] = app->readHandles_[1] =
              nullptr;
          app->readMarks_.clear();
        },
        LV_EVENT_DELETE, this);
    for (int i = 0; i < 2; ++i) {
      readHandles_[i] = box(readOverlay_, 0, 0, 44, 44);
      lv_obj_set_style_bg_opa(readHandles_[i], LV_OPA_TRANSP, 0);
      lv_obj_set_clickable(readHandles_[i], false);
      lv_obj_set_scrollable(readHandles_[i], false);
      attachIcon(readHandles_[i], i ? "selection-end" : "selection-start", theme::link);
    }
    readMenu_ = box(readOverlay_, 0, 0, 188, 52, theme::sidebar);
    lv_obj_set_style_radius(readMenu_, 10, 0);
    lv_obj_set_scrollable(readMenu_, false);
    button(readMenu_, "全选", 4, 4, 88, 44, [this] {
      if (!readRegion(readActive_))
        return;
      readSelection_.select(0, editLength(readSelection_.text));
      readDraw(true);
    });
    button(readMenu_, "复制", 96, 4, 88, 44, [this] {
      if (!readRegion(readActive_))
        return;
      if (editorClipboard_.copy(readSelection_.selection()))
        replyToast("已复制");
      else
        replyToast("选区过长，无法复制");
    });
  }
  const auto range = readSelection_.range();
  std::vector<lv_area_t> marks;
  LayoutRect selectedBounds{};
  lv_point_t endpoints[2]{};
  int endpointTops[2]{};
  bool found[2] = {false, false};
  const ReadPart *endpointParts[2] = {nullptr, nullptr};
  for (const auto &part : region->parts) {
    const auto length = part.rich ? part.length : editLength(part.text);
    if (!length)
      continue;
    if (!endpointParts[0] && range.first < part.begin + length)
      endpointParts[0] = &part;
    if (range.second > part.begin)
      endpointParts[1] = &part;
  }
  for (auto &part : region->parts) {
    const auto length = part.rich ? part.length : editLength(part.text),
               finish = part.begin + length;
    const auto clip = readClip(part.object);
    if (clip.x2 < clip.x1 || clip.y2 < clip.y1) {
      part.hits = RichLayout{};
      std::vector<size_t>().swap(part.scalarBytes);
      continue;
    }
    lv_area_t area;
    lv_obj_get_coords(part.object, &area);
    if (!part.rich) {
      if (part.text != lv_label_get_text(part.object)) {
        part.deferred = lv_label_get_text(part.object);
        part.hasDeferred = true;
        lv_label_set_text(part.object, part.text.c_str());
      }
      const bool selected =
          readSelection_.selected() && range.first < finish && range.second > part.begin;
      lv_label_set_text_selection_start(part.object,
                                        selected ? std::max(range.first, part.begin) - part.begin
                                                 : LV_LABEL_TEXT_SELECTION_OFF);
      lv_label_set_text_selection_end(part.object, selected
                                                       ? std::min(range.second, finish) - part.begin
                                                       : LV_LABEL_TEXT_SELECTION_OFF);
      if (selected)
        includeSelectionRect(
            selectedBounds,
            selectionLabelBounds(part.object, std::max(range.first, part.begin) - part.begin,
                                 std::min(range.second, finish) - part.begin, clip));
      for (int i = 0; i < 2; ++i) {
        if (endpointParts[i] != &part)
          continue;
        const auto at = i ? range.second : range.first;
        const auto local = at <= part.begin ? 0 : std::min(at - part.begin, length);
        const auto relative = selectionLabelBoundary(part.object, part.text, local, i);
        const int x = area.x1 + relative.x, y = area.y1 + relative.y;
        const int h = lv_obj_get_style_text_font(part.object, LV_PART_MAIN)->line_height;
        const lv_area_t glyph{x - (i ? 1 : 0), y, x + (i ? 0 : 1), y + h - 1};
        found[i] = selectionVisibleAnchor(clip, glyph, i, endpoints[i]);
        endpointTops[i] = std::max(clip.y1, glyph.y1);
      }
      continue;
    }
    readMap(part);
    const int ox = area.x1 - lv_obj_get_scroll_x(part.object),
              oy = area.y1 - lv_obj_get_scroll_y(part.object);
    for (const auto &hit : part.hits.textHits) {
      const auto begin = part.begin + scalar(part.scalarBytes, hit.begin),
                 end = part.begin + scalar(part.scalarBytes, hit.end);
      lv_area_t rect{ox + hit.x, oy + hit.y, ox + hit.x + hit.w - 1, oy + hit.y + hit.h - 1};
      if (range.first < end && range.second > begin) {
        rect.x1 = std::max(rect.x1, clip.x1);
        rect.x2 = std::min(rect.x2, clip.x2);
        rect.y1 = std::max(rect.y1, clip.y1);
        rect.y2 = std::min(rect.y2, clip.y2);
        if (rect.x2 >= rect.x1 && rect.y2 >= rect.y1) {
          includeSelectionRect(selectedBounds,
                               {rect.x1, rect.y1, rect.x2 - rect.x1 + 1, rect.y2 - rect.y1 + 1});
          if (!marks.empty() && std::abs(marks.back().y1 - rect.y1) <= 2 &&
              std::abs(marks.back().y2 - rect.y2) <= 2 && rect.x1 <= marks.back().x2 + 2)
            marks.back().x2 = std::max(marks.back().x2, rect.x2);
          else
            marks.push_back(rect);
        }
      }
    }
    for (int i = 0; i < 2; ++i) {
      if (endpointParts[i] != &part || part.scalarBytes.empty())
        continue;
      const auto at = i ? range.second : range.first;
      const auto local =
          at <= part.begin ? 0 : std::min(at - part.begin, part.scalarBytes.size() - 1);
      const auto hit = part.hits.selectionEndpoint(part.scalarBytes[local], i);
      if (!hit)
        continue;
      const lv_area_t glyph{ox + hit->x, oy + hit->y, ox + hit->x + hit->w - 1,
                            oy + hit->y + hit->h - 1};
      found[i] = selectionVisibleAnchor(clip, glyph, i, endpoints[i]);
      endpointTops[i] = std::max(clip.y1, glyph.y1);
    }
  }
  while (readMarks_.size() < marks.size()) {
    auto mark = box(readOverlay_, 0, 0, 1, 1, theme::accent);
    lv_obj_set_style_bg_opa(mark, LV_OPA_40, 0);
    lv_obj_set_clickable(mark, false);
    lv_obj_set_scrollable(mark, false);
    readMarks_.push_back(mark);
  }
  for (size_t i = 0; i < readMarks_.size(); ++i) {
    lv_obj_set_hidden(readMarks_[i], i >= marks.size());
    if (i < marks.size()) {
      const auto &r = marks[i];
      lv_obj_set_pos(readMarks_[i], r.x1, r.y1);
      lv_obj_set_size(readMarks_[i], r.x2 - r.x1 + 1, r.y2 - r.y1 + 1);
    }
  }
  const int side = layout().portrait() ? layout().safeSide() : 8,
            top = layout().portrait() ? layout().safeTop() : 4, bottom = layout().safeBottom();
  for (int i = 0; i < 2; ++i) {
    lv_obj_set_hidden(readHandles_[i], !found[i] || !readSelection_.selected());
    if (found[i]) {
      const int y = std::max(top, std::min(screen_.height - bottom - 44, int(endpoints[i].y) - 8));
      const int x = std::max(side, std::min(screen_.width - side - 44, int(endpoints[i].x) - 22));
      lv_obj_set_pos(readHandles_[i], x, y);
      setSelectionHandleTop(
          readHandles_[i], endpointTops[i] - y,
          std::max(top, std::min(int(endpoints[i].y), screen_.height - bottom - 9)) - y,
          int(endpoints[i].x) - x);
    }
    lv_obj_move_foreground(readHandles_[i]);
  }
  readSelectionBounds_ = selectedBounds;
  const auto placement = placeSelectionMenu(
      selectedBounds, selectionMenuSafeArea(screen_.width, screen_.height), 188, 52);
  const auto &popup = placement.bounds;
  lv_obj_set_pos(readMenu_, popup.x, popup.y);
  lv_obj_set_size(readMenu_, popup.width, popup.height);
  lv_obj_set_hidden(readMenu_, !menu || !readSelection_.selected() || selectedBounds.width <= 0);
  lv_obj_move_foreground(readMenu_);
}
bool App::readTouch(int x, int y, bool down, bool cancelled) {
  const bool began = down && !readDown_;
  readDown_ = down && !cancelled;
  if (readSuppressed_) {
    if (!down || cancelled)
      readSuppressed_ = false;
    return true;
  }
  if (editor_.active && !readGesture_.active() && !readMenuPointer_ &&
      (editorPointer_ || editorTextTouch_ || !editorInPlace() || editorSelectionOnly_ ||
       y >= editorLayout().panelTop || editorExpanded_ || editorClipboardOpen_ ||
       editorLayoutMenu_ || editorMenu_ || editorPunctuation_)) {
    if (readActive_)
      readReset();
    return false;
  }
  if (cancelled) {
    if (!readGesture_.active() && !readMenuPointer_)
      return false;
    if (auto saved = readRegion(readSavedRegion_)) {
      if (saved->id != readActive_) {
        readReset();
        readActive_ = saved->id;
        readSelection_.text = saved->text;
      }
    }
    readSelection_.cursor = readSavedCursor_;
    readSelection_.anchor = readSavedAnchor_;
    readSelection_.all = readSavedAll_;
    readGesture_.cancel();
    readMenuPointer_ = readScrollPassthrough_ = false;
    readLinkDue_ = 0;
    input_.cancel();
    if (readSelection_.selected())
      readDraw(false);
    else
      readReset();
    return true;
  }
  if (readMenuPointer_) {
    input_.touch(x, y, down);
    if (!down)
      readMenuPointer_ = false;
    return true;
  }
  if (down && !readGesture_.active() && !began)
    return false;
  if (down && !readGesture_.active() && readMenu_ && !lv_obj_is_hidden(readMenu_)) {
    lv_area_t area;
    lv_obj_get_coords(readMenu_, &area);
    if (contains(area, x, y)) {
      readSavedRegion_ = readActive_;
      readSavedCursor_ = readSelection_.cursor;
      readSavedAnchor_ = readSelection_.anchor;
      readSavedAll_ = readSelection_.all;
      readMenuPointer_ = true;
      input_.touch(x, y, true);
      return true;
    }
  }
  if (down && !readGesture_.active()) {
    if (toBottom_ && !lv_obj_is_hidden(toBottom_)) {
      lv_area_t a;
      lv_obj_get_coords(toBottom_, &a);
      if (contains(a, x, y)) {
        if (readActive_)
          readReset();
        return false;
      }
    }
    ReadRegion *hit = nullptr;
    for (auto &entry : readRegions_) {
      auto &region = entry.second;
      if ((modal_ && !inside(region.root, modal_)) ||
          (attachments_ && !modal_ && !inside(region.root, attachments_)) ||
          (navigationOpen_ && !modal_ && !inside(region.root, navigation_)))
        continue;
      if (region.id != readActive_ && region.parts.size() == 1 && !region.parts[0].rich &&
          !region.parts[0].slice) {
        region.text = region.parts[0].text = lv_label_get_text(region.parts[0].object);
        region.length = editLength(region.text);
      }
      if (region.text.empty())
        continue;
      for (const auto &part : region.parts)
        if (!lv_obj_is_hidden(part.object) &&
            !(editor_.active && editorTarget_ && inside(part.object, editorTarget_)) &&
            contains(readClip(part.target.empty() ? part.object : lv_obj_get_parent(part.object)),
                     x, y) &&
            (!hit || hit->id < region.id))
          hit = &region;
    }
    readHandle_ = -1;
    int handleDistance = std::numeric_limits<int>::max();
    if (readActive_ && readSelection_.selected())
      for (int i = 0; i < 2; ++i)
        if (readHandles_[i] && !lv_obj_is_hidden(readHandles_[i])) {
          const auto grip = selectionHandleGrip(readHandles_[i]);
          const int dx = x - grip.x, dy = y - grip.y;
          const int distance = dx * dx + dy * dy;
          if (selectionHandleContains(readHandles_[i], x, y) &&
              (distance < handleDistance || (distance == handleDistance && i && dx >= 0))) {
            handleDistance = distance;
            readHandle_ = i;
            hit = readRegion(readActive_);
          }
        }
    if (!hit || (hit->id != readActive_ && readSelection_.selected())) {
      if (readActive_ && readSelection_.selected()) {
        // Let blank space and business controls keep their normal pointer path.
        // Defer deselection until this is known to be a tap, rather than a scroll.
        readSavedRegion_ = readActive_;
        readScrollRegion_ = hit ? hit->id : 0;
        readSavedCursor_ = readSelection_.cursor;
        readSavedAnchor_ = readSelection_.anchor;
        readSavedAll_ = readSelection_.all;
        readGesture_.begin(x, y, ticks(), false);
        readScrollPassthrough_ = true;
        readDraw(false);
      } else if (readActive_)
        readReset();
      return false;
    }
    if (readActive_ != hit->id) {
      readReset();
      readActive_ = hit->id;
      readSelection_.text = hit->text;
    }
    readSavedRegion_ = readActive_;
    readSavedCursor_ = readSelection_.cursor;
    readSavedAnchor_ = readSelection_.anchor;
    readSavedAll_ = readSelection_.all;
    readHadSelection_ = readSelection_.selected();
    readPreview_ = false;
    readGesture_.begin(x, y, ticks(), false, readHandle_ >= 0);
    readLinkDue_ = 0;
    if (readHandle_ >= 0) {
      auto r = readSelection_.range();
      readWordStart_ = readWordEnd_ = readHandle_ ? r.first : r.second;
    }
    return true;
  }
  if (!readGesture_.active())
    return false;
  auto region = readRegion(readActive_);
  if (!region) {
    readReset();
    return true;
  }
  if (readScrollPassthrough_) {
    readGesture_.lastX = x;
    readGesture_.lastY = y;
    readGesture_.distance = std::max(readGesture_.distance, std::max(std::abs(x - readGesture_.x),
                                                                     std::abs(y - readGesture_.y)));
    if (readGesture_.distance > 12)
      readGesture_.mode = TextGesture::Scroll;
    if (!down) {
      const bool scrolled = readGesture_.mode == TextGesture::Scroll;
      const bool cancelLink = readScrollRegion_ && !scrolled;
      readGesture_.cancel();
      readScrollPassthrough_ = false;
      readScrollRegion_ = 0;
      if (scrolled)
        readDraw(false);
      else
        readReset();
      if (cancelLink) {
        input_.cancel();
        return true;
      }
    }
    return false;
  }
  readGesture_.move(x, y, ticks());
  if (readGesture_.mode == TextGesture::Scroll) {
    if (!readPreview_) {
      if (readOverlay_)
        readDraw(false);
      input_.touch(readGesture_.x, readGesture_.y, true);
      if (view_ == "chat")
        chatTouch(readGesture_.x, readGesture_.y, true);
      readPreview_ = true;
    }
    if (!down) {
      readGesture_.cancel();
      if (readSelection_.selected())
        readDraw(false);
      else
        readReset();
    }
    return false;
  }
  if ((readGesture_.mode == TextGesture::Held || readGesture_.mode == TextGesture::Range) &&
      !readPreview_)
    readWord(*region, readGesture_.x, readGesture_.y);
  if (readGesture_.mode == TextGesture::Range || readGesture_.mode == TextGesture::Handle) {
    std::pair<size_t, size_t> atom;
    auto at = readPosition(*region, x, y, false, &atom);
    const auto anchor = readGesture_.mode == TextGesture::Handle
                            ? readWordStart_
                            : (at < readWordStart_ ? readWordEnd_ : readWordStart_);
    if (atom.second > atom.first)
      at = at < anchor ? atom.first : atom.second;
    readSelection_.extendSelection(anchor, at);
    readDraw(false);
  }
  if (!down) {
    const auto mode = readGesture_.mode;
    readGesture_.end();
    if (mode == TextGesture::Pending) {
      if (readGesture_.doubleTap(x, y, ticks())) {
        readWord(*region, x, y);
        readDraw(true);
      } else if (readHadSelection_)
        readReset();
      else {
        for (const auto &part : region->parts)
          if (!part.target.empty() && contains(readClip(lv_obj_get_parent(part.object)), x, y)) {
            readLink_ = part.target;
            readLinkRegion_ = region->id;
            readLinkDue_ = ticks() + 350;
          }
        for (auto &part : region->parts)
          if (part.rich && contains(readClip(part.object), x, y)) {
            lv_area_t a;
            lv_obj_get_coords(part.object, &a);
            for (const auto &link : part.links)
              if (x - a.x1 + lv_obj_get_scroll_x(part.object) >= link.x &&
                  x - a.x1 + lv_obj_get_scroll_x(part.object) < link.x + link.w &&
                  y - a.y1 >= link.y && y - a.y1 < link.y + link.h) {
                readLink_ = link.target;
                readLinkRegion_ = region->id;
                readLinkDue_ = ticks() + 350;
              }
          }
      }
    } else
      readDraw(true);
    readHandle_ = -1;
  }
  return true;
}
void App::readTick() {
  if (!visible_ || keyboard_ || !inputFocused_) {
    if (readActive_ || readOverlay_)
      readReset();
    return;
  }
  auto region = readRegion(readActive_);
  if (!region && readOverlay_) {
    readReset();
    return;
  }
  if (readScrollPassthrough_ && readScrollRegion_ && readGesture_.hold(ticks())) {
    if (auto target = readRegion(readScrollRegion_)) {
      const auto gesture = readGesture_;
      readReset();
      readActive_ = target->id;
      readSelection_.text = target->text;
      readGesture_ = gesture;
      region = target;
      readWord(*region, gesture.x, gesture.y);
    }
  } else if (!readScrollPassthrough_ && readGesture_.hold(ticks()) && region)
    readWord(*region, readGesture_.x, readGesture_.y);
  if (region && readSelection_.selected()) {
    if ((readGesture_.mode == TextGesture::Range || readGesture_.mode == TextGesture::Handle) &&
        ticks() - readScrollAt_ >= 30) {
      readScrollAt_ = ticks();
      const auto at = readSelection_.cursor;
      if (at > 0 && at < editLength(readSelection_.text))
        for (auto p = region->root; p; p = lv_obj_get_parent(p))
          if (lv_obj_is_scrollable(p)) {
            auto clip = readClip(p);
            int step = readGesture_.lastY < clip.y1 + 14   ? -8
                       : readGesture_.lastY > clip.y2 - 14 ? 8
                                                           : 0;
            if (step) {
              const int before = lv_obj_get_scroll_y(p);
              lv_obj_scroll_to_y(p, std::max(0, before + step), LV_ANIM_OFF);
              if (before != lv_obj_get_scroll_y(p)) {
                const auto pos = readPosition(*region, readGesture_.lastX, readGesture_.lastY);
                const auto anchor = readGesture_.mode == TextGesture::Handle
                                        ? readWordStart_
                                        : (pos < readWordStart_ ? readWordEnd_ : readWordStart_);
                readSelection_.extendSelection(anchor, pos);
              }
            }
            break;
          }
    }
    readDraw(readMenuWanted_ && !readGesture_.active());
  }
  if (readLinkDue_ && ticks() >= readLinkDue_ && !readGesture_.active()) {
    const auto link = readLink_;
    const bool valid = readRegion(readLinkRegion_) && !readSelection_.selected();
    readLinkDue_ = 0;
    if (valid) {
      readReset();
      openTextLink(link);
    }
  }
}
Json App::readDiagnostic() const {
  size_t cached = 0;
  for (const auto &entry : readRegions_)
    for (const auto &part : entry.second.parts)
      cached += part.hits.textHits.size();
  Json regions = Json::array();
  for (const auto &entry : readRegions_) {
    Json parts = Json::array(), links = Json::array();
    for (const auto &part : entry.second.parts) {
      auto a = readClip(part.object);
      parts.push_back({a.x1, a.y1, a.x2, a.y2});
      lv_area_t position;
      lv_obj_get_coords(part.object, &position);
      for (const auto &link : part.links) {
        const int x = position.x1 + link.x - lv_obj_get_scroll_x(part.object),
                  y = position.y1 + link.y;
        links.push_back({x, y, x + link.w - 1, y + link.h - 1});
      }
    }
    regions.push_back({{"id", entry.second.id},
                       {"key", entry.second.key},
                       {"length", entry.second.length},
                       {"parts", parts},
                       {"links", links}});
  }
  Json handles = Json::array(), grips = Json::array();
  for (auto h : readHandles_)
    if (h && !lv_obj_is_hidden(h)) {
      lv_area_t a;
      lv_obj_get_coords(h, &a);
      handles.push_back({a.x1, a.y1, a.x2, a.y2});
      const auto grip = selectionHandleGrip(h);
      grips.push_back({grip.x, grip.y});
    }
  Json menu = Json::array();
  if (readMenu_ && !lv_obj_is_hidden(readMenu_)) {
    lv_area_t a;
    lv_obj_get_coords(readMenu_, &a);
    menu = {a.x1, a.y1, a.x2, a.y2};
  }
  auto r = readSelection_.range();
  return {{"regions", regions},
          {"active", readActive_},
          {"selected", readSelection_.selected()},
          {"start", r.first},
          {"end", r.second},
          {"handles", handles},
          {"handle_grips", grips},
          {"selection_bounds",
           readSelectionBounds_.width > 0
               ? Json::array({readSelectionBounds_.x, readSelectionBounds_.y,
                              readSelectionBounds_.x + readSelectionBounds_.width - 1,
                              readSelectionBounds_.y + readSelectionBounds_.height - 1})
               : Json::array()},
          {"menu_bounds", menu},
          {"menu", readMenu_ && !lv_obj_is_hidden(readMenu_)},
          {"gesture", int(readGesture_.mode)},
          {"link_pending", readLinkDue_ != 0},
          {"cached_hits", cached},
          {"clipboard_count", editorClipboard_.records.size()},
          {"clipboard_bytes", editorClipboard_.current.size()}};
}
void App::openTextLink(const std::string &target) {
  if (view_ == "web_source") {
    fetchWebSource(target);
    return;
  }
  for (const auto &source : webDetails_.value("web_sources", Json::array()))
    if (source.value("url", std::string()) == target) {
      openWebSource(source);
      return;
    }
  for (const auto &entry : webMessageMetadata_)
    for (const auto &source : entry.second.value("web_sources", Json::array()))
      if (source.value("url", std::string()) == target) {
        openWebSource(source);
        return;
      }
  for (const auto &source : reasoningMetadata_.value("web_sources", Json::array()))
    if (source.value("url", std::string()) == target) {
      openWebSource(source);
      return;
    }
  modal("链接", target);
}
} // namespace penchat
