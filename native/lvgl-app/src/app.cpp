#include "app.hpp"
#include "chat_time.hpp"
#include "context.hpp"
#include "icons.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <dirent.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
namespace penchat {
namespace {
constexpr int64_t PullPauseMs = 500;
constexpr int64_t PullSlowTimeMs = 160;
void onClick(lv_obj_t *object, std::function<void()> action) {
  auto f = new std::function<void()>(std::move(action));
  lv_obj_set_clickable(object, true);
  lv_obj_add_event_cb(
      object,
      [](lv_event_t *e) {
        try {
          auto callback =
              *static_cast<std::function<void()> *>(lv_event_get_user_data(e));
          callback();
        } catch (const std::exception &err) {
          std::cerr << "UI action: " << err.what() << '\n';
        }
      },
      LV_EVENT_CLICKED, f);
  lv_obj_add_event_cb(
      object,
      [](lv_event_t *e) {
        delete static_cast<std::function<void()> *>(lv_event_get_user_data(e));
      },
      LV_EVENT_DELETE, f);
}
std::string stringValue(const Json &j, const char *key,
                        const std::string &fallback = "") {
  auto i = j.find(key);
  return i != j.end() && i->is_string() ? i->get<std::string>() : fallback;
}
bool retryableStatus(const std::string &status) {
  return status == "error" || status == "cancelled" || status == "interrupted";
}
bool hasText(const std::string &text) {
  return std::any_of(text.begin(), text.end(),
                     [](unsigned char c) { return !std::isspace(c); });
}
std::string basename(const std::string &p) {
  auto slash = p.rfind('/');
  return p.substr(slash == std::string::npos ? 0 : slash + 1);
}
} // namespace
App::App(Database &db, Bridge &bridge, Display &display, Input &input,
         const Screen &s, std::string workspace, std::string dataDir,
         const std::string &fontPath, const std::string &resources)
    : db_(db), bridge_(bridge), display_(display), input_(input), screen_(s), imageInput_(s),
      pinyin_(events_, join(workspace, "assets/pinyin"), dataDir),
      images_(events_), camera_(events_, join(dataDir, "camera-tmp"), workspace),
      scanner_(events_, workspace, dataDir),
      rich_(fontPath, resources,
            join(workspace, "assets/fonts/DejaVuSans.ttf")),
      workspace_(std::move(workspace)), dataDir_(std::move(dataDir)) {
  initializeMessageEdits();
  transfer_.reset(new Transfer(db_, events_, dataDir_));
  transfer_->start("init");
  clearCrop();
  auto config = db_.settings();
  editor_.preferredChinese=config.value("pinyin_chinese_layout",std::string("zh26"))=="zh9"?2:0;
  const auto savedPull = config.value("pull_sensitivity_offset", Json());
  if (savedPull.is_number_integer())
    pullSensitivityOffset_ = int(std::max(-25.0, std::min(25.0, savedPull.get<double>())));
  const auto savedFont = config.value("reading_font_percent", Json());
  if (savedFont.is_number_integer())
    readingFontPercent_ = int(std::max<int64_t>(75, std::min<int64_t>(150, savedFont.get<int64_t>())));
  cameraAngle_ = config.value("camera_orientation",0);
  if (cameraAngle_!=0 && cameraAngle_!=90 && cameraAngle_!=180 && cameraAngle_!=270) cameraAngle_=0;
  cameraAutomatic_ = config.value("camera_auto_orientation",true);
  camera_.orientation(cameraAutomatic_,cameraAngle_);
  session_ = stringValue(config, "active_session_id");
  if (session_.empty() ||
      db_.query(
             "SELECT id FROM chat_sessions WHERE id=? AND status!='archived'",
             {session_})
          .empty())
    session_ = uid("session");
  sessionPreferences_ = db_.sessionPreferences(session_);
  std::string presetWarning;
  if (db_.sessionInfo(session_).empty()) {
    sessionPreferences_ = applyPresetSnapshot(sessionPreferences_, presetList(config)[0],
                                              db_.models(), db_.activeModel(), true, presetWarning);
  }
  build();
  if (!presetWarning.empty()) modal("默认预设已部分应用", presetWarning);
  for (const auto &job : db_.pendingTitles())
    titleJobs_.push_back(job);
}
App::~App() {
  shutdown();
  clear();
}
lv_obj_t *App::box(lv_obj_t *parent, int x, int y, int w, int h,
                   uint32_t color) {
  auto o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, 255, 0);
  lv_obj_set_style_text_font(o, font(21), 0);
  lv_obj_set_style_text_color(o, lv_color_hex(theme::text), 0);
  return o;
}
lv_obj_t *App::label(lv_obj_t *parent, const std::string &text, int x, int y,
                     int w, int size, uint32_t color) {
  auto o = lv_label_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_width(o, w);
  lv_obj_set_style_text_font(o, font(size), 0);
  lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
  lv_label_set_text(o, text.c_str());
  return o;
}
lv_obj_t *App::button(lv_obj_t *parent, const std::string &text, int x, int y,
                      int w, int h, std::function<void()> click, uint32_t color,
                      lv_text_align_t align) {
  auto o = box(parent, x, y, w, h, color);
  lv_obj_set_scrollable(o, false);
  lv_obj_set_style_radius(o, 10, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(theme::border), LV_STATE_PRESSED);
  auto l = label(o, text, 0, 0, w - 24, 20);
  // A fixed-height action must never center an arbitrarily tall wrapped label.
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(l, font(20)->line_height);
  lv_obj_set_style_text_align(l, align, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
  onClick(o, std::move(click));
  return o;
}
void App::primary(lv_obj_t *object) {
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::text), 0);
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::muted), LV_STATE_PRESSED);
  lv_obj_set_style_opa(object, LV_OPA_50, LV_STATE_DISABLED);
  lv_obj_set_style_text_color(lv_obj_get_child(object, 0), lv_color_hex(theme::background), 0);
}
lv_obj_t *App::fieldButton(lv_obj_t *parent, const std::string &caption,
                            const std::string &value, int x, int y, int width,
                            std::function<void()> action) {
  auto object = button(parent, value, x, y, width, 48, std::move(action), theme::surface, LV_TEXT_ALIGN_LEFT);
  auto text = lv_obj_get_child(object, 0);
  lv_obj_set_style_text_font(text, font(19), 0);
  lv_obj_set_height(text, 23);
  lv_obj_align(text, LV_ALIGN_TOP_LEFT, 12, 23);
  auto name = label(object, caption, 12, 3, width - 24, 15, theme::muted);
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(name, 19);
  lv_obj_set_clickable(name, false);
  return object;
}
void App::emptyState(const std::string &title, const std::string &hint) {
  const int width = screen_.width - 118;
  auto parent = pageContent_ ? pageContent_ : body_;
  auto heading = label(parent, title, 10, 62, width, 23);
  lv_obj_set_style_text_align(heading, LV_TEXT_ALIGN_CENTER, 0);
  auto description = label(parent, hint, 26, 99, width - 32, 17, theme::muted);
  lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, 0);
}
void App::texture(lv_obj_t *object, std::shared_ptr<ImageFrame> frame,
                  bool premultiplied) {
  if (!frame)
    return;
  auto t = std::unique_ptr<Texture>(new Texture);
  t->frame = std::move(frame);
  auto &d = t->descriptor;
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = LV_COLOR_FORMAT_ARGB8888;
  d.header.w = t->frame->width;
  d.header.h = t->frame->height;
  d.header.stride = t->frame->width * 4;
  d.header.flags = premultiplied ? LV_IMAGE_FLAGS_PREMULTIPLIED : 0;
  d.data = t->frame->pixels.data();
  d.data_size = t->frame->pixels.size();
  if (textures_.count(object))
    lv_image_cache_drop(&textures_[object]->descriptor);
  lv_image_set_src(object, &d);
  textures_[object] = std::move(t);
}
lv_obj_t *App::imageWidget(lv_obj_t *parent) {
  auto object = lv_image_create(parent);
  lv_obj_add_event_cb(
      object,
      [](lv_event_t *event) {
        auto self = static_cast<App *>(lv_event_get_user_data(event));
        auto object = static_cast<lv_obj_t *>(lv_event_get_target(event));
        self->imageRequests_.erase(
            std::remove_if(
                self->imageRequests_.begin(), self->imageRequests_.end(),
                [object](const std::pair<lv_obj_t *, std::string> &r) {
                  return r.first == object;
                }),
            self->imageRequests_.end());
        self->placeholders_.erase(object);
        self->chatPhotos_.erase(object);
        auto texture = self->textures_.find(object);
        if (texture != self->textures_.end()) {
          lv_image_cache_drop(&texture->second->descriptor);
          self->textures_.erase(texture);
        }
      },
      LV_EVENT_DELETE, this);
  return object;
}
void App::image(lv_obj_t *parent, const std::string &path, int x, int y, int w,
                int h, std::function<void()> click, bool rightAligned,
                bool chatPhoto, const std::string &pendingMessage, const std::string &sentMessage) {
  auto holder =
      box(parent, x, y, chatPhoto ? std::min(w, h) : w, h, theme::surface);
  lv_obj_set_scrollable(holder, false);
  lv_obj_set_style_bg_opa(holder, LV_OPA_TRANSP, 0);
  auto placeholder = label(holder, "加载图片…", 0, 0, w, 17, theme::muted);
  lv_obj_center(placeholder);
  auto o = imageWidget(holder);
  placeholders_[o] = placeholder;
  if (chatPhoto) {
    chatPhotos_.insert(o);
    lv_obj_set_style_radius(holder, 12, 0);
    lv_obj_set_style_clip_corner(holder, true, 0);
    lv_obj_set_width(placeholder, LV_PCT(100));
  }
  lv_obj_align(o, rightAligned ? LV_ALIGN_RIGHT_MID : LV_ALIGN_CENTER, 0, 0);
  auto key = images_.request(path, w, h, chatPhoto);
  imageRequests_.push_back({o, key});
  if (auto frame = images_.get(key)) {
    texture(o, frame);
    if (chatPhoto)
      lv_obj_set_size(holder, frame->width, frame->height);
    lv_obj_set_hidden(placeholder, true);
  } else if (images_.failed(key))
    lv_label_set_text(placeholder, "图片读取失败");
  if (!click && view_ == "chat")
    click = [this, path] { requestImageViewer(path); };
  if(!sentMessage.empty()&&pendingMessage.empty())messageLongPress(holder,sentMessage);
  if (click || !pendingMessage.empty()) {
    struct Press {
      App *app;
      std::string session, message, sent;
      lv_point_t start{};
      int distance = 0;
      bool held = false;
    };
    auto state = std::make_shared<Press>();
    state->app = this; state->session = session_; state->message = pendingMessage; state->sent=sentMessage;
    auto owner = new std::shared_ptr<Press>(state);
    lv_obj_set_clickable(holder, true);
    lv_obj_add_event_cb(holder, [](lv_event_t *event) {
      auto owner = static_cast<std::shared_ptr<Press> *>(lv_event_get_user_data(event));
      if (lv_event_get_code(event) == LV_EVENT_DELETE) { delete owner; return; }
      auto p = *owner;
      auto code = lv_event_get_code(event);
      if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_LONG_PRESSED) return;
      auto indev = lv_indev_active(); if (!indev) return;
      lv_point_t point; lv_indev_get_point(indev, &point);
      if (code == LV_EVENT_PRESSED) { p->start = point; p->distance = 0; p->held = false; }
      const int dx = point.x-p->start.x, dy = point.y-p->start.y;
      p->distance = std::max(p->distance,dx*dx+dy*dy);
      if (code == LV_EVENT_LONG_PRESSED) {
        p->held = true;
        if (p->distance <= 144) {
          if(!p->message.empty())p->app->events_.push("image_delete_request",{{"session",p->session},{"message",p->message}});

        }
      }
    }, LV_EVENT_ALL, owner);
    if (click) onClick(holder,[state,click] { if (!state->held && state->distance<=144) click(); });
  }
}
lv_obj_t *App::icon(lv_obj_t *parent, const std::string &kind, int x, int y,
                    int w, int h, std::function<void()> action,
                    uint32_t background, uint32_t ink) {
  auto object = box(parent, x, y, w, h, background);
  lv_obj_set_scrollable(object, false);
  lv_obj_set_style_radius(object, std::min(w, h) / 2, 0);
  attachIcon(object, kind, ink);
  if (action)
    onClick(object, std::move(action));
  else
    lv_obj_set_clickable(object, false);
  return object;
}
void App::closeAttachments() {
  if (popupKind_ == "attachments") { ++sessionInfoGeneration_; sessionInfoPending_ = false; }
  contextUsageArc_ = contextUsageLabel_ = nullptr;
  attachmentEntries_.clear();
  popupKind_.clear();
  if (!attachments_)
    return;
  auto object = attachments_;
  attachments_ = nullptr;
  lv_obj_delete(object);
}
void App::toggleAttachments() {
  if (contextBusy_) {
    contextProgress("正在整理历史，请稍候…");
    return;
  }
  if (attachments_) {
    closeAttachments();
    return;
  }
  resetPull();
  attachments_ = box(screenObj_, 86, 0, screen_.width - 86, screen_.height);
  popupKind_ = "attachments";
  lv_obj_set_style_bg_color(attachments_, lv_color_hex(theme::scrim), 0);
  lv_obj_set_style_bg_opa(attachments_, LV_OPA_50, 0);
  lv_obj_set_scrollable(attachments_, false);
  onClick(attachments_, [this] { closeAttachments(); });
  auto menu = box(attachments_, 20, 12, std::min(320, screen_.width - 126), screen_.height - 78, theme::surface);
  lv_obj_set_style_radius(menu, 16, 0);
  lv_obj_set_scroll_dir(menu, LV_DIR_VER);
  lv_obj_set_scroll_elastic(menu, false);
  lv_obj_set_scroll_momentum(menu, false);
  std::vector<std::string> names{"拍照", "相册", "模型", "推理强度"};
  std::vector<std::string> icons{"camera", "album", "settings", "gauge"};
  std::vector<std::string> ids{"camera", "album", "models", "effort", "compress", "summary"};
  names.push_back("压缩上下文（计算中）");
  icons.push_back("history");
  names.push_back("查看上下文摘要");
  icons.push_back("context-summary");
  if (db_.settings().value("presets_enabled", false)) {
    ids.push_back("presets");
    names.push_back("预设");
    icons.push_back("settings");
  }
  const int infoIndex = int(names.size());
  ids.push_back("session_info");
  names.push_back("会话信息");
  icons.push_back("info");
  for (int i = 0; i < int(names.size()); ++i) {
    auto action = [this, i, infoIndex] {
      if (i == infoIndex) { openSessionInfo(); return; }
      if (i == 0)
        show("camera");
      else if (i == 1)
        show("album");
      else if (i == 2)
        modelPicker();
      else if (i == 4)
        startContext(true);
      else if (i == 5)
        contextDetails();
      else if (i == 6)
        presetPicker();
      else {
        bool enabled =
            sessionPreferences_.value("gauge", !effortMemory_.empty());
        if (enabled)
          chooseEffort("");
        sessionPreferences_["gauge"] = !enabled;
        savePreferences();
        build();
      }
    };
    if (i == 4) {
      auto control = button(menu, "", 4, 4 + i * 44, 44, 44, action, theme::surface);
      contextUsageArc_ = lv_arc_create(control);
      lv_obj_remove_style_all(contextUsageArc_);
      lv_obj_set_scrollable(contextUsageArc_, false);
      lv_obj_set_clickable(contextUsageArc_, false);
      lv_obj_set_size(contextUsageArc_, 24, 24);
      lv_obj_center(contextUsageArc_);
      lv_arc_set_rotation(contextUsageArc_, 270);
      lv_arc_set_bg_angles(contextUsageArc_, 0, 360);
      lv_arc_set_range(contextUsageArc_, 0, 1000);
      lv_arc_set_value(contextUsageArc_, 0);
      lv_obj_set_style_arc_color(contextUsageArc_, lv_color_hex(theme::muted), LV_PART_MAIN);
      lv_obj_set_style_arc_opa(contextUsageArc_, LV_OPA_50, LV_PART_MAIN);
      lv_obj_set_style_arc_width(contextUsageArc_, 2, LV_PART_MAIN);
      lv_obj_set_style_arc_color(contextUsageArc_, lv_color_hex(theme::link), LV_PART_INDICATOR);
      lv_obj_set_style_arc_opa(contextUsageArc_, LV_OPA_TRANSP, LV_PART_INDICATOR);
      lv_obj_set_style_arc_width(contextUsageArc_, 2, LV_PART_INDICATOR);
      lv_obj_set_style_arc_rounded(contextUsageArc_, true, LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(contextUsageArc_, LV_OPA_TRANSP, LV_PART_KNOB);
    } else icon(menu, icons[i], 4, 4 + i * 44, 44, 44, action, theme::surface);
    auto entry = button(menu, names[i], 48, 4 + i * 44, std::min(320, screen_.width - 126) - 52, 44, action, theme::surface,
                        LV_TEXT_ALIGN_LEFT);
    attachmentEntries_[ids[i]] = entry;
    if (i == 4) contextUsageLabel_ = lv_obj_get_child(entry, 0);
  }
  ++sessionInfoGeneration_;
  sessionInfoSession_ = session_;
  sessionInfoData_ = Json::object();
  refreshSessionInfo();
}
Json App::currentModel() {
  auto id = sessionPreferences_.value("model_id", std::string());
  Json model = Json::object();
  if (id.empty())
    model = db_.activeModel();
  else
    for (const auto &candidate : db_.models())
      if (candidate["id"] == id) {
        model = candidate;
        break;
      }
  return model.empty()
             ? model
             : resolveReasoning(model, sessionPreferences_, effortMemory_);
}
void App::savePreferences() {
  if (!messageEdit_.empty() || emptyForPreset()) return;
  db_.saveSessionPreferences(session_, sessionPreferences_);
}
void App::bindPreferences(const Json &model) {
  if (model.empty())
    return;
  // Bind an inherited default when content is committed. A later explicit
  // selection made while context preparation runs belongs to the next request.
  if (sessionPreferences_.value("model_id", std::string()).empty())
    sessionPreferences_["model_id"] = model["id"];
  if (!sessionPreferences_.contains("effort")) {
    const auto effort = model.value("effective_effort", std::string());
    if (!effort.empty())
      sessionPreferences_["effort"] = effort;
  }
  if (!sessionPreferences_.contains("gauge"))
    sessionPreferences_["gauge"] = !effortMemory_.empty();
}
void App::chooseModel(const std::string &id) {
  sessionPreferences_["model_id"] = id;
  sessionPreferences_["preset_adjusted"] = true;
  if(messageEdit_.empty())db_.selectModel(id);
  savePreferences();
}
void App::chooseEffort(const std::string &key) {
  sessionPreferences_["effort"] = key;
  sessionPreferences_["preset_adjusted"] = true;
  effortMemory_ = key;
  savePreferences();
}
void App::modelPicker(bool forTitles) {
  closeAttachments();
  resetPull();
  attachments_ = box(screenObj_, 86, 0, screen_.width - 86, screen_.height);
  popupKind_ = forTitles ? "title_models" : "models";
  lv_obj_set_style_bg_color(attachments_, lv_color_hex(theme::scrim), 0);
  lv_obj_set_style_bg_opa(attachments_, LV_OPA_50, 0);
  lv_obj_set_scrollable(attachments_, false);
  onClick(attachments_, [this] { closeAttachments(); });
  const int panelWidth = std::min(650, screen_.width - 130);
  auto panel = box(attachments_, 20, 12, panelWidth, screen_.height - 78, theme::surface);
  lv_obj_set_style_radius(panel, 16, 0);
  lv_obj_set_scroll_dir(panel, LV_DIR_VER);
  auto current = currentModel();
  if (forTitles)
    current = {{"id", db_.settings().value("title_model_id", std::string())}};
  int y = 4;
  if (forTitles) {
    button(
        panel, "跟随当前聊天模型", 8, y, panelWidth - 30, 44,
        [this] {
          db_.setting("title_model_id", "");
          build();
        },
        theme::surface, LV_TEXT_ALIGN_LEFT);
    y += 48;
  }
  auto models = db_.models();
  std::sort(models.begin(), models.end(), [](const Json &a, const Json &b) {
    return a.value("name", std::string()) < b.value("name", std::string());
  });
  for (const auto &provider : db_.providers()) {
    label(panel, provider.value("name", std::string("Provider")), 14, y, panelWidth - 40,
          18, theme::muted);
    y += 30;
    for (const auto &model : models)
      if (model["provider_id"] == provider["id"]) {
        auto id = model["id"].get<std::string>();
        auto held = std::make_shared<bool>(false);
        auto item = button(
            panel,
            (current.value("id", std::string()) == id ? "● " : "○ ") +
                model.value("name", std::string()),
            8, y, panelWidth - 30, 44,
            [this, id, forTitles, held] {
              if (*held) return;
              if (forTitles)
                db_.setting("title_model_id", id);
              else
                chooseModel(id);
              build();
            },
            theme::surface, LV_TEXT_ALIGN_LEFT);
        if (forTitles) {
          auto action = new std::function<void()>([this, id, held] {
            *held = true;
            events_.push("title_model_detail", {{"id", id}});
          });
          lv_obj_add_event_cb(item, [](lv_event_t *e) {
            auto callback = *static_cast<std::function<void()> *>(lv_event_get_user_data(e));
            callback();
          }, LV_EVENT_LONG_PRESSED, action);
          lv_obj_add_event_cb(item, [](lv_event_t *e) {
            delete static_cast<std::function<void()> *>(lv_event_get_user_data(e));
          }, LV_EVENT_DELETE, action);
        }
        y += 46;
      }
  }
  if (models.empty())
    button(panel, "添加 Provider / 模型", 10, 50, panelWidth - 50, 48,
           [this] { show("provider"); });
  if (forTitles && !models.empty())
    label(attachments_, "长按模型查看完整名称", 24, screen_.height - 50, panelWidth, 16, theme::muted);
}
void App::effortPicker() {
  auto model = currentModel();
  if (model.empty()) {
    modal("选择模型", "请先选择一个可用模型。");
    return;
  }
  auto options = effortChoices(model);
  closeAttachments();
  resetPull();
  attachments_ = box(screenObj_, 86, 0, screen_.width - 86, screen_.height);
  popupKind_ = "effort";
  lv_obj_set_style_bg_color(attachments_, lv_color_hex(theme::scrim), 0);
  lv_obj_set_style_bg_opa(attachments_, LV_OPA_50, 0);
  lv_obj_set_scrollable(attachments_, false);
  onClick(attachments_, [this] { closeAttachments(); });
  const int panelWidth = screen_.width - 130;
  const int sliderWidth = panelWidth - 80;
  auto panel = box(attachments_, 20, 12, panelWidth, screen_.height - 78, theme::surface);
  lv_obj_set_scrollable(panel, false);
  lv_obj_set_style_radius(panel, 16, 0);
  auto selected = model.value("effective_effort", std::string());
  if (selected.empty())
    selected = "api_default";
  auto title =
      label(panel, effortLabel(selected) + " · 推理强度", 24, 14, panelWidth - 230, 24);
  button(panel, "跟随设置", panelWidth - 166, 4, 156, 44, [this] {
    chooseEffort("");
    build();
  });
  auto slider = lv_slider_create(panel);
  lv_obj_set_pos(slider, 40, 76);
  lv_obj_set_size(slider, sliderWidth, 44);
  lv_slider_set_range(slider, 0,
                      options.size() == 1 ? 2 : int(options.size()) - 1);
  auto found = std::find(options.begin(), options.end(), selected);
  lv_slider_set_value(slider,
                      options.size() == 1      ? 1
                      : found == options.end() ? 0
                                               : int(found - options.begin()),
                      LV_ANIM_OFF);
  lv_obj_set_style_bg_color(slider, lv_color_hex(theme::border), LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, lv_color_hex(theme::accent),
                            LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_color_hex(theme::text), LV_PART_KNOB);
  lv_obj_set_style_radius(slider, 22, LV_PART_MAIN);
  lv_obj_set_style_radius(slider, 22, LV_PART_INDICATOR);
  if (selected.empty()) {
    lv_obj_set_style_bg_opa(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, 0, LV_PART_KNOB);
  }
  struct Choice {
    App *app;
    lv_obj_t *title;
    std::vector<std::string> options;
    bool selected;
  };
  auto data = new Choice{this, title, options, !selected.empty()};
  lv_obj_add_event_cb(
      slider,
      [](lv_event_t *e) {
        auto data = static_cast<Choice *>(lv_event_get_user_data(e));
        if (lv_event_get_code(e) == LV_EVENT_DELETE) {
          delete data;
          return;
        }
        auto slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
        int index = std::min(int(data->options.size()) - 1,
                             int(lv_slider_get_value(slider)));
        const auto key = data->options[index];
        if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN) {
          lv_area_t area;
          lv_obj_get_coords(slider, &area);
          for (size_t i = 0; i < data->options.size(); ++i) {
            if (data->selected && int(i) == index)
              continue;
            int x = data->options.size() == 1
                        ? (area.x1 + area.x2) / 2
                        : area.x1 + int(i) * (area.x2 - area.x1) /
                                        int(data->options.size() - 1);
            int y = (area.y1 + area.y2) / 2;
            lv_draw_rect_dsc_t dot;
            lv_draw_rect_dsc_init(&dot);
            dot.bg_color = lv_color_hex(theme::muted);
            dot.bg_opa = 150;
            dot.radius = 4;
            lv_area_t bounds{x - 4, y - 4, x + 4, y + 4};
            lv_draw_rect(lv_event_get_layer(e), &dot, &bounds);
          }
        } else if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
          lv_label_set_text(data->title,
                            (effortLabel(key) + " · 推理强度").c_str());
          data->selected = true;
          if (data->options.size() == 1)
            lv_slider_set_value(slider, 1, LV_ANIM_OFF);
          lv_obj_set_style_bg_opa(slider, 255, LV_PART_KNOB);
          lv_obj_set_style_bg_opa(slider, 255, LV_PART_INDICATOR);
        } else if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
          data->app->chooseEffort(key);
          lv_label_set_text(data->title,
                            (effortLabel(key) + " · 推理强度").c_str());
          data->selected = true;
          if (data->options.size() == 1)
            lv_slider_set_value(slider, 1, LV_ANIM_OFF);
          lv_obj_set_style_bg_opa(slider, 255, LV_PART_KNOB);
          lv_obj_set_style_bg_opa(slider, 255, LV_PART_INDICATOR);
        }
      },
      LV_EVENT_ALL, data);
  for (size_t i = 0; i < options.size(); ++i) {
    int x =
        options.size() == 1 ? panelWidth / 2 : 40 + int(i) * sliderWidth / int(options.size() - 1);
    auto caption = label(panel, effortLabel(options[i]), x - 38, 138, 76, 18);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
  }
}
void App::modelPage() {
  if (anthropicProtocol(modelForm_)) { anthropicModelPage(); return; }
  pageHeader("模型设置");
  const int width = screen_.width - 118, column = (width - 14) / 2;
  const auto id = modelForm_.value("id", std::string());
  pageAction("参数映射", 170, 140, [this] {
    modelAdvanced_ = !modelAdvanced_;
    build();
  });
  auto save = pageAction("保存", 24, 130, [this, id] {
    db_.saveModelCapabilities(
        id, modelForm_["reasoning"],
        modelForm_.value("context_budget", contextBudget(modelForm_)));
    show("provider");
  });
  primary(save);
  auto name = label(pageContent_, modelForm_.value("name", std::string()), 10, 0, width,
                    19, theme::muted);
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(name, 22);
  auto &caps = modelForm_["reasoning"];
  auto &levels = caps["levels"];
  int n = 0;
  for (const auto &key : effortOrder()) {
    if (modelAdvanced_) {
      if (levels.contains(key)) {
        button(
            pageContent_, effortLabel(key) + "：" + levels[key].get<std::string>(), 10,
            26 + n * 48, width, 44,
            [this, key] {
              edit(
                  modelForm_["reasoning"]["levels"][key].get<std::string>(),
                  "API 参数值",
                  [this, key](const std::string &value) {
                    if (hasText(value))
                      modelForm_["reasoning"]["levels"][key] = value;
                    build();
                  },
                  128, "EnUSOnly");
            },
            theme::surface, LV_TEXT_ALIGN_LEFT);
        ++n;
      }
    } else {
      button(
          pageContent_,
          std::string(levels.contains(key) ? "● " : "○ ") + effortLabel(key),
          10 + (n % 2) * (column + 14), 26 + (n / 2) * 48, column, 44,
          [this, key] {
            auto &levels = modelForm_["reasoning"]["levels"];
            if (levels.contains(key))
              levels.erase(key);
            else
              levels[key] = key;
            build();
          },
          theme::surface, LV_TEXT_ALIGN_LEFT);
      ++n;
    }
  }
  if (!modelAdvanced_)
    button(
        pageContent_, caps.value("summary", false) ? "公开总结：开" : "公开总结：关",
        10, 172, width, 44,
        [this] {
          auto &caps = modelForm_["reasoning"];
          caps["summary"] = !caps.value("summary", false);
          build();
        },
        theme::surface, LV_TEXT_ALIGN_LEFT);
  const int budgetY = modelAdvanced_ ? 32 + n * 48 : 224;
  fieldButton(
      pageContent_, "上下文预算（本地估算 token）",
      std::to_string(modelForm_.value("context_budget", contextBudget(modelForm_))), 10,
      budgetY, width, [this] {
        edit(
            std::to_string(
                modelForm_.value("context_budget", contextBudget(modelForm_))),
            "上下文预算：1024～2097152",
            [this](const std::string &value) {
              try {
                size_t end;
                int n = std::stoi(value, &end);
                if (end != value.size() || n < 1024 || n > 2097152)
                  throw std::runtime_error("budget");
                modelForm_["context_budget"] = n;
                build();
              } catch (...) {
                modal("预算无效", "请输入 1024～2097152 "
                                  "的整数。这是本地估算预算，不是接口精确计费数据。");
              }
            },
            7, "EnUSOnly");
      });
  label(pageContent_, "达到 75% 自动压缩，尽量降至 50%；不代表接口精确计费。", 10,
        budgetY + 60, width, 16, theme::muted);
}
void App::resetPull() {
  const bool wasPulling = pulling_;
  if (pulling_)
    input_.cancel();
  if (body_ && lv_obj_get_style_translate_y(body_, LV_PART_MAIN) != 0) {
    const int translation = lv_obj_get_style_translate_y(body_, LV_PART_MAIN);
    lv_anim_delete(body_, nullptr);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, body_);
    lv_anim_set_exec_cb(&animation, [](void *object, int32_t y) {
      lv_obj_set_style_translate_y(static_cast<lv_obj_t *>(object), y, 0);
    });
    lv_anim_set_values(&animation, translation, 0);
    lv_anim_set_duration(&animation, 160);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
  }
  pullCandidate_ = pulling_ = false;
  pullCreateAllowed_ = false;
  pullProgress_ = 0;
  if (pullIndicator_)
    lv_obj_delete(pullIndicator_);
  pullIndicator_ = pullArc_ = pullLabel_ = pullBadge_ = nullptr;
  updateChatControls(wasPulling);
}
bool App::chatTouch(int x, int y, bool pressed) {
  if(!messageEdit_.empty()||messageEditBusy_||replyBusy_||contextRegenerating_){touchDown_=pressed;return false;}
  const bool began = pressed && !touchDown_;
  touchDown_ = pressed;
  const auto now = ticks();
  if (began) {
    attachmentMenuTouch_ = false;
    if (attachments_ && popupKind_ == "attachments") {
      lv_area_t menu;
      lv_obj_get_coords(lv_obj_get_child(attachments_, 0), &menu);
      attachmentMenuTouch_ = x >= menu.x1 && x <= menu.x2 && y >= menu.y1 && y <= menu.y2;
    }
    // Read resting viewport bounds, not its animated pull translation. The
    // launcher has already applied rotation and touch offsets exactly once.
    if (view_ == "chat" && body_ && chatControls_) {
      lv_obj_update_layout(body_);
      lv_area_t bounds;
      lv_obj_get_coords(body_, &bounds);
      const int tx = lv_obj_get_style_translate_x(body_, LV_PART_MAIN);
      const int ty = lv_obj_get_style_translate_y(body_, LV_PART_MAIN);
      pullGeometry_ =
          pullGeometry({bounds.x1 - tx, bounds.y1 - ty, bounds.x2 + 1 - tx,
                        bounds.y2 + 1 - ty},
                       screen_.width, screen_.height,
                       screen_.height - lv_obj_get_height(chatControls_) - 10, pullSensitivityOffset_);
    }
    const bool inReader = pullGeometry_.contains(x, y);
    bool inComposer = false;
    if (view_ == "chat" && composer_ && chatControls_ && !lv_obj_is_hidden(chatControls_)) {
      lv_area_t area;
      lv_obj_get_coords(composer_, &area);
      inComposer = x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
    }
    // The text entry shares the pull gesture, but attachment/send controls do
    // not. Keep threshold scaling tied to the unobstructed reading viewport.
    const bool inPullArea = inReader || inComposer;
    pullTrackActivity_ = view_ == "chat" && !navigationOpen_ && !modal_ &&
                         !attachments_ && inPullArea;
    pullCreateAllowed_ =
        !pullNeedsPause_ || now - pullLastActivity_ >= PullPauseMs;
    pullCandidate_ = view_ == "chat" && !navigationOpen_ && !modal_ &&
                     !attachments_ && hasConversationContent_ &&
                     messageOffset_ == 0 && chatFollowing_ && body_ &&
                     lv_obj_get_scroll_bottom(body_) <= 10 && inPullArea;
    if (pullCandidate_ && inReader) {
      for (const auto &r : rasters_) {
        lv_area_t a;
        lv_obj_get_coords(r.holder, &a);
        if (r.contentWidth > r.width && x >= a.x1 && x <= a.x2 && y >= a.y1 &&
            y <= a.y2)
          pullCandidate_ = false;
      }
    }
    // Catch an in-flight return at its visible position when touched again.
    if (pullCandidate_)
      lv_anim_delete(body_, nullptr);
    pullStartX_ = x;
    pullStartY_ = y;
    pullMotionY_ = y;
    pullMotionTime_ = 0;
    pullSlowDistance_ = 0;
    pullSlowTime_ = 0;
  }
  if (pullNeedsPause_ && pullTrackActivity_)
    pullLastActivity_ = now;
  if (!pressed)
    pullTrackActivity_ = false;
  if (attachments_ && popupKind_ == "attachments" && !attachmentMenuTouch_ && pressed && !began &&
      std::abs(pullStartY_ - y) > 8) {
    closeAttachments();
    input_.cancel();
  }
  if (!pullCandidate_ && !pulling_)
    return false;
  const int dy = pullStartY_ - y, dx = std::abs(pullStartX_ - x);
  if (pressed && y != pullMotionY_) {
    const int step = pullMotionY_ - y;
    // Falcon can deliver several moves together. Average a stretch of motion,
    // rather than treating each near-zero delivery interval as a fast flick.
    // Exclude the initial hold and bound later gaps; a stationary release never
    // promotes a guarded gesture.
    const auto elapsed =
        pullMotionTime_ ? std::min<int64_t>(250, now - pullMotionTime_) : 0;
    if (step > 0 && pullMotionTime_) {
      pullSlowDistance_ += step;
      pullSlowTime_ += elapsed;
      if (pullSlowDistance_ >= pullGeometry_.slowDistance &&
          pullSlowTime_ >= PullSlowTimeMs &&
          pullSlowDistance_ * 1000 <= pullGeometry_.slowSpeed * pullSlowTime_)
        pullCreateAllowed_ = true;
    } else {
      pullSlowDistance_ = 0;
      pullSlowTime_ = 0;
    }
    pullMotionY_ = y;
    pullMotionTime_ = now;
  }
  if (!pulling_ && (dy < -pullGeometry_.start ||
                    (dx > pullGeometry_.sideways && dx >= std::abs(dy)))) {
    resetPull();
    return false;
  }
  if (!pulling_ && pressed && dy > pullGeometry_.start && dy > dx) {
    pulling_ = true;
    pullVisualOrigin_ = lv_obj_get_style_translate_y(body_, LV_PART_MAIN);
    lv_anim_delete(body_, nullptr);
    chatLayout_ = true;
    input_.cancel();
    lv_obj_stop_scroll_anim(body_);
    lv_obj_scroll_to_y(body_, LV_COORD_MAX, LV_ANIM_OFF);
    chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
    chatLayout_ = false;
    pullIndicator_ = box(screenObj_, 86 + (screen_.width - 90 - 280) / 2, screen_.height - 98, 280, 98);
    lv_obj_set_style_bg_opa(pullIndicator_, 0, 0);
    lv_obj_set_scrollable(pullIndicator_, false);
    pullArc_ = lv_arc_create(pullIndicator_);
    lv_obj_set_pos(pullArc_, 114, 0);
    lv_obj_set_size(pullArc_, 52, 52);
    lv_arc_set_rotation(pullArc_, 270);
    lv_arc_set_bg_angles(pullArc_, 0, 360);
    lv_arc_set_range(pullArc_, 0, 100);
    lv_obj_remove_style(pullArc_, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_color(pullArc_, lv_color_hex(0x19284c), LV_PART_MAIN);
    lv_obj_set_style_arc_color(pullArc_, lv_color_hex(theme::accent),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(pullArc_, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(pullArc_, 3, LV_PART_INDICATOR);
    lv_obj_set_clickable(pullArc_, false);
    pullBadge_ =
        icon(pullIndicator_, "newchat", 114, 0, 52, 52, {}, theme::accent);
    lv_obj_set_hidden(pullBadge_, true);
    pullLabel_ = label(pullIndicator_, "新聊天", 0, 60, 280, 20);
    lv_obj_set_style_text_align(pullLabel_, LV_TEXT_ALIGN_CENTER, 0);
    updateChatControls(true);
  }
  if (pulling_) {
    const int dragOffset = std::max(0, dy - pullGeometry_.start);
    lv_obj_set_style_translate_y(
        body_, std::max(-pullGeometry_.stretch, pullVisualOrigin_ - dragOffset),
        0);
    paintVisible();
    // Guarded rapid swipes still stretch/retract, using the same viewport
    // transform as an accepted pull instead of LVGL's elastic scroll physics.
    lv_obj_set_hidden(pullIndicator_, !pullCreateAllowed_);
    if (pullCreateAllowed_) {
      // Once progress is shown, releasing (even partially) needs no quiet gap
      // before the next attempt. Only a new return from history rearms it.
      pullNeedsPause_ = false;
      pullTrackActivity_ = false;
    }
    pullProgress_ =
        pullCreateAllowed_
            ? std::max(0, std::min(100, (dy - pullGeometry_.start) * 100 /
                                            pullGeometry_.fill))
            : 0;
    lv_arc_set_value(pullArc_, pullProgress_);
    lv_obj_set_hidden(pullBadge_, pullProgress_ != 100);
    lv_obj_set_hidden(pullArc_, pullProgress_ == 100);
    lv_label_set_text(pullLabel_, pullProgress_ == 100 ? "松开新建" : "新聊天");
    if (!pressed) {
      const bool create = pullProgress_ == 100;
      resetPull();
      if (create)
        newChat(true);
    }
    return true;
  }
  if (!pressed)
    resetPull();
  // Keep LVGL's elastic scroll from moving the page before this gesture wins.
  // Downward/horizontal motion above the slop already releases the candidate.
  return pressed && pullCandidate_ && !began;
}
void App::clear() {
  fontSlider_ = fontPercentLabel_ = fontPreview_ = nullptr;
  cropActions_.clear();cropViewport_=cropBitmap_=cropSelection_=nullptr;
  if(!imageViewer_)imageGesture_={};
  settingsControls_.clear();
  settingsHeading_ = nullptr;
  pageHeading_ = pageHeader_ = pageContent_ = nullptr;
  pageActions_.clear();
  transferProgress_ = nullptr;
  scanImage_ = scanLabel_ = scanUse_ = scanHint_ = nullptr;
  historyRows_.clear();
  historyCaptions_.clear();historyActions_.clear();historyFullTitle_=historyFullTime_=nullptr;
  historyContextId_.clear();
  reasoningHeaders_.clear();
  if (navigationList_)
    navigationScroll_ = std::max(0, int(lv_obj_get_scroll_y(navigationList_)));
  navigation_ = navigationList_ = navigationActive_ = nullptr;
  resetPull();
  if (touchDown_)
    input_.cancel();
  if (popupKind_ == "attachments") { ++sessionInfoGeneration_; sessionInfoPending_ = false; }
  contextUsageArc_ = contextUsageLabel_ = nullptr;
  attachmentEntries_.clear();
  attachments_ = modal_ = nullptr;
  popupKind_.clear();
  if (chatControls_)
    lv_anim_delete(chatControls_, nullptr);
  if (body_)
    lv_anim_delete(body_, nullptr);
  chatControls_ = toBottom_ = nullptr;
  chatControlsTarget_ = -1;
  rasters_.clear();
  archiveViewButtons_.clear();
  imageRequests_.clear();
  placeholders_.clear();
  if (screenObj_) {
    lv_obj_delete(screenObj_);
    screenObj_ = nullptr;
  }
  for (auto &t : textures_)
    lv_image_cache_drop(&t.second->descriptor);
  textures_.clear();
  body_ = messageContent_ = composer_ = status_ = cameraImage_ = liveContent_ =
      nullptr;
}
void App::build() {
  const int pageY = pageHeader_ && body_ && builtView_ == view_ && builtPageKey_ == pageKey()
                      ? std::max(0, int(lv_obj_get_scroll_y(body_))) : 0;
  const int settingsY = body_ && builtView_ == "settings" && view_ == "settings"
                            ? std::max(0, int(lv_obj_get_scroll_y(body_))) : settingsScroll_;
  const int transferY = body_ && builtView_ == "transfer" &&
                                transferBuiltScreen_ == transferScreen_ &&
                                !transfer_->busy()
                            ? std::max(0, int(lv_obj_get_scroll_y(body_)))
                            : 0;
  if (body_ && builtView_ == "chat" && !chatLayout_)
    chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  chatLayout_ = true;
  clear();
  lv_obj_set_scrollable(lv_screen_active(), false);
  const bool photo = view_ == "camera" || view_ == "crop";
  const int canvasW = photo ? photoWidth() : screen_.width;
  const int canvasH = photo ? photoHeight() : screen_.height;
  screenObj_ = box(lv_screen_active(), (screen_.width-canvasW)/2, (screen_.height-canvasH)/2, canvasW, canvasH);
  if (photo && cameraAngle_) {
    lv_obj_set_style_transform_pivot_x(screenObj_,canvasW/2,0);
    lv_obj_set_style_transform_pivot_y(screenObj_,canvasH/2,0);
    lv_obj_set_style_transform_rotation(screenObj_,(360-cameraAngle_)*10,0);
  }
  lv_obj_set_scrollable(screenObj_, false);
  if(view_!="crop") icon(screenObj_, view_ == "chat" ? (messageEdit_.empty()?"menu":"close") : "back", 16, 8, 48, 48, [this] {
    if (view_ == "chat") {
      if(!messageEdit_.empty())cancelMessageEdit();else toggleNavigation();
    }
    else
      goBack();
  });
  int w = screen_.width - 90;
  body_ = view_=="crop" ? box(screenObj_,0,0,canvasW,canvasH) : photo && cameraAngle_%180 ? box(screenObj_,0,56,canvasW,canvasH-56)
                                      : box(screenObj_,86,0,w,screen_.height);
  lv_obj_set_scroll_dir(body_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body_, LV_SCROLLBAR_MODE_AUTO);
  if (view_ == "chat")
    chat();
  else if (view_ == "settings")
    settingsPage();
  else if (view_ == "font_size")
    fontSizePage();
  else if (view_ == "keyboard_settings")
    keyboardSettingsPage();
  else if (view_ == "transfer")
    transferPage();
  else if (view_ == "presets")
    presetsPage();
  else if (view_ == "preset_edit")
    presetEditor();
  else if (view_ == "provider")
    providerPage();
  else if (view_ == "api_compatibility")
    apiCompatibilityPage();
  else if (view_ == "anthropic_connection")
    anthropicConnectionPage();
  else if (view_ == "archives")
    archivesPage();
  else if (view_ == "archive_chat")
    archiveReaderPage();
  else if (view_ == "session_info")
    sessionInfoPage();
  else if (view_ == "model")
    modelPage();
  else if (view_ == "scan")
    scanPage();
  else if (view_ == "camera")
    cameraPage();
  else if (view_ == "album")
    albumPage();
  else if (view_ == "crop")
    cropPage();
  if (view_ == "chat" && scanner_.available()) {
    auto hint = box(screenObj_, 12, 60, 64, 64);
    lv_obj_set_scrollable(hint, false);
    scanHint_ = label(hint, "扫描\n准备中", 4, 5, 60, 14, theme::muted);
    onClick(hint, [this] {
      auto state = scanner_.state().value("type", std::string());
      if (state == "error" || state == "fatal") {
        scanner_.eligible(false, session_);
        scanner_.eligible(true, session_);
        syncScanner();
      }
    });
  }
  refreshImages();
  if (navigationOpen_)
    historyDrawer();
  lv_obj_update_layout(screenObj_);
  if (pageHeader_) {
    lv_obj_scroll_to_y(body_, pageY, LV_ANIM_OFF);
    builtPageKey_ = pageKey();
  }
  if (view_ == "transfer") {
    lv_obj_scroll_to_y(body_, transferY, LV_ANIM_OFF);
    transferBuiltScreen_ = transfer_->busy() ? "" : transferScreen_;
  }
  if (view_ == "settings") {
    lv_obj_scroll_to_y(body_, settingsY, LV_ANIM_OFF);
    settingsScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  }
  builtView_ = view_;
  chatLayout_ = false;
  syncScanner();
  if (view_=="archive_chat")paintVisible();
  if (editorRoot_) lv_obj_move_foreground(editorRoot_);
  if (imageViewer_) lv_obj_move_foreground(imageViewer_);
}
void App::show(const std::string &view) {
  if (view_ == "session_info" && view != view_) {
    ++sessionInfoGeneration_; sessionInfoPending_ = false;
  }
  closeImageViewer(false);
  if(view_=="archive_chat"&&view!="archive_chat"){archiveReaderId_.clear();archiveReaderMessages_.clear();archiveReasoningOverrides_.clear();}
  if (view_ == "settings" && body_)
    settingsScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  if (view == "settings" && view_ == "chat") settingsScroll_ = 0;
  if (view_ == "crop" && view != "crop")
    clearCrop();
  if (view_ == "scan" && view != "scan") {
    scanner_.eligible(false, session_);
    scanId_ = 0;
    scanResult_ = Json::object();
    scanConfirming_ = false;
  }
  closeNavigation(false);
  closeAttachments();
  resetPull();
  if ((view_ == "provider" || view_ == "api_compatibility" || view_ == "anthropic_connection") &&
      view != "provider" && view != "api_compatibility" && view != "anthropic_connection") {
    providerForm_ = Json::object();
    modelNames_.clear();
    ++modelGeneration_;
    modelCancel_ = true;
  }
  if (view_ == "camera" && view != "camera")
    camera_.stop();
  view_ = view;
  notice_.clear();
  build();
  if (view_ == "camera" && visible_)
    whenScannerReleased([this] {
      if (view_ == "camera" && visible_ && !keyboard_)
        startCamera();
    });
}
void App::error(const std::string &m) {
  notice_ = m;
  if (status_)
    lv_label_set_text(status_, m.c_str());
  else
    modal("提示", m);
}
void App::modal(const std::string &title, const std::string &text) {
  closeAttachments();
  resetPull();
  if (modal_)
    lv_obj_delete(modal_);
  const bool portraitPhoto = (view_=="camera" || view_=="crop") && cameraAngle_%180;
  const int mw=portraitPhoto?photoWidth():screen_.width,mh=portraitPhoto?photoHeight():screen_.height;
  const int layerWidth=portraitPhoto?mw-16:mw-92;
  auto layer = box(screenObj_, portraitPhoto?8:84, 0, layerWidth, mh, theme::surface);
  modal_ = layer;
  lv_obj_add_event_cb(
      layer,
      [](lv_event_t *e) {
        static_cast<App *>(lv_event_get_user_data(e))->modal_ = nullptr;
      },
      LV_EVENT_DELETE, this);
  lv_obj_set_style_radius(layer, 16, 0);
  lv_obj_set_style_border_color(layer, lv_color_hex(theme::border), 0);
  lv_obj_set_style_border_width(layer, 1, 0);
  label(layer, title, 15, 7, portraitPhoto?layerWidth-30:screen_.width-140, 23);
  auto scroll = box(layer, 10, 42, portraitPhoto?layerWidth-20:screen_.width-120, mh-115, theme::surface);
  label(scroll, text, 5, 0, portraitPhoto?layerWidth-30:screen_.width-145, 20);
  button(layer, "关闭", portraitPhoto?layerWidth-110:screen_.width-205, mh-66, 100, 44,
         [layer] { lv_obj_delete(layer); });
}
void App::chat() {
  int width = screen_.width - 90;
  lv_obj_set_height(body_, screen_.height);
  // At the edge, rapid guarded swipes must not repeatedly stretch and snap
  // back.
  lv_obj_set_scroll_elastic(body_, false);
  lv_obj_set_scroll_chain_ver(body_, false);
  messageContent_ = box(body_, 0, 0, width, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(messageContent_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(messageContent_, 10, 0);
  lv_obj_set_style_pad_bottom(messageContent_, 74, 0);
  lv_obj_set_scrollable(messageContent_, false);
  lv_obj_add_event_cb(
      body_,
      [](lv_event_t *e) {
        static_cast<App *>(lv_event_get_user_data(e))->chatScrolled();
      },
      LV_EVENT_SCROLL, this);
  renderMessages(chatFollowing_);
  const int composerWidth = screen_.width - 130;
  chatControls_ =
      box(screenObj_, 106, screen_.height - 58, composerWidth, 48, theme::surface);
  lv_obj_set_scrollable(chatControls_, false);
  lv_obj_set_style_radius(chatControls_, 24, 0);
  auto attachment = icon(
      chatControls_, "plus", 2, 2, 44, 44, [this] { toggleAttachments(); },
      theme::surface);
  // Keep the glyph in place; near-edge taps belong to attachments, not IME.
  lv_obj_set_ext_click_area(attachment, 8);
  const int entryLeft = 62;
  const bool gauge = sessionPreferences_.value("gauge", !effortMemory_.empty());
  if (gauge)
    icon(
        chatControls_, "gauge", composerWidth - 100, 2, 44, 44, [this] { effortPicker(); },
        theme::surface, theme::muted);
  composer_ = button(
      chatControls_, draft_.empty() ? "询问 " PENCHAT_NAME : draft_, entryLeft, 2,
      composerWidth - entryLeft - (gauge ? 106 : 54), 44,
      [this] {
        if (contextBusy_ || !composerTapAllowed_)
          return;
        composerTapAllowed_ = false;
        edit(
            draft_, "请输入消息",
            [this](const std::string &s) {
              draft_ = s;
              build();
            },
            8192, "ZhCNPreferred", {}, true);
      },
      theme::surface, LV_TEXT_ALIGN_LEFT);
  if (draft_.empty())
    lv_obj_set_style_text_color(lv_obj_get_child(composer_, 0),
                                lv_color_hex(theme::muted), 0);
  const bool sendEnabled = contextBusy_ || generating_ || canSend();
  auto send = icon(
      chatControls_, (generating_ || contextBusy_) ? "stop" : "up", composerWidth - 50, 2, 44, 44,
      [this] {
        if (contextBusy_) {
          cancelContext();
          build();
        } else if (generating_)
          cancel_ = true;
        else if (canSend())
          sendMessage();
      },
      sendEnabled ? theme::text : theme::border, theme::background);
  // Equal 2px insets keep the circle concentric with the capsule end cap.
  lv_obj_align(send, LV_ALIGN_RIGHT_MID, -2, 0);
  if (!sendEnabled)
    lv_obj_set_clickable(send, false);
  toBottom_ = icon(
      screenObj_, "down", 106 + (composerWidth - 44) / 2, screen_.height - 114, 44, 44,
      [this] { jumpToBottom(); }, theme::surface);
  lv_obj_set_style_border_color(toBottom_, lv_color_hex(theme::border), 0);
  lv_obj_set_style_border_width(toBottom_, 1, 0);
  updateChatControls(false);
}
void App::chatScrolled() {
  if (chatLayout_ || view_ != "chat" || !body_)
    return;
  closeAttachments();
  chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  const bool wasFollowing = chatFollowing_;
  chatFollowing_ = messageOffset_ == 0 && lv_obj_get_scroll_bottom(body_) <= 10;
  if (!wasFollowing && chatFollowing_ &&
      lv_obj_get_scroll_y(body_) + lv_obj_get_scroll_bottom(body_) > 0)
    pullNeedsPause_ = true;
  // Native inertia and repeated swipes both postpone the quiet period.
  if (pullNeedsPause_ && !pulling_)
    pullLastActivity_ = ticks();
  updateChatControls(true);
  paintVisible();
}
void App::updateChatControls(bool animate) {
  if (!chatControls_ || !toBottom_)
    return;
  lv_obj_set_hidden(toBottom_, chatFollowing_ || pulling_);
  bool overContent = false;
  if (messageContent_) {
    lv_area_t buttonArea;
    lv_obj_get_coords(toBottom_, &buttonArea);
    for (uint32_t i = 0; i < lv_obj_get_child_count(messageContent_); ++i) {
      lv_area_t area;
      lv_obj_get_coords(lv_obj_get_child(messageContent_, i), &area);
      if (area.x1 <= buttonArea.x2 && area.x2 >= buttonArea.x1 &&
          area.y1 <= buttonArea.y2 && area.y2 >= buttonArea.y1) {
        overContent = true;
        break;
      }
    }
  }
  lv_obj_set_style_bg_opa(toBottom_, overContent ? 160 : 255, 0);
  lv_obj_set_style_border_opa(toBottom_, overContent ? 160 : 255, 0);
  const int target =
      chatFollowing_ && !pulling_ ? screen_.height - 58 : screen_.height + 2;
  if (chatControlsTarget_ == target)
    return;
  chatControlsTarget_ = target;
  lv_anim_delete(chatControls_, nullptr);
  if (!animate) {
    lv_obj_set_y(chatControls_, target);
    return;
  }
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, chatControls_);
  lv_anim_set_exec_cb(&animation, [](void *object, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t *>(object), value);
  });
  lv_anim_set_values(&animation, lv_obj_get_y(chatControls_), target);
  lv_anim_set_duration(&animation, 160);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_start(&animation);
}
void App::jumpToBottom() {
  pullNeedsPause_ = true;
  pullLastActivity_ = ticks();
  chatFollowing_ = true;
  if (messageOffset_ != 0) {
    messageOffset_ = 0;
    renderMessages(true);
    return;
  }
  chatLayout_ = true;
  lv_obj_scroll_to_y(body_, LV_COORD_MAX, LV_ANIM_OFF);
  chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  chatLayout_ = false;
  updateChatControls(true);
  paintVisible();
}
void App::newChat(bool inherit) {
  if(!messageEdit_.empty() || messageEditBusy_ || replyBusy_ || contextRegenerating_)return;
  const auto nextSession=uid("session");
  if(!switchDraft(nextSession))return;
  auto preferences = sessionPreferences_;
  if (inherit) {
    auto model = currentModel();
    if (!model.empty()) {
      preferences["model_id"] = model["id"];
      auto effort = model.value("effective_effort", std::string());
      preferences["effort"] = effort.empty() ? "api_default" : effort;
    }
    preferences.erase("has_sent");
  }
  std::string warning;
  if (!inherit)
    preferences = applyPresetSnapshot(Json::object(), presetList(db_.settings())[0],
                                      db_.models(), db_.activeModel(), true, warning);
  pullNeedsPause_ = false;
  pullTrackActivity_ = false;
  stopNetwork();
  session_ = nextSession;
  sessionPreferences_ = preferences;
  reasoningMetadata_ = Json::object();
  expandedReasoning_.clear();
  reasoningOverrides_.clear();
  tableScrolls_.clear();
  draft_.clear();
  chatFollowing_ = true;
  chatScroll_ = 0;
  messageOffset_ = 0;
  show("chat");
  if (!warning.empty()) modal("默认预设已部分应用", warning);
}
void App::richMessage(lv_obj_t *parent, const std::string &text, int width,
                      bool subdued, const std::string &context) {
  auto doc = parseMarkdown(text, generating_ && parent == liveContent_);
  const int size = readingFontSize();
  rich_.prepare(doc, width - 4, size);
  auto appendText = [&](const Block &b) {
    if (b.runs.empty() && b.cells.empty() && b.kind != "hr")
      return;
    auto layout = rich_.layout(b, width - 4, size);
    auto holder = box(parent, 0, 0, width, layout.height, theme::background);
    lv_obj_set_scroll_dir(holder, LV_DIR_HOR);
    if (b.tableId)
      lv_obj_set_scroll_elastic(holder, false);
    lv_obj_set_scrollbar_mode(holder, LV_SCROLLBAR_MODE_AUTO);
    auto spacer = box(holder, layout.width - 1, 0, 1, 1, theme::background);
    lv_obj_set_style_bg_opa(spacer, 0, 0);
    lv_obj_add_event_cb(
        holder,
        [](lv_event_t *e) {
          static_cast<App *>(lv_event_get_user_data(e))
              ->richScrolled(static_cast<lv_obj_t *>(lv_event_get_target(e)));
        },
        LV_EVENT_SCROLL, this);
    rasters_.push_back(
        {holder,
         parent,
         b,
         width - 4,
         layout.height,
         layout.width,
         size,
         subdued,
         b.tableId,
         b.tableId ? context + ":table:" + std::to_string(b.tableId) : "",
         {}});
    if (b.tableId) {
      syncingTableScroll_ = true;
      lv_obj_update_layout(holder);
      lv_obj_scroll_to_x(holder, tableScrolls_[rasters_.back().tableKey],
                         LV_ANIM_OFF);
      syncingTableScroll_ = false;
    }
  };
  for (const auto &b : doc.blocks) {
    Block part = b;
    part.runs.clear();
    bool split = false;
    for (const auto &r : b.runs) {
      if (!(r.style & Image)) {
        part.runs.push_back(r);
        continue;
      }
      appendText(part);
      part.runs.clear();
      part.marker.clear();
      split = true;
      auto h = box(parent, 0, 0, width, 145, theme::background);
      image(h, r.link, 0, 0, width, 145);
    }
    if (split)
      appendText(part);
    else
      appendText(b);
  }
}
void App::renderMessageRows(lv_obj_t *parent,const Json &messages,const std::string &source,
                            int width,bool readOnly,int lastAssistant,int64_t questionSeq) {
  const auto reasoningMode=reasoningDisplay_;
  lv_obj_t *photoGroup = nullptr;
  for (const auto &m : messages) {
    bool user = m.value("role", std::string()) == "user";
    const bool hasImage = !stringValue(m, "stored_path").empty() ||
                          !stringValue(m, "original_path").empty();
    // Consecutive image-only user messages share one wrapping gallery. Keep
    // database rows and request order unchanged; never group across a reply.
    if (user && hasImage && stringValue(m, "output_text").empty()) {
      if (!photoGroup) {
        photoGroup = box(parent, 0, 0, width, LV_SIZE_CONTENT);
        lv_obj_set_scrollable(photoGroup, false);
        lv_obj_set_flex_flow(photoGroup, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(photoGroup, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(photoGroup, 8, 0);
        lv_obj_set_style_pad_row(photoGroup, 8, 0);
      }
      auto path = stringValue(m, "display_src");
      if (path.empty())
        path = stringValue(m, "stored_path");
      if (path.empty())
        path = stringValue(m, "original_path");
      auto original = stringValue(m, "stored_path");
      if (original.empty()) original = stringValue(m, "original_path");
      if (original.empty()) original = path;
      image(photoGroup, path, 0, 0, width, 140,
            [this,original] { requestImageViewer(original); }, false, true,
            !readOnly&&m["seq"].get<int>() > lastAssistant ? m["id"].get<std::string>() : "",readOnly||m.value("edit_virtual",false)?"":m["id"].get<std::string>());
      continue;
    }
    photoGroup = nullptr;
    int contentWidth = user ? width * 3 / 4 : width;
    if (user && stringValue(m, "stored_path").empty() &&
        stringValue(m, "original_path").empty()) {
      lv_point_t measured;
      const auto text = stringValue(m, "output_text");
      lv_text_get_size(&measured, text.c_str(), font(readingFontSize()), 0, 0, LV_COORD_MAX,
                       LV_TEXT_FLAG_NONE);
      contentWidth = std::min(contentWidth, std::max(48, int(measured.x) + 24));
    }
    auto row = box(parent, 0, 0, width, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    auto wrap = box(row, 0, 0, contentWidth, LV_SIZE_CONTENT,
                    user && !hasImage ? theme::surface : theme::background);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(wrap, false);
    lv_obj_set_style_pad_all(wrap, hasImage ? 0 : (user ? 12 : 8), 0);
    lv_obj_set_style_radius(wrap, user ? 18 : 0, 0);
    if(user&&!readOnly)messageLongPress(wrap,m["id"].get<std::string>());
    const int textWidth = contentWidth - (hasImage ? 0 : (user ? 24 : 16));
    auto id = m["id"].get<std::string>();
    const bool live=!readOnly&&id==assistant_;
    std::string content =
        live ? output_ : stringValue(m, "output_text");
    if (live && content.empty() && generating_)
      content = "正在生成…";
    auto reason =
        live ? reasoning_ : stringValue(m, "reasoning_text");
    auto metadata =
        live ? reasoningMetadata_ : objectField(m, "metadata_json");
    if (metadata.value("imported_reference", false))
      label(wrap, "导入的参考内容", 0, 0, textWidth, 17, theme::muted);
    if (!user &&
        (!reason.empty() || metadata.value("reasoning_observed", false))) {
      std::string title = "思考过程";
      if (live && generating_ &&
          !metadata.value("reasoning_complete", false))
        title = "正在思考…";
      else if (metadata.contains("reasoning_duration_ms")) {
        const auto seconds =
            metadata["reasoning_duration_ms"].get<int64_t>() / 1000;
        title = "思考了 " +
                (seconds >= 60 ? std::to_string(seconds / 60) + "分" : "") +
                std::to_string(seconds % 60) + "秒";
      }
      auto header = box(wrap, 0, 0, textWidth, 44);
      reasoningHeaders_.push_back(header);
      lv_obj_set_scrollable(header, false);
      if (!reason.empty()) {
        bool expanded =
            reasoningMode == "always" ||
            (reasoningMode == "during" && live && generating_ &&
             !metadata.value("reasoning_complete", false));
        auto &overrides=readOnly?archiveReasoningOverrides_:reasoningOverrides_;
        auto manual = overrides.find(id);
        if (manual != overrides.end())
          expanded = manual->second;
        if (expanded)
          expandedReasoning_.insert(id);
        auto toggle = [this, id, expanded, source] {
          events_.push(
              "reasoning_toggle",
              {{"id", id}, {"expanded", !expanded}, {"session", source}});
        };
        icon(header,
             expandedReasoning_.count(id) ? "chevron-down" : "chevron-right", 0,
             0, 44, 44, toggle, theme::background, theme::muted);
        auto titleButton = button(header, title, 44, 0, 300, 44, toggle,
                                  theme::background, LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_text_color(lv_obj_get_child(titleButton, 0),
                                    lv_color_hex(theme::reasoning), 0);
        if (expandedReasoning_.count(id)) {
          auto detail = box(wrap, 0, 0, textWidth, LV_SIZE_CONTENT);
          lv_obj_set_scrollable(detail, false);
          lv_obj_set_flex_flow(detail, LV_FLEX_FLOW_COLUMN);
          richMessage(detail, reason, textWidth, true, id + ":reasoning");
        }
      } else
        label(header, title, 0, 10, textWidth, 20, theme::reasoning);
    }
    auto path = stringValue(m, "display_src");
    if (path.empty())
      path = stringValue(m, "stored_path");
    if (path.empty())
      path = stringValue(m, "original_path");
    if (!path.empty()) {
      auto imgBox = box(wrap, 0, 0, textWidth, 140);
      lv_obj_set_scrollable(imgBox, false);
      auto original = stringValue(m, "stored_path");
      if (original.empty()) original = stringValue(m, "original_path");
      if (original.empty()) original = path;
      image(imgBox, path, 0, 0, textWidth, 140,
            [this,original] { requestImageViewer(original); }, user, true);
    }
    if (!content.empty()) {
      if (user)
        label(wrap, content, 0, 0, textWidth, readingFontSize());
      else {
        auto contentBox =
            box(wrap, 0, 0, textWidth, LV_SIZE_CONTENT, theme::background);
        lv_obj_set_flex_flow(contentBox, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollable(contentBox, false);
        if (live)
          liveContent_ = contentBox;
        richMessage(contentBox, content, textWidth, false, id);
      }
    }
    if (retryableStatus(stringValue(m, "status"))) {
      label(wrap, stringValue(m, "status") == "error" ? "生成失败" :
            (stringValue(m, "status") == "interrupted" ? "上次生成已中断" : "已停止生成"),
            0, 0, textWidth, 17, theme::danger);
    }
    if(!readOnly&&stringValue(m,"role")=="assistant")replyBar(wrap,m,textWidth,m["seq"].get<int64_t>()>questionSeq);
  }
}
void App::renderMessages(bool bottom, bool cached) {
  if (!messageContent_)
    return;
  const int scroll = chatScroll_;
  const bool follow = messageOffset_ == 0 && (bottom || chatFollowing_);
  const bool previousLayout = chatLayout_;
  chatLayout_ = true;
  rasters_.clear();
  liveContent_ = nullptr;
  reasoningHeaders_.clear();
  lv_obj_clean(messageContent_);
  auto messages = cached ? rows_ : messageEdit_.empty()?db_.messages(session_, messageOffset_, 30):editedMessages();
  lv_obj_set_style_pad_top(messageContent_, messages.empty() ? 0 : 8, 0);
  if (!cached)
    reasoningDisplay_ =
        db_.settings().value("reasoning_display", std::string("collapsed"));
  expandedReasoning_.clear();
  rows_ = messages;
  if (!cached)
    hasConversationContent_ =
        !db_.query("SELECT id FROM chat_messages WHERE session_id=? AND "
                   "(length(trim(coalesce(output_text,'')))>0 OR "
                   "message_type='image') LIMIT 1",
                   {session_})
             .empty();
  int width = screen_.width - 112;
  const auto questionSeq=db_.query("SELECT coalesce(min(seq),?) seq FROM chat_messages WHERE session_id=? AND role='user' AND (length(trim(coalesce(output_text,'')))>0 OR message_type='image')",{INT64_MAX,session_})[0]["seq"].get<int64_t>();
  bool more = int(messages.size()) >= 30;
  if (more)
    button(messageContent_, "加载更早消息", 0, 0, width, 35, [this] {
      messageOffset_ += 30;
      renderMessages(true);
    });
  if (messages.empty()) {
    auto welcome = box(messageContent_, 0, 0, width, 166);
    lv_obj_set_scrollable(welcome, false);
    auto title = label(welcome, PENCHAT_NAME, 0, 42, width, 30);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    auto hint = label(welcome,
                      db_.models().empty() ? "请在设置中添加 Provider 和模型"
                                           : "有什么可以帮你？",
                      0, 86, width, 23, theme::muted);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
  }
  if (!cached)
    lastAssistantSeq_ =
        db_.query("SELECT coalesce(max(seq),0) AS seq FROM chat_messages WHERE "
                  "session_id=? AND role='assistant' AND seq<?",
                  {session_,messageEdit_.value("pivot",INT64_MAX)})[0]["seq"]
            .get<int>();
  const auto lastAssistant = lastAssistantSeq_;
  renderMessageRows(messageContent_,messages,session_,width,false,lastAssistant,questionSeq);
  syncReplyControls();
  if(!messageEdit_.empty()) {
    auto banner=box(messageContent_,0,0,width,44,theme::background);
    lv_obj_set_scrollable(banner,false);
    label(banner,"发送后将从此处继续 · 后续对话已暂时隐藏",8,10,width-16,18,theme::muted);
  }
  if (messageOffset_ > 0)
    button(messageContent_, "加载较新消息", 0, 0, width, 35, [this] {
      messageOffset_ = std::max(0, messageOffset_ - 30);
      renderMessages(true);
    });
  lv_obj_update_layout(messageContent_);
  lv_obj_stop_scroll_anim(body_);
  const int maximum = std::max(
      0, int(lv_obj_get_scroll_y(body_) + lv_obj_get_scroll_bottom(body_)));
  lv_obj_scroll_to_y(body_,
                     (bottom || follow) ? maximum : std::min(scroll, maximum),
                     LV_ANIM_OFF);
  chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  chatFollowing_ = follow || (messageOffset_ == 0 && maximum == 0);
  chatLayout_ = previousLayout;
  updateChatControls(true);
  paintVisible();
  refreshImages();
}
void App::renderLiveMessage() {
  if (reasoningMetadata_.value("reasoning_observed", false) ||
      !reasoning_.empty()) {
    renderMessages(false, true);
    return;
  }
  if (!liveContent_ || !messageContent_)
    return;
  const int scroll = chatScroll_;
  const bool previousLayout = chatLayout_;
  chatLayout_ = true;
  rasters_.erase(std::remove_if(rasters_.begin(), rasters_.end(),
                                [this](const Raster &r) {
                                  return r.owner == liveContent_;
                                }),
                 rasters_.end());
  lv_obj_clean(liveContent_);
  richMessage(liveContent_, output_.empty() ? "正在生成…" : output_,
              lv_obj_get_width(liveContent_), false, assistant_);
  lv_obj_update_layout(messageContent_);
  lv_obj_scroll_to_y(body_, chatFollowing_ ? LV_COORD_MAX : scroll,
                     LV_ANIM_OFF);
  chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  chatLayout_ = previousLayout;
  updateChatControls(true);
  paintVisible();
  refreshImages();
}
void App::richScrolled(lv_obj_t *object) {
  if (syncingTableScroll_)
    return;
  syncingTableScroll_ = true;
  for (const auto &source : rasters_) {
    if (source.holder != object || !source.tableId)
      continue;
    const int x = lv_obj_get_scroll_x(object);
    tableScrolls_[source.tableKey] = x;
    for (auto &row : rasters_)
      if (row.owner == source.owner && row.tableId == source.tableId &&
          row.holder != object)
        lv_obj_scroll_to_x(row.holder, x, LV_ANIM_OFF);
    break;
  }
  paintVisible();
  syncingTableScroll_ = false;
}
void App::paintVisible() {
  if (!body_)
    return;
  lv_area_t viewport;
  lv_obj_get_coords(body_, &viewport);
  for (auto &r : rasters_) {

    lv_area_t area;
    lv_obj_get_coords(r.holder, &area);
    int from = std::max(0, viewport.y1 - area.y1 - 128),
        to = std::min(r.height, viewport.y2 - area.y1 + 128);
    int left = std::max(0, int(lv_obj_get_scroll_x(r.holder))),
        right = std::min(r.contentWidth, left + r.width + 512);
    std::set<std::pair<int, int>> wanted;
    if (to >= from)
      for (int y = (from / 128) * 128; y < to; y += 128)
        for (int x = (left / 512) * 512; x < right; x += 512)
          wanted.insert({x, y});
    for (auto it = r.tiles.begin(); it != r.tiles.end();) {
      if (wanted.count(it->first)) {
        ++it;
        continue;
      }
      auto object = it->second;
      lv_obj_delete(object);
      auto texture = textures_.find(object);
      if (texture != textures_.end()) {
        lv_image_cache_drop(&texture->second->descriptor);
        textures_.erase(texture);
      }
      it = r.tiles.erase(it);
    }
    for (auto position : wanted)
      if (!r.tiles.count(position)) {
        int x = position.first, y = position.second;
        auto bitmap =
            rich_.paint(r.block, r.width, y, std::min(128, r.height - y), r.fontSize, x,
                        512, r.subdued);
        auto frame = std::make_shared<ImageFrame>();
        frame->width = bitmap.width;
        frame->height = bitmap.height;
        frame->pixels = std::move(bitmap.pixels);
        auto object = imageWidget(r.holder);
        lv_obj_set_pos(object, x, y);
        texture(object, frame, true);
        r.tiles[position] = object;
        auto hits = rich_.layout(r.block, r.width, r.fontSize).links;
        onClick(object, [this, object, hits, x, y] {
          lv_point_t point;
          auto indev = lv_indev_active();
          if (!indev)
            return;
          lv_indev_get_point(indev, &point);
          lv_area_t area;
          lv_obj_get_coords(object, &area);
          for (auto &hit : hits)
            if (point.x - area.x1 + x >= hit.x &&
                point.x - area.x1 + x < hit.x + hit.w &&
                point.y - area.y1 + y >= hit.y &&
                point.y - area.y1 + y < hit.y + hit.h) {
              modal("链接", hit.target);
              break;
            }
        });
      }
  }
}
void App::refreshImages() {
  bool resized = false;
  for (auto &request : imageRequests_) {
    auto frame = images_.get(request.second);
    if (frame) {
      if (!textures_.count(request.first) ||
          textures_[request.first]->frame != frame)
        texture(request.first, frame);
      if (chatPhotos_.count(request.first)) {
        auto holder = lv_obj_get_parent(request.first);
        if (lv_obj_get_width(holder) != frame->width ||
            lv_obj_get_height(holder) != frame->height) {
          lv_obj_set_size(holder, frame->width, frame->height);
          resized = true;
        }
      }
      if (placeholders_.count(request.first))
        lv_obj_set_hidden(placeholders_[request.first], true);
    } else if (images_.failed(request.second) &&
               placeholders_.count(request.first))
      lv_label_set_text(placeholders_[request.first], "图片读取失败");
  }
  if (view_ == "crop") updateCrop();
  if(resized&&view_=="archive_chat"&&body_){
    const int scroll=lv_obj_get_scroll_y(body_);lv_obj_update_layout(body_);
    lv_obj_scroll_to_y(body_,scroll,LV_ANIM_OFF);paintVisible();
  }
  if (resized && view_ == "chat" && body_ && messageContent_) {
    const bool previous = chatLayout_;
    chatLayout_ = true;
    lv_obj_update_layout(messageContent_);
    const int maximum = std::max(
        0, int(lv_obj_get_scroll_y(body_) + lv_obj_get_scroll_bottom(body_)));
    lv_obj_scroll_to_y(
        body_, chatFollowing_ ? maximum : std::min(chatScroll_, maximum),
        LV_ANIM_OFF);
    chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
    chatLayout_ = previous;
    updateChatControls(true);
    paintVisible();
  }
}
std::string App::pageKey() const {
  if (view_ == "provider") return view_ + ":" + (providerForm_.empty() ? "list" : providerForm_.value("id",std::string("new")));
  if (view_ == "model") return view_ + ":" + modelForm_.value("id",std::string());
  if (view_ == "preset_edit") return view_ + ":" + presetForm_.value("id",std::string());
  if (view_ == "album") return view_ + ":" + std::to_string(albumPage_);
  if (view_ == "archives") return view_ + ":" + std::to_string(archiveOffset_);
  if (view_ == "archive_chat")return view_+":"+archiveReaderId_+":"+std::to_string(archiveReaderOffset_);
  return view_;
}
void App::pageHeader(const std::string &title, int top) {
  // Header and business content share one full-height reader. No overlay may
  // cover the space released when the title scrolls out.
  const int width = screen_.width - 90;
  pageHeader_ = box(body_, 0, top, width, 48);
  lv_obj_set_scrollable(pageHeader_, false);
  pageHeading_ = label(pageHeader_, title, 10, 7, width - 20, 26);
  lv_label_set_long_mode(pageHeading_, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(pageHeading_,font(26)->line_height);
  pageContent_ = box(body_, 0, top + 48, width, LV_SIZE_CONTENT);
  lv_obj_set_scrollable(pageContent_, false);
  lv_obj_set_style_min_height(pageContent_, screen_.height - top - 60, 0);
  lv_obj_set_style_pad_bottom(body_, 12, 0);
  lv_obj_set_style_width(body_, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(body_, lv_color_hex(theme::muted), LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(body_, 3, LV_PART_SCROLLBAR);
}
lv_obj_t *App::pageAction(const std::string &title, int right, int width, std::function<void()> action) {
  const int x = screen_.width - 86 - right - width;
  auto result = button(pageHeader_, title, x, 2, width, 44, std::move(action));
  pageActions_[title] = result;
  const int titleWidth = lv_obj_get_style_width(pageHeading_,LV_PART_MAIN);
  lv_obj_set_width(pageHeading_, std::min(titleWidth,x-24));
  return result;
}
Json App::pageDiagnostic() const {
  if (!pageHeader_ || !body_) return Json();
  auto bounds=[](lv_obj_t *o) {lv_area_t a;lv_obj_get_coords(o,&a);return Json::array({a.x1,a.y1,a.x2,a.y2});};
  Json actions=Json::object();
  for(const auto &entry:pageActions_) actions[entry.first]=bounds(entry.second);
  return {{"viewport",bounds(body_)},{"heading",bounds(pageHeading_)},{"actions",actions},
          {"scrolling",lv_obj_is_scrolling(body_)}};
}
void App::settingsRow(const std::string &id, const std::string &title,
                      const std::string &detail, int x, int y, int width,
                      std::function<void()> action, int checked, bool unavailable) {
  auto row = box(pageContent_, x, y, width, 56, theme::surface);
  lv_obj_set_scrollable(row, false);
  lv_obj_set_style_radius(row, 12, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::border), LV_STATE_PRESSED);
  auto caption = label(row, title, 12, 3, width - 80, 20);
  auto value = label(row, detail, 12, 31, width - 80, 16,
                     unavailable ? theme::danger : theme::muted);
  for (auto text : {caption, value}) {
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(text, lv_obj_get_style_text_font(text, LV_PART_MAIN)->line_height);
    lv_obj_set_clickable(text, false);
  }
  lv_obj_t *indicator;
  if (checked >= 0) {
    indicator = lv_switch_create(row);
    lv_obj_set_pos(indicator, width - 56, 16);
    lv_obj_set_size(indicator, 44, 24);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(theme::border), 0);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(theme::border), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(theme::accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(theme::text), LV_PART_KNOB);
    lv_obj_set_style_pad_all(indicator, -2, LV_PART_KNOB);
    if (checked) lv_obj_add_state(indicator, LV_STATE_CHECKED);
  } else {
    indicator = box(row, width - 50, 6, 44, 44, theme::surface);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollable(indicator, false);
    attachIcon(indicator, "chevron-right", theme::muted);
  }
  // The row is the only click target. The visual switch must not also toggle.
  lv_obj_set_clickable(indicator, false);
  onClick(row, std::move(action));
  settingsControls_[id] = {row, caption, value, indicator, checked >= 0};
}
Json App::settingsDiagnostic() const {
  Json rows = Json::object();
  auto bounds = [](lv_obj_t *object) {
    lv_area_t area; lv_obj_get_coords(object, &area);
    return Json::array({area.x1, area.y1, area.x2, area.y2});
  };
  for (const auto &entry : settingsControls_) {
    const auto &control = entry.second;
    Json row = {{"bounds", bounds(control.row)}, {"title", bounds(control.title)},
                {"detail", bounds(control.detail)}, {"indicator", bounds(control.indicator)},
                {"toggle", control.toggle}};
    if (control.toggle) row["checked"] = lv_obj_has_state(control.indicator, LV_STATE_CHECKED);
    rows[entry.first] = std::move(row);
  }
  const bool active = view_ == "settings" && body_;
  return {{"rows", rows}, {"scrolling", active && lv_obj_is_scrolling(body_)},
          {"viewport", active ? bounds(body_) : Json()},
          {"heading", active && settingsHeading_ ? bounds(settingsHeading_) : Json()}};
}
void App::settingsPage() {
  pageHeader("设置");
  settingsHeading_ = pageHeading_;
  const int width = screen_.width - 118;
  const int gap = 16, column = (width - gap) / 2;
  const int left = 10, right = left + column + gap;
  const auto config = db_.settings();
  const auto settings = config.value("penchat_settings", Json::object());
  label(pageContent_, "聊天与会话", left, 4, width, 16, theme::muted);
  settingsRow("models", "模型管理", "Provider 与模型", left, 28, column,
              [this] { show("provider"); });
  settingsRow("presets", "会话预设", "管理会话配置", right, 28, column,
              [this] { show("presets"); });
  const auto reasoningMode = config.value("reasoning_display", std::string("collapsed"));
  const std::string reasoningName = reasoningMode == "always" ? "始终展开"
                                    : reasoningMode == "during" ? "思考完毕后收起" : "不展开";
  settingsRow("reasoning", "思考过程", reasoningName, left, 92, column,
              [this] { reasoningDisplayPicker(); });
  settingsRow("image_auto_send", "图片自动发送", "开启后自动发送图片消息", right, 92, column,
              [this] {
                auto value = db_.settings().value("penchat_settings", Json::object());
                // Read first: C++14 allows [] to insert null before the RHS
                // value() is evaluated on the device's GCC 6 toolchain.
                const bool enabled = value.value("autoSendCapturedContent", false);
                value["autoSendCapturedContent"] = !enabled;
                db_.setting("penchat_settings", value);
                build();
              }, settings.value("autoSendCapturedContent", false));
  const bool autoTitle = config.value("auto_title_enabled", true);
  settingsRow("auto_title", "自动命名", "首条回复完成后生成标题", left, 156, column,
              [this, autoTitle] {
                db_.setting("auto_title_enabled", !autoTitle);
                if (autoTitle) titleCancel_ = true;
                build();
              }, autoTitle);
  auto titleModelId = config.value("title_model_id", std::string());
  std::string titleModelName = titleModelId.empty() ? "跟随当前聊天模型" : "指定模型不可用";
  bool titleModelMissing = !titleModelId.empty();
  for (const auto &model : db_.models()) {
    if (model["id"] == titleModelId) {
      titleModelName = model.value("name", std::string());
      titleModelMissing = false;
    }
  }
  settingsRow("title_model", "命名模型", titleModelName, right, 156, column,
              [this] { modelPicker(true); }, -1, titleModelMissing);
  const bool autoContext = config.value("auto_context_compression", true);
  settingsRow("auto_context", "自动上下文压缩", "压缩前归档，保留消息和图片", left, 220, column,
              [this, autoContext] {
                db_.setting("auto_context_compression", !autoContext);
                build();
              }, autoContext);
  settingsRow("archives", "归档会话", "查看、还原或删除归档", right, 220, column,
              [this] { archiveOffset_ = 0; show("archives"); });
  const auto historyMode=config.value("history_time_display",std::string("time"));
  const auto editDefault=config.value("message_edit_default",std::string("ask"));
  settingsRow("message_edit","编辑消息",editDefault=="archive"?"发送时先归档":editDefault=="overwrite"?"发送时直接覆盖":"每次询问",right,284,column,[this]{messageEditDefaultPicker();});
  settingsRow("history_time","历史时间显示",historyMode=="title"?"会话标题优先":historyMode=="off"?"关闭时间显示":"时间显示优先",left,284,column,[this]{historyTimePicker();});
  label(pageContent_, "阅读与交互", left, 356, width, 16, theme::muted);
  settingsRow("font_size", "字体大小", std::to_string(readingFontPercent_) + "% · 聊天、思考和公式",
              left, 380, column, [this] {
                readingFontDraft_ = readingFontPercent_;
                show("font_size");
              });
  settingsRow("keyboard", "键盘与输入", "独立拼音与学习记忆", right, 380, column, [this] { show("keyboard_settings"); });
  const auto offset = (pullSensitivityOffset_ > 0 ? "+" : "") + std::to_string(pullSensitivityOffset_) + "%";
  settingsRow("pull_sensitivity", "上滑新建灵敏度", "偏移 " + offset + (pullSensitivityOffset_ ? " · 相对新版基准" : " · 新版基准"),
              left, 444, column, [this] { pullSensitivityPicker(); });
  label(pageContent_, "数据与应用", left, 516, width, 16, theme::muted);
  settingsRow("transfer", "导入与导出", "聊天与预设备份", left, 540, column,
              [this] { openTransfer(); });
  const bool persistentExports = config.value("persistent_export_directory", true);
  settingsRow("persistent_exports", "持久化导出目录",
              persistentExports ? "关闭应用后保留导出文件" : "关闭应用时清理导出文件",
              right, 540, column, [this, persistentExports] {
                db_.setting("persistent_export_directory", !persistentExports);
                build();
              }, persistentExports);
  settingsRow("about", "关于 " PENCHAT_NAME, "版本、存储、作者与项目地址", left, 604, width,
              [this] {
                const auto summary = db_.summary();
                modal("关于 " PENCHAT_NAME,
                      "版本：" PENCHAT_VERSION " · LVGL 9.6.0\n"
                      "存储：" + summary["sessions"].dump() + " 个会话 / " +
                      summary["messages"].dump() + " 条消息\n"
                      "作者：" PENCHAT_AUTHOR "\n"
                      "AI 协作：" PENCHAT_AI_CONTRIBUTOR "\n"
                      "项目地址：" PENCHAT_PROJECT_URL);
              });
}
void App::updateFontPreview(int percent) {
  readingFontDraft_ = std::max(75, std::min(150, percent));
  if (fontSlider_)
    lv_slider_set_value(fontSlider_, (readingFontDraft_ + 2) / 5, LV_ANIM_OFF);
  if (fontPercentLabel_)
    lv_label_set_text(fontPercentLabel_, (std::to_string(readingFontDraft_) + "%").c_str());
  if (fontPreview_)
    lv_obj_set_style_text_font(fontPreview_, font((21 * readingFontDraft_ + 50) / 100), 0);
}
void App::fontSizePage() {
  pageHeader("字体大小");
  pageAction("恢复默认", 96, 120, [this] { updateFontPreview(100); });
  pageAction("保存", 0, 84, [this] { events_.push("reading_font_apply"); });
  const int width = screen_.width - 118;
  label(pageContent_, "聊天正文、思考过程与公式同步缩放；菜单和键盘保持原大小。",
        10, 4, width - 20, 16, theme::muted);
  fontPercentLabel_ = label(pageContent_, "", 10, 29, width - 20, 20);
  lv_obj_set_style_text_align(fontPercentLabel_, LV_TEXT_ALIGN_CENTER, 0);
  label(pageContent_, "75%", 10, 59, 70, 18, theme::muted);
  auto maximum = label(pageContent_, "150%", width - 80, 59, 70, 18, theme::muted);
  lv_obj_set_style_text_align(maximum, LV_TEXT_ALIGN_RIGHT, 0);
  fontSlider_ = lv_slider_create(pageContent_);
  lv_obj_remove_style_all(fontSlider_);
  lv_obj_set_pos(fontSlider_, 100, 64);
  lv_obj_set_size(fontSlider_, width - 200, 16);
  lv_obj_set_ext_click_area(fontSlider_, 16);
  lv_slider_set_range(fontSlider_, 15, 30);
  for (auto part : {LV_PART_MAIN, LV_PART_INDICATOR, LV_PART_KNOB}) {
    lv_obj_set_style_bg_opa(fontSlider_, LV_OPA_COVER, part);
    lv_obj_set_style_radius(fontSlider_, LV_RADIUS_CIRCLE, part);
    lv_obj_set_style_bg_color(fontSlider_, lv_color_hex(part == LV_PART_MAIN ? theme::border :
                                                     part == LV_PART_KNOB ? theme::text : theme::accent), part);
  }
  lv_obj_set_style_pad_all(fontSlider_, 7, LV_PART_KNOB);
  lv_obj_add_event_cb(fontSlider_, [](lv_event_t *event) {
    auto app = static_cast<App *>(lv_event_get_user_data(event));
    app->updateFontPreview(lv_slider_get_value(app->fontSlider_) * 5);
  }, LV_EVENT_VALUE_CHANGED, this);
  fontPreview_ = label(pageContent_, "文字大小预览 Aa 123\n让阅读更轻松，按自己的习惯调整。",
                        10, 104, width - 20, 21);
  updateFontPreview(readingFontDraft_);
}
void App::reasoningDisplayPicker() {
  modal("自动展开思考过程", "");
  const std::pair<const char *, const char *> choices[] = {
      {"collapsed", "不展开"},
      {"during", "思考完毕后收起"},
      {"always", "始终展开"}};
  const auto mode =
      db_.settings().value("reasoning_display", std::string("collapsed"));
  const int choiceWidth = (screen_.width - 92 - 70) / 3;
  for (int i = 0; i < 3; ++i) {
    const std::string key = choices[i].first;
    button(modal_, (mode == key ? "● " : "○ ") + std::string(choices[i].second),
           16 + i * (choiceWidth + 12), 65, choiceWidth, 52, [this, key] {
             events_.push("reasoning_display", {{"mode", key}});
           });
  }
}
void App::providerPage() {
  int w = screen_.width - 118;
  pageHeader(providerForm_.empty()          ? "Provider / 模型"
             : providerForm_.contains("id") ? "编辑 Provider"
                                            : "添加 Provider", providerForm_.empty() ? 8 : 0);
  if (providerForm_.empty()) {
    pageAction("添加 Provider", 17, 195,
           [this] {
             providerForm_ = {{"name", "OpenAI"},
                              {"baseURL", "https://api.openai.com/v1"},
                              {"apiKey", ""},
                              {"api", "responses"}};
             modelNames_.clear();
             ++modelGeneration_;
             modelCancel_ = true;
             build();
           });
    int y = 5;
    auto models = db_.models();
    auto providers = db_.providers();
    for (const auto &p : providers) {
      auto title = label(pageContent_, p.value("name", std::string("Provider")), 10,
                         y + 5, w - 130, 21);
      lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
      lv_obj_set_height(title, font(21)->line_height);
      auto providerId = p["id"].get<std::string>();
      button(pageContent_, "编辑", w - 105, y, 95, 44,
             [this, providerId] { editProvider(providerId); });
      y += 46;
      for (const auto &m : models) {
        if (m["provider_id"] != providerId)
          continue;
        std::string title = m.value("is_default", 0) ? "● " : "○ ";
        title += m["name"].get<std::string>();
        auto id = m["id"].get<std::string>();
        button(
            pageContent_, title, 10, y, w - 125, 44,
            [this, id] {
              chooseModel(id);
              build();
            },
            theme::surface, LV_TEXT_ALIGN_LEFT);
        button(pageContent_, "编辑", w - 105, y, 95, 44, [this, m] {
          modelForm_ = m;
          modelForm_["reasoning"] = reasoningCapabilities(m);
          modelAdvanced_ = false;
          show("model");
        });
        y += 53;
      }
      y += 10;
    }
    if (providers.empty())
      emptyState("还没有可用模型", "点击右上角“添加 Provider”，连接你的模型服务。");
    return;
  }
  auto save = pageAction("保存", 24, 130, [this] { saveProvider(); });
  primary(save);
  auto field = [&](const std::string &key, const std::string &name, int x,
                   int y, int width, bool secret = false) {
    std::string value = providerForm_.value(key, std::string());
    if (secret && !value.empty())
      value = "••••••••";
    fieldButton(
        pageContent_, name, value.empty() ? "点击填写" : value, x, y, width,
        [this, key, name] {
          edit(
              providerForm_.value(key, std::string()), name,
              [this, key](const std::string &v) {
                providerForm_[key] = v;
                build();
              },
              4096,
              key == "name"     ? "ZhCNPreferred"
              : key == "apiKey" ? "EnUSOnly"
                                : "EnUSPreferred", {}, false, key == "apiKey");
        });
  };
  const int columnWidth = (w - 30) / 2;
  const int right = 20 + columnWidth;
  field("name", "名称", 10, 4, columnWidth);
  field("baseURL", "Base URL", right, 4, columnWidth);
  field("apiKey", "API Key", 10, 58, columnWidth, true);
  const auto api = providerForm_.value("api", std::string("responses"));
  button(pageContent_, api == "chat" ? "接口：Chat Completions" :
                       api == "anthropic" ? "接口：Anthropic Messages" : "接口：Responses",
         right, 58, columnWidth, 48, [this] { protocolPicker(); });
  field("model", "手动模型（逗号分隔）", 10, 112, w - 20);
  button(pageContent_, "自动获取模型", 10, 166, columnWidth, 48,
         [this] { fetchModels(); });
  button(
      pageContent_,
      "默认推理强度：" +
          effortLabel(providerForm_.value("default_effort", std::string())),
      right, 166, columnWidth, 48,
      [this] {
        std::vector<std::string> values{""};
        values.insert(values.end(), effortOrder().begin(), effortOrder().end());
        auto current =
            std::find(values.begin(), values.end(),
                      providerForm_.value("default_effort", std::string()));
        providerForm_["default_effort"] =
            current == values.end() || ++current == values.end() ? values[0]
                                                                 : *current;
        build();
      },
      theme::surface, LV_TEXT_ALIGN_LEFT);
  const bool compatible = objectField(providerForm_, "compatibility").value("enabled", false);
  settingsRow("api_compatibility", "API 兼容模式", compatible ? "已启用 · 按需调整连接与扩展" : "标准模式 · 不自动重发请求",
              10, 224, w - 20, [this] {
                providerScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
                show("api_compatibility");
              });
  const bool anthropic = api == "anthropic";
  if (anthropic)
    settingsRow("anthropic_connection", "Anthropic 连接设置", "鉴权、API 版本与自定义请求头", 10, 288, w - 20, [this] {
      providerScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_))); show("anthropic_connection");
    });
  status_ = label(pageContent_, notice_, 10, anthropic ? 356 : 292, w, 17, theme::muted);
  int y = anthropic ? 382 : 318;
  for (auto &name : modelNames_) {
    auto nameLabel = label(pageContent_, name, 15, y, w, 20);
    lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(nameLabel, font(20)->line_height);
    y += 30;
  }
}
void App::editProvider(const std::string &id) {
  ++modelGeneration_;
  modelCancel_ = true;
  providerForm_ = db_.providerForm(id);
  modelNames_.clear();
  notice_.clear();
  build();
}
void App::fetchModels() {
  if (modelsBusy_.exchange(true))
    return;
  if (modelWorker_.joinable())
    modelWorker_.join();
  Json model = {{"base_url", providerForm_.value("baseURL", std::string())},
                {"api_key", providerForm_.value("apiKey", std::string())},
                {"headers_json", providerForm_.value("headers", Json::object()).dump()},
                {"provider_metadata", Json({{"api", providerForm_.value("api", std::string("responses"))},
                                           {"compatibility", objectField(providerForm_, "compatibility")},
                                           {"anthropic", objectField(providerForm_, "anthropic")}}).dump()}};
  notice_ = "正在拉取模型…";
  build();
  modelCancel_ = false;
  const int generation = ++modelGeneration_;
  modelWorker_ = std::thread([this, model, generation] {
    Json data = {{"generation", generation}};
    try { data["body"] = fetchModelList(model, modelCancel_); data["status"] = 200; }
    catch (const std::exception &e) { data["error"] = redactApiError(e.what(), model); data["status"] = 0; }
    events_.push("models", data);
    modelsBusy_ = false;
  });
}
void App::saveProvider() {
  auto name = providerForm_.value("name", std::string()),
       base = providerForm_.value("baseURL", std::string()),
       key = providerForm_.value("apiKey", std::string());
  if (name.empty() || base.empty() || key.empty() ||
      (base.find("https://") != 0 && base.find("http://") != 0)) {
    modal("请检查连接配置", "请填写名称、Base URL 和 API Key；地址需以 http:// 或 https:// 开头。");
    return;
  }
  try {
    Json model = {{"base_url", base}, {"api_key", key}, {"headers_json", providerForm_.value("headers", Json::object())},
                  {"provider_metadata", {{"api", providerForm_.value("api", std::string("responses"))},
                                         {"anthropic", objectField(providerForm_, "anthropic")}}}};
    apiRoot(model); apiHeaders(model);
  }
  catch (const std::exception &e) { modal("请检查接口地址", e.what()); return; }
  auto manual = providerForm_.value("model", std::string());
  std::vector<std::string> names;
  std::replace(manual.begin(), manual.end(), '\n', ',');
  size_t start = 0;
  while (start < manual.size()) {
    auto end = manual.find(',', start);
    auto n = manual.substr(start, end == std::string::npos ? end : end - start);
    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.front())))
      n.erase(n.begin());
    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.back())))
      n.pop_back();
    if (!n.empty() && std::find(names.begin(), names.end(), n) == names.end())
      names.push_back(n);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  if (names.empty())
    names = modelNames_;
  if (names.empty()) {
    modal("还没有选择模型", "请填写模型名称，或点击“自动获取模型”后再保存。");
    return;
  }
  db_.saveProvider(providerForm_, names);
  ++modelGeneration_;
  modelCancel_ = true;
  providerForm_ = Json::object();
  show("provider");
}
void App::goBack() {
  if (view_ == "transfer") {
    transferBack();
    return;
  }
  if (view_ == "api_compatibility" || view_ == "anthropic_connection") {
    show("provider"); lv_obj_scroll_to_y(body_, providerScroll_, LV_ANIM_OFF);
  } else if (view_ == "font_size" || view_ == "keyboard_settings")
    show("settings");
  else if (view_ == "presets")
    show("settings");
  else if (view_ == "preset_edit") {
    presetForm_ = Json::object();
    show("presets");
  } else if(view_=="archive_chat") {
    show("archives");lv_obj_scroll_to_y(body_,archiveListScroll_,LV_ANIM_OFF);
  } else if (view_ == "archives")
    show("settings");
  else if (view_ == "model")
    show("provider");
  else if (view_ == "provider") {
    if (!providerForm_.empty()) {
      providerForm_ = Json::object();
      modelNames_.clear();
      ++modelGeneration_;
      modelCancel_ = true;
      build();
    } else
      show("settings");
  } else if (view_ == "settings") {
    show("chat");
    if(messageEdit_.empty())toggleNavigation();
  } else
    show("chat");
}
void App::closeNavigation(bool resumeCamera) {
  const bool wasOpen = navigationOpen_;
  if(wasOpen)historyCaptions_.clear();
  navigationOpen_ = false;
  historyRows_.clear();
  navigationBackdrop_ = navigationPointerDown_ = false;
  auto object = navigation_;
  navigation_ = navigationList_ = navigationActive_ = nullptr;
  if (object)
    lv_obj_delete(object);
  if (wasOpen && resumeCamera && view_ == "camera" && visible_ && !keyboard_)
    startCamera();
}
void App::toggleNavigation() {
  if(!messageEdit_.empty()||messageEditBusy_||replyBusy_||contextRegenerating_)return;
  if (navigationOpen_) {
    closeNavigation();
    return;
  }
  closeAttachments();
  resetPull();
  if (view_ == "camera")
    camera_.stop();
  historyOffset_ = 0;
  navigationScroll_ = 0;
  navigationOpen_ = true;
  historyDrawer();
}
void App::historyDrawer() {
  // Only the drawer is replaced when paging, keeping the underlying form/draft.
  if (navigationList_)
    navigationScroll_ = std::max(0, int(lv_obj_get_scroll_y(navigationList_)));
  historyRows_.clear();historyCaptions_.clear();
  auto previous = navigation_;
  navigation_ = navigationList_ = navigationActive_ = nullptr;
  if (previous)
    lv_obj_delete(previous);
  navigation_ = box(screenObj_, 0, 0, screen_.width, screen_.height, 0x000000);
  lv_obj_set_scrollable(navigation_, false);
  lv_obj_set_style_bg_opa(navigation_, 120, 0);
  lv_obj_set_clickable(navigation_, true);
  const int panelWidth = screen_.width * 2 / 3;
  const int listWidth = panelWidth - 24;
  auto panel =
      box(navigation_, 0, 0, panelWidth, screen_.height, theme::sidebar);
  lv_obj_set_scrollable(panel, false);
  navigationList_ =
      box(panel, 12, 8, listWidth, screen_.height - 70, theme::sidebar);
  lv_obj_set_scroll_dir(navigationList_, LV_DIR_VER);
  lv_obj_set_scroll_elastic(navigationList_, true);
  lv_obj_set_scroll_momentum(navigationList_, true);
  lv_obj_set_scroll_chain_ver(navigationList_, false);
  lv_obj_set_scroll_chain_hor(navigationList_, false);
  lv_obj_set_scrollbar_mode(navigationList_, LV_SCROLLBAR_MODE_AUTO);
  auto sessions = db_.sessions(historyOffset_);
  if (sessions.empty()) {
    label(navigationList_, "暂无历史对话", 12, 8, listWidth - 24, 21,
          theme::muted);
    label(navigationList_, "发送文字或图片后会自动保存", 12, 44, listWidth - 24,
          18, theme::muted);
  }
  int y = 0;
  for (const auto &s : sessions) {
    auto id = s["id"].get<std::string>();
    auto row = historyRow(navigationList_, s, y, listWidth);
    if (id == session_)
      navigationActive_ = row;
    y += 50;
  }
  if (historyOffset_ > 0)
    button(navigationList_, "上一页", 0, y, 160, 44, [this] {
      historyOffset_ = std::max(0, historyOffset_ - 30);
      navigationScroll_ = 0;
      historyDrawer();
    });
  if (sessions.size() == 30)
    button(navigationList_, "下一页", 180, y, 160, 44, [this] {
      historyOffset_ += 30;
      navigationScroll_ = 0;
      historyDrawer();
    });
  const int footerWidth = (panelWidth - 36) / 2;
  auto footerAction = [&](int x, int width, const char *kind, const char *title,
                          std::function<void()> action) {
    icon(panel, kind, x, screen_.height - 54, 48, 48, action, theme::sidebar);
    button(panel, title, x + 52, screen_.height - 54, width - 52, 48, action,
           theme::sidebar, LV_TEXT_ALIGN_LEFT);
  };
  footerAction(12, footerWidth, "newchat", "新建聊天", [this] { newChat(); });
  footerAction(panelWidth - 156, 144, "settings", "设置",
               [this] { show("settings"); });
  lv_obj_update_layout(navigation_);
  // LVGL needs scrollable extent before it will start an elastic gesture.
  // A transparent one-pixel tail also enables bounce for short/empty lists.
  if (lv_obj_get_scroll_bottom(navigationList_) <= 0) {
    auto tail =
        box(navigationList_, 0, lv_obj_get_height(navigationList_), 1, 1);
    lv_obj_set_style_bg_opa(tail, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollable(tail, false);
    lv_obj_set_clickable(tail, false);
    lv_obj_update_layout(navigationList_);
  }
  const int end = std::max(0, int(lv_obj_get_scroll_y(navigationList_) +
                                  lv_obj_get_scroll_bottom(navigationList_)));
  lv_obj_scroll_to_y(navigationList_, std::min(navigationScroll_, end),
                     LV_ANIM_OFF);
}
lv_point_t App::photoPoint(lv_point_t point) const {
  // LVGL hit testing follows the rotated container; custom crop geometry needs
  // the same inverse transform before comparing with untransformed object bounds.
  const int x=point.x-screen_.width/2,y=point.y-screen_.height/2;
  if(cameraAngle_==90) return {screen_.width/2-y,screen_.height/2+x};
  if(cameraAngle_==180) return {screen_.width/2-x,screen_.height/2-y};
  if(cameraAngle_==270) return {screen_.width/2+y,screen_.height/2-x};
  return point;
}
void App::startCamera() {
  cameraCapturing_=false;pendingCameraAngle_=-1;cameraError_.clear();
  camera_.orientation(cameraAutomatic_,cameraAngle_);
  camera_.start();
}
void App::cameraMode(bool automatic,int angle) {
  camera_.stop();
  cameraAutomatic_=automatic;cameraAngle_=angle;pendingCameraAngle_=-1;
  db_.setting("camera_auto_orientation",automatic);
  db_.setting("camera_orientation",angle);
  camera_.orientation(automatic,angle);
  build();
  if(visible_ && !keyboard_) startCamera();
}
void App::cameraPage() {
  lv_obj_set_scrollable(body_, false);
  lv_obj_update_layout(body_);
  const bool portrait=cameraAngle_%180;
  const int w=lv_obj_get_width(body_),h=lv_obj_get_height(body_);
  auto preview=box(body_,8,8,portrait?w-16:w-140,portrait?h-204:h-16,theme::sidebar);
  lv_obj_set_scrollable(preview,false);
  cameraImage_=imageWidget(preview);lv_obj_center(cameraImage_);
  const int railX=portrait?12:w-130;
  button(body_,cameraAutomatic_?"自动方向":"方向已锁定",railX,portrait?h-190:16,portrait?w-24:124,44,
         [this]{events_.push("camera_mode",{{"auto",!cameraAutomatic_},{"angle",cameraAngle_}});});
  auto shutter=box(body_,portrait?(w-72)/2:railX+26,portrait?h-126:(h-72)/2,72,72,theme::background);
  lv_obj_set_scrollable(shutter,false);lv_obj_set_style_radius(shutter,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(shutter,3,0);lv_obj_set_style_border_color(shutter,lv_color_hex(theme::text),0);
  auto center=box(shutter,5,5,56,56,theme::text);lv_obj_set_style_radius(center,LV_RADIUS_CIRCLE,0);
  lv_obj_set_scrollable(center,false);lv_obj_set_clickable(center,false);lv_obj_center(center);
  onClick(shutter,[this]{if(!cameraCapturing_){cameraCapturing_=true;camera_.capture(cameraAngle_);}});
  button(body_,"转向 90°",railX,portrait?h-48:h-66,portrait?w-24:124,44,
         [this]{events_.push("camera_mode",{{"auto",false},{"angle",(cameraAngle_+90)%360}});});
  status_=label(preview,cameraError_.empty()?"相机启动中…":cameraError_,12,12,lv_obj_get_width(preview)-24,19,theme::muted);
}
void App::albumPage() {
  int w = screen_.width - 118;
  pageHeader("相册");
  std::vector<std::pair<int64_t, std::string>> files;
  for (auto &dir :
       std::vector<std::string>{join(dataDir_, "DCIM"), "/userdisk/Pictures"}) {
    DIR *d = opendir(dir.c_str());
    if (!d)
      continue;
    while (auto e = readdir(d)) {
      std::string n = e->d_name;
      auto lower = n;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (lower.find(".jpg") == std::string::npos &&
          lower.find(".jpeg") == std::string::npos &&
          lower.find(".png") == std::string::npos &&
          lower.find(".bmp") == std::string::npos &&
          lower.find(".webp") == std::string::npos)
        continue;
      auto p = join(dir, n);
      struct stat st;
      if (!stat(p.c_str(), &st) && S_ISREG(st.st_mode))
        files.push_back({int64_t(st.st_mtime), p});
    }
    closedir(d);
  }
  std::map<std::string, std::string> scanPreviews;
  for (auto &asset : db_.query("SELECT stored_path,display_src,created_at FROM "
                               "message_assets WHERE source_type='scan'")) {
    auto path = asset.value("stored_path", std::string());
    if (!path.empty() && access(path.c_str(), R_OK) == 0) {
      files.push_back({asset.value("created_at", int64_t(0)) / 1000, path});
      scanPreviews[path] = stringValue(asset, "display_src");
    }
  }
  std::sort(files.rbegin(), files.rend());
  int begin = albumPage_ * 12, end = std::min(int(files.size()), begin + 12);
  for (int i = begin; i < end; ++i) {
    auto path = files[i].second;
    int pos = i - begin, x = 10 + (pos % 3) * (w / 3), y = 4 + (pos / 3) * 145;
    auto preview =
        scanPreviews.count(path) ? scanPreviews[path] : std::string();
    image(pageContent_, preview.empty() ? path : preview, x, y, w / 3 - 15, 105,
          [this, path, preview] {
            Json image = {{"path", path}, {"source", "album"}};
            if (!preview.empty())
              image["previewPath"] = preview;
            attach(image);
          });
    auto caption = label(pageContent_, basename(path), x, y + 108, w / 3 - 20, 15);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(caption, font(15)->line_height);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
  }
  int y = 4 + ((end - begin + 2) / 3) * 145;
  if (albumPage_)
    button(pageContent_, "上一页", 10, y, 140, 44, [this] {
      --albumPage_;
      build();
    });
  if (end < int(files.size()))
    button(pageContent_, "下一页", 170, y, 140, 44, [this] {
      ++albumPage_;
      build();
    });
  if (files.empty())
    emptyState("相册还是空的", "拍照时选择“加入系统相册”，照片就会显示在这里。");
}
void App::persistReply(const std::string &status, const Json &error) {
  if (assistant_.empty())
    return;
  if(status=="complete" && !messageTimeline_.contains("completed_at"))messageTimeline_["completed_at"]=nowMs();
  auto metadata=reasoningMetadata_;metadata["timeline"]=messageTimeline_;
  db_.queueMessage(session_, assistant_, networkGeneration_, ++saveVersion_, output_,
                   reasoning_, status, error, metadata);
  persistDirty_ = false;
  persistDue_ = ticks() + 500;
}
void App::stopNetwork() {
  cancelContext();
  cancel_ = true;
  if (network_.joinable())
    network_.join();
  for (auto &event : events_.drain()) {
    if (event.type == "response_state" &&
        event.data.value("generation", -1) == networkGeneration_) {
      output_ = event.data.value("output", std::string());
      reasoning_ = event.data.value("reasoning", std::string());
      reasoningMetadata_ = event.data.value("metadata", Json::object());
    } else if (event.type != "done" && event.type != "cancelled" &&
               event.type != "network_error")
      events_.push(event.type, event.data);
  }
  if (generating_ && !assistant_.empty() && !saveInFlight_)
    persistReply("cancelled");
  persistDirty_ = false;
  saveInFlight_ = false;
  generating_ = false;
  assistant_.clear();
  ++networkGeneration_;
}
bool App::canSend() {
  if(messageEditBusy_)return false;
  if(!messageEdit_.empty())return hasText(draft_)||!messageEditImages_.empty();
  if (hasText(draft_))
    return true;
  // Pending user content survives an interruption or image auto-send being off.
  auto latest = db_.messages(session_, 0, 1);
  if (!latest.empty() && stringValue(latest[0], "role") == "assistant" &&
      retryableStatus(stringValue(latest[0], "status"))) return true;
  return !latest.empty() && stringValue(latest[0], "role") == "user" &&
         (hasText(stringValue(latest[0], "output_text")) ||
          !stringValue(latest[0], "stored_path").empty() ||
          !stringValue(latest[0], "original_path").empty());
}
void App::sendMessage(const std::string &retry) {
  if(replyBusy_)return;
  if(!messageEdit_.empty()){if(retry.empty())commitMessageEdit();return;}
  if (generating_ || contextBusy_)
    return;
  auto latest = db_.messages(session_, 0, 1);
  const bool failed = !latest.empty() && stringValue(latest[0], "role") == "assistant" &&
      retryableStatus(stringValue(latest[0], "status"));
  if (!retry.empty() && (!failed || stringValue(latest[0], "id") != retry)) return;
  const std::string retryId = !retry.empty() ? retry :
      (!hasText(draft_) && failed ? stringValue(latest[0], "id") : std::string());
  if(!retryId.empty()){retryWarning(retryId);return;}
  if (retryId.empty() && !canSend()) return;
  sendActionAt_=nowMs();
  startContext(false, retryId);
}
void App::sendPrepared(Json model, Json input, const std::string &retryId,
                       const std::string &draft) {
  if (input.empty()) {
    build();
    return;
  }
  bindPreferences(model);
  sessionPreferences_["has_sent"] = true;
  savePreferences();
  if (retryId.empty() && hasText(draft)) {
    db_.append(session_, "user", draft, Json::object(), sessionPreferences_);
    if (draft_ == draft)
      draft_.clear();
  }
  if (network_.joinable())
    network_.join();
  messageOffset_ = 0;
  chatFollowing_ = true;
  if (retryId.empty())
    assistant_ = db_.append(session_, "assistant", "", Json::object(), sessionPreferences_);
  else {
    assistant_ = retryId;
    db_.execute("UPDATE chat_messages SET metadata_json='{}' WHERE id=?", {retryId});
    db_.updateMessage(retryId, "", "", "streaming");
    reasoningOverrides_.erase(retryId);
  }
  const auto started=nowMs();
  messageTimeline_={{"version",1},{"request_started_at",started}};
  db_.recordRequest(assistant_, model,sendActionAt_?sendActionAt_:started,started);
  notice_.clear();
  reasoningMetadata_ = Json::object();
  output_.clear();
  reasoning_.clear();
  generating_ = true;
  cancel_ = false;
  int generation = ++networkGeneration_;
  persistDue_ = ticks() + 500;
  persistDirty_ = false;
  saveInFlight_ = false;
  saveVersion_ = 0;
  network_ = std::thread([this, model = std::move(model), input = std::move(input), generation] {
    try {
      streamChat(model.contains("api_key") ? model : db_.requestModel(model), input,
                 cancel_, events_, generation);
    } catch (...) {
      events_.push("network_error",
                   {{"generation", generation}, {"error", "无法读取请求配置，请重试"}});
    }
  });
  build();
}
void App::storeImage(const Json &image, std::function<void(bool)> completion) {
  if (imageStoreBusy_ || messageEditBusy_ || replyBusy_)
    return;
  if (imageStoreWorker_.joinable())
    imageStoreWorker_.join();
  imageStoreBusy_ = true;
  imageStoreCompletion_ = std::move(completion);
  const auto session = session_;
  const auto preferences = sessionPreferences_;
  const auto editToken=messageEdit_.empty()?std::string():messageEditToken_;
  modal("正在保存图片", "图片准备完成后会自动返回，请稍候…");
  imageStoreWorker_ = std::thread([this, image, session, preferences,editToken] {
    bool ok = true;
    Json asset=Json::object();std::vector<std::string> created;
    try {
      if(editToken.empty())db_.append(session, "user", "", image, preferences);
      else {
        const auto directory=join(dataDir_,".message-edit");
        ensureDirectory(directory);
        struct stat st{};
        if(lstat(directory.c_str(),&st)||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))throw std::runtime_error("Invalid staging directory");
        for(auto mapping:{std::make_pair("path","stored_path"),std::make_pair("previewPath","display_src")}) {
          if(!image.contains(mapping.first))continue;
          auto file=join(join(dataDir_,".message-edit"),uid("image")+".png");created.push_back(file);
          auto bytes=fileText(image[mapping.first].get<std::string>(),16*1024*1024);
          if(bytes.empty())throw std::runtime_error("Cannot read image");
          atomicText(file,bytes);asset[mapping.second]=file;
        }
        asset["edit_new"]=true;asset["edit_id"]=uid("edit-image");asset["width"]=image.value("width",0);asset["height"]=image.value("height",0);
        asset["source_type"]=image.value("source",std::string("camera"));asset["metadata_json"]=image.value("metadata",Json::object()).dump();
      }
    } catch (...) {ok=false;for(const auto &path:created){unlink(path.c_str());unlink((path+".part").c_str());}}
    events_.push("image_stored", {{"ok", ok}, {"session", session},{"edit_token",editToken},{"asset",asset}});
  });
}
void App::attach(const Json &image) {
  chatFollowing_ = true;
  messageOffset_ = 0;
  auto model = currentModel();
  bindPreferences(model);
  const auto session = session_;
  storeImage(image, [this, session](bool ok) {
    if (!ok || session != session_)
      return;
    show("chat");
    if (db_.settings()
            .value("penchat_settings", Json::object())
            .value("autoSendCapturedContent", false) &&
        !generating_ && messageEdit_.empty())
      sendMessage();
  });
}
void App::suspend(bool value) {
  resetBack();
  backGesture_ = 0; backStart_ = Json::object();
  viewerPointer_ = viewerCanvasTouch_ = false; viewerLastTap_ = 0;
  finishImageGesture(false);nonImageMulti_=false;
  if(imageViewer_ || view_=="crop")imageGesture_.mode=-2;
  if(directImageInput_)imageInputExitDue_=ticks()+80;
  imageInput_.close();directImageInput_=false;
  if (value && imageStoreWorker_.joinable())
    imageStoreWorker_.join();
  if (value) {replyCancel_=true;if(contextRegenerating_)cancelContext();editorInterrupt();}
  if (value) {
    visible_ = false;
    scanner_.eligible(false, session_);
    afterScanRelease_ = {};
    if (view_ == "scan") {
      scanId_ = 0;
      scanResult_ = Json::object();
      view_ = "chat";
      build();
    }
    closeNavigation(false);
  }
  closeAttachments();
  resetPull();
  touchDown_ = false;
  pullTrackActivity_ = false;
  visible_ = !value;
  input_.release();
  display_.visible(!value && !keyboard_);
  if (value) {
    camera_.stop();
  } else if (editor_.active && !keyboard_) {editorPolicy(!editorSelectionOnly_);editorChanged();editorFollowCursor_=false;editorBuild();editorOcrStart();}
  else if (view_ == "camera")
    startCamera();
}
void App::shutdown() {
  if (shutdownComplete_) return;
  shutdownComplete_ = true;
  resetBack();
  closeImageViewer(false);
  cancelContext();
  if (sessionInfoWorker_.joinable()) sessionInfoWorker_.join();
  replyCancel_=true;if(replyWorker_.joinable())replyWorker_.join();replyBusy_=false;
  if (transfer_)
    transfer_->stop();
  if (imageStoreWorker_.joinable())
    imageStoreWorker_.join();
  if(messageEditWorker_.joinable())messageEditWorker_.join();
  messageEditBusy_=false;cancelMessageEdit(false);
  editorInterrupt();
  pinyin_.stop();
  if (editorRoot_) { lv_obj_delete(editorRoot_); editorRoot_ = editorText_ = nullptr; }
  scanner_.eligible(false, session_);
  afterScanRelease_ = {};
  cancel_ = true;
  modelCancel_ = true;
  titleCancel_ = true;
  camera_.stop();
  clearCrop();
  if (network_.joinable())
    network_.join();
  if (modelWorker_.joinable())
    modelWorker_.join();
  if (titleWorker_.joinable())
    titleWorker_.join();
  if (generating_ && !assistant_.empty())
    stopNetwork();
  db_.flush();
  if (transfer_ && !transfer_->finish(db_.settings().value("persistent_export_directory", true)))
    db_.setting("export_cleanup_failed", true);
}
void App::routeTouch(const Json &m) {
  if (imageViewer_ || view_=="crop") {
    if(!imageTouch(m))input_.touch(m.value("x",0),m.value("y",0),m.value("pressed",false));
    return;
  }
  if (editor_.active) { editorTouch(m.value("x",0), m.value("y",0), m.value("pressed",false), m.value("cancelled",false)); return; }
  const int x = m.value("x", 0), y = m.value("y", 0);
  const bool pressed = m.value("pressed", false);
  if (navigationOpen_ && !modal_) {
    if (pressed && !navigationPointerDown_) {
      navigationBackdrop_ = x >= screen_.width * 2 / 3;
      navigationStartX_ = x;
      navigationStartY_ = y;
      navigationDistance_ = 0;
    }
    navigationPointerDown_ = pressed;
    if (navigationBackdrop_) {
      const int dx = x - navigationStartX_, dy = y - navigationStartY_;
      navigationDistance_ = std::max(navigationDistance_, dx * dx + dy * dy);
      if (!pressed || m.value("cancelled", false)) {
        bool close = !m.value("cancelled", false) &&
                     navigationDistance_ <= 144 && x >= screen_.width * 2 / 3;
        navigationBackdrop_ = navigationPointerDown_ = false;
        input_.cancel();
        if (close)
          closeNavigation();
      }
      return;
    }
    if (m.value("cancelled", false))
      navigationPointerDown_ = false;
  }
  // Track the entire physical gesture before LVGL dispatches CLICKED.
  // A swipe that returns to its origin must never become an input tap.
  if (pressed && !touchDown_) {
    composerTouchX_ = x;
    composerTouchY_ = y;
    composerTapAllowed_ = false;
    if (view_ == "chat" && composer_ && !modal_ && !navigationOpen_ &&
        !attachments_ && !lv_obj_is_hidden(chatControls_)) {
      lv_area_t bounds;
      lv_obj_get_coords(composer_, &bounds);
      composerTapAllowed_ = x >= bounds.x1 && x <= bounds.x2 &&
                            y >= bounds.y1 && y <= bounds.y2;
    }
  }
  const int composerDx = x - composerTouchX_, composerDy = y - composerTouchY_;
  if (composerDx * composerDx + composerDy * composerDy > 144 ||
      m.value("cancelled", false))
    composerTapAllowed_ = false;
  if (m.value("cancelled", false)) {
    resetPull();
    touchDown_ = false;
    pullTrackActivity_ = false;
    input_.cancel();
  } else if (!chatTouch(x, y, pressed))
    input_.touch(x, y, pressed);
}
void App::handle(const Json &m) {
  auto type = m.value("type", std::string());
#ifndef __aarch64__
  if (type == "icons.test") {
    clear();
    view_ = "icons";
    screenObj_ = box(lv_screen_active(), 0, 0, screen_.width, screen_.height);
    const std::vector<std::string> names = {
        "menu",         "back",     "up",         "down",   "plus",
        "stop",         "settings", "context-summary", "camera", "album",
        "newchat",      "trash",    "pin",        "gauge",  "chevron-right",
        "chevron-down", "shift",    "shift-lock", "delete", "enter",
        "left",         "retry"};
    for (size_t i = 0; i < names.size(); ++i) {
      const int x = 8 + int(i % 11) * 86, y = 8 + int(i / 11) * 126;
      auto object = icon(screenObj_, names[i], x + 19, y, 48, 48, {}, theme::surface);
      label(screenObj_, names[i], x, y + 53, 84, 12, theme::muted);
      auto small =
          icon(screenObj_, names[i], x + 29, y + 76, 28, 28, {}, theme::surface);
      if (i % 3 == 0)
        lv_obj_set_style_opa(small, LV_OPA_50, 0);
      (void)object;
    }
    return;
  }
  if (type == "camera.test") {
    beginCrop(m.at("photo"));
    return;
  }
  if (type == "scanner.test") {
    scanner_.test(m);
    return;
  }
#endif
  if (type == "init") {
    bridge_.generation(m.value("generation", 0));
    bridge_.send("ready");
    syncBackPolicy();
    syncScanner();
    return;
  }
  if (m.value("generation", bridge_.generation()) != bridge_.generation())
    return;
  if (type == "display.area") {
    if (m.value("x", 0) != 0 || m.value("y", 0) != 0 ||
        m.value("width", screen_.width) != screen_.width ||
        m.value("height", screen_.height) != screen_.height)
      throw std::runtime_error("Display area does not match Falcon screen configuration");
    resetPull();
    input_.cancel();
    bridge_.send("display.ready", {{"requestId", m.value("requestId", Json())},
                                   {"width", screen_.width},
                                   {"height", screen_.height},
                                   {"rotation", screen_.rotation},
                                   {"xoffset", screen_.xoffset},
                                   {"yoffset", screen_.yoffset}});
  }
  if (type == "touch.debug")
    lastTouch_ = m;
  if (type == "touch" && visible_ && !keyboard_) {
    if (imageStoreBusy_) return;
    const bool direct=m.value("source",std::string())=="direct-image";
    if(!direct && directImageInput_) {
      if(m.value("cancelled",false)){imageInput_.cancel();finishImageGesture(false);input_.cancel();resetBack();}
      return;
    }
    if(!direct && ticks()<imageInputExitDue_)return;
    lastTouch_ = m;
    const bool multi=m.contains("points") && m["points"].is_array() && m["points"].size()>1;
    if(multi || nonImageMulti_ || imageGesture_.multi || imageGesture_.mode<0) {
      resetBack();
      if(imageViewer_ || view_=="crop") routeTouch(m);
      else {nonImageMulti_=m.value("pressed",false);auto cancel=m;cancel["cancelled"]=true;cancel["pressed"]=false;routeTouch(cancel);}
    } else if (!backTouch(m)) routeTouch(m);
  } else if (type == "visibility")
    suspend(!m.value("visible", false));
  else if (type == "keyboard.result") {
    if (!keyboard_ || m.value("requestId", std::string()) != keyboardId_)
      return;
    keyboard_ = false;
    if (editor_.active) {
      if (m.value("confirmed", false)) {
        auto text = m.value("text", std::string());
        if (editLength(text) <= editor_.limit) { editor_.text = text; editor_.preedit.clear(); editorFinish(true); }
        else { editor_.notice = "官方输入超过长度上限，请调整";editorPolicy(true);editorBuild();editorOcrStart(); }
      } else {editorPolicy(true);editorChanged();editorFollowCursor_=false;editorBuild();editorOcrStart();}
    }
    display_.visible(visible_);
  } else if(type=="navigation.back") {
    systemBack(m);
  } else if(type=="navigation.policy") {
    if(m.value("sequence",uint64_t(0))==backPolicySequence_) {
      backPolicyOk_=m.value("ok",false); backPolicyBlocked_=m.value("blocked",false);
    }
  } else if(type=="editor.policy") {
    if(editor_.active && m.value("session",std::string())==editor_.id && m.value("sequence",uint64_t(0))==editorPolicySequence_) {
      editorTopBlocked_=m.value("blocked",false);
      if(!m.value("ok",false)){editor_.notice="当前固件无法禁用下拉面板";editorBuild();}
    }
  } else if(type=="ocr.status") {
    if(editor_.active&&editorOcrActive_&&m.value("requestId",std::string())==editorOcrId_&&m.value("session",std::string())==editor_.id) {
      editorOcrBusy_=m.value("started",false);if(editorOcrBusy_){editorOcrShown_=true;editor_.notice.clear();}editorBuild();
    }
  } else if(type=="ocr.result") {
    editorOcrResult(m);
  } else if (type == "diagnostic") {
    auto state = diagnostics();
    state["summary"] = db_.summary();
    bridge_.send("diagnostic", state);
  } else if (type == "exit")
    suspend(true);
  syncScanner();
}
void App::tick() {
  const bool wanted=visible_ && !keyboard_ && (imageViewer_ || view_=="crop");
  auto touches=imageInput_.poll(wanted);
  if(directImageInput_ && !imageInput_.active())imageInputExitDue_=ticks()+80;
  directImageInput_=imageInput_.active();
  for(const auto &touch:touches){
    if(!visible_ || keyboard_ || (!imageViewer_ && view_!="crop"))break;
    handle(touch);
  }
  imageReturnTick();
  for (const auto &saved : db_.saveEvents()) {
    if (saved.type == "message_saved") {
      const auto session = saved.data.value("session", std::string());
      const auto message = saved.data.value("message", std::string());
      if ((view_ == "session_info" || (view_ == "chat" && attachments_ && popupKind_ == "attachments")) &&
          sessionInfoSession_ == session) refreshSessionInfo();
      if (saved.data.value("status", std::string()) == "complete") {
        historyDirty_=true; scheduleTitle(session, message);
      }
      if (message == assistant_ &&
          saved.data.value("generation", -1) == networkGeneration_) {
        saveInFlight_ = false;
        generating_ = false;
        if (view_ == "chat") {
          build();
          if (saved.data.value("status", std::string()) == "error") {
            modal("生成失败", notice_);
            button(modal_, "重试", screen_.width - 325, screen_.height - 66, 100, 44,
                   [this, message, session] {
                     events_.push("retry_request",
                                  {{"id", message}, {"session", session}});
                   });
          }
        }
      }
    } else if (saved.type == "setting_error") {
      const auto message=std::string("中文布局偏好未能保存，本次选择仍保留。");
      if(editor_.active){editor_.notice=message;editorBuild();}else error(message);
    } else if (saved.type == "save_error") {
      notice_ = saved.data.value("error", std::string("保存失败"));
      modal("内容尚未保存", notice_);
      button(modal_, "重试保存", screen_.width - 335, screen_.height - 66, 120, 44,
             [this] {
               db_.retrySaves();
               if (modal_)
                 lv_obj_delete(modal_);
             });
    }
  }
  editorTick();
  if (afterScanRelease_ && scanner_.released()) {
    auto callback = std::move(afterScanRelease_);
    afterScanRelease_ = {};
    callback();
  }
  syncScanner();
  for (auto &e : events_.drain()) {
    if (e.type.compare(0, 8, "context_") == 0) {
      contextEvent(e);
      continue;
    }
    if (e.type.compare(0, 9, "transfer_") == 0) {
      transferEvent(e);
      continue;
    }
    if (e.type == "image_stored") {
      if (imageStoreWorker_.joinable())
        imageStoreWorker_.join();
      imageStoreBusy_ = false;
      if (modal_)
        lv_obj_delete(modal_);
      auto callback = std::move(imageStoreCompletion_);
      imageStoreCompletion_ = {};
      bool ok = e.data.value("ok", false);
      auto editToken=e.data.value("edit_token",std::string());
      if(!editToken.empty()&&ok) {
        auto asset=e.data.at("asset");
        if(editToken==messageEditToken_&&!messageEdit_.empty()) {
          messageEditImages_.push_back(asset);
          for(auto field:{"stored_path","display_src"})if(asset.contains(field))messageEditFiles_.push_back(asset[field].get<std::string>());
        } else {for(auto field:{"stored_path","display_src"})if(asset.contains(field))unlink(asset[field].get<std::string>().c_str());ok=false;}
      }
      if (callback)
        callback(ok);
      if (!ok)
        error("图片保存失败，请检查存储空间后重试");
      continue;
    }
    if (e.type == "pinyin.result") { editorResult(e.data); continue; }
    if (e.type == "pinyin.notice") { notice_ = e.data.value("message",std::string()); if(editor_.active){editor_.notice=notice_;editorBuild();}else error(notice_);continue; }
    if (e.type == "pinyin_clear_confirm") {if(view_=="keyboard_settings"){input_.cancel();modal("正在清除拼音记忆","请稍候…");pinyin_.clear();}continue;}
    if (e.type == "pinyin.cleared") {if(view_=="keyboard_settings"){build();if(e.data.value("ok",false))modal("拼音记忆","已恢复基础词典排序。");else modal("未能清除拼音记忆","文件未能保存，请检查可用空间后重试。");}continue;}
    if (e.type == "editor_action") { if (editor_.active && visible_ && !keyboard_ && e.data.value("id",std::string()) == editor_.id) editorAction(e.data.value("action",std::string())); continue; }
    if (e.type.find("scan_") == 0) {
      scanEvent(e);
      continue;
    }
    if(replyEvent(e))continue;
    if (sessionInfoEvent(e)) continue;
    if(messageEvent(e))continue;
    if (e.type == "image_view_close") {closeImageViewer();continue;}
    if (e.type == "image_view_request") {
      if ((view_ == "chat"||view_=="archive_chat") && visible_ && !keyboard_ && !editor_.active && !modal_ &&
          !navigationOpen_ && !imageViewer_ && e.data.value("session",std::string()) == (view_=="archive_chat"?archiveReaderId_:session_))
        openImageViewer(e.data.value("path",std::string()));
      continue;
    }
    if (e.type == "image_delete_request") {
      auto id = e.data.value("message", std::string());
      if (view_ == "chat" && visible_ && !contextBusy_ && !navigationOpen_ &&
          !attachments_ && !modal_ && !keyboard_ &&
          e.data.value("session", std::string()) == session_ &&
          ((!messageEdit_.empty()&&std::any_of(messageEditImages_.begin(),messageEditImages_.end(),[&](const Json &a){return a["edit_id"]==id;}))||db_.pendingImage(session_, id))) {
        input_.cancel();
        modal("删除图片", "删除这张待发送图片？系统相册原图不会删除。");
        imageDeleteId_ = id;
        lv_obj_add_event_cb(
            modal_,
            [](lv_event_t *event) {
              static_cast<App *>(lv_event_get_user_data(event))
                  ->imageDeleteId_.clear();
            },
            LV_EVENT_DELETE, this);
        auto close = lv_obj_get_child(modal_, -1);
        lv_obj_set_height(close, 44);
        lv_label_set_text(lv_obj_get_child(close, 0), "取消");
        const auto session = session_;
        auto remove = button(
            modal_, "删除", screen_.width - 325, screen_.height - 66, 100, 44,
            [this, session, id] {
              events_.push("image_delete_confirm",
                           {{"session", session}, {"message", id}});
            },
            theme::danger);
        lv_obj_set_style_text_color(lv_obj_get_child(remove, 0), lv_color_hex(theme::background), 0);
      }
    } else if (e.type == "image_delete_confirm") {
      if (view_ == "chat" && visible_ && modal_ &&
          e.data.value("session", std::string()) == session_ &&
          e.data.value("message", std::string()) == imageDeleteId_) {
        input_.cancel();
        if(!messageEdit_.empty()) {
          const auto id=e.data.value("message",std::string());
          messageEditImages_.erase(std::remove_if(messageEditImages_.begin(),messageEditImages_.end(),[&](const Json &a){return a["edit_id"]==id;}),messageEditImages_.end());build();
        } else if (db_.removePendingImage(session_,e.data.value("message", std::string()))) {
          messageOffset_ = 0;
          build();
        } else {
          modal("无法删除", "图片已进入发送请求，不能从待发送区删除。");
        }
      }
    } else if (e.type == "retry_request") {
      if (view_ == "chat" && visible_ && !keyboard_ &&
          e.data.value("session", std::string()) == session_) {
        input_.cancel();
        retryWarning(e.data.value("id", std::string()));
      }
    } else if (e.type == "reasoning_toggle") {
      if(view_=="archive_chat"&&e.data.value("session",std::string())==archiveReaderId_) {
        input_.cancel();lv_obj_stop_scroll_anim(body_);
        archiveReasoningOverrides_[e.data["id"].get<std::string>()]=e.data["expanded"].get<bool>();
        build();
      } else if (view_ == "chat" &&
          e.data.value("session", std::string()) == session_) {
        input_.cancel();
        lv_obj_stop_scroll_anim(body_);
        reasoningOverrides_[e.data["id"].get<std::string>()] =
            e.data["expanded"].get<bool>();
        chatFollowing_ = false;
        renderMessages(false);
      }
    } else if (e.type == "title_model_detail") {
      if (view_ == "settings" && visible_ && popupKind_ == "title_models") {
        for (const auto &model : db_.models()) {
          if (model["id"] == e.data.value("id", std::string())) {
            input_.cancel();
            modal("命名模型", model.value("name", std::string()));
            break;
          }
        }
      }
    } else if (e.type == "history_time_display") {
      input_.cancel();db_.setting("history_time_display",e.data.at("mode"));build();
    } else if (e.type == "reasoning_display") {
      input_.cancel();
      db_.setting("reasoning_display", e.data.at("mode"));
      reasoningOverrides_.clear();
      build();
    } else if (e.type == "pull_sensitivity_apply" && view_ == "settings" && visible_) {
      input_.cancel();
      const auto offset = pullSensitivityOffset(e.data.value("offset", 0));
      try {
        db_.setting("pull_sensitivity_offset", offset);
        pullSensitivityOffset_ = offset;
        build();
      } catch (...) { modal("未能保存灵敏度", "本次调整未生效，请检查存储空间后重试。"); }
    } else if (e.type == "reading_font_apply" && view_ == "font_size") {
      input_.cancel();
      db_.setting("reading_font_percent", readingFontDraft_);
      readingFontPercent_ = readingFontDraft_;
      rich_.clearCache();
      tableScrolls_.clear();
      show("settings");
    } else if(e.type=="archive_page"&&view_=="archive_chat"&&e.data.value("id",std::string())==archiveReaderId_) {
      input_.cancel();archiveReaderOffset_=std::max(0,e.data.value("offset",0));build();
    } else if(e.type=="archive_open"&&view_=="archives") {
      input_.cancel();openArchiveReader(e.data.value("id",std::string()));
    } else if (e.type == "history_details" && view_=="archives") {
      input_.cancel();auto session=db_.sessionInfo(e.data.value("id",std::string()));
      if(!session.empty())modal("会话详情",session.value("title",std::string())+"\n\n"+
          (sessionChatTime(session)?"最后聊天："+historyTime(sessionChatTime(session),nowMs(),true):"没有已记录的聊天时间"));
    } else if (e.type == "history_context") {
      auto id = e.data.value("id", std::string());
      if (navigationOpen_ && !modal_ && !keyboard_ &&
          longPressedSession_ == id) {
        input_.cancel();
        historyContext(id);
      }
    } else if (e.type == "title_result") {
      if(replyBusy_||contextRegenerating_){events_.push(e.type,e.data);continue;}
      auto title = db_.settings().value("auto_title_enabled", true)
                       ? fitTitle(e.data.value("title", std::string()))
                       : std::string();
      if (db_.finishTitle(e.data["session"], e.data["token"], title))
        historyDirty_ = true;
    } else if (e.type == "response_state" || e.type == "done" ||
               e.type == "cancelled" || e.type == "network_error") {
      if (e.data.value("generation", -1) != networkGeneration_ || !generating_)
        continue;
      if (e.type == "response_state") {
        output_ = e.data.value("output", std::string());
        reasoning_ = e.data.value("reasoning", std::string());
        reasoningMetadata_ = e.data.value("metadata", Json::object());
      }
      dirty_ = true;
      persistDirty_ = true;
      if (e.type == "done" || e.type == "cancelled" ||
          e.type == "network_error") {
        dirty_ = false;
        saveInFlight_ = true;
        auto status = e.type == "done"        ? "complete"
                      : e.type == "cancelled" ? "cancelled"
                                              : "error";
        Json err = e.type == "network_error" ? e.data["error"] : Json();
        persistReply(status, err);
        if (e.type == "network_error")
          notice_ = e.data.value("error", std::string("网络错误"));
      }
    } else if (e.type == "models") {
      if (e.data.value("generation", -1) != modelGeneration_)
        continue;
      modelNames_.clear();
      if (e.data.value("status", 0) >= 200 && e.data.value("status", 0) < 300 &&
          e.data.contains("body") && e.data["body"].is_object() &&
          e.data["body"].contains("data") && e.data["body"]["data"].is_array()) {
        for (auto &m : e.data["body"]["data"])
          if (m.contains("id") && m["id"].is_string())
            modelNames_.push_back(m["id"]);
        notice_ = "已获取 " + std::to_string(modelNames_.size()) + " 个模型";
      } else
        notice_ = "拉取失败：" + e.data.value("error", std::string());
      if (view_ == "provider")
        build();
    } else if (e.type == "image_ready" || e.type == "image_error") {
      refreshImages(); refreshImageViewer();
    }
    else if (e.type == "camera_mode" && view_ == "camera" && visible_ && !cameraCapturing_)
      cameraMode(e.data.value("auto",false),e.data.value("angle",0));
    else if (e.type == "camera_orientation" && view_ == "camera")
      pendingCameraAngle_ = e.data.value("angle",0);
    else if (e.type == "camera_sensor")
      cameraSensorAvailable_ = e.data.value("available",false);
    else if (e.type == "crop_cancel" && view_=="crop" && !cropBusy_)
      show("camera");
    else if (e.type == "crop_view" && view_ == "crop" && !cropBusy_)
      cropView(e.data.value("action",std::string()));
    else if (e.type == "camera_error" && view_ == "camera") {
      cameraCapturing_=false;
      cameraError_=e.data.value("error", std::string("相机失败"));
      error(cameraError_);
    }
    else if (e.type == "camera_started" && view_ == "camera") {
      if (status_)
        lv_label_set_text(status_, "");
    } else if (e.type == "photo") {
      if (view_ == "camera" && visible_ && !navigationOpen_)
        beginCrop(e.data);
      else
        unlink(e.data.value("path", std::string()).c_str());
    } else if (e.type == "crop_confirm" && view_ == "crop" && !cropBusy_ && cropLoaded_)
      confirmCrop();
    else if (e.type == "crop_done" && view_ == "crop" && cropBusy_ &&
             e.data.value("generation",-1)==cropGeneration_) {
      if (cropWorker_.joinable()) cropWorker_.join();
      cropBusy_ = false;
      if (cropResult_.contains("error")) {
        build();
        error(cropResult_["error"].get<std::string>());
      } else {
        try {
          auto model = currentModel();
          bindPreferences(model);
          const auto session = session_;
          storeImage(cropResult_, [this, session](bool ok) {
            if (!ok || session != session_)
              return;
            clearCrop(true);
            chatFollowing_ = true;
            messageOffset_ = 0;
            show("chat");
          });
        } catch (const std::exception &failure) {
          build();
          error(failure.what());
        }
      }
    }
  }
  syncBackPolicy();
  syncReplyControls();
  if(!replyBusy_&&!contextRegenerating_)tickTitles();
  if (persistDirty_ && !saveInFlight_ && ticks() >= persistDue_)
    persistReply("streaming");
  if (dirty_ && ticks() >= renderDue_) {
    renderDue_ = ticks() + 100;
    dirty_ = false;
    if (view_ == "chat")
      renderLiveMessage();
  }
  if (view_ == "camera" && cameraAutomatic_ && pendingCameraAngle_>=0 && !cameraCapturing_ && !touchDown_ && visible_) {
    cameraAngle_=pendingCameraAngle_;pendingCameraAngle_=-1;build();
  }
  if (view_ == "camera" && visible_ && !keyboard_ && !navigationOpen_ &&
      ticks() - lastCameraFrame_ >= 60) {
    lastCameraFrame_ = ticks();
    auto frame = camera_.frame();
    if (frame && cameraImage_ && frame->rotation == cameraAngle_) {
      texture(cameraImage_, frame);
      if(status_ && cameraError_.empty()) lv_label_set_text(status_, "");
      auto holder = lv_obj_get_parent(cameraImage_);
      lv_image_set_scale(
          cameraImage_,
          std::min(lv_obj_get_width(holder) * 256 / frame->width,
                   lv_obj_get_height(holder) * 256 / frame->height));
      lv_obj_center(cameraImage_);
    }
  }
}
Json App::diagnostics() const {
  Json transferDiagnostic = Json::object();
  if (view_ == "transfer") {
    for (auto key : {"path", "added", "skipped", "count"})
      if (transferData_.contains(key))
        transferDiagnostic[key] = transferData_[key];
    if (transferData_.contains("items") && transferData_["items"].is_array()) {
      transferDiagnostic["items"] = Json::array();
      for (const auto &item : transferData_["items"]) {
        Json row = Json::object();
        for (auto key : {"id", "name", "title", "bytes"})
          if (item.contains(key))
            row[key] = item[key];
        transferDiagnostic["items"].push_back(row);
      }
    }
  }
  Json tableRows = Json::array();
  for (const auto &r : rasters_)
    if (r.tableId) {
      lv_area_t area;
      lv_obj_get_coords(r.holder, &area);
      tableRows.push_back({{"id", r.tableId},
                           {"y", area.y1},
                           {"scroll", lv_obj_get_scroll_x(r.holder)},
                           {"width", r.contentWidth}});
    }
  Json photoBounds = Json::array();
  for (auto object : chatPhotos_) {
    auto holder = lv_obj_get_parent(object);
    lv_area_t bounds;
    lv_obj_get_coords(holder, &bounds);
    photoBounds.push_back(
        {{"x", bounds.x1},
         {"y", bounds.y1},
         {"width", lv_obj_get_width(holder)},
         {"height", lv_obj_get_height(holder)},
         {"radius", lv_obj_get_style_radius(holder, LV_PART_MAIN)}});
  }
  auto model = db_.activeModel();
  auto selected = sessionPreferences_.value("model_id", std::string());
  if (!selected.empty()) {
    model = Json::object();
    for (const auto &m : db_.models())
      if (m["id"] == selected) {
        model = m;
        break;
      }
  }
  if (!model.empty())
    model = resolveReasoning(model, sessionPreferences_, effortMemory_);
  lv_area_t reasonHeader{};
  if (!reasoningHeaders_.empty())
    lv_obj_get_coords(reasoningHeaders_.back(), &reasonHeader);

  lv_area_t active{}, capsule{}, send{};
  if (chatControls_) {
    lv_obj_get_coords(chatControls_, &capsule);
    lv_obj_get_coords(lv_obj_get_child(chatControls_, -1), &send);
  }
  if (navigationActive_)
    lv_obj_get_coords(navigationActive_, &active);
  Json historyRows = Json::array();
  for (const auto &entry : historyRows_) {
    lv_area_t area;
    lv_obj_get_coords(entry.second, &area);
    historyRows.push_back({{"id", entry.first},
                           {"x", (area.x1 + area.x2) / 2},
                           {"y", (area.y1 + area.y2) / 2}});
  }
  auto bounds=[](lv_obj_t *object)->Json {if(!object)return Json();lv_area_t a;lv_obj_get_coords(object,&a);return Json::array({a.x1,a.y1,a.x2,a.y2});};
  Json historyCaptions=Json::object(),historyActions=Json::object();
  for(const auto &entry:historyCaptions_) {
    auto title=entry.second.title;auto time=entry.second.time;
    historyCaptions[entry.first]={{"title_bounds",bounds(title)},{"time_bounds",bounds(time)},
       {"title_length",editLength(lv_label_get_text(title))},{"marquee",lv_label_get_long_mode(title)==LV_LABEL_LONG_MODE_SCROLL_CIRCULAR},
       {"time",time?lv_label_get_text(time):""}};
  }
  for(const auto &entry:historyActions_)historyActions[entry.first]=bounds(entry.second);
  Json attachmentActions = Json::object();
  for (const auto &entry : attachmentEntries_) attachmentActions[entry.first] = bounds(entry.second);
  Json archiveViews=Json::object();
  for(const auto &entry:archiveViewButtons_){lv_area_t a;lv_obj_get_coords(entry.second,&a);archiveViews[entry.first]={a.x1,a.y1,a.x2,a.y2};}

  return {
      {"editor", editorDiagnostic()},
      {"draft_length", editLength(draft_)},
      {"storage", db_.storageStats()},
      {"diagnostic_time", ticks()},
      {"history_rows", historyRows},
      {"history_captions",historyCaptions},{"history_actions",historyActions},
      {"history_details",{{"title_length",historyFullTitle_?editLength(lv_label_get_text(historyFullTitle_)):0},{"time",historyFullTime_?lv_label_get_text(historyFullTime_):""}}},
      {"history_context", historyContextId_},
      {"title_busy", titleBusy_.load()},
      {"title_pending", titleJobs_.size()},
      {"title_limit", titleLimit()},
      {"title_width", titleWidth()},
      {"generation", bridge_.generation()},
      {"pid", getpid()},
      {"touch", lastTouch_},
      {"view", view_},
      {"provider_protocol", providerForm_.is_object() ? providerForm_.value("api", std::string()) : std::string()},
      {"anthropic_connection", view_ == "anthropic_connection" ? objectField(providerForm_, "anthropic") : Json::object()},
      {"model_parameters", view_ == "model" && anthropicProtocol(modelForm_) ?
          Json({{"reasoning", modelForm_["reasoning"]}, {"maximum", modelForm_.value("max_output_tokens", anthropicMaxOutput(modelForm_))}}) : Json::object()},
      {"transfer_screen", transferScreen_},
      {"transfer_busy", transfer_ && transfer_->busy()},
      {"transfer_error", transferError_},
      {"transfer_data", transferDiagnostic},
      {"scanner", scanner_.state()},
      {"scan_id", scanId_},
      {"scan_result", scanResult_},
      {"scan_released", scanner_.released()},
      {"send_center_x2", send.x1 + send.x2},
      {"send_center_y2", send.y1 + send.y2},
      {"capsule_endcap_x2", 2 * capsule.x2 - (capsule.y2 - capsule.y1)},
      {"capsule_center_y2", capsule.y1 + capsule.y2},
      {"navigation_open", navigationOpen_},
      {"session_info", view_ == "session_info" ? sessionInfoData_ : Json::object()},
      {"session_info_busy", sessionInfoBusy_},
      {"context_usage", contextUsageDiagnostic()},
      {"attachment_actions", attachmentActions},
      {"attachment_viewport", attachmentEntries_.empty() ? Json() : bounds(lv_obj_get_parent(attachmentEntries_.begin()->second))},
      {"archive_views",archiveViews},
      {"archive_reader",{{"id",archiveReaderId_},{"offset",archiveReaderOffset_},{"messages",archiveReaderMessages_}}},
      {"navigation_active_x", navigationActive_ ? (active.x1 + active.x2) / 2 : -1},
      {"navigation_active_y", navigationActive_ ? (active.y1 + active.y2) / 2 : -1},
      {"navigation_scroll", navigationList_ ? lv_obj_get_scroll_y(navigationList_) : 0},
      {"message_interactions",messageDiagnostic()},
      {"reply_actions",replyDiagnostic()},
      {"navigation_button", view_ == "chat" ? (messageEdit_.empty()?"menu":"close") : "back"},
      {"pull_indicator_y", pullIndicator_ ? lv_obj_get_y(pullIndicator_) : -1},
      {"pull_content_y", body_ ? lv_obj_get_style_translate_y(body_, LV_PART_MAIN) : 0},
      {"following_latest", chatFollowing_},
      {"attachments", attachments_ != nullptr},
      {"popup", popupKind_},
      {"has_conversation_content", hasConversationContent_},
      {"effort_gauge", sessionPreferences_.value("gauge", !effortMemory_.empty())},
      {"session_preferences", sessionPreferences_},
      {"active_model", model.value("id", std::string())},
      {"effective_effort", model.value("effective_effort", std::string())},
      {"supported_efforts",
       model.empty() ? std::vector<std::string>() : supportedEfforts(model)},
      {"effort_choices",
       model.empty() ? std::vector<std::string>() : effortChoices(model)},
      {"reasoning_header_count", reasoningHeaders_.size()},
      {"reasoning_header_x", reasonHeader.x1 + 120},
      {"reasoning_header_y", (reasonHeader.y1 + reasonHeader.y2) / 2},
      {"reasoning_observed", reasoningMetadata_.value("reasoning_observed", false)},
      {"reasoning_expanded_count", expandedReasoning_.size()},
      {"camera_ready",
       cameraImage_ && textures_.count(cameraImage_) && cameraError_.empty()},
      {"camera_angle", cameraAngle_},
      {"camera_auto", cameraAutomatic_},
      {"camera_sensor", cameraSensorAvailable_},
      {"crop_source", {cropPhoto_.value("width", 0), cropPhoto_.value("height", 0)}},
      {"crop_zoom", cropZoom_},
      {"crop_pan", {cropPanX_, cropPanY_}},
      {"crop_viewport", {cropViewportW_, cropViewportH_}},
      {"crop_loaded", cropLoaded_},
      {"crop_viewport_bounds", cropViewport_ ? bounds(cropViewport_) : Json()},
      {"crop_actions", [&]{Json a=Json::object();for(const auto &i:cropActions_)a[i.first]=bounds(i.second);return a;}()},
      {"crop_selection", {cropX_,cropY_,cropW_,cropH_}},
      {"crop_source_rect", cropSourceRect()},
      {"image_touch", {{"peak",imageTouchPeak_},{"pinch_updates",imagePinchUpdates_},{"mode",imageGesture_.mode},{"source",directImageInput_?"direct-read-only":"falcon"}}},
      {"crop_rect", {cropX_, cropY_, cropW_, cropH_}},
      {"crop_preview", {cropPreviewW_, cropPreviewH_}},
      {"crop_save_album", cropSaveAlbum_},
      {"crop_busy", cropBusy_},
      {"chat_images", photoBounds},
      {"table_rows", tableRows},
      {"to_bottom_x", toBottom_ ? lv_obj_get_x(toBottom_) : -1},
      {"to_bottom_y", toBottom_ ? lv_obj_get_y(toBottom_) : -1},
      {"to_bottom_opacity",
       toBottom_ ? int(lv_obj_get_style_bg_opa(toBottom_, LV_PART_MAIN)) : 0},
      {"modal", modal_ != nullptr},
      {"pull_active", pulling_},
      {"pull_create_allowed", pulling_ && pullCreateAllowed_},
      {"pull_progress", pullProgress_},
      {"pull_fill_distance", pullGeometry_.fill},
      {"pull_sensitivity_offset", pullSensitivityOffset_},
      {"pull_frozen_offset", pullGeometry_.sensitivityOffset},
      {"pull_preferences", {{"slider", bounds(pullSlider_)},
                            {"draft", pullSlider_ ? Json(lv_slider_get_value(pullSlider_)) : Json()},
                            {"minimum", -25}, {"maximum", 25}}},
      {"pull_start_distance", pullGeometry_.start},
      {"pull_full_distance", pullGeometry_.start + pullGeometry_.fill},
      {"pull_reader_height", pullGeometry_.reader.bottom - pullGeometry_.reader.top},
      {"pull_pause_ms", PullPauseMs},
      {"pull_guard_remaining_ms",
       pullNeedsPause_
           ? std::max<int64_t>(0, PullPauseMs - (ticks() - pullLastActivity_))
           : 0},
      {"scroll_y", body_ ? lv_obj_get_scroll_y(body_) : 0},
      {"settings", settingsDiagnostic()},
      {"reading_font", {{"percent", readingFontPercent_}, {"size", readingFontSize()},
                        {"draft", readingFontDraft_}, {"slider", bounds(fontSlider_)},
                        {"preview", bounds(fontPreview_)}}},
      {"page", pageDiagnostic()},
      {"image_viewer", imageViewerDiagnostic()},
      {"back_navigation", {{"root",navigationRoot()},{"sequence",backPolicySequence_},{"policy_ok",backPolicyOk_},{"blocked",backPolicyBlocked_},{"claimed",backClaimed_}}},
      {"scroll_bottom", body_ ? lv_obj_get_scroll_bottom(body_) : 0},
      {"composer_width", composer_ ? lv_obj_get_width(composer_) : 0},
      {"toolbar_y", chatControls_ ? lv_obj_get_y(chatControls_) : -1},
      {"to_bottom_visible", toBottom_ && !lv_obj_is_hidden(toBottom_)},
      {"session", session_},
      {"generating", generating_},
      {"compressing", contextBusy_},
      {"keyboard", keyboard_},
      {"ocr", editorOcrActive_},
      {"visible", visible_},
      {"width", screen_.width},
      {"height", screen_.height},
      {"rotation", screen_.rotation}};
}
void App::demo(const std::string &markdown) {
  db_.append(session_, "assistant", markdown);
  show("chat");
}
} // namespace penchat
