#include "attachment_layout.hpp"
#include "app.hpp"
#include "back_geometry.hpp"
#include "surface_motion.hpp"
#include "refresh_settings.hpp"
#include "empty_state.hpp"
#include "selection_menu.hpp"
#include "ui_motion.hpp"
#include "ui_scale_settings.hpp"
#include "ui_motion.hpp"
#include "chat_text.hpp"
#include "chat_time.hpp"
#include "context.hpp"
#include "icons.hpp"
#include "device_profile.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "tools.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
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
  return hasChatText(text);
}
std::string basename(const std::string &p) {
  auto slash = p.rfind('/');
  return p.substr(slash == std::string::npos ? 0 : slash + 1);
}
} // namespace
App::App(Database &db, Bridge &bridge, Display &display, Input &input,
         const Screen &s, std::string workspace, std::string dataDir,
         const std::string &fontPath, const std::string &resources, bool nativeTouch, const Json &configuration, const UiMetrics &metrics)
    : uiMetrics_(metrics), db_(db), bridge_(bridge), display_(display), input_(input), screen_(s), touchInput_(s, nativeTouch),
      pinyin_(events_, join(workspace, "assets/pinyin"), dataDir),
      images_(events_), camera_(events_, join(dataDir, "camera-tmp"), workspace, s.height > s.width),
      scanner_(events_, workspace, dataDir),
      rich_(fontPath, resources,
            join(workspace, "assets/fonts/DejaVuSans.ttf"), uiMetrics_),
      workspace_(std::move(workspace)), dataDir_(std::move(dataDir)) {
  editorMultiLineAllowed_=camera::deviceCapabilities().multiLineOcr;
#ifndef PENCHAT_NATIVE_DEVICE
  if(const char *test=std::getenv("PENCHAT_TEST_SINGLE_LINE_OCR"))editorMultiLineAllowed_=std::string(test)!="1";
#endif
  const auto boardModel=fileText("/proc/device-tree/model",4096);
  roundedDisplay_=boardModel.find("RK3562 Y07 ")!=std::string::npos;
#ifndef PENCHAT_NATIVE_DEVICE
  if(const char *test=std::getenv("PENCHAT_TEST_Y07_ROUNDED"))roundedDisplay_=std::string(test)=="1";
#endif
  inputFocus_ = InputFocus(nativeTouch);
  inputFocused_ = touchLeaseActive_ = !nativeTouch;
  initializeMessageEdits();
  feedback_.reset(new Feedback(events_, dataDir_, feedbackScreen(configuration, s), nativeTouch));
  transfer_.reset(new Transfer(db_, events_, dataDir_));
  transfer_->start("init");
  clearCrop();
  auto config = db_.settings();
  uiScaleSaved_=savedUiScale(config);uiScaleDraft_=uiMetrics_.scalePercent();
  readingFontPercent_=savedTextScale(config);readingFontDraft_=readingFontPercent_;
  uiMetrics_=UiMetrics(uiMetrics_.densityPercent(),uiMetrics_.scalePercent(),readingFontPercent_);
  rich_.setMetrics(uiMetrics_);
  refreshSaved_=configuredRefreshRate(config);
  display_.setRefreshRate(savedRefreshRate(config,screen_.width,screen_.height));
  composerShortcut_.configure(stringValue(config, "landscape_composer_shortcut", "auto"));
  wideKeyboardSwap_=config.value("wide_keyboard_swap",false);
  const auto officialKeyboard=config.value("prefer_official_keyboard",Json());
  preferOfficialKeyboard_=officialKeyboard.is_boolean()&&officialKeyboard.get<bool>();
  const auto scanMode=config.value("ocr_multiline",Json());
  ocrMultilinePreferred_=scanMode.is_boolean()&&scanMode.get<bool>();
  const auto quickOfficial=config.value("quick_official_keyboard",Json());
  officialShortcut_=quickOfficial.is_boolean()&&quickOfficial.get<bool>();
  const auto idleSeconds = config.value("http_idle_seconds", Json());
  setHttpIdleSeconds(idleSeconds.is_number_integer() && idleSeconds.get<double>() >= 15 && idleSeconds.get<double>() <= 600
                      ? idleSeconds.get<int>() : 90);
  editor_.preferredChinese=config.value("pinyin_chinese_layout",std::string("zh26"))=="zh9"?2:0;
  const auto savedPull = config.value("pull_sensitivity_offset", Json());
  if (savedPull.is_number_integer())
    pullSensitivityOffset_ = int(std::max(-25.0, std::min(25.0, savedPull.get<double>())));
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
  sessionPreferences_["web_enabled"] = webEnabled(sessionPreferences_, config);
  std::string presetWarning;
  if (db_.sessionInfo(session_).empty()) {
    sessionPreferences_ = applyPresetSnapshot(sessionPreferences_, presetList(config)[0],
                                              db_.models(), db_.activeModel(), true, presetWarning);
  }
  initializeUpdates();
  if(!draft_.empty()) {
    const auto key=db_.sessionInfo(session_).empty()?std::string("new"):session_;
    if(db_.query("SELECT draft_key FROM chat_drafts WHERE draft_key=?",{key}).empty()) {
      auto legacy=db_.draft(key);legacy["revision"]=1;
      db_.queueDraft(legacy,draft_);db_.flush();
    }
  }
  loadDraft(session_);
  initializeOnboarding(config);
  build();sendAppearance();
  if (!onboardingActive_ && !presetWarning.empty()) modal("默认预设已部分应用", presetWarning);
  for (const auto &job : db_.pendingTitles())
    titleJobs_.push_back(job);
}
App::~App() {
  cancelWebReader();
  shutdown();
  clear();
}
bool App::keyboardControl(lv_obj_t *parent) const {
  for (auto ancestor = parent; editorRoot_ && ancestor; ancestor = lv_obj_get_parent(ancestor))
    if (ancestor == editorRoot_) return true;
  return false;
}
const UiMetrics &App::controlMetrics(lv_obj_t *parent) const {
  return keyboardControl(parent) ? editorMetrics() : uiMetrics_;
}
lv_obj_t *App::label(lv_obj_t *parent, const std::string &text, int x, int y,
                     int w, UiFont role, uint32_t color) {
  return label(parent, text, x, y, w, controlMetrics(parent).fontSize(role), color);
}
lv_obj_t *App::box(lv_obj_t *parent, int x, int y, int w, int h,
                   uint32_t color) {
  return box(parent, x, y, w, h, color, controlMetrics(parent));
}
lv_obj_t *App::box(lv_obj_t *parent, int x, int y, int w, int h,
                   uint32_t color, const UiMetrics &metrics) {
  auto o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(o, 255, 0);
  lv_obj_set_style_text_font(o, font(metrics.fontSize(UiFont::Body)), 0);
  lv_obj_set_style_text_color(o, lv_color_hex(theme::palette().text), 0);
  return o;
}
lv_obj_t *App::label(lv_obj_t *parent, const std::string &text, int x, int y,
                     int w, int size, uint32_t color) {
  if(size<0)size=controlMetrics(parent).fontSize(UiFont::Body);
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
  const bool keyboard = keyboardControl(parent);
  const UiMetrics metrics = keyboard ? editorMetrics() : uiMetrics_;
  const auto style = metrics.button();
  auto o = box(parent, x, y, w, h, color, metrics);
  lv_obj_set_scrollable(o, false);
  lv_obj_set_style_radius(o, style.radius, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(theme::palette().border), LV_STATE_PRESSED);
  std::string caption=text;int checked=-1;
  if(!keyboard)for(const auto &prefix:{std::make_pair(std::string("● "),true),
      {"○ ",false},{"✓ ",true},{"□ ",false}}) {
    if(caption.compare(0,prefix.first.size(),prefix.first)==0) {
      checked=prefix.second;caption.erase(0,prefix.first.size());break;
    }
  }
  const auto captionFont = font(metrics.fontSize(UiFont::Button));
  if(!keyboard&&metrics.textPercent()!=100)
    lv_obj_set_height(o,std::max(h,int(captionFont->line_height)+2*metrics.dp(4)));
  auto l = label(o, caption, 0, 0, std::max(1, w - style.captionPadding), metrics.fontSize(UiFont::Button));
  // A fixed-height action must never center an arbitrarily tall wrapped label.
  lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(l, captionFont->line_height);
  lv_obj_set_style_text_align(l, align, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
  if(!keyboard)styleDisabledButton(o);
  if(checked>=0)selected(o,checked);
  if(layout().portrait()) {
    if(!keyboard) {
      inputTarget(o,"button:"+std::to_string(inputOrdinal_++));
      onClick(o,[this,o,click]{inputAction(o,click);});
    } else onClick(o,std::move(click));
  } else onClick(o,std::move(click));
  if(layout().portrait()&&parent==modal_) {modalButtons_.push_back(o);arrangeModal();}
  return o;
}
void App::primary(lv_obj_t *object) {
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::palette().text), 0);
  lv_obj_set_style_bg_color(object, lv_color_hex(theme::palette().muted), LV_STATE_PRESSED);
  lv_obj_set_style_opa(object, LV_OPA_COVER, LV_STATE_DISABLED);
  lv_obj_set_style_text_color(lv_obj_get_child(object, 0), lv_color_hex(theme::palette().primaryText), 0);
}
UiMetrics::Field App::fieldMetrics(const UiMetrics &metrics) const {
  auto field = metrics.field();
  // Preserve the established identity-scale frames, including compact's
  // measured two-line fields. Scaled forms must fit the real font line boxes.
  if (layout().compact() || !metrics.identity() || metrics.textPercent()!=100) {
    field.captionHeight = font(metrics.fontSize(UiFont::FieldTitle))->line_height;
    field.valueHeight = font(metrics.fontSize(UiFont::FieldValue))->line_height;
    field.valueTop = field.top + field.captionHeight + field.gap;
    field.height = std::max(field.height, field.valueTop + field.valueHeight + field.top);
  }
  return field;
}
lv_obj_t *App::fieldButton(lv_obj_t *parent, const std::string &caption,
                            const std::string &value, int x, int y, int width,
                            std::function<void()> action) {
  const auto &metrics = controlMetrics(parent);
  const auto field = fieldMetrics(metrics);
  auto object = button(parent, value, x, y, width, field.height, std::move(action), theme::palette().surface, LV_TEXT_ALIGN_LEFT);
  auto text = lv_obj_get_child(object, 0);
  lv_obj_set_style_text_font(text, font(metrics.fontSize(UiFont::FieldValue)), 0);
  const int captionHeight = field.captionHeight;
  const int valueHeight = field.valueHeight;
  const int valueY = field.valueTop;
  lv_obj_set_height(text, valueHeight);
  lv_obj_align(text, LV_ALIGN_TOP_LEFT, field.inset, valueY);
  auto name = label(object, caption, field.inset, field.top, std::max(1, width - metrics.button().captionPadding), UiFont::FieldTitle, theme::palette().muted);
  lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(name, captionHeight);
  lv_obj_set_clickable(name, false);
  if(layout().portrait())inputTarget(object,"field:"+caption+":"+std::to_string(inputOrdinal_-1),1);
  return object;
}
void App::emptyState(const std::string &title, const std::string &hint) {
  auto parent=pageContent_?pageContent_:body_;
  lv_obj_update_layout(parent);
  const int inset=uiMetrics_.dp(10);
  const int width=std::max(1,int(lv_obj_get_width(parent))-2*inset);
  const int available=std::max(0,int(lv_obj_get_height(body_))-(pageContent_?int(lv_obj_get_y(pageContent_)):0)-uiMetrics_.dp(12));
  statusState(parent,{StatusKind::Empty,title,hint,view_=="album"?"album":"info"},
              uiMetrics_,font(uiMetrics_.fontSize(UiFont::EmptyTitle)),
              font(uiMetrics_.fontSize(UiFont::FormNote)),inset,0,width,available);
}
void App::texture(lv_obj_t *object, std::shared_ptr<ImageFrame> frame,
                  bool premultiplied) {
  if (!frame)
    return;
  auto existing=textures_.find(object);
  if(existing!=textures_.end() && existing->second->frame==frame &&
     bool(existing->second->descriptor.header.flags&LV_IMAGE_FLAGS_PREMULTIPLIED)==premultiplied)return;
  auto t = std::unique_ptr<Texture>(new Texture);
  t->frame = std::move(frame);
  auto &d = t->descriptor;
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = t->frame->opaque?LV_COLOR_FORMAT_XRGB8888:LV_COLOR_FORMAT_ARGB8888;
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
      box(parent, x, y, chatPhoto ? std::min(w, h) : w, h, theme::palette().surface);
  lv_obj_set_scrollable(holder, false);
  lv_obj_set_style_bg_opa(holder, LV_OPA_TRANSP, 0);
  auto placeholder = label(holder, "加载图片…", 0, 0, w, uiMetrics_.fontSize(UiFont::FormHint), theme::palette().muted);
  lv_obj_center(placeholder);
  auto o = imageWidget(holder);
  placeholders_[o] = placeholder;
  if (chatPhoto) {
    chatPhotos_.insert(o);
    lv_obj_set_style_radius(holder, uiMetrics_.dp(12), 0);
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
        if (p->distance <= p->app->uiMetrics_.gestures().tapSquared()) {
          if(!p->message.empty()) {
            const auto area=p->app->readClip(lv_event_get_current_target_obj(event));
            p->app->events_.push("image_delete_request",{{"session",p->session},{"message",p->message},
              {"anchor",Json::array({area.x1,area.y1,area.x2,area.y2})}});
          }

        }
      }
    }, LV_EVENT_ALL, owner);
    if (click) onClick(holder,[state,click] { if (!state->held && state->distance<=state->app->uiMetrics_.gestures().tapSquared()) click(); });
  }
}
lv_obj_t *App::icon(lv_obj_t *parent, const std::string &kind, int x, int y,
                    int w, int h, std::function<void()> action,
                    uint32_t background, uint32_t ink) {
  auto object = box(parent, x, y, w, h, background);
  lv_obj_set_scrollable(object, false);
  lv_obj_set_style_radius(object, std::min(w, h) / 2, 0);
  attachIcon(object, kind, ink, 1.f, uiMetrics_);
  if (action)
    onClick(object, std::move(action));
  else
    lv_obj_set_clickable(object, false);
  return object;
}
Json App::currentModel() {
  auto id = sessionPreferences_.value("model_id", std::string());
  const auto model = id.empty() ? db_.activeModel() : db_.model(id);
  return model.empty()
             ? model
             : resolveReasoning(model, sessionPreferences_, "");
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
  effortMemory_.clear();
  savePreferences();
}
void App::modelPage() {
  if (anthropicProtocol(modelForm_)) { anthropicModelPage(); return; }
  pageHeader("模型设置");
  const int width = layout().contentWidth(), column = (width - uiMetrics_.dp(14)) / 2;
  const auto id = modelForm_.value("id", std::string());
  auto save = pageAction("完成", uiMetrics_.dp(24), uiMetrics_.dp(130), [this, id] {
    try { saveModelForm(); returnProvider(); }
    catch (const std::exception &e) { modal("模型设置未保存", e.what()); }
  });
  primary(save);
  const int identityHeight=modelIdentityFields();
  auto &caps = modelForm_["reasoning"];
  auto &levels = caps["levels"];
  int n = 0;
  for (const auto &key : effortOrder()) {
    if (modelAdvanced_) {
      if (levels.contains(key)) {
        auto control = button(
            pageContent_, effortLabel(key) + "：" + levels[key].get<std::string>(), uiMetrics_.dp(10),
            identityHeight + n * uiMetrics_.dp(48), width, uiMetrics_.dp(44),
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
            theme::palette().surface, LV_TEXT_ALIGN_LEFT);
        pageActions_["mapping:" + key] = control;
        ++n;
      }
    } else {
      auto control = button(
          pageContent_,
          std::string(levels.contains(key) ? "● " : "○ ") + effortLabel(key),
          uiMetrics_.dp(10) + (n % 2) * (column + uiMetrics_.dp(14)), identityHeight + (n / 2) * uiMetrics_.dp(48), column, uiMetrics_.dp(44),
          [this, key] {
            auto &levels = modelForm_["reasoning"]["levels"];
            if (levels.contains(key))
              levels.erase(key);
            else
              levels[key] = key;
            build();
          },
          theme::palette().surface, LV_TEXT_ALIGN_LEFT);
      pageActions_["effort:" + key] = control;
      ++n;
    }
  }
  if (!modelAdvanced_)
    button(
        pageContent_, caps.value("summary", false) ? "公开总结：开" : "公开总结：关",
        uiMetrics_.dp(10), identityHeight + ((n + 1) / 2) * uiMetrics_.dp(48), width, uiMetrics_.dp(44),
        [this] {
          auto &caps = modelForm_["reasoning"];
          caps["summary"] = !caps.value("summary", false);
          build();
        },
        theme::palette().surface, LV_TEXT_ALIGN_LEFT);
  const int capabilityY = modelAdvanced_ ? identityHeight + uiMetrics_.dp(6) + n * uiMetrics_.dp(48) : identityHeight + uiMetrics_.dp(58) + ((n + 1) / 2) * uiMetrics_.dp(48);
  modelCapabilityRows(capabilityY);

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
    // NativeTouch has already applied rotation and touch offsets exactly once.
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
                       chatComposerY(), pullSensitivityOffset_);
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
    const bool floatingNavigation=layout().portrait()&&y<layout().chatReaderTop();
    const bool inPullArea = !floatingNavigation && (inReader || inComposer);
    pullTrackActivity_ = view_ == "chat" && !navigationOpen_ && !modal_ &&
                         !attachments_ && inPullArea;
    pullCreateAllowed_ =
        !pullNeedsPause_ || now - pullLastActivity_ >= PullPauseMs;
    pullCandidate_ = view_ == "chat" && !navigationOpen_ && !modal_ &&
                     !attachments_ && hasConversationContent_ &&
                     messageOffset_ == 0 && chatFollowing_ && body_ &&
                     lv_obj_get_scroll_bottom(body_) <= 10 && inPullArea;
    // A bounded landscape draft owns its scroll until the gesture ends.
    // Reaching its last line cannot turn that same touch into chat creation.
    // An overflowing draft keeps edge drags local, including at its local bottom.
    if(inComposer && lv_obj_is_scrollable(composer_) &&
       (lv_obj_get_scroll_y(composer_)>0 || lv_obj_get_scroll_bottom(composer_)>0))
      pullCandidate_=false;
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
      std::abs(pullStartY_ - y) > uiMetrics_.dp(8)) {
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
    pullIndicator_ = box(screenObj_, layout().gutter() + (layout().bodyWidth() - uiMetrics_.dp(280)) / 2, screen_.height - uiMetrics_.dp(98), uiMetrics_.dp(280), uiMetrics_.dp(98));
    lv_obj_set_style_bg_opa(pullIndicator_, 0, 0);
    lv_obj_set_scrollable(pullIndicator_, false);
    pullArc_ = lv_arc_create(pullIndicator_);
    lv_obj_set_pos(pullArc_, uiMetrics_.dp(114), 0);
    lv_obj_set_size(pullArc_, uiMetrics_.dp(52), uiMetrics_.dp(52));
    lv_arc_set_rotation(pullArc_, 270);
    lv_arc_set_bg_angles(pullArc_, 0, 360);
    lv_arc_set_range(pullArc_, 0, 100);
    lv_obj_remove_style(pullArc_, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_color(pullArc_, lv_color_hex(theme::palette().border), LV_PART_MAIN);
    lv_obj_set_style_arc_color(pullArc_, lv_color_hex(theme::palette().accent),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(pullArc_, uiMetrics_.dp(3), LV_PART_MAIN);
    lv_obj_set_style_arc_width(pullArc_, uiMetrics_.dp(3), LV_PART_INDICATOR);
    lv_obj_set_clickable(pullArc_, false);
    pullBadge_ =
        icon(pullIndicator_, "newchat", uiMetrics_.dp(114), 0, uiMetrics_.dp(52), uiMetrics_.dp(52), {}, theme::palette().accent);
    lv_obj_set_hidden(pullBadge_, true);
    pullLabel_ = label(pullIndicator_, "新聊天", 0, uiMetrics_.dp(60), uiMetrics_.dp(280), uiMetrics_.fontSize(UiFont::SettingsTitle));
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
  if(modal_)lv_obj_delete(modal_);
  stopOfficialReaderGlide();
  chatBottomState_.pause();
  motion::finishAll();
  if(contextMenuKind_=="balances")cancelBalances();
  closeAttachments();
  motion::finishAll();
  composerShortcut_.pause();
  if(readGesture_.active())readSuppressed_=readDown_;
  readReset();
  onboarding_.reset();modalButtons_.clear();modalScroll_=nullptr;
  contextMenuPanel_=contextMenuList_=nullptr;contextMenuKind_.clear();contextMenuOpening_=false;
  updateActions_.clear(); updateProgress_=updateProgressText_=updateTransferText_=nullptr;
  feedbackActions_.clear();
  readerDown_ = readerPointer_ = readerVertical_ = readerSettle_ = false;
  chatRenderPending_ = chatRefreshPending_ = chatImagesPending_ = false;
  chatFinishPending_ = Json::object();
  fontSlider_ = fontPercentLabel_ = fontPreview_ = fontStatus_ = nullptr;fontControls_.clear();
  uiScaleSlider_=uiScaleCaption_=uiScalePreview_=uiScaleStatus_=nullptr;uiScaleControls_.clear();
  scaleMessageRows_.clear();
  cropActions_.clear();cropViewport_=cropBitmap_=cropSelection_=nullptr;
  if(!imageViewer_)imageGesture_={};
  settingsControls_.clear();settingsGroups_.clear();
  settingsHeading_ = nullptr;
  pageHeading_ = pageHeader_ = pageContent_ = nullptr;
  pageActions_.clear();
  transferProgress_ = nullptr;
  scanWarning_ = scanHolder_ = scanImage_ = scanLabel_ = scanUse_ = scanCancel_ = scanHint_ = nullptr;
  chatHeader_ = chatTitle_ = nullptr;
  historyRows_.clear();navigationFooter_.clear();
  historyCaptions_.clear();historyActions_.clear();
  historyContextId_.clear();
  reasoningHeaders_.clear();reasoningControls_.clear();
  if (navigationList_)
    navigationScroll_ = std::max(0, int(lv_obj_get_scroll_y(navigationList_)));
  navigation_ = navigationList_ = navigationActive_ = nullptr;
  resetPull();
  if (touchDown_)
    input_.cancel();
  if (popupKind_ == "attachments") { ++sessionInfoGeneration_; sessionInfoPending_ = false; }
  contextUsageArc_ = contextUsageLabel_ = nullptr;
  attachmentPanel_ = nullptr;
  attachmentEntries_.clear();
  attachments_ = modal_ = nullptr;
  popupKind_.clear();
  if (chatControls_)
    lv_anim_delete(chatControls_, nullptr);
  if (body_)
    lv_anim_delete(body_, nullptr);
  chatControls_ = toBottom_ = composerGauge_ = nullptr;
  chatControlsTarget_ = -1;
  rasters_.clear();
  archiveViewButtons_.clear();
  imageRequests_.clear();
  placeholders_.clear();
  cameraRotatingControls_.clear();
  if (screenObj_) {
    lv_obj_delete(screenObj_);
    screenObj_ = nullptr;
  }
  for (auto &t : textures_)
    lv_image_cache_drop(&t.second->descriptor);
  textures_.clear();
  inputOrdinal_=0;
  body_ = messageContent_ = composer_ = status_ = cameraImage_ = liveContent_ =
      nullptr;
}
void App::build() {
  const auto reading=readRegion(readActive_);
  if(reading&&reading->grouped&&reading->key=="page:"+view_&&readBusy()) {
    readRebuildPending_=true;return;
  }
  readRebuildPending_=false;
  if(editorFocusSwitch_){editorPendingBuild_=true;return;}
  editorPendingBuild_=false;
  feedbackActions_.clear();
  transferActions_.clear();
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
  lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(theme::palette().background),0);
  const bool photo = view_ == "camera" || view_ == "crop";
  const int canvasW = photo ? photoWidth() : screen_.width;
  const int canvasH = photo ? photoHeight() : screen_.height;
  screenObj_ = box(lv_screen_active(), (screen_.width-canvasW)/2, (screen_.height-canvasH)/2, canvasW, canvasH);
  if (photo && cameraAngle_ && !layout().portrait()) {
    lv_obj_set_style_transform_pivot_x(screenObj_,canvasW/2,0);
    lv_obj_set_style_transform_pivot_y(screenObj_,canvasH/2,0);
    lv_obj_set_style_transform_rotation(screenObj_,photoTransforms(cameraAngle_, false).workspace*10,0);
  }
  lv_obj_set_scrollable(screenObj_, false);
  if(view_=="onboarding") {
    onboardingPage();builtView_=view_;builtPageKey_=pageKey();syncScanner();syncBackPolicy();return;
  }
  lv_obj_t *navigation=nullptr;
  if(view_!="crop"&&(!layout().portrait()||view_=="chat"||photo)) navigation=icon(screenObj_, view_ == "chat" ? (messageEdit_.empty()?"menu":"close") : "back", layout().portrait()?layout().navigationX():uiMetrics_.dp(16), layout().portrait()?layout().safeTop():uiMetrics_.dp(8), uiMetrics_.touchTarget(48), uiMetrics_.touchTarget(48), [this] {
    if(editor_.active){editorAccept();return;}
    if (view_ == "chat") {
      if(!messageEdit_.empty())cancelMessageEdit();else toggleNavigation();
    }
    else
      goBack();
  });
  int w = layout().bodyWidth();
  body_ = view_=="crop" || (photo&&layout().portrait()) ? box(screenObj_,0,0,canvasW,canvasH) : photo && cameraAngle_%180 ? box(screenObj_,0,uiMetrics_.dp(56),canvasW,canvasH-uiMetrics_.dp(56))
                                      : box(screenObj_,layout().gutter(),0,w,screen_.height);
  lv_obj_set_scroll_dir(body_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body_, LV_SCROLLBAR_MODE_AUTO);
  if (view_ == "chat")
    chat();
  else if (view_ == "settings")
    settingsPage();
  else if (view_ == "ui_scale")
    uiScalePage();
  else if (view_ == "font_size")
    fontSizePage();
  else if (view_ == "http_settings")
    httpSettingsPage();
  else if (view_ == "keyboard_settings")
    keyboardSettingsPage();
  else if (view_ == "update")
    updatePage();
  else if (view_ == "feedback")
    feedbackPage();
  else if (view_ == "transfer")
    transferPage();
  else if (view_ == "presets")
    presetsPage();
  else if (view_ == "preset_edit")
    presetEditor();
  else if (view_ == "provider")
    providerPage();
  else if (view_ == "provider_models")
    providerModelsPage();
  else if (view_ == "api_compatibility")
    apiCompatibilityPage();
  else if (view_ == "anthropic_connection")
    anthropicConnectionPage();
  else if (view_ == "archives")
    archivesPage();
  else if (view_ == "archive_chat")
    archiveReaderPage();
  else if (view_ == "web_details")
    webDetailsPage();
  else if (view_ == "web_source")
    webSourcePage();
  else if (view_ == "session_info")
    sessionInfoPage();
  else if (view_ == "context_summary")
    contextSummaryPage();
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
  int scanTextWidth=0,scanLineHeight=0;
  if(view_=="chat") {
    const bool portrait=layout().portrait();
    const auto scanFont=font(uiMetrics_.sp(portrait?16:14));scanLineHeight=scanFont->line_height;
    scanTextWidth=uiMetrics_.dp(portrait?80:60);
    const char *portraitText[]={"扫描就绪","扫描准备中","扫描不可用"};
    const char *landscapeText[]={"扫描\n就绪","扫描\n准备中","扫描\n不可用"};
    for(int i=0;i<3;++i) {
      lv_point_t size{};lv_text_get_size(&size,portrait?portraitText[i]:landscapeText[i],scanFont,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
      scanTextWidth=std::max(scanTextWidth,int(size.x));
    }
  }
  if (view_ == "chat" && layout().portrait()) {
    chatHeader_ = box(screenObj_, 0, 0, screen_.width, layout().chatReaderTop());
    lv_obj_set_scrollable(chatHeader_, false);
    if (navigation) lv_obj_set_parent(navigation, chatHeader_);
    const int left = layout().navigationX() + uiMetrics_.dp(64);
    const int statusX = screen_.width - layout().navigationX() - scanTextWidth-uiMetrics_.dp(8);
    chatTitle_ = label(chatHeader_, "", left, layout().safeTop()+uiMetrics_.dp(10), statusX-left-uiMetrics_.dp(12), UiFont::Button);
    lv_label_set_long_mode(chatTitle_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(chatTitle_, font(uiMetrics_.fontSize(UiFont::Button))->line_height);
    refreshChatTitle();
  }
  if (view_ == "chat" && scanner_.available()) {
    const bool portrait = layout().portrait();
    const int hintWidth=scanTextWidth+uiMetrics_.dp(portrait?8:4);
    const int hintHeight=std::max(portrait?uiMetrics_.touchTarget(48):uiMetrics_.dp(64),scanLineHeight*(portrait?1:2)+uiMetrics_.dp(portrait?8:10));
    auto hint = box(portrait?chatHeader_:screenObj_,portrait?screen_.width-layout().navigationX()-hintWidth:uiMetrics_.dp(12),portrait?layout().safeTop():uiMetrics_.dp(60),hintWidth,hintHeight);
    lv_obj_set_scrollable(hint,false);
    scanHint_=label(hint,portrait?"扫描准备中":"扫描\n准备中",uiMetrics_.dp(4),portrait?(hintHeight-scanLineHeight)/2:uiMetrics_.dp(5),scanTextWidth,uiMetrics_.sp(portrait?16:14),theme::palette().muted);
    lv_label_set_long_mode(scanHint_,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(scanHint_,scanLineHeight*(portrait?1:2));
    onClick(hint, [this] {
      auto state = scanner_.state().value("type", std::string());
      if (state == "error" || state == "fatal") {
        scanner_.eligible(false, session_);
        scanner_.eligible(true, session_);
        syncScanner();
      }
    });
  }
  if(view_=="chat"&&!layout().portrait())balanceButton(screenObj_,16,screen_.height-56,theme::palette().background);
  if(navigation)lv_obj_move_foreground(navigation);
  portraitPage();
  refreshImages();
  if (navigationOpen_)
    historyDrawer();
  lv_obj_update_layout(screenObj_);
  if (pageHeader_) {
    lv_obj_scroll_to_y(body_, pageY, LV_ANIM_OFF);
    builtPageKey_ = pageKey();
  }
  if (view_ == "transfer") {
    const int restore = !transfer_->busy() && transferRestoreScroll_ >= 0 ? transferRestoreScroll_ : transferY;
    lv_obj_scroll_to_y(body_, restore, LV_ANIM_OFF);
    if (!transfer_->busy()) transferRestoreScroll_ = -1;
    transferBuiltScreen_ = transfer_->busy() ? "" : transferScreen_;
  }
  if (view_ == "settings") {
    lv_obj_scroll_to_y(body_, settingsY, LV_ANIM_OFF);
    settingsScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  }
  builtView_ = view_;
  chatLayout_ = false;
  syncScanner();
  if (view_=="archive_chat" || (view_=="transfer" && transferScreen_.compare(0,7,"format_")==0))paintVisible();
  if (editorRoot_) {editorViewport();editorSyncText();lv_obj_move_foreground(editorRoot_);if(attachments_&&popupKind_=="attachments")lv_obj_move_foreground(attachments_);}
  if (imageViewer_) lv_obj_move_foreground(imageViewer_);
}
void App::show(const std::string &view) {
  const bool settingsEnter=view=="settings"&&view_=="chat";
  std::unique_ptr<SurfaceSnapshot> settingsBackground;
  if(settingsEnter&&screenObj_&&visible_&&inputFocused_)settingsBackground.reset(new SurfaceSnapshot(screenObj_));
  std::unique_ptr<SurfaceSnapshot> settingsLeave;
  if(view_=="settings"&&view=="chat"&&screenObj_&&visible_&&inputFocused_)
    settingsLeave.reset(new SurfaceSnapshot(screenObj_));
  if(view_=="chat" && view!="chat")captureScaleAnchor();
  if (view != view_) composerShortcut_.reset();
  if ((view_ == "web_source" || view_ == "web_details") && view != "web_source")
    cancelWebReader();
  if (view_ == "feedback" && view != view_ && feedback_) feedback_->request("keys.cancel");
  if (view_ == "session_info" && view != view_) {
    ++sessionInfoGeneration_; sessionInfoPending_ = false;
  }
  closeImageViewer(false);
  if (view_ == "archive_chat" && view != "archive_chat" && view != "web_details" &&
      view != "web_source") {
    archiveReaderId_.clear();
    archiveReaderMessages_.clear();
    archiveReasoningOverrides_.clear();
  }
  if (view_ == "settings" && body_)
    settingsScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  if (view == "settings" && view_ == "chat") settingsScroll_ = 0;
  if (view_ == "crop" && view != "crop")
    clearCrop();
  if (view_ == "scan" && view != "scan") {
    if (view == "chat" && scanResult_.value("type", std::string()) == "finished" &&
        scanResult_.value("hardware_armed", false) && !scanConfirming_)
      scanner_.discard(scanId_);
    else
      scanner_.eligible(false, session_);
    scanId_ = 0;
    scanResult_ = Json::object();
    scanConfirming_ = false;
  }
  closeNavigation(false);
  closeAttachments();
  resetPull();
  if ((view_ == "provider" || view_ == "provider_models" || view_ == "model" || view_ == "api_compatibility" || view_ == "anthropic_connection") &&
      view != "provider" && view != "provider_models" && view != "model" && view != "api_compatibility" && view != "anthropic_connection") {
    providerForm_ = Json::object();providerDraftModels_=Json::array();providerOriginal_=Json::object();providerWorkspaceId_.clear();
    modelNames_.clear();
    ++modelGeneration_;
    modelCancel_ = true;
  }
  if (view_ == "camera" && view != "camera")
    camera_.stop();
  view_ = view;
  notice_.clear();
  build();
  if(settingsEnter&&screenObj_&&visible_&&inputFocused_) {
    if(settingsBackground)settingsBackground->show(0,0);
    lv_obj_move_foreground(screenObj_);slideSurfaceIn(screenObj_,screen_.width,0);
  }
  if(settingsLeave)settingsLeave->show(screen_.width,0);
  if (view_ == "camera" && visible_)
    whenScannerReleased([this] {
      if (view_ == "camera" && visible_ && !keyboard_)
        startCamera();
    });
}
void App::error(const std::string &m) {
  diagnosticRecord("ui", "error");
  notice_ = m;
  if (status_)
    lv_label_set_text(status_, m.c_str());
  else
    modal("提示", m);
}
void App::modal(const std::string &title, const std::string &text) {
  if(keyboard_&&!officialDocked_&&
     (title=="联网搜索不可用"||title=="已修改联网搜索"||title=="上下文未更新")) {
    deferredModalTitle_=title;deferredModalText_=text;return;
  }

  if(readGesture_.active())readSuppressed_=readDown_;
  readReset();
  closeAttachments();
  resetPull();
  if (modal_)
    lv_obj_delete(modal_);
  const bool portraitPhoto = !layout().portrait() && (view_=="camera" || view_=="crop") && cameraAngle_%180;
  const int mw=portraitPhoto?photoWidth():screen_.width;
  const int mh=keyboard_&&officialDocked_?display_.topHeight():(portraitPhoto?photoHeight():screen_.height);
  const int layerWidth=portraitPhoto?mw-uiMetrics_.dp(16):layout().modalWidth();
  auto layer = box(lv_screen_active(), portraitPhoto?uiMetrics_.dp(8):layout().modalX(), 0, layerWidth, mh, theme::palette().popup);
  modal_ = layer;modalButtons_.clear();
  if(view_=="chat"&&chatControls_)lv_obj_set_hidden(chatControls_,true);
  if(toBottom_)lv_obj_set_hidden(toBottom_,true);
  lv_obj_add_event_cb(
      layer,
      [](lv_event_t *e) {
        auto app=static_cast<App *>(lv_event_get_user_data(e));
        if(app->modal_!=lv_event_get_target_obj(e))return;
        app->modal_=nullptr;app->modalButtons_.clear();app->modalScroll_=nullptr;
        if(app->chatControls_)lv_obj_set_hidden(app->chatControls_,false);
        app->updateChatControls(true);
      },
      LV_EVENT_DELETE, this);
  lv_obj_set_style_radius(layer, uiMetrics_.dp(16), 0);
  lv_obj_set_style_border_color(layer, lv_color_hex(theme::palette().border), 0);
  lv_obj_set_style_border_width(layer, uiMetrics_.dp(1), 0);
  const bool rounded=layout().safety().enabled()&&!portraitPhoto;
  const int top=rounded?layout().safeTop():uiMetrics_.dp(7);
  const int left=rounded?std::max(uiMetrics_.dp(15),layout().navigationX()-layout().modalX()):uiMetrics_.dp(15);
  label(layer, title, left, top, layerWidth-2*left, uiMetrics_.fontSize(UiFont::EmptyTitle));
  auto scroll = box(layer, uiMetrics_.dp(10), rounded?top+uiMetrics_.dp(40):uiMetrics_.dp(42), layerWidth-uiMetrics_.dp(20), mh-uiMetrics_.dp(115), theme::palette().popup);
  modalScroll_=scroll;
  readGroup(scroll,"dialog");
  readableLabel(scroll, text, uiMetrics_.dp(5), 0, layerWidth-uiMetrics_.dp(30), uiMetrics_.fontSize(UiFont::SettingsTitle));
  button(layer, "关闭", layerWidth-uiMetrics_.dp(110), mh-uiMetrics_.dp(66), uiMetrics_.dp(100), uiMetrics_.dp(44),
         [layer] { lv_obj_delete(layer); });
}
void App::chat() {
  int width = layout().bodyWidth();
  lv_obj_set_y(body_, layout().chatReaderTop());
  lv_obj_set_height(body_, screen_.height - layout().chatReaderTop());
  // At the edge, rapid guarded swipes must not repeatedly stretch and snap
  // back.
  lv_obj_set_scroll_elastic(body_, false);
  lv_obj_set_scroll_chain_ver(body_, false);
  messageContent_ = box(body_, 0, 0, width, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(messageContent_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(messageContent_, uiMetrics_.dp(10), 0);
  syncChatBottomPadding();
  if(layout().portrait()) {
    lv_obj_set_style_pad_left(messageContent_,uiMetrics_.dp(8),0);
    lv_obj_set_style_pad_right(messageContent_,uiMetrics_.dp(8),0);
  }
  lv_obj_set_scrollable(messageContent_, false);
  lv_obj_add_event_cb(
      body_,
      [](lv_event_t *e) {
        static_cast<App *>(lv_event_get_user_data(e))->chatScrolled();
      },
      LV_EVENT_SCROLL, this);
  renderMessages(chatFollowing_);
  syncChatBottomState(true);
  rebuildChatControls();
}
void App::rebuildChatControls() {
  if (chatControls_) { lv_anim_delete(chatControls_, nullptr); lv_obj_delete(chatControls_); }
  if (toBottom_) lv_obj_delete(toBottom_);
  composer_ = chatControls_ = toBottom_ = composerGauge_ = nullptr;
  chatControlsTarget_ = -1;
  const auto geometry = layout();
  const auto c = uiMetrics_.composer();
  const int composerWidth = geometry.composerWidth();
  chatControls_ =
      box(screenObj_, geometry.composerX(), geometry.composerY(), composerWidth, geometry.composerHeight(), theme::palette().surface);
  lv_obj_set_scrollable(chatControls_, false);
  lv_obj_set_style_radius(chatControls_, geometry.composerHeight()/2, 0);
  if(geometry.portrait()) {
    // Cover the lower-corner cutouts and everything below them. Paint the
    // capsule curve back over the backing before its controls are drawn.
    lv_obj_add_flag(chatControls_,LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_add_event_cb(chatControls_,[](lv_event_t *event){
      auto self=static_cast<App *>(lv_event_get_user_data(event));
      auto extent=static_cast<int32_t *>(lv_event_get_param(event));
      *extent=std::max(*extent,self->screen_.height);
    },LV_EVENT_REFR_EXT_DRAW_SIZE,this);
    lv_obj_refresh_ext_draw_size(chatControls_);
    auto below=box(chatControls_,-geometry.composerX(),LV_PCT(100),screen_.width,screen_.height,theme::palette().background);
    lv_obj_set_clickable(below,false);lv_obj_set_scrollable(below,false);
    lv_obj_set_style_bg_opa(below,LV_OPA_TRANSP,0);
    lv_obj_set_style_translate_y(below,-geometry.composerHeight()/2,0);
    lv_obj_add_event_cb(below,[](lv_event_t *event){
      auto self=static_cast<App *>(lv_event_get_user_data(event));
      auto object=lv_event_get_target_obj(event);auto parent=lv_obj_get_parent(object);
      if(!(self->editor_.active&&self->editorInlineChat_)&&!surfaceEntering(parent))return;
      lv_area_t backing,capsule;lv_obj_get_coords(object,&backing);lv_obj_get_coords(parent,&capsule);
      lv_draw_rect_dsc_t style;lv_draw_rect_dsc_init(&style);
      style.bg_color=lv_color_hex(theme::palette().background);style.bg_opa=LV_OPA_COVER;
      lv_draw_rect(lv_event_get_layer(event),&style,&backing);
      style.bg_color=lv_color_hex(theme::palette().surface);style.bg_opa=LV_OPA_COVER;
      style.radius=lv_obj_get_style_radius(parent,LV_PART_MAIN);
      lv_draw_rect(lv_event_get_layer(event),&style,&capsule);
    },LV_EVENT_DRAW_MAIN,this);
  }
  auto attachment = icon(
      chatControls_, "plus", geometry.composerInset(), geometry.composerInset(), c.control, c.control, [this] { toggleAttachments(); },
      theme::palette().surface);
  // Keep the glyph in place; near-edge taps belong to attachments, not IME.
  lv_obj_set_ext_click_area(attachment, c.attachmentHitGrow);
  const int entryLeft = geometry.composerEntryLeft();
  const bool gauge = sessionPreferences_.value("gauge", !effortMemory_.empty());
  composerGauge_=icon(
        chatControls_, "gauge", composerWidth - (geometry.portrait()?geometry.composerInset()+c.initialGaugeTrailing:c.initialGaugeTrailing), c.initialTop, c.control, c.control, [this] { effortPicker(); },
        theme::palette().surface, theme::palette().muted);
  auto compose = [this] {
    if (contextBusy_ || !composerTapAllowed_)
      return;
    composerTapAllowed_ = false;
    edit(
        draft_, "请输入消息",
        [this](const std::string &s) {
          draft_ = s;
          saveDraft();
          build();
        },
        8192, "ZhCNPreferred", {}, true, false, false, true);
  };
  const auto &preview = draft_;
  composer_ = button(
      chatControls_, preview.empty() ? "询问 " PENCHAT_NAME : preview, entryLeft, c.initialTop,
      composerWidth - entryLeft - geometry.composerEntryRight(gauge), c.control,
      compose,
      theme::palette().surface, LV_TEXT_ALIGN_LEFT);
  onClick(chatControls_,compose);
  if(geometry.portrait())inputTarget(composer_,"chat-composer");
  if (draft_.empty())
    lv_obj_set_style_text_color(lv_obj_get_child(composer_, 0),
                                lv_color_hex(theme::palette().muted), 0);
  const bool sendEnabled = contextBusy_ || generating_ || canSend();
  const int sendDiameter = geometry.composerSendDiameter();
  auto send = icon(
      chatControls_, (generating_ || contextBusy_) ? "stop" : "up", composerWidth - c.initialSendTrailing, c.initialTop, sendDiameter, sendDiameter,
      [this] {
        if(editor_.active&&editorInPlace()&&!generating_&&!contextBusy_) {editorAction("chat-send");return;}
        if (contextBusy_) {
          cancelContext();
          build();
        } else if (generating_)
          cancel_ = true;
        else if (canSend())
          sendMessage();
      },
      sendEnabled ? theme::palette().text : theme::palette().border, theme::palette().primaryText);
  // Grow the portrait circle equally around its existing center.
  lv_obj_align(send, LV_ALIGN_RIGHT_MID, -geometry.composerInset() + (sendDiameter - c.control) / 2, 0);
  // Keep the disabled circle as the hit target so its press cannot fall
  // through to the capsule's blank-space editor shortcut.
  if (!sendEnabled)
    lv_obj_add_state(send, LV_STATE_DISABLED);
  toBottom_ = icon(
      screenObj_, "down", geometry.composerX() + (composerWidth - c.control) / 2, chatComposerY() - c.returnOffset(), c.control, c.control,
      [this] { jumpToBottom(); }, theme::palette().surface);
  lv_obj_set_style_border_color(toBottom_, lv_color_hex(theme::palette().border), 0);
  lv_obj_set_style_border_width(toBottom_, c.border, 0);
  layoutChatComposer();syncEffortGauge();
  updateChatControls(false);
  if(editor_.active&&editorInPlace()&&editorRoot_) {
    editorViewport();editorSyncText();lv_obj_move_foreground(editorRoot_);if(attachments_&&popupKind_=="attachments")lv_obj_move_foreground(attachments_);
  }
}
bool App::atLatestBottom() const {
  return view_ == "chat" && body_ && messageOffset_ == 0 && lv_obj_get_scroll_bottom(body_) <= 10;
}
void App::syncChatBottomState(bool immediate) {
  if (view_ != "chat" || !body_) { chatBottomState_.pause(); return; }
  if (immediate) {
    chatBottomState_.reset(messageOffset_ == 0 && lv_obj_get_scroll_bottom(body_) <= uiMetrics_.dp(10));
    return;
  }
  if (!visible_ || !inputFocused_ || keyboard_ || imageViewer_ || navigationOpen_ || modal_ || attachments_ || pulling_) {
    chatBottomState_.pause();
    return;
  }
  chatBottomState_.update(messageOffset_ == 0, lv_obj_get_scroll_bottom(body_),
                         uiMetrics_.dp(10), uiMetrics_.dp(24), ticks());
}
bool App::chatComposerVisible() const {
  if(pulling_)return false;
  return layout().portrait() || chatBottomState_.atBottom() || composerShortcut_.shown();
}
bool App::composerShortcutAvailable() const {
  return composerShortcut_.enabled() && !layout().portrait() && view_ == "chat" &&
      visible_ && inputFocused_ && !keyboard_ && !editor_.active && !imageViewer_ &&
      !navigationOpen_ && !modal_ && !attachments_ && !pulling_;
}
bool App::composerShortcutTouch(int x, int y, bool pressed, bool cancelled) {
  if(readActive_&&readSelection_.selected()) {composerShortcut_.pause();return false;}
  if (!composerShortcutAvailable()) { composerShortcut_.pause(); return false; }
  if (!composerShortcut_.tracking() && (touchDown_ || readerDown_)) return false;
  const auto area = layout().composerShortcut();
  const bool inside = x >= area.x && x < area.x + area.width &&
                      y >= area.y && y < area.y + area.height;
  const bool before = composerShortcut_.shown();
  const bool consumed = composerShortcut_.touch(x, y, pressed, cancelled, inside, ticks(), screen_.height, uiMetrics_.gestures().tap);
  if (before != composerShortcut_.shown()) updateChatControls(true);
  return consumed;
}
void App::composerShortcutTick() {
  const bool bottomBefore = chatBottomState_.atBottom();
  syncChatBottomState();
  const bool before = composerShortcut_.shown();
  if (composerShortcutAvailable()) composerShortcut_.tick(ticks());
  else composerShortcut_.pause();
  if (view_ == "chat" && atLatestBottom()) composerShortcut_.reset();
  if (before != composerShortcut_.shown() || bottomBefore != chatBottomState_.atBottom()) updateChatControls(true);
}
bool App::readerInteracting() const {
  if(readBusy())return true;
  if (view_ != "chat" || !body_ || chatLayout_ || keyboard_ || (editor_.active&&!editorLayout().docked) || imageViewer_ ||
      navigationOpen_ || modal_) return false;
  if (editorDismissing_ || editorDismissReturnAt_ || readerPointer_ || lv_obj_is_scrolling(body_)) return true;
  for (const auto &r : rasters_) if (lv_obj_is_scrolling(r.holder)) return true;
  return false;
}
void App::readerTouch(int x, int y, bool pressed, bool cancelled) {
  const bool began = pressed && !readerDown_;
  readerDown_ = pressed && !cancelled;
  if (began) {
    readerPointer_ = readerVertical_ = false;
    if (view_ == "chat" && body_ && !keyboard_ && (!editor_.active||editorLayout().docked) && !imageViewer_ &&
        !navigationOpen_ && !modal_ && !attachments_) {
      lv_area_t area; lv_obj_get_coords(body_, &area);
      readerPointer_ = x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
      if (chatControls_) {
        lv_area_t controls; lv_obj_get_coords(chatControls_, &controls);
        if (x >= controls.x1 && x <= controls.x2 && y >= controls.y1 && y <= controls.y2)
          readerPointer_ = false;
      }
    }
    readerStartX_ = x; readerStartY_ = y;
    readerSettle_ = readerPointer_;
    if (readerPointer_) chatBottomState_.pause();
  }
  if (readerPointer_ && pressed) {
    const int dx = x - readerStartX_, dy = y - readerStartY_;
    if (std::abs(dy) > uiMetrics_.gestures().drag && std::abs(dy) > std::abs(dx)) {
      readerVertical_ = true;
      // Finger moving down reads older content. Detach before the next delta
      // can resize/reanchor the viewport; horizontal table gestures stay separate.
      if (dy > 0 && lv_obj_get_scroll_y(body_) + lv_obj_get_scroll_bottom(body_) > 10)
        chatFollowing_ = false;
    }
  }
  if (!pressed || cancelled) {
    if (cancelled) chatBottomState_.pause();
    readerPointer_ = false;
  }
}
void App::settleReader() {
  if (view_ != "chat" || keyboard_ || (editor_.active&&!editorLayout().docked) || imageViewer_ || navigationOpen_ || modal_) {
    readerDown_ = readerPointer_ = readerVertical_ = readerSettle_ = false;
    return;
  }
  if (readerSettle_ && !readerInteracting()) {
    readerSettle_ = readerVertical_ = false;
    if (atLatestBottom()) {
      if (!chatFollowing_) { pullNeedsPause_ = true; pullLastActivity_ = ticks(); }
      chatFollowing_ = true;
    }
    updateChatControls(true);
  }
}
void App::chatScrolled() {
  if (chatLayout_ || view_ != "chat" || !body_)
    return;
  closeAttachments();
  const bool readerScroll = readerVertical_ &&
      (readerPointer_ || readerSettle_ || lv_obj_is_scrolling(body_));
  // Layout can adjust scroll extent after its immediate layout guard ended.
  // A follower stays at the new bottom; only an actual reader gesture detaches it.
  if (chatFollowing_ && !readerScroll && !pulling_ && messageOffset_ == 0 && !atLatestBottom()) {
    const bool previous = chatLayout_;
    chatLayout_ = true;
    lv_obj_update_layout(body_);
    lv_obj_scroll_to_y(body_, LV_COORD_MAX, LV_ANIM_OFF);
    chatLayout_ = previous;
  }
  const int nextScroll = std::max(0, int(lv_obj_get_scroll_y(body_)));
  // Only a recognized reader gesture and its inertia count. Layout reflows,
  // programmatic navigation and horizontal rich-text scrolling do not.
  if (readerScroll && composerShortcutAvailable())
    composerShortcut_.scroll(nextScroll - chatScroll_);
  chatScroll_ = nextScroll;
  if (atLatestBottom()) composerShortcut_.reset();
  const bool wasFollowing = chatFollowing_;
  if (!atLatestBottom()) chatFollowing_ = false;
  else if (!readerInteracting() && !readerSettle_) chatFollowing_ = true;
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
  syncChatBottomPadding();
  syncChatBottomState();
  if (!chatControls_ || !toBottom_)
    return;
  lv_obj_set_hidden(toBottom_, !hasConversationContent_ || chatBottomState_.atBottom() || pulling_);
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
  const int target = chatComposerVisible() ? chatComposerY() : screen_.height + uiMetrics_.composer().hiddenOffset;
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
  composerShortcut_.reset();
  pullNeedsPause_ = true;
  pullLastActivity_ = ticks();
  chatFollowing_ = true;
  readerDown_ = readerPointer_ = readerVertical_ = readerSettle_ = false;
  messageOffset_ = 0;
  const bool previous = chatLayout_;
  chatLayout_ = true;
  lv_obj_stop_scroll_anim(body_);
  // Flush the latest stream state before measuring the destination. This also
  // returns from an older history page; live rows use output_, not the DB lag.
  renderMessages(true);
  dirty_ = false;
  lv_obj_scroll_to_y(body_, LV_COORD_MAX, LV_ANIM_OFF);
  chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  chatLayout_ = previous;
  syncChatBottomState(true);
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
      if(!sessionPreferences_.value("effort",std::string()).empty())preferences["effort"]=sessionPreferences_.at("effort");
      else preferences.erase("effort");
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
  if (!sessionPreferences_.contains("web_enabled"))
    sessionPreferences_["web_enabled"] = webEnabled(sessionPreferences_, db_.settings());
  reasoningMetadata_ = Json::object();
  expandedReasoning_.clear();
  reasoningOverrides_.clear();
  tableScrolls_.clear();
  chatFollowing_ = true;
  chatScroll_ = 0;
  messageOffset_ = 0;
  show("chat");
  if (!warning.empty()) modal("默认预设已部分应用", warning);
}
void App::richMessage(lv_obj_t *parent, const std::string &text, int width,
                      bool subdued, const std::string &context) {
  readBegin(parent,context);
  // Block margins are collapsed explicitly; the flex container adds no gap.
  lv_obj_set_style_pad_row(parent, 0, 0);
  Block previousBlock;
  bool hasPreviousBlock = false;
  auto beginBlock = [&](const Block &block) {
    const int gap = rich_.blockGap(hasPreviousBlock ? &previousBlock : nullptr, block, readingFontSize());
    if (gap) {
      auto space = box(parent, 0, 0, width, gap, theme::palette().background);
      lv_obj_set_style_bg_opa(space, LV_OPA_TRANSP, 0);
      lv_obj_set_scrollable(space, false);
    }
    previousBlock = block;
    hasPreviousBlock = true;
  };
  auto doc = parseMarkdown(text, generating_ && parent == liveContent_);
  const int size = readingFontSize();
  rich_.prepare(doc, width - uiMetrics_.dp(4), size);
  auto appendText = [&](const Block &b) {
    if (b.runs.empty() && b.cells.empty() && b.kind != "hr")
      return;
    beginBlock(b);
    auto layout = rich_.layout(b, width - uiMetrics_.dp(4), size, false, subdued);
    auto holder = box(parent, 0, 0, width, layout.height, theme::palette().background);
    readRich(parent,holder,b,width-uiMetrics_.dp(4),size,layout);
    lv_obj_set_scroll_dir(holder, LV_DIR_HOR);
    if (b.tableId)
      lv_obj_set_scroll_elastic(holder, false);
    lv_obj_set_scrollbar_mode(holder, LV_SCROLLBAR_MODE_AUTO);
    auto spacer = box(holder, layout.width - 1, 0, uiMetrics_.dp(1), uiMetrics_.dp(1), theme::palette().background);
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
         width - uiMetrics_.dp(4),
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
      if (context == "release-note") {
        auto caption = r;caption.style &= ~Image;caption.text = "[图片省略]";caption.link.clear();part.runs.push_back(std::move(caption));continue;
      }
      appendText(part);
      part.runs.clear();
      part.marker.clear();
      split = true;
      beginBlock(b);
      const auto metadata = webMetadata(context);
      if (r.link.compare(0, 14, "penchat-image:") == 0) {
        const auto ref = r.link.substr(14);
        Json asset;
        for (const auto &entry : metadata.value("web_images", Json::array()))
          if (entry.value("ref", std::string()) == ref)
            asset = entry;
        const auto path = asset.is_object() ? asset.value("path", std::string()) : std::string();
        if (!path.empty()) {
          // New flex children have no resolved height until LVGL lays them out.
          // Use the intended viewport size for decoding, never that transient 0.
          const int imageHeight = std::min(uiMetrics_.dp(240), std::max(uiMetrics_.dp(100), screen_.height / 3));
          auto h = box(parent, 0, 0, width, imageHeight,
                       theme::palette().background);
          lv_obj_set_scrollable(h, false);
          image(
              h, path, 0, 0, width, imageHeight,
              [this, path] { requestImageViewer(path); }, false, true);
          auto caption=button(
              parent, r.text.empty() ? "图片来源" : r.text, 0, 0, width, uiMetrics_.dp(44),
              [this, asset] {
                openWebSource({{"url", asset.value("source_url", std::string())},
                               {"title", asset.value("title", std::string("图片来源"))}});
              },
              theme::palette().background, LV_TEXT_ALIGN_LEFT);
          readLinkText(lv_obj_get_child(caption,0),r.text.empty()?"图片来源":r.text,asset.value("source_url",std::string()));
          const auto key=images_.request(path,width,imageHeight,true);
          auto retry=button(parent,"重试读取图片",0,0,width,uiMetrics_.dp(44),[this,path]{
            images_.retry(path);if(view_=="chat")renderMessages(false,true);else build();
          },theme::palette().background,LV_TEXT_ALIGN_LEFT);
          webImageRetries_[retry]=key;
          lv_obj_set_hidden(retry,!images_.failed(key)&&!access(path.c_str(),R_OK));
          lv_obj_add_event_cb(retry,[](lv_event_t *event){
            auto app=static_cast<App *>(lv_event_get_user_data(event));
            app->webImageRetries_.erase(static_cast<lv_obj_t *>(lv_event_get_target(event)));
          },LV_EVENT_DELETE,this);
        } else
          label(parent, (r.text.empty() ? "图片" : r.text) + " · 引用不可用", 0, 0, width, uiMetrics_.fontSize(UiFont::FormNote),
                theme::palette().muted);
      } else if (r.link.find("http://") == 0 || r.link.find("https://") == 0 ||
                 (!metadata.value("web_records", Json::array()).empty() &&
                  r.link.find("file://") == 0)) {
        label(parent, (r.text.empty() ? "图片" : r.text) + " · 请使用检索图片引用", 0, 0, width, uiMetrics_.fontSize(UiFont::FormNote),
              theme::palette().muted);
      } else {
        auto h = box(parent, 0, 0, width, uiMetrics_.dp(145), theme::palette().background);
        image(h, r.link, 0, 0, width, uiMetrics_.dp(145));
      }
    }
    if (split)
      appendText(part);
    else
      appendText(b);
  }
}
void App::replyContent(lv_obj_t *parent, const std::string &text, int width,
                       const std::string &id, bool live) {
  const auto web = webMetadata(id);
  const bool inlineSearch=live&&generating_&&(!reasoning_.empty()||web.value("reasoning_observed",false))&&
      (text.empty()||web.value("web_pending",false));
  if (!inlineSearch&&(!web.value("web_records", Json::array()).empty() ||
      !web.value("web_sources", Json::array()).empty())) {
    std::string status = "检索记录与来源";
    for (const auto &record : web.value("web_records", Json::array()))
      if (record.value("status", std::string()) == "running") {
        const auto name = record.value("name", std::string());
        status = name == "web_search"       ? "正在搜索…"
                 : name == "web_read_image" ? "正在获取图片…"
                                            : "正在阅读…";
      }
    const bool expanded = expandedWeb_.count(id);
    auto row = box(parent, 0, 0, width, uiMetrics_.dp(44), theme::palette().background);
    lv_obj_set_scrollable(row, false);
    auto arrow = icon(
        row, "chevron-right", 0, 0, uiMetrics_.dp(44), uiMetrics_.dp(44),
        [this, id] { events_.push("web_toggle", {{"id", id}}); }, theme::palette().background);
    setDisclosureProgress(arrow, expanded ? 1000 : 0);
    button(
        row, status, uiMetrics_.dp(44), 0, width - uiMetrics_.dp(44), uiMetrics_.dp(44),
        [this, id] { events_.push("web_toggle", {{"id", id}}); }, theme::palette().background,
        LV_TEXT_ALIGN_LEFT);
    ReasoningControl control;
    control.arrow = arrow;
    control.wrap = parent;
    control.width = width;
    control.expanded = expanded;
    control.detail = box(parent, 0, 0, width, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(control.detail, false);
    lv_obj_set_flex_flow(control.detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(control.detail, 0, 0);
    for (const auto &source : web.value("web_sources", Json::array())) {
      auto sourceButton = button(
          control.detail, source.value("title", source.value("url", std::string())), 0, 0, width, uiMetrics_.dp(44),
          [this, source] { openWebSource(source); }, theme::palette().background, LV_TEXT_ALIGN_LEFT);
      readLinkText(lv_obj_get_child(sourceButton, 0), source.value("title", source.value("url", std::string())), source.value("url", std::string()));
    }
    button(
        control.detail, "查看完整检索记录", 0, 0, width, uiMetrics_.dp(44), [this, id] { openWebDetails(id); },
        theme::palette().background, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_hidden(control.detail, !expanded);
    auto cleanup = new std::pair<App *, std::string>(this, id);
    lv_obj_add_event_cb(control.detail, [](lv_event_t *event) {
      if (lv_event_get_target_obj(event) != lv_event_get_current_target_obj(event)) return;
      auto owner = static_cast<std::pair<App *, std::string> *>(lv_event_get_user_data(event));
      auto found = owner->first->webControls_.find(owner->second);
      if (found != owner->first->webControls_.end() && found->second.detail == lv_event_get_target_obj(event))
        owner->first->webControls_.erase(found);
      delete owner;
    }, LV_EVENT_DELETE, cleanup);
    webControls_[id] = std::move(control);
  }
  if (!text.empty()) {
    richMessage(parent, text, width, false, id);
  } else if (live && receivingReply_ && !inlineSearch) {
    // A UI-only placeholder: never mix it into Markdown or saved output.
    generationHint_ = label(parent, "正在生成…", 0, 0, width, readingFontSize());
    lv_obj_add_event_cb(generationHint_, [](lv_event_t *e) {
      auto *app = static_cast<App *>(lv_event_get_user_data(e));
      if (app->generationHint_ == lv_event_get_target(e))
        app->generationHint_ = nullptr;
    }, LV_EVENT_DELETE, this);
  }
}
void App::renderMessageRows(lv_obj_t *parent,const Json &messages,const std::string &source,
                            int width,bool readOnly,int lastAssistant,int64_t questionSeq) {
  webMessageMetadata_.clear();
  replyChoiceRows_=readOnly?Json::object():db_.replyChoices(source);
  const auto reasoningMode=reasoningDisplay_;
  lv_obj_t *photoGroup = nullptr;
  for (const auto &m : messages) {
    bool user = m.value("role", std::string()) == "user";
    webMessageMetadata_[stringValue(m, "id")] = objectField(m, "metadata_json");
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
        lv_obj_set_style_pad_column(photoGroup,uiMetrics_.dp(8), 0);
        lv_obj_set_style_pad_row(photoGroup,uiMetrics_.dp(8), 0);
      }
      auto path = stringValue(m, "display_src");
      if (path.empty())
        path = stringValue(m, "stored_path");
      if (path.empty())
        path = stringValue(m, "original_path");
      auto original = stringValue(m, "stored_path");
      if (original.empty()) original = stringValue(m, "original_path");
      if (original.empty()) original = path;
      image(photoGroup, path, 0, 0, width, uiMetrics_.dp(140),
            [this,original] { requestImageViewer(original); }, false, true,
            !readOnly&&m["seq"].get<int>() > lastAssistant ? m["id"].get<std::string>() : "",readOnly||m.value("edit_virtual",false)?"":m["id"].get<std::string>());
      if(!readOnly && source==session_)scaleMessageRows_[m["id"].get<std::string>()]=lv_obj_get_child(photoGroup,-1);
      continue;
    }
    photoGroup = nullptr;
    int contentWidth = user ? width * 3 /4 : width;
    if (user && stringValue(m, "stored_path").empty() &&
        stringValue(m, "original_path").empty()) {
      lv_point_t measured;
      const auto text = stringValue(m, "output_text");
      lv_text_get_size(&measured, text.c_str(), font(readingFontSize()), 0, 0, LV_COORD_MAX,
                       LV_TEXT_FLAG_NONE);
      contentWidth = std::min(contentWidth, std::max(uiMetrics_.dp(48), int(measured.x) + uiMetrics_.dp(24)));
    }
    auto row = box(parent, 0, 0, width, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, user ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    auto wrap = box(row, 0, 0, contentWidth, LV_SIZE_CONTENT,
                    user && !hasImage ? theme::palette().surface : theme::palette().background);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(wrap, false);
    lv_obj_set_style_pad_all(wrap, hasImage ? 0 : (user ? uiMetrics_.dp(12) : uiMetrics_.dp(8)), 0);
    lv_obj_set_style_radius(wrap, user ? uiMetrics_.dp(18) : 0, 0);
    if(user&&!readOnly)messageLongPress(wrap,m["id"].get<std::string>());
    const int textWidth = contentWidth - (hasImage ? 0 : (user ? uiMetrics_.dp(24) : uiMetrics_.dp(16)));
    auto id = m["id"].get<std::string>();
    if(!readOnly && source==session_)scaleMessageRows_[id]=row;
    const bool live=!readOnly&&id==assistant_;
    std::string content =
        live ? output_ : stringValue(m, "output_text");
    auto reason =
        live ? reasoning_ : stringValue(m, "reasoning_text");
    auto metadata =
        live ? reasoningMetadata_ : objectField(m, "metadata_json");
    if (metadata.value("imported_reference", false))
      label(wrap, "导入的参考内容", 0, 0, textWidth, uiMetrics_.fontSize(UiFont::FormHint), theme::palette().muted);
    if (!user &&
        (!reason.empty() || metadata.value("reasoning_observed", false))) {
      const bool thinking=live&&generating_&&(!metadata.value("reasoning_complete",false)||content.empty()||metadata.value("web_pending",false));
      const auto searches=reasoningSearchText(metadata,live&&generating_);
      std::string title = "思考过程";
      if (thinking)
        title = metadata.value("stream_waiting", false) ? "等待服务商继续返回…" : "正在思考…";
      else if (metadata.contains("reasoning_duration_ms")) {
        const auto seconds =
            metadata["reasoning_duration_ms"].get<int64_t>() / 1000;
        title = "思考了 " +
                (seconds >= 60 ? std::to_string(seconds / 60) + "分" : "") +
                std::to_string(seconds % 60) + "秒";
      }
      auto header = box(wrap, 0, 0, textWidth, uiMetrics_.dp(44));
      reasoningHeaders_.push_back(header);
      lv_obj_set_scrollable(header, false);
      if (!reason.empty()||!searches.empty()) {
        bool expanded =
            reasoningMode == "always" ||
            (reasoningMode == "during" && thinking);
        auto &overrides=readOnly?archiveReasoningOverrides_:reasoningOverrides_;
        auto manual = overrides.find(id);
        if (manual != overrides.end())
          expanded = manual->second;
        if (expanded)
          expandedReasoning_.insert(id);
        auto toggle = [this, id, source] {
          events_.push("reasoning_toggle",{{"id",id},{"session",source}});
        };
        auto arrow=icon(header,"chevron-right",0,0,uiMetrics_.dp(44),uiMetrics_.dp(44),toggle,theme::palette().background,theme::palette().muted);
        lv_obj_set_style_bg_color(arrow,lv_color_hex(theme::palette().surface),LV_STATE_PRESSED);
        setDisclosureProgress(arrow, expanded ? 1000 : 0);
        lv_obj_set_clickable(header,false);
        auto caption=label(header,title,uiMetrics_.dp(52),uiMetrics_.dp(10),std::max(1,textWidth-uiMetrics_.dp(52)),uiMetrics_.fontSize(UiFont::SettingsTitle),theme::palette().reasoning);
        lv_label_set_long_mode(caption,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_height(caption,font(uiMetrics_.fontSize(UiFont::SettingsTitle))->line_height);
        ReasoningControl control;control.arrow=arrow;control.wrap=wrap;control.text=std::move(reason);control.searchText=searches;
        control.width=textWidth;control.expanded=expanded;
        if(expanded) {
          control.detail=box(wrap,0,0,textWidth,LV_SIZE_CONTENT);
          lv_obj_set_scrollable(control.detail,false);lv_obj_set_flex_flow(control.detail,LV_FLEX_FLOW_COLUMN);
          lv_obj_set_style_pad_left(control.detail,uiMetrics_.dp(52),0);lv_obj_set_style_pad_top(control.detail,uiMetrics_.dp(2),0);lv_obj_set_style_pad_bottom(control.detail,uiMetrics_.dp(8),0);
          richMessage(control.detail,control.text,std::max(uiMetrics_.dp(44),textWidth-uiMetrics_.dp(52)),true,id+":reasoning");
          if(!control.searchText.empty())readableLabel(control.detail,control.searchText,0,0,std::max(uiMetrics_.dp(44),textWidth-uiMetrics_.dp(52)),uiMetrics_.fontSize(UiFont::FormNote),theme::palette().reasoning);
        }
        reasoningControls_[id]=std::move(control);
      } else
        label(header, title, 0, uiMetrics_.dp(10), textWidth, uiMetrics_.fontSize(UiFont::SettingsTitle), theme::palette().reasoning);
    }
    auto path = stringValue(m, "display_src");
    if (path.empty())
      path = stringValue(m, "stored_path");
    if (path.empty())
      path = stringValue(m, "original_path");
    if (!path.empty() && (user || webMetadata(id).value("web_images", Json::array()).empty())) {
      auto imgBox = box(wrap, 0, 0, textWidth, uiMetrics_.dp(140));
      lv_obj_set_scrollable(imgBox, false);
      auto original = stringValue(m, "stored_path");
      if (original.empty()) original = stringValue(m, "original_path");
      if (original.empty()) original = path;
      image(imgBox, path, 0, 0, textWidth, uiMetrics_.dp(140),
            [this,original] { requestImageViewer(original); }, user, true);
    }
    if (!content.empty() || (live && receivingReply_) ||
        !webMetadata(id).value("web_records", Json::array()).empty()) {
      if (user) {
        if(readOnly)readableLabel(wrap,content,0,0,textWidth,readingFontSize());
        else label(wrap, content, 0, 0, textWidth, readingFontSize());
      }
      else {
        auto contentBox =
            box(wrap, 0, 0, textWidth, LV_SIZE_CONTENT, theme::palette().background);
        lv_obj_set_flex_flow(contentBox, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scrollable(contentBox, false);
        if (live)
          liveContent_ = contentBox;
        replyContent(contentBox, content, textWidth, id, live);
      }
    }
    if (retryableStatus(stringValue(m, "status"))) {
      label(wrap, stringValue(m, "status") == "error" ? "生成失败" :
            (stringValue(m, "status") == "interrupted" ? "上次生成已中断" : "已停止生成"),
            0, 0, textWidth, uiMetrics_.fontSize(UiFont::FormHint), theme::palette().danger);
    }
    if(!readOnly&&stringValue(m,"role")=="assistant")replyBar(wrap,m,textWidth,m["seq"].get<int64_t>()>questionSeq);
  }
}
void App::animateDisclosure(ReasoningControl &control, bool expanded) {
  auto detail = control.detail;
  if (!detail) return;
  control.expanded = expanded;
  // Preserve selections outside a section that is being collapsed.
  if (!expanded) {
    auto region = readRegion(readActive_);
    for (auto object = region ? region->root : nullptr; object; object = lv_obj_get_parent(object))
      if (object == detail) { readReset(); break; }
  }
  const int scroll = lv_obj_get_scroll_y(body_);
  const bool previousLayout = chatLayout_;
  chatLayout_ = true;
  const int start = lv_obj_is_hidden(detail) ? 0 : lv_obj_get_height(detail);
  motion::cancel(detail);
  lv_obj_set_hidden(detail, false);
  lv_obj_set_height(detail, LV_SIZE_CONTENT);
  lv_obj_update_layout(detail);
  control.naturalHeight = std::max(1, int(lv_obj_get_height(detail)));
  lv_obj_set_height(detail, start);
  auto wrap = control.wrap;
  motion::animate(detail, start, expanded ? control.naturalHeight : 0, 180,
      [this, detail, wrap](int height) {
        const bool previous = chatLayout_;
        chatLayout_ = true;
        const int scroll = lv_obj_get_scroll_y(body_);
        lv_obj_set_height(detail, height);
        lv_obj_update_layout(wrap);
        lv_obj_scroll_to_y(body_, scroll, LV_ANIM_OFF);
        paintVisible();
        if (readActive_ && readSelection_.selected()) readDraw(readMenuWanted_);
        chatLayout_ = previous;
      },
      [this, detail, wrap, expanded] {
        const bool previous = chatLayout_;
        chatLayout_ = true;
        const int scroll = lv_obj_get_scroll_y(body_);
        if (expanded) lv_obj_set_height(detail, LV_SIZE_CONTENT);
        else lv_obj_set_hidden(detail, true);
        lv_obj_update_layout(wrap);
        lv_obj_scroll_to_y(body_, scroll, LV_ANIM_OFF);
        chatLayout_ = previous;
        if (view_ == "chat") {
          chatScroll_ = lv_obj_get_scroll_y(body_);
          // The height animation can reach the real bottom after the tap's
          // original settle pass. Reuse normal reader settling so an active
          // drag or inertia still owns its position and follow state.
          readerSettle_ = true;
          updateChatControls(true);
        }
        paintVisible();
        if (readActive_ && readSelection_.selected()) readDraw(readMenuWanted_);
      });
  if (control.arrow) {
    auto arrow = control.arrow;
    motion::animate(arrow, disclosureProgress(arrow), expanded ? 1000 : 0, 180,
        [arrow](int progress) { setDisclosureProgress(arrow, progress); });
  }
  lv_obj_update_layout(wrap);
  lv_obj_scroll_to_y(body_, scroll, LV_ANIM_OFF);
  if (view_ == "chat") chatScroll_ = lv_obj_get_scroll_y(body_);
  chatLayout_ = previousLayout;
}
void App::toggleWeb(const std::string &id, bool expanded) {
  auto found = webControls_.find(id);
  if (found == webControls_.end()) return;
  if (expanded) expandedWeb_.insert(id); else expandedWeb_.erase(id);
  animateDisclosure(found->second, expanded);
}
void App::toggleReasoning(const std::string &id, bool expanded) {
  auto found = reasoningControls_.find(id);
  if (found == reasoningControls_.end()) return;
  auto &control = found->second;
  if (expanded) expandedReasoning_.insert(id); else expandedReasoning_.erase(id);
  if (!control.detail) {
    control.detail = box(control.wrap, 0, 0, control.width, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(control.detail, false);
    lv_obj_set_flex_flow(control.detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(control.detail, uiMetrics_.dp(52), 0);
    lv_obj_set_style_pad_top(control.detail, uiMetrics_.dp(2), 0);
    lv_obj_set_style_pad_bottom(control.detail, uiMetrics_.dp(8), 0);
    richMessage(control.detail, control.text, std::max(uiMetrics_.dp(44), control.width-uiMetrics_.dp(52)), true, id+":reasoning");
    if(!control.searchText.empty())readableLabel(control.detail,control.searchText,0,0,std::max(uiMetrics_.dp(44),control.width-uiMetrics_.dp(52)),uiMetrics_.fontSize(UiFont::FormNote),theme::palette().reasoning);
    lv_obj_move_to_index(control.detail, lv_obj_get_index(lv_obj_get_parent(control.arrow))+1);
    lv_obj_set_hidden(control.detail, true);
  }
  animateDisclosure(control, expanded);
}

void App::renderMessages(bool bottom, bool cached) {
  if (!messageContent_)
    return;
  if (readerInteracting()) {
    chatRenderPending_ = true;
    chatRefreshPending_ = chatRefreshPending_ || !cached;
    return;
  }
  chatRenderPending_ = chatRefreshPending_ = false;
  const int scroll = chatScroll_;
  const bool follow = messageOffset_ == 0 && (bottom || chatFollowing_);
  const bool previousLayout = chatLayout_;
  chatLayout_ = true;
  rasters_.clear();
  liveContent_ = nullptr;
  reasoningHeaders_.clear();reasoningControls_.clear();
  scaleMessageRows_.clear();
  lv_obj_clean(messageContent_);
  auto messages = cached ? rows_ : messageEdit_.empty()?db_.messages(session_, messageOffset_, 30):editedMessages();
  lv_obj_set_style_pad_top(messageContent_, messages.empty() ? 0 : uiMetrics_.dp(8), 0);
  if (!cached)
    reasoningDisplay_ =
        db_.settings().value("reasoning_display", std::string("collapsed"));
  expandedReasoning_.clear();
  rows_ = messages;
  webMessageMetadata_.clear();
  if (!cached)
    hasConversationContent_ =
        !db_.query("SELECT id FROM chat_messages WHERE session_id=? AND "
                   "(length(trim(coalesce(output_text,'')))>0 OR "
                   "message_type='image') LIMIT 1",
                   {session_})
             .empty();
  int width = layout().chatMessageWidth();
  if (!cached) refreshChatTitle();
  const auto questionSeq=db_.query("SELECT coalesce(min(seq),?) seq FROM chat_messages WHERE session_id=? AND role='user' AND (length(trim(coalesce(output_text,'')))>0 OR message_type='image')",{INT64_MAX,session_})[0]["seq"].get<int64_t>();
  bool more = int(messages.size()) >= 30;
  if (more)
    button(messageContent_, "加载更早消息", 0, 0, width, uiMetrics_.dp(35), [this] {
      messageOffset_ += 30;
      renderMessages(true);
    });
  if (messages.empty()&&draftState_.value("images",Json::array()).empty()) {
    const auto detail=db_.models().empty()?"先添加服务商和模型":"";
    const int available=std::max(0,int(lv_obj_get_height(body_))-chatComposerHeight()-layout().safeBottom()-uiMetrics_.dp(16));
    statusState(messageContent_,{StatusKind::Empty,PENCHAT_NAME,detail,""},uiMetrics_,
                font(uiMetrics_.fontSize(UiFont::Hero)),font(uiMetrics_.fontSize(UiFont::FormNote)),0,0,width,available);
  }
  if (!cached)
    lastAssistantSeq_ =
        db_.query("SELECT coalesce(max(seq),0) AS seq FROM chat_messages WHERE "
                  "session_id=? AND role='assistant' AND seq<?",
                  {session_,messageEdit_.value("pivot",INT64_MAX)})[0]["seq"]
            .get<int>();
  const auto lastAssistant = lastAssistantSeq_;
  auto visibleRows=messages;
  if(messageEdit_.empty()&&messageOffset_==0)
    for(const auto &image:draftRows())visibleRows.push_back(image);
  renderMessageRows(messageContent_,visibleRows,session_,width,false,lastAssistant,questionSeq);
  syncReplyControls();
  if(!messageEdit_.empty()) {
    auto banner=box(messageContent_,0,0,width,uiMetrics_.dp(44),theme::palette().background);
    lv_obj_set_scrollable(banner,false);
    label(banner,"发送后将从此处继续 · 后续对话已暂时隐藏",uiMetrics_.dp(8),uiMetrics_.dp(10),width-uiMetrics_.dp(16),uiMetrics_.fontSize(UiFont::FormNote),theme::palette().muted);
  }
  if (messageOffset_ > 0)
    button(messageContent_, "加载较新消息", 0, 0, width, uiMetrics_.dp(35), [this] {
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
  restoreScaleAnchor();
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
  replyContent(liveContent_, output_, lv_obj_get_width(liveContent_),
               assistant_, true);
  lv_obj_update_layout(messageContent_);
  lv_obj_scroll_to_y(body_, chatFollowing_ ? LV_COORD_MAX : scroll,
                     LV_ANIM_OFF);
  chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  chatLayout_ = previousLayout;
  updateChatControls(true);
  paintVisible();
  refreshImages();
}
void App::flushChatRender() {
  if (view_ != "chat" || readerInteracting()) return;
  if (!chatFinishPending_.empty() && !touchDown_) {
    auto finish = std::move(chatFinishPending_); chatFinishPending_ = Json::object();
    dirty_ = false;
    renderMessages(false);
    rebuildChatControls();
    if (!finish.value("error", std::string()).empty()) {
      modal("生成失败", finish["error"]);
      const auto message = finish["id"], session = finish["session"];
      button(modal_, "重试", screen_.width - uiMetrics_.dp(325), screen_.height - uiMetrics_.dp(66), uiMetrics_.dp(100), uiMetrics_.touchTarget(),
             [this, message, session] { events_.push("retry_request", {{"id", message}, {"session", session}}); });
    }
  } else if ((chatRenderPending_ || dirty_) && ticks() >= renderDue_) {
    renderDue_ = ticks() + 100;
    const bool full = chatRenderPending_, refresh = chatRefreshPending_;
    dirty_ = false;
    if (full) renderMessages(false, !refresh); else renderLiveMessage();
  }
  if (chatImagesPending_) { chatImagesPending_ = false; refreshImages(); }
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
    const int from=std::max(0,viewport.y1-area.y1-128);
    const int to=std::min(r.height,viewport.y2-area.y1+128);
    const int left=std::max(0,int(lv_obj_get_scroll_x(r.holder)));
    const int right=std::min(r.contentWidth,left+r.width+512);
    const int firstX=(left/512)*512,firstY=(from/128)*128;
    // Visible tiles form a rectangle in the fixed raster grid. Inclusive
    // viewport boundaries retain the same partial row as the original loop.
    for(auto it=r.tiles.begin();it!=r.tiles.end();) {
      const auto &position=it->first;
      if(to>=from&&position.first>=firstX&&position.first<right&&
          position.second>=firstY&&position.second<to) { ++it;continue; }
      lv_obj_delete(it->second); // imageWidget's DELETE callback owns texture cleanup.
      it=r.tiles.erase(it);
    }
    std::vector<std::pair<int,int>> positions;
    std::vector<RichWindow> windows;
    if(to>=from)
      for(int x=firstX;x<right;x+=512)
        for(int y=firstY;y<to;y+=128)
          if(r.tiles.find({x,y})==r.tiles.end()) {
            positions.push_back({x,y});
            windows.push_back({y,std::min(128,r.height-y),x,512});
          }
    if(windows.empty())continue;
    // Layout is shared only within this explicit batch. Create LVGL objects
    // after drawing finishes so callbacks cannot replace the prepared pieces.
    auto bitmaps=rich_.paintBatch(r.block,r.width,windows,r.fontSize,r.subdued);
    for(size_t i=0;i<positions.size();++i) {
      auto &bitmap=bitmaps[i];
      auto frame=std::make_shared<ImageFrame>();
      frame->width=bitmap.width;frame->height=bitmap.height;
      frame->pixels=std::move(bitmap.pixels);
      auto object=imageWidget(r.holder);
      lv_obj_set_pos(object,positions[i].first,positions[i].second);
      texture(object,frame,true);
      r.tiles.emplace(positions[i],object);
    }
  }
}
void App::refreshImages() {
  if (readerInteracting()) { chatImagesPending_ = true; return; }
  bool resized = false;
  for(const auto &entry:webImageRetries_) {
    const bool failed=images_.failed(entry.second);
    if(lv_obj_is_hidden(entry.first)==failed){lv_obj_set_hidden(entry.first,!failed);resized=true;}
  }
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
  if (view_ == "update") return view_ + ":" + updateState_.value("status",std::string()) + ":" + std::to_string(updateState_.value("note_index",size_t(0))) + ":" + std::to_string(updateState_.value("note_page",size_t(0)));
  if (view_ == "feedback") return view_ + ":" + feedbackView_ + ":" + std::to_string(feedbackPreviewPage_);
  if (view_ == "provider_models") return view_+":"+providerModelsId_;
  if (view_ == "provider") return view_ + ":" + (providerForm_.empty() ? "list" : providerForm_.value("id",std::string("new")));
  if (view_ == "model") return view_ + ":" + modelForm_.value("id",std::string());
  if (view_ == "preset_edit") return view_ + ":" + presetForm_.value("id",std::string());
  if (view_ == "album") return view_ + ":" + std::to_string(albumPage_);
  if (view_ == "archives") return view_ + ":" + std::to_string(archiveOffset_);
  if (view_ == "archive_chat")return view_+":"+archiveReaderId_+":"+std::to_string(archiveReaderOffset_);
  return view_;
}
void App::pageHeader(const std::string &title, int top) {
  readGroup(body_,"page:"+view_);
  // Header and business content share one full-height reader. No overlay may
  // cover the space released when the title scrolls out.
  const auto metrics = uiMetrics_.page();
  const int width = layout().bodyWidth();
  top+=layout().safeTop();
  const int titleHeight=font(uiMetrics_.fontSize(UiFont::PageTitle))->line_height;
  const int headerHeight=uiMetrics_.textPercent()==100?metrics.headerHeight:
      std::max(metrics.headerHeight,metrics.titleTop+titleHeight+uiMetrics_.dp(4));
  pageHeader_ = box(body_, 0, top, width, headerHeight);
  lv_obj_set_scrollable(pageHeader_, false);
  if(layout().portrait())icon(pageHeader_,"back",layout().navigationX(),metrics.actionTop,metrics.actionHeight,metrics.actionHeight,[this]{if(editor_.active){editorAccept();return;}goBack();});
  const int titleX=layout().portrait()?layout().navigationX()+metrics.navigationGap:metrics.titleInset;
  pageHeading_ = label(pageHeader_, title, titleX, metrics.titleTop,
    width-titleX-(layout().safety().enabled()?layout().navigationX():metrics.titleInset),UiFont::PageTitle);
  readLabel(pageHeading_,title);
  lv_label_set_long_mode(pageHeading_, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(pageHeading_,font(uiMetrics_.fontSize(UiFont::PageTitle))->line_height);
  pageContent_ = box(body_, 0, top + headerHeight, width, LV_SIZE_CONTENT);
  lv_obj_set_scrollable(pageContent_, false);
  lv_obj_set_style_min_height(pageContent_, screen_.height - top - metrics.minimumTail, 0);
  lv_obj_set_style_pad_bottom(body_, layout().readerBottomSpace(), 0);
  lv_obj_set_style_width(body_, metrics.scrollWidth, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(body_, lv_color_hex(theme::palette().muted), LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(body_, metrics.scrollWidth, LV_PART_SCROLLBAR);
}
lv_obj_t *App::pageAction(const std::string &title, int right, int width, std::function<void()> action) {
  const auto metrics = uiMetrics_.page();
  const int x = layout().portrait()?layout().bodyWidth()-right-width-layout().navigationX():screen_.width-layout().gutter()-right-width;
  auto result = button(pageHeader_, title, x, metrics.actionTop, width, metrics.actionHeight, std::move(action));
  pageActions_[title] = result;
  const int titleWidth = lv_obj_get_style_width(pageHeading_,LV_PART_MAIN);
  lv_obj_set_width(pageHeading_, std::max(metrics.minimumTitle,std::min(titleWidth,x-(layout().portrait()?metrics.portraitActionGap:metrics.actionGap))));
  return result;
}
Json App::pageDiagnostic() const {
  if (!pageHeader_ || !body_) return Json();
  auto bounds=[](lv_obj_t *o) {lv_area_t a;lv_obj_get_coords(o,&a);return Json::array({a.x1,a.y1,a.x2,a.y2});};
  Json actions=Json::object(), controls=Json::object();
  for(const auto &entry:pageActions_) {
    const bool control=entry.first.compare(0,7,"effort:")==0 || entry.first.compare(0,8,"mapping:")==0;
    (control?controls:actions)[entry.first]=bounds(entry.second);
  }
  for(const auto &entry:settingsControls_)controls[entry.first]=bounds(entry.second.row);
  return {{"viewport",bounds(body_)},{"heading",bounds(pageHeading_)},{"actions",actions},{"controls",controls},
          {"scrolling",lv_obj_is_scrolling(body_)}};
}
void App::settingsRow(const std::string &id, const std::string &title,
                      const std::string &detail, int x, int y, int width,
                      std::function<void()> action, int checked, bool unavailable) {
  const auto metrics = uiMetrics_.settings();
  const int rowHeight=settingsRowHeight();
  const int detailTop=uiMetrics_.textPercent()==100?metrics.detailTop:
      std::max(metrics.detailTop,metrics.titleTop+int(font(uiMetrics_.fontSize(UiFont::SettingsTitle))->line_height)+uiMetrics_.dp(4));
  auto row = box(pageContent_, x, y, width, rowHeight, theme::palette().surface);
  lv_obj_set_scrollable(row, false);
  lv_obj_set_style_radius(row, metrics.radius, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::palette().border), LV_STATE_PRESSED);
  auto caption = label(row, title, metrics.inset, metrics.titleTop, std::max(1, width - metrics.textTrailing), UiFont::SettingsTitle);
  auto value = label(row, detail, metrics.inset, detailTop, std::max(1, width - metrics.textTrailing), UiFont::Detail,
                     unavailable ? theme::palette().danger : theme::palette().muted);
  for (auto text : {caption, value}) {
    lv_label_set_long_mode(text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(text, lv_obj_get_style_text_font(text, LV_PART_MAIN)->line_height);
    lv_obj_set_clickable(text, false);
  }
  lv_obj_t *indicator;
  if (checked >= 0) {
    indicator = lv_switch_create(row);
    lv_obj_set_pos(indicator, width - metrics.toggleTrailing, metrics.toggleTop+(rowHeight-metrics.height)/2);
    lv_obj_set_size(indicator, metrics.toggleWidth, metrics.toggleHeight);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(theme::palette().border), 0);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(theme::palette().border), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(theme::palette().accent), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(theme::palette().text), LV_PART_KNOB);
    lv_obj_set_style_pad_all(indicator, metrics.knobPadding, LV_PART_KNOB);
    if (checked) lv_obj_add_state(indicator, LV_STATE_CHECKED);
  } else {
    indicator = box(row, width - metrics.arrowTrailing, metrics.arrowTop+(rowHeight-metrics.height)/2, metrics.arrowSize, metrics.arrowSize, theme::palette().surface);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollable(indicator, false);
    attachIcon(indicator, "chevron-right", theme::palette().muted, 1.f, uiMetrics_);
  }
  // The row is the only click target. The visual switch must not also toggle.
  lv_obj_set_clickable(indicator, false);
  if(layout().portrait()) {
    inputTarget(row,"setting:"+id,2);
    onClick(row,[this,row,action]{inputAction(row,action);});
  } else onClick(row,std::move(action));
  settingsControls_[id] = {row, caption, value, indicator, checked >= 0};
}
int App::settingsRowHeight() const {
  const auto metrics=uiMetrics_.settings();
  if(uiMetrics_.textPercent()==100)return metrics.height;
  const int detailTop=std::max(metrics.detailTop,metrics.titleTop+
      int(font(uiMetrics_.fontSize(UiFont::SettingsTitle))->line_height)+uiMetrics_.dp(4));
  return std::max(metrics.height,detailTop+int(font(uiMetrics_.fontSize(UiFont::Detail))->line_height)+uiMetrics_.dp(6));
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
  Json groups = Json::array();
  for (const auto &entry : settingsGroups_) groups.push_back({{"id", entry.first}, {"bounds", bounds(entry.second)}});
  const bool active = view_ == "settings" && body_;
  return {{"rows", rows}, {"groups", groups}, {"scrolling", active && lv_obj_is_scrolling(body_)},
          {"viewport", active ? bounds(body_) : Json()},
          {"heading", active && settingsHeading_ ? bounds(settingsHeading_) : Json()}};
}
void App::settingsPage() {
  pageHeader("设置");
  const auto flow=uiMetrics_.form();
  const int rowPitch=std::max(flow.settingsPitch,settingsRowHeight()+flow.settingsGap);
  settingsHeading_ = pageHeading_;
  const int width = layout().contentWidth();
  const bool twoColumns = layout().category() == LayoutClass::WideLandscape;
  const int left = twoColumns ? uiMetrics_.dp(10) : uiMetrics_.dp(16), gap = flow.columnGap, column = (width - gap) / 2;
  int y = flow.settingsTop;
  bool secondColumn = false;
  auto finishRow = [&] {
    if (secondColumn) { y += rowPitch; secondColumn = false; }
  };
  auto section = [&](const std::string &id, const std::string &name) {
    finishRow();
    // Completed rows already include 8px spacing; add 8px for the 16px group gap.
    if (!settingsGroups_.empty()) y += flow.settingsGap;
    auto heading = label(pageContent_, name, left, y, width, uiMetrics_.fontSize(UiFont::Detail), theme::palette().muted);
    const int height = std::max(flow.sectionMinimum, int(font(uiMetrics_.fontSize(UiFont::Detail))->line_height));
    lv_obj_set_height(heading, height);
    settingsGroups_.push_back({id, heading});
    y += height + flow.settingsGap;
  };
  auto row = [&](const std::string &id, const std::string &title, const std::string &detail,
                 std::function<void()> action, int checked = -1, bool unavailable = false,
                 bool fullWidth = false) {
    if (!twoColumns || fullWidth) {
      finishRow();
      settingsRow(id, title, detail, left, y, width, std::move(action), checked, unavailable);
      y += rowPitch;
    } else {
      settingsRow(id, title, detail, left + (secondColumn ? column + gap : 0), y, column,
                  std::move(action), checked, unavailable);
      secondColumn = !secondColumn;
      if (!secondColumn) y += rowPitch;
    }
  };
  const auto config = db_.settings();
  const auto settings = config.value("penchat_settings", Json::object());

  section("models", "模型");
  row("models", "模型管理", "Provider 与模型", [this] { show("provider"); }, -1, false, true);

  section("tools", "工具调用");
  const auto webPolicy = config.value("new_chat_web", std::string("on"));
  row("web_policy", "新聊天联网搜索",
      webPolicy == "off" ? "默认关闭" : webPolicy == "recent" ? "使用最近一次状态" : "默认开启",
      [this] { webPolicyPicker(); }, -1, false, true);

  section("chat", "聊天");
  row("presets", "会话预设", "管理会话配置", [this] { show("presets"); });
  const auto reasoningMode = config.value("reasoning_display", std::string("collapsed"));
  const std::string reasoningName = reasoningMode == "always" ? "始终展开"
                                    : reasoningMode == "during" ? "思考完毕后收起" : "不展开";
  row("reasoning", "思考过程", reasoningName, [this] { reasoningDisplayPicker(); });
  const bool autoTitle = config.value("auto_title_enabled", true);
  row("auto_title", "自动命名", "首条回复完成后生成标题", [this, autoTitle] {
    db_.setting("auto_title_enabled", !autoTitle);
    if (autoTitle) titleCancel_ = true;
    build();
  }, autoTitle);
  const auto titleModelId = config.value("title_model_id", std::string());
  std::string titleModelName = titleModelId.empty() ? "跟随当前聊天模型" : "指定模型不可用";
  bool titleModelMissing = !titleModelId.empty();
  for (const auto &model : db_.models()) {
    if (model["id"] == titleModelId) {
      titleModelName = model.value("display_name",model.value("name",std::string()));
      titleModelMissing = false;
    }
  }
  row("title_model", "命名模型", titleModelName, [this] { modelPicker(true); }, -1, titleModelMissing);
  const bool autoContext = config.value("auto_context_compression", true);
  row("auto_context", "自动上下文压缩", "压缩前归档，保留消息和图片", [this, autoContext] {
    db_.setting("auto_context_compression", !autoContext); build();
  }, autoContext);
  row("compression_threshold", "自动压缩阈值", std::to_string(compressionPercent(config)) + "% × 模型最大上下文",
      [this] { compressionSettings(); });
  row("image_auto_send", "图片自动发送", "开启后自动发送图片消息", [this] {
    auto value = db_.settings().value("penchat_settings", Json::object());
    // Read before assigning through []: GCC 6/C++14 operand order is unspecified.
    const bool enabled = value.value("autoSendCapturedContent", false);
    value["autoSendCapturedContent"] = !enabled;
    db_.setting("penchat_settings", value); build();
  }, settings.value("autoSendCapturedContent", false));
  const auto historyMode = historyTimeDisplay(config,layout().portrait());
  row("history_time", "历史时间显示", historyMode == "title" ? "会话标题优先" : historyMode == "off" ? "关闭时间显示" : "时间显示优先",
      [this] { historyTimePicker(); });
  const auto editDefault = config.value("message_edit_default", std::string("ask"));
  row("message_edit", "编辑消息", editDefault == "archive" ? "发送时先归档" : editDefault == "overwrite" ? "发送时直接覆盖" : "每次询问",
      [this] { messageEditDefaultPicker(); });
  const bool retryPrompt = config.value("retry_archive_prompt", false);
  row("retry_archive_prompt", "重试前提示归档", "开启后，重试较早回复时询问是否归档",
      [this, retryPrompt] { db_.setting("retry_archive_prompt", !retryPrompt); build(); }, retryPrompt);
  row("archives", "归档会话", "查看、还原或删除归档", [this] { archiveOffset_ = 0; show("archives"); });
  const auto offset = (pullSensitivityOffset_ > 0 ? "+" : "") + std::to_string(pullSensitivityOffset_) + "%";
  row("pull_sensitivity", "上滑新建灵敏度", "偏移 " + offset + (pullSensitivityOffset_ ? " · 相对新版基准" : " · 新版基准"),
      [this] { pullSensitivityPicker(); });
  const std::string shortcutMode = composerShortcut_.mode();
  row("composer_shortcut", "横屏输入框快捷唤出",
      shortcutMode == "off" ? "关闭" : shortcutMode == "manual" ? "开启，仅手动收回" : "开启，滚动后自动收回",
      [this] { composerShortcutPicker(); }, -1, false, true);

  section("app", "应用");
  row("theme", "主题", theme::mode()==theme::Mode::Light?"浅色":"深色", [this] {
    choiceMenu("theme",{{"dark","深色"},{"light","浅色"}},theme::name(theme::mode()),
      [this](const std::string &name){events_.push("theme_apply",{{"mode",name}});});
  });
  row("refresh_rate", "界面刷新率", std::to_string(display_.refreshRate())+"Hz", [this] {
    choiceMenu("refresh_rate",{{"30","30Hz"},{"60","60Hz"}},std::to_string(display_.refreshRate()),
      [this](const std::string &rate){events_.push("refresh_rate_apply",{{"hz",rate=="60"?60:30}});});
  });
  row("ui_scale", "界面缩放", std::to_string(uiMetrics_.scalePercent())+"% · 全应用", [this] {
    uiScaleDraft_=uiMetrics_.scalePercent();uiScaleError_.clear();show("ui_scale");
  });
  row("font_size", "字体大小", std::to_string(readingFontPercent_) + "% · 全应用", [this] {
    readingFontDraft_ = readingFontPercent_;fontError_.clear();show("font_size");
  });
  row("keyboard", "键盘与输入", "独立拼音与学习记忆", [this] { show("keyboard_settings"); });
  row("transfer", "导入与导出", "聊天与预设备份", [this] { openTransfer(); });
  const bool persistentExports = config.value("persistent_export_directory", false);
  row("persistent_exports", "持久化导出目录", persistentExports ? "关闭应用后保留导出文件" : "关闭应用时清理导出文件",
      [this, persistentExports] { db_.setting("persistent_export_directory", !persistentExports); build(); }, persistentExports);
  const auto idle = httpIdleSeconds();
  const auto idleText = idle < 60 ? std::to_string(idle) + "秒" : std::to_string(idle/60) + "分" + (idle%60 ? std::to_string(idle%60)+"秒" : "");
  row("http_idle", "连接保留时间", idleText + " · 全应用", [this] { show("http_settings"); });
  row("update", "检查更新", std::string("当前 ")+PENCHAT_VERSION+" · 在线安装新版本", [this] { openUpdate(true); });
  const auto pending = feedback_ ? feedback_->state().value("pending", size_t(0)) : 0;
  row("feedback", "问题反馈", std::to_string(pending) + " 份待处理 · 诊断与机型适配",
      [this] { feedbackView_ = "form"; show("feedback"); }, -1, false, true);
  row("about", "关于 " PENCHAT_NAME, "版本、存储、作者与项目地址", [this] {
    const auto summary = db_.summary();
    modal("关于 " PENCHAT_NAME,
          "版本：" PENCHAT_VERSION " · LVGL 9.6.0\n"
          "存储：" + summary["sessions"].dump() + " 个会话 / " + summary["messages"].dump() + " 条消息\n"
          "作者：" PENCHAT_AUTHOR "\n"
          "AI 协作：" PENCHAT_AI_CONTRIBUTOR "\n"
          "项目地址：" PENCHAT_PROJECT_URL);
    button(modal_,"使用引导",uiMetrics_.dp(16),screen_.height-uiMetrics_.dp(66),uiMetrics_.dp(112),uiMetrics_.dp(44),[this]{events_.push("open_onboarding");});
  }, -1, false, true);
  finishRow();
  lv_obj_set_height(pageContent_, std::max(y, int(lv_obj_get_style_min_height(pageContent_, LV_PART_MAIN))));
}
void App::updateFontPreview(int percent) {
  readingFontDraft_ = std::max(75, std::min(150, percent));
  if (fontSlider_)
    lv_slider_set_value(fontSlider_, (readingFontDraft_ + 2) / 5, LV_ANIM_OFF);
  if (fontPercentLabel_)
    lv_label_set_text(fontPercentLabel_, (std::to_string(readingFontDraft_) + "%").c_str());
  if (fontPreview_)
    lv_obj_set_style_text_font(fontPreview_, font(uiMetrics_.readingFontSize(readingFontDraft_)), 0);
}
void App::fontSizePage() {
  pageHeader("字体大小");
  auto reset=pageAction("恢复默认",uiMetrics_.dp(96),uiMetrics_.dp(120),[this] {
    if(fontRequest_.empty())updateFontPreview(100);
  });
  auto save=pageAction("保存",0,uiMetrics_.dp(84),[this]{events_.push("reading_font_apply");});
  primary(save);fontControls_.push_back(reset);fontControls_.push_back(save);
  const int left=uiMetrics_.dp(10),width=std::max(1,layout().contentWidth()-2*left);
  auto description=label(pageContent_,"调整全应用的文字，包括菜单、输入框、键盘、聊天和公式；界面缩放仍独立控制控件和间距。保存后生效。",
      left,uiMetrics_.dp(4),width,uiMetrics_.fontSize(UiFont::Detail),theme::palette().muted);
  lv_obj_update_layout(description);
  int y=lv_obj_get_y(description)+lv_obj_get_height(description)+uiMetrics_.dp(10);
  fontPercentLabel_=label(pageContent_,"",left,y,width,uiMetrics_.fontSize(UiFont::SettingsTitle));
  lv_obj_set_style_text_align(fontPercentLabel_,LV_TEXT_ALIGN_CENTER,0);
  y+=font(uiMetrics_.fontSize(UiFont::SettingsTitle))->line_height+uiMetrics_.dp(16);
  fontSlider_=lv_slider_create(pageContent_);lv_obj_remove_style_all(fontSlider_);
  const int inset=uiMetrics_.dp(22);
  lv_obj_set_pos(fontSlider_,left+inset,y);lv_obj_set_size(fontSlider_,std::max(1,width-2*inset),uiMetrics_.dp(16));
  lv_obj_set_ext_click_area(fontSlider_,uiMetrics_.dp(16));lv_slider_set_range(fontSlider_,15,30);
  for(auto part:{LV_PART_MAIN,LV_PART_INDICATOR,LV_PART_KNOB}) {
    lv_obj_set_style_bg_opa(fontSlider_,LV_OPA_COVER,part);lv_obj_set_style_radius(fontSlider_,LV_RADIUS_CIRCLE,part);
    lv_obj_set_style_bg_color(fontSlider_,lv_color_hex(part==LV_PART_MAIN?theme::palette().border:
        part==LV_PART_KNOB?theme::palette().text:theme::palette().accent),part);
  }
  lv_obj_set_style_pad_all(fontSlider_,uiMetrics_.dp(7),LV_PART_KNOB);
  lv_obj_add_event_cb(fontSlider_,[](lv_event_t *event) {
    auto app=static_cast<App *>(lv_event_get_user_data(event));
    if(app->fontRequest_.empty())app->updateFontPreview(lv_slider_get_value(app->fontSlider_)*5);
  },LV_EVENT_VALUE_CHANGED,this);
  fontControls_.push_back(fontSlider_);
  y+=uiMetrics_.dp(30);
  auto minimum=label(pageContent_,"75%",left,y,width/2,uiMetrics_.fontSize(UiFont::FormNote),theme::palette().muted);
  auto maximum=label(pageContent_,"150%",left+width/2,y,width/2,uiMetrics_.fontSize(UiFont::FormNote),theme::palette().muted);
  lv_obj_set_style_text_align(maximum,LV_TEXT_ALIGN_RIGHT,0);
  y+=font(uiMetrics_.fontSize(UiFont::FormNote))->line_height+uiMetrics_.dp(12);
  (void)minimum;
  fontStatus_=label(pageContent_,"",left,y,width,uiMetrics_.fontSize(UiFont::Detail),theme::palette().muted);
  y+=uiMetrics_.dp(24);
  fontPreview_=label(pageContent_,"文字大小预览 Aa 123\n菜单、输入和阅读使用同一字号偏好。",left,y,width,readingFontSize());
  updateFontPreview(readingFontDraft_);fontSizeBusy(!fontRequest_.empty());
}
void App::reasoningDisplayPicker() {
  choiceMenu("reasoning_display", {{"collapsed","不展开"},{"during","思考完毕后收起"},{"always","始终展开"}},
    db_.settings().value("reasoning_display",std::string("collapsed")),
    [this](const std::string &key){events_.push("reasoning_display",{{"mode",key}});});
}
void App::goBack() {
  if(!themeRequest_.empty()||!refreshRequest_.empty())return;
  if(view_=="ui_scale"&&!uiScaleRequest_.empty())return;
  if(view_=="font_size"&&!fontRequest_.empty())return;
  if (view_ == "web_source") {
    show("web_details");
    return;
  }
  if (view_ == "web_details") {
    show(webReturnView_);
    return;
  }
  if(view_=="onboarding"){if(onboardingReplay_)onboardingAction("finish");else onboardingAction("back");return;}
  if(onboardingConfig_&&view_=="provider"&&providerForm_.empty()){onboardingConfig_=false;show("onboarding");return;}
  if (view_ == "update") { updateBack(); return; }
  if (view_ == "feedback") {
    feedback_->request("keys.cancel");
    if (feedbackView_ == "form") show("settings");
    else { feedbackView_ = "form"; build(); }
    return;
  }
  if (view_ == "transfer") {
    transferBack();
    return;
  }
  if (view_ == "api_compatibility" || view_ == "anthropic_connection") {
    show("provider"); lv_obj_scroll_to_y(body_, providerScroll_, LV_ANIM_OFF);
  } else if (view_ == "ui_scale" || view_ == "font_size" || view_ == "keyboard_settings" || view_ == "http_settings")
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
    returnProvider();
  else if (view_ == "provider_models") {
    ++modelGeneration_;modelCancel_=true;modelNames_.clear();providerModelSelection_.clear();
    returnProvider();
  } else if (view_ == "provider") {
    if (!providerForm_.empty()) {
      if(!providerDirty()){leaveProvider();return;}
      choiceMenu("provider_exit",{{"edit","继续编辑"},{"discard","放弃修改"},{"save","保存并返回"}},"edit",
        [this](const std::string &key){if(key=="discard")leaveProvider();else if(key=="save")saveProvider(true);});
    } else show("settings");
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
  historyRows_.clear();navigationFooter_.clear();
  navigationBackdrop_ = navigationPointerDown_ = false;
  auto object = navigation_;
  navigation_ = navigationList_ = navigationActive_ = nullptr;
  if (object) {
    if(resumeCamera&&visible_&&inputFocused_) {
      auto panel=lv_obj_get_child(object,0);SurfaceSnapshot snapshot(panel);
      snapshot.show(-lv_obj_get_width(panel),0,true);
    }
    lv_obj_delete(object);
  }
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
  if(navigation_) {
    auto panel=lv_obj_get_child(navigation_,0);
    slideSurfaceIn(panel,-lv_obj_get_width(panel),0);
  }
}
lv_point_t App::photoPoint(lv_point_t point) const {
  if(layout().portrait())return point;
  // LVGL hit testing follows the rotated container; custom crop geometry needs
  // the same inverse transform before comparing with untransformed object bounds.
  const int x=point.x-screen_.width/2,y=point.y-screen_.height/2;
  if(cameraAngle_==90) return {screen_.width/2-y,screen_.height/2+x};
  if(cameraAngle_==180) return {screen_.width/2-x,screen_.height/2-y};
  if(cameraAngle_==270) return {screen_.width/2+y,screen_.height/2-x};
  return point;
}
void App::startCamera() {
  if(!visible_||!inputFocused_||screenPower_.blocked(ticks())||keyboard_)return;
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
void App::cameraControlsOrientation() {
  const int angle=photoTransforms(cameraAngle_,false).workspace;
  for(auto visual:cameraRotatingControls_) {
    lv_obj_set_style_transform_pivot_x(visual,lv_obj_get_width(visual)/2,0);
    lv_obj_set_style_transform_pivot_y(visual,lv_obj_get_height(visual)/2,0);
    lv_obj_set_style_transform_rotation(visual,angle*10,0);
  }
}
void App::cameraPage() {
  lv_obj_set_scrollable(body_, false);
  lv_obj_update_layout(body_);
  const int w=lv_obj_get_width(body_),h=lv_obj_get_height(body_);
  const auto workspace=layout().cameraWorkspace(w,h,cameraAngle_%180!=0);
  camera_.previewSize(workspace.preview.width,workspace.preview.height);
  auto preview=box(body_,workspace.preview.x,workspace.preview.y,workspace.preview.width,workspace.preview.height,theme::palette().background);
  lv_obj_set_scrollable(preview,false);
  cameraImage_=imageWidget(preview);lv_obj_center(cameraImage_);
  auto auxiliary=[this](const LayoutRect &area,const std::string &name,const std::string &title,std::function<void()> action) {
    auto control=button(body_,"",area.x,area.y,area.width,area.height,
      [this,action]{if(!cameraCapturing_)action();},theme::palette().background);
    lv_obj_set_style_radius(control,uiMetrics_.dp(12),0);
    lv_obj_set_style_bg_color(control,lv_color_hex(theme::palette().surface),LV_STATE_PRESSED);
    const bool stacked=area.height>=64;
    auto visual=control;
    const int visualWidth=layout().portrait()?uiMetrics_.dp(72):area.width;
    if(layout().portrait()) {
      // Rotate a square visual, never the larger hit area. All four turns stay
      // inside the same safe bottom row, including captions and their baselines.
      visual=box(control,(area.width-uiMetrics_.dp(72))/2,0,uiMetrics_.dp(72),uiMetrics_.dp(72));
      lv_obj_set_style_bg_opa(visual,LV_OPA_TRANSP,0);lv_obj_set_scrollable(visual,false);lv_obj_set_clickable(visual,false);
      cameraRotatingControls_.push_back(visual);
    }
    auto mark=box(visual,stacked?(visualWidth-uiMetrics_.dp(28))/2:uiMetrics_.dp(6),stacked?uiMetrics_.dp(4):(area.height-uiMetrics_.dp(28))/2,uiMetrics_.dp(28),uiMetrics_.dp(28));
    lv_obj_set_style_bg_opa(mark,LV_OPA_TRANSP,0);lv_obj_set_scrollable(mark,false);lv_obj_set_clickable(mark,false);
    attachIcon(mark,name,theme::palette().text, 1.f, uiMetrics_);
    const int captionSize=uiMetrics_.fontSize(stacked?UiFont::Detail:UiFont::FieldTitle);
    const int captionHeight=std::max(uiMetrics_.dp(24),int(font(captionSize)->line_height));
    auto caption=label(visual,title,stacked?uiMetrics_.dp(2):uiMetrics_.dp(36),stacked?uiMetrics_.dp(40):(area.height-captionHeight)/2,
      visualWidth-(stacked?uiMetrics_.dp(4):uiMetrics_.dp(40)),captionSize,theme::palette().muted);
    lv_obj_set_height(caption,captionHeight);lv_label_set_long_mode(caption,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_clickable(caption,false);
  };
  auxiliary(workspace.direction,cameraAutomatic_?"orientation-auto":"orientation-lock",
    cameraAutomatic_?"自动方向":"锁定方向",
    [this]{events_.push("camera_mode",{{"auto",!cameraAutomatic_},{"angle",cameraAngle_}});});
  auto shutter=box(body_,workspace.shutter.x,workspace.shutter.y,workspace.shutter.width,workspace.shutter.height,theme::palette().background);
  lv_obj_set_scrollable(shutter,false);lv_obj_set_style_radius(shutter,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(shutter,uiMetrics_.dp(3),0);lv_obj_set_style_border_color(shutter,lv_color_hex(theme::palette().text),0);
  auto center=box(shutter,uiMetrics_.dp(7),uiMetrics_.dp(7),workspace.shutter.width-uiMetrics_.dp(14),workspace.shutter.height-uiMetrics_.dp(14),theme::palette().text);lv_obj_set_style_radius(center,LV_RADIUS_CIRCLE,0);
  lv_obj_set_scrollable(center,false);lv_obj_set_clickable(center,false);lv_obj_center(center);
  auto shutterMark=box(center,0,0,uiMetrics_.dp(28),uiMetrics_.dp(28));
  lv_obj_set_style_bg_opa(shutterMark,LV_OPA_TRANSP,0);lv_obj_set_scrollable(shutterMark,false);lv_obj_set_clickable(shutterMark,false);lv_obj_center(shutterMark);
  attachIcon(shutterMark,"camera",theme::palette().primaryText, 1.f, uiMetrics_);
  if(layout().portrait())cameraRotatingControls_.push_back(shutterMark);
  onClick(shutter,[this]{if(inputFocused_&&!screenPower_.blocked(ticks())&&!cameraCapturing_){cameraCapturing_=true;camera_.capture(cameraAngle_);}});
  auxiliary(workspace.turn,"rotate90","转向90°",
    [this]{events_.push("camera_mode",{{"auto",false},{"angle",(cameraAngle_+90)%360}});});
  lv_obj_update_layout(body_);cameraControlsOrientation();
  status_=label(preview,cameraError_.empty()?"相机启动中…":cameraError_,uiMetrics_.dp(12),layout().portrait()?layout().safeTop()+uiMetrics_.dp(56):uiMetrics_.dp(12),lv_obj_get_width(preview)-uiMetrics_.dp(24),uiMetrics_.fontSize(UiFont::FormCaption),theme::palette().muted);
}
void App::albumPage() {
  int w = layout().contentWidth();
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
    const int columns=layout().portrait()?std::max(1,w/uiMetrics_.dp(160)):3;
    int pos = i - begin, x = uiMetrics_.dp(10) + (pos % columns) * (w / columns), y = uiMetrics_.dp(4) + (pos / columns) * uiMetrics_.dp(145);
    auto preview =
        scanPreviews.count(path) ? scanPreviews[path] : std::string();
    image(pageContent_, preview.empty() ? path : preview, x, y, w / columns - uiMetrics_.dp(15), uiMetrics_.dp(105),
          [this, path, preview] {
            Json image = {{"path", path}, {"source", "album"}};
            if (!preview.empty())
              image["previewPath"] = preview;
            attach(image);
          });
    auto caption = label(pageContent_, basename(path), x, y + uiMetrics_.dp(108), w / (layout().portrait()?std::max(1,w/uiMetrics_.dp(160)):3) - uiMetrics_.dp(20), uiMetrics_.fontSize(UiFont::FieldTitle));
    lv_label_set_long_mode(caption, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(caption, font(uiMetrics_.fontSize(UiFont::FieldTitle))->line_height);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
  }
  const int columns=layout().portrait()?std::max(1,w/uiMetrics_.dp(160)):3;
  int y = uiMetrics_.dp(4) + ((end - begin + columns-1) / columns) * uiMetrics_.dp(145);
  if (albumPage_)
    button(pageContent_, "上一页", uiMetrics_.dp(10), y, uiMetrics_.dp(140), uiMetrics_.dp(44), [this] {
      --albumPage_;
      build();
    });
  if (end < int(files.size()))
    button(pageContent_, "下一页", uiMetrics_.dp(170), y, uiMetrics_.dp(140), uiMetrics_.dp(44), [this] {
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
  receivingReply_ = false;
  assistant_.clear();
  ++networkGeneration_;
}
bool App::canSend() {
  if(messageEditBusy_)return false;
  if(!messageEdit_.empty())return hasText(draft_)||!messageEditImages_.empty();
  if (!draftState_.value("images",Json::array()).empty() || hasText(draft_))
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
      (!hasText(draft_) && draftState_.value("images",Json::array()).empty() && failed ? stringValue(latest[0], "id") : std::string());
  if(!retryId.empty()){retryWarning(retryId);return;}
  if (retryId.empty() && !canSend()) return;
  sendActionAt_=nowMs();
  startContext(false, retryId);
}
void App::sendPrepared(Json model, Json input, const std::string &retryId,
                       const std::string &draft,Json prepared,Json checkpoint) {
  if (input.empty()) {
    build();
    return;
  }
  if (!model.contains("web_enabled"))
    model["web_enabled"] = webEnabled(sessionPreferences_, db_.settings());
  model["web_binary"] = join(workspace_, "assets/bin/penchat-web-search");
  model["web_asset_root"] = join(dataDir_, "assets");
  bindPreferences(model);
  sessionPreferences_["has_sent"] = true;
  std::string preparedAssistant;
  if(retryId.empty()) {
    const auto sent=db_.commitDraftSend(contextDraft_,session_,normalizedChatText(draft),
        sessionPreferences_,model,sendActionAt_,prepared,checkpoint);
    preparedAssistant=sent.at("message").get<std::string>();
    loadDraft(session_);
  }
  if (network_.joinable())
    network_.join();
  messageOffset_ = 0;
  chatFollowing_ = true;
  if (retryId.empty())
    assistant_ = preparedAssistant.empty()?db_.append(session_, "assistant", "", Json::object(), sessionPreferences_):preparedAssistant;
  else {
    assistant_ = retryId;
    db_.execute("UPDATE chat_messages SET metadata_json='{}' WHERE id=?", {retryId});
    db_.updateMessage(retryId, "", "", "streaming");
    reasoningOverrides_.erase(retryId);
  }
  const auto started=nowMs();
  messageTimeline_={{"version",1},{"request_started_at",started}};
  if(preparedAssistant.empty())db_.recordRequest(assistant_, model,sendActionAt_?sendActionAt_:started,started);
  notice_.clear();
  reasoningMetadata_ = Json::object();
  output_.clear();
  reasoning_.clear();
  generating_ = true;
  receivingReply_ = true;
  cancel_ = false;
  int generation = ++networkGeneration_;
  persistDue_ = ticks() + 500;
  persistDirty_ = false;
  saveInFlight_ = false;
  saveVersion_ = 0;
  network_ = std::thread([this, model = std::move(model), input = std::move(input),
                          session = session_, message = assistant_, generation] {
    try {
      auto requestModel = model.contains("api_key") ? model : db_.requestModel(model);
      std::function<Json(const Json &)> compact;
      if (requestModel.value("web_enabled", false)) {
        auto toolSnapshot = db_.contextSnapshot(session, false, true);
        auto &toolRows = toolSnapshot["rows"];
        toolRows.erase(std::remove_if(toolRows.begin(), toolRows.end(), [&](const Json &row) {
          return row["id"] == message;
        }), toolRows.end());
        compact = toolContextCompactor(std::move(toolSnapshot),
            requestModel.value("_compression_model", Json::object()), requestModel, cancel_);
      }
      requestModel.erase("_compression_model");
      auto saveCompaction = [&, compact](const Json &state) mutable {
        auto result = compact(state);
        if (result.contains("checkpoint"))
          result["checkpoint"] = db_.commitToolCompression(session, message, result["checkpoint"], state, cancel_);
        return result;
      };
      streamChat(requestModel, input, cancel_, events_, generation, std::move(saveCompaction));
    } catch (const std::exception &error) {
      events_.push("network_error", {{"generation", generation}, {"error", error.what()}});
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
  saveDraft();db_.flush();
  const auto draftSnapshot=draftState_;
  const auto editToken=messageEdit_.empty()?std::string():messageEditToken_;
  modal("正在保存图片", "图片准备完成后会自动返回，请稍候…");
  imageStoreWorker_ = std::thread([this, image, session,editToken,draftSnapshot] {
    bool ok = true;
    Json asset=Json::object();std::vector<std::string> created;
    try {
      if(editToken.empty())db_.addDraftImage(draftSnapshot,image);
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
void App::cancelNativeInteraction(bool preserveSettledReadSelection) {
  chatBottomState_.pause();
  // A short touch-only lease gap is not a page/focus transition. Keep a
  // completed read selection, but never carry an active drag or menu press.
  if(!preserveSettledReadSelection || readGesture_.active() || readMenuPointer_ ||
      !readActive_ || !readSelection_.selected())readReset();
  readDown_=readSuppressed_=false;
  touchInput_.close(); input_.interrupt();
  systemEdgeDown_=systemEdgeGesture_=false;
  readerDown_ = readerPointer_ = readerVertical_ = readerSettle_ = false;
  navigationBackdrop_ = navigationPointerDown_ = false;
  // The cancelled contact may have started while a popup snapshot was retiring.
  // Its release is rejected after focus loss, so do not carry release absorption
  // into the next independently focused contact.
  popupConsumeTouch_ = contextMenuOpening_ = popupTracking_ = popupBack_ = false;
  imageDeleteScrolling_=false;
  officialComposerHit_.clear();officialContactIds_.clear();officialActionId_.clear();officialActionKind_.clear();officialMenuAction_={};officialPopupContact_=false;officialTextContact_=false;officialEditPending_=0;
  stopOfficialReaderGlide();officialReaderDown_=officialReaderCaptured_=officialReaderDragging_=false;officialReaderPending_=0;
  resetBack(); resetPull(); touchDown_ = pullTrackActivity_ = false;
  finishImageGesture(false); nonImageMulti_ = false; viewerLastTap_ = 0;
  if (imageViewer_ || view_ == "crop") imageGesture_.mode = -2;
  // A short input-heartbeat gap cancels fingers/keys, not an OCR stroke.
  // Media has a separate lease and explicit focus loss still revokes it.
  if (editor_.active) editorInterrupt(!inputFocused_);
}
void App::recordInputFocus(InputFocusTrace::Reason reason, bool value, uint64_t sequence) {
  InputFocusTrace::Entry entry;
  entry.time=ticks();const auto state=inputFocus_.snapshot(entry.time);
  entry.reason=reason;entry.value=value;entry.eventSequence=sequence;
  entry.required=state.required;entry.active=state.active;entry.sequence=state.sequence;entry.age=state.age;
  entry.page=pageVisible_;entry.window=windowFocused_;entry.official=keyboard_;entry.touch=touchLeaseActive_;
  unsigned bit=1;
  for(const char *key:{"window_api","window_valid","app_focused","panel_api","panel_available","panel_supported","panel_visible"}) {
    const auto found=systemOverlayStatus_.find(key);
    if(found!=systemOverlayStatus_.end()&&found->is_boolean()) {
      entry.overlayKnown|=bit;if(found->get<bool>())entry.overlayValues|=bit;
    }
    bit<<=1;
  }
  inputFocusTrace_.record(entry);
}
Json App::inputFocusDiagnostics() const {
  Json rows=Json::array();
  const char *reasons[]={"input_focus_applied","window_focus_received_before_apply","overlay_status_applied","touch_lease_applied_before_cancel"};
  for(size_t i=0;i<inputFocusTrace_.size();++i) {
    const auto &entry=inputFocusTrace_.at(i);
    Json overlay=Json::object();unsigned bit=1;
    for(const char *key:{"window_api","window_valid","app_focused","panel_api","panel_available","panel_supported","panel_visible"}) {
      overlay[key]=(entry.overlayKnown&bit)?Json(bool(entry.overlayValues&bit)):Json();bit<<=1;
    }
    rows.push_back({{"time",entry.time},{"reason",reasons[entry.reason]},{"event_value",entry.value},
      {"event_sequence",entry.eventSequence},{"focus_sequence",entry.sequence},{"lease_age_ms",entry.age},
      {"focus_required",entry.required},{"focus_active",entry.active},{"page_visible",entry.page},
      {"window_focused",entry.window},{"official_keyboard",entry.official},{"touch_lease",entry.touch},{"overlay",overlay}});
  }
  return {{"capacity",uint64_t(InputFocusTrace::Capacity)},{"total",inputFocusTrace_.total()},{"entries",rows}};
}
bool App::officialReaderInput() const {
  const auto now=ticks();
  return visible_&&pageVisible_&&(view_=="chat"||editorInPlace())&&body_&&!modal_&&!navigationOpen_&&
    keyboard_&&officialDocked_&&officialForeground_&&now>=officialLayoutAt_&&now-officialLayoutAt_<750&&
    display_.visible()&&display_.topHeight()>0&&!screenPower_.blocked(now)&&!systemOverlayStatus_.value("panel_visible",false);
}
std::string App::officialComposerActionAt(int x,int y) const {
  if(!chatControls_||lv_obj_is_hidden(chatControls_)||y>=display_.topHeight())return {};
  auto hit=[&](lv_obj_t *object){
    if(!object||lv_obj_is_hidden(object)||lv_obj_has_state(object,LV_STATE_DISABLED))return false;
    lv_area_t area;lv_obj_get_coords(object,&area);
    return x>=area.x1&&x<=area.x2&&y>=area.y1&&y<=area.y2;
  };
  if(hit(toBottom_))return "bottom";
  if(hit(lv_obj_get_child(chatControls_,layout().portrait()?1:0)))return "attachments";
  if(hit(composerGauge_))return "effort";
  if(hit(lv_obj_get_child(chatControls_,-1)))return generating_||contextBusy_?"stop":"send";
  return {};
}
void App::stopOfficialReaderGlide() {
  if(officialReaderGlideBody_&&officialReaderGlideBody_==body_)lv_obj_stop_scroll_anim(body_);
  officialReaderGlideBody_=nullptr;officialReaderMomentum_=0;officialReaderVelocity_=0;
}
void App::officialReaderTouch(const Json &message) {
  if(modal_) {
    if(message.value("y",0)>=display_.topHeight())return;
    if(message.value("cancelled",false))input_.cancel();
    else input_.touch(message.value("x",0),message.value("y",0),message.value("pressed",false));
    return;
  }
  const bool down=message.value("pressed",false);
  const bool cancel=message.value("cancelled",false)||
    (message.contains("points")&&message["points"].is_array()&&message["points"].size()>1);
  const int x=message.value("x",0),y=message.value("y",0);
  // The official IME keeps unconfirmed phonetics outside the proxy value.
  // Only an actual text-input event acknowledges a system-key contact; cursor
  // heartbeats must not clear it. Kernel event time handles cross-process order.
  bool systemKey=false;
  if(message.contains("points")&&message["points"].is_array()) {
    std::set<std::string> contacts;
    for(const auto &point:message["points"])if(point.is_object()&&point.contains("id")) {
      const auto id=point["id"].dump();contacts.insert(id);
      if(!officialContactIds_.count(id)&&point.value("y",0)>=display_.topHeight())systemKey=true;
    }
    officialContactIds_=std::move(contacts);
  } else systemKey=down&&!officialReaderDown_&&y>=display_.topHeight();
  if(systemKey) {
    const auto now=nowMs();auto at=message.value("event_time_ms",now);
    if(at<=0||std::abs(at-now)>5000)at=now;
    officialKeyEventAt_=std::max(officialKeyEventAt_,at);
  }
  if(cancel){if(officialTextContact_){editorTouch(x,y,false,true);officialTextContact_=false;}if(officialPopupContact_)input_.interrupt();officialPopupContact_=false;officialComposerHit_.clear();stopOfficialReaderGlide();officialReaderPending_=0;officialReaderCaptured_=false;officialReaderDown_=down;return;}
  if(!attachments_) {
    if(down&&!officialReaderDown_&&!officialEditPending_&&officialKeyEventAt_<=officialTextEventAt_) {
      const auto area=editorTextArea();officialTextContact_=area.contains(x,y);
      for(auto object:{editorSelectionMenu_,editorHandles_[0],editorHandles_[1]})if(object&&!lv_obj_is_hidden(object)) {
        lv_area_t bounds;lv_obj_get_coords(object,&bounds);
        officialTextContact_|=x>=bounds.x1&&x<=bounds.x2&&y>=bounds.y1&&y<=bounds.y2;
      }
    }
    if(officialTextContact_) {
      const auto base=editor_.text;const bool textContact=editorTextTouch_;
      editorTouch(x,y,down,cancel);
      officialReaderDown_=down;
      if(!down){officialTextContact_=false;if(textContact)officialEditSync(base);}
      return;
    }
  }
  if(attachments_) {
    if(down&&!officialReaderDown_)officialPopupContact_=y>=officialReaderBounds_.y1&&y<display_.topHeight();
    if(officialPopupContact_) {
      auto event=message;
      if(cancel||y>=display_.topHeight()){event["cancelled"]=true;event["pressed"]=false;officialPopupContact_=false;}
      popupTouch(event);
    }
    if(!down)officialPopupContact_=false;
    officialReaderDown_=down;return;
  }
  if(!down){
    const auto action=officialComposerHit_;officialComposerHit_.clear();
    if(!action.empty()) {
      if(!officialComposerMoved_&&officialComposerActionAt(x,y)==action&&officialActionId_.empty()) {
        if(action=="attachments"||action=="bottom"||action=="stop") {
          if(action=="menu") {auto callback=std::move(officialMenuAction_);officialMenuAction_={};if(callback)callback();}
        else if(action=="attachments")toggleAttachments();else if(action=="bottom")jumpToBottom();else if(contextBusy_)cancelContext();else if(generating_)cancel_=true;
          officialReaderDown_=officialReaderCaptured_=officialReaderDragging_=false;return;
        }
        officialActionId_=uid("official-action");officialActionKind_=action;officialActionSession_=session_;officialActionAt_=ticks();
        if(!bridge_.send("keyboard.commit-action",{{"requestId",keyboardId_},{"actionId",officialActionId_},{"action",action},{"uncommitted",officialKeyEventAt_>officialTextEventAt_},{"keyEventTime",officialKeyEventAt_}}))officialActionId_.clear();
      }
      officialReaderDown_=officialReaderCaptured_=officialReaderDragging_=false;return;
    }
    if(officialReaderCaptured_&&officialReaderDragging_&&ticks()-officialReaderSampleAt_<100) {
      const int limit=std::max(1,display_.topHeight()*3/4);
      officialReaderMomentum_=std::max(-limit,std::min(limit,int(std::lround(officialReaderVelocity_*100.))));
      if(std::abs(officialReaderMomentum_)<uiMetrics_.dp(8))officialReaderMomentum_=0;
    }
    officialReaderDown_=officialReaderCaptured_=officialReaderDragging_=false;return;
  }
  if(!officialReaderDown_) {
    stopOfficialReaderGlide();
    officialReaderDown_=true;officialReaderDragging_=false;
    officialReaderBounds_={0,std::max(uiMetrics_.dp(8),layout().safeTop()),screen_.width-1,display_.topHeight()-1};
    officialReaderSampleAt_=ticks();officialReaderSampleY_=y;
    officialReaderX_=x;officialReaderY_=officialReaderLastY_=y;
    officialComposerHit_=officialActionId_.empty()?officialComposerActionAt(x,y):std::string();officialComposerMoved_=false;
    officialReaderCaptured_=x>=officialReaderBounds_.x1&&x<=officialReaderBounds_.x2&&
      y>=officialReaderBounds_.y1&&y<=officialReaderBounds_.y2;
    if(chatControls_) {
      lv_area_t editor;lv_obj_get_coords(chatControls_,&editor);
      if(x>=editor.x1&&x<=editor.x2&&y>=editor.y1&&y<=editor.y2)officialReaderCaptured_=false;
    }
    return;
  }
  if(!officialComposerHit_.empty()){officialComposerMoved_|=std::abs(x-officialReaderX_)>uiMetrics_.dp(12)||std::abs(y-officialReaderY_)>uiMetrics_.dp(12);return;}
  if(!officialReaderCaptured_)return;
  if(x<officialReaderBounds_.x1||x>officialReaderBounds_.x2||y<officialReaderBounds_.y1||y>officialReaderBounds_.y2) {
    officialReaderCaptured_=false;return;
  }
  if(!officialReaderDragging_) {
    const int dx=x-officialReaderX_,dy=y-officialReaderY_;
    if(std::abs(dy)<=uiMetrics_.gestures().drag||std::abs(dy)<=std::abs(dx))return;
    officialReaderDragging_=true;chatFollowing_=false;lv_obj_stop_scroll_anim(body_);
  }
  const uint64_t now=ticks(),elapsed=now-officialReaderSampleAt_;
  if(elapsed>=8) {
    const double velocity=double(y-officialReaderSampleY_)/elapsed;
    officialReaderVelocity_=officialReaderVelocity_*velocity<0?velocity:.35*officialReaderVelocity_+.65*velocity;
    officialReaderSampleAt_=now;officialReaderSampleY_=y;
  }
  officialReaderPending_+=y-officialReaderLastY_;
  officialReaderLastY_=y;
}
void App::applyScreenPower(const Json &state) {
  if(!state.is_object()||!state.contains("sequence")||!state["sequence"].is_number())return;
  const double sequence=state["sequence"].get<double>();
  if(!std::isfinite(sequence)||sequence<=0||sequence>9007199254740991.||std::floor(sequence)!=sequence)return;
  for(const char *key:{"available","known","awake"})if(!state.contains(key)||!state[key].is_boolean())return;
  screenPower_.update(uint64_t(sequence),state["available"].get<bool>(),
    state["known"].get<bool>(),state["awake"].get<bool>(),ticks());
  updateInputFocus();
}
void App::updateInputFocus() {
  const auto now=ticks();
  const bool powerPaused=screenPower_.blocked(now);
  if(powerPaused!=screenPowerPaused_) {
    screenPowerPaused_=powerPaused;
    // Neither the old release nor a held wake contact can activate a control.
    inputFocus_.block();motion::finishAll();cancelNativeInteraction();
    diagnosticRecord("lifecycle",powerPaused?"screen_power_paused":"screen_power_resumed");
  }
  const bool partialOfficial=keyboard_&&officialDocked_&&officialForeground_&&
      now>=officialLayoutAt_&&now-officialLayoutAt_<750&&display_.topHeight()>0&&
      display_.supportsTopRegion()&&!powerPaused&&!systemOverlayStatus_.value("panel_visible",false);
  const bool foregroundKnown=display_.systemOverlaySafe()||systemOverlayStatus_.value("window_valid",false);
  const auto permission=inputFocus_.permissions(now,pageVisible_,windowFocused_&&foregroundKnown&&!powerPaused&&!systemOverlayStatus_.value("panel_visible",false),keyboard_,
      partialOfficial,display_.systemOverlaySafe());
  const bool retainCovered=systemOverlayStatus_.value("panel_visible",false)&&display_.systemOverlaySafe();
  const bool media=permission.media&&pageAttached_,shown=(permission.display||(pageVisible_&&retainCovered))&&pageAttached_&&!powerPaused;
  if (display_.visible() != shown) display_.visible(shown);
  if(inputFocused_!=media) {
    inputFocused_=media;
    if(!media) {
      editorDefaultOfficial_.clear();
      camera_.stop();
      // This can occur after input already expired, so it must not depend on
      // touchLeaseActive_ changing a second time to release the OCR session.
      if(editor_.active){
        editorPolicy(false);editorOcrStop();
        if(editorLeasePaused_)editorInterrupt();
      }
    } else {
      if(view_=="camera"&&!keyboard_)startCamera();
      if(!penObserverReady_&&penObserverRequest_.empty()) {
        penObserverRequest_=uid("pen-observer-reset");
        bridge_.send("pen.observe",{{"requestId",penObserverRequest_},{"enabled",false}});
      }
    }
    syncScanner();
  }
  const bool touch=permission.input&&display_.available()&&!powerPaused;
  if(touchLeaseActive_==touch)return;
  touchLeaseActive_=touch;
  recordInputFocus(InputFocusTrace::TouchApplied,touch);
  if(!touch) {
    editorLeasePaused_=editor_.active&&inputFocused_;
    cancelNativeInteraction(inputFocused_&&visible_&&!keyboard_);
  }
  else if(editor_.active){
    editorPolicy(!editorSelectionOnly_);
    if(!editorLeasePaused_) {
      auto notice=std::move(editor_.notice);editorChanged();
      if(editor_.notice.empty())editor_.notice=std::move(notice);
    }
    editorLeasePaused_=false;
    editorOcrStart();
  }
}
void App::suspend(bool value) {
  if(value){saveDraft();db_.flush();}
  if(pageVisible_==!value){updateInputFocus();return;}
  if(value){penObserverRequest_.clear();penObserverReady_=false;}
  if(value)chatBottomState_.pause();
  if(value)motion::finishAll();
  if (value) composerShortcut_.reset();
  if(value)readReset();
  if(value)readDown_=readSuppressed_=false;
  pageVisible_=!value;
  if(value)inputFocus_.block();
  diagnosticRecord("lifecycle", value ? "background" : "foreground");
  if(onboarding_)onboarding_->tick(false);
  if (value && feedback_) feedback_->background();
  if (value) cancelBalances();
  if(value){popupConsumeTouch_=contextMenuOpening_=false;if(contextMenuPanel_)closeHistoryContext();else closeAttachments();}
  readerDown_ = readerPointer_ = readerVertical_ = readerSettle_ = false;
  resetBack();
  backStart_ = Json::object();
  viewerLastTap_ = 0;
  finishImageGesture(false);nonImageMulti_=false;
  if(imageViewer_ || view_=="crop")imageGesture_.mode=-2;
  touchInput_.close();
  systemEdgeDown_=systemEdgeGesture_=false;
  if (value && imageStoreWorker_.joinable())
    imageStoreWorker_.join();
  if (value) {replyCancel_=true;if(contextRegenerating_)cancelContext();editorInterrupt();}
  if (value) {
    visible_ = false;
    cancelWebReader();
    if(viewerScanResult_)closeImageViewer(false);
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
  if(value)motion::finishAll();
  resetPull();
  touchDown_ = false;
  pullTrackActivity_ = false;
  visible_ = pageVisible_;
  if (!value && visible_ && contextMenuKind_ == "balances") refreshBalances();
  input_.cancel();
  updateInputFocus();
  if (value) {
    camera_.stop();
  } else if (visible_ && editor_.active && !keyboard_) {
    editorPolicy(!editorSelectionOnly_);
    auto notice=std::move(editor_.notice);editorChanged();
    if(editor_.notice.empty())editor_.notice=std::move(notice);
    editorFollowCursor_=false;editorBuild();editorOcrStart();
  }
  else if (visible_ && view_ == "camera")
    startCamera();
}
void App::windowFocus(bool focused) {
  if(windowFocused_==focused)return;
  windowFocused_=focused;
  if(!focused)motion::finishAll();
  updateInputFocus();
}
void App::shutdown() {
#ifdef PENCHAT_CVI_PLATFORM
  display_.mediaHandoff(true);
#endif
  if (shutdownComplete_) return;
  saveDraft();db_.flush();
  shutdownComplete_ = true;
  motion::finishAll();
  cancelWebReader();
  cancelBalances();
  if (balanceService_) balanceService_->stop();
  if(onboarding_)onboarding_->tick(false);
  touchInput_.close();
  input_.cancel();
  // Signal every independent task before waiting for any one of them. A slow
  // compaction/image job must not leave another request or capture running.
  cancel_ = true; modelCancel_ = true; titleCancel_ = true;
  contextCancel_ = true; replyCancel_ = true;
  resetBack();
  closeImageViewer(false);
  display_.visible(false);
  images_.stop();
  scanner_.eligible(false, session_);
  scanner_.stop();
  camera_.stop();
  cancelContext();
  if (sessionInfoWorker_.joinable()) sessionInfoWorker_.join();
  replyCancel_=true;if(replyWorker_.joinable())replyWorker_.join();replyBusy_=false;
  if (updater_) updater_->stop();
  if (updatePrepareWorker_.joinable()) updatePrepareWorker_.join();
  if (transfer_)
    transfer_->stop();
  if (imageStoreWorker_.joinable())
    imageStoreWorker_.join();
  if(messageEditWorker_.joinable())messageEditWorker_.join();
  messageEditBusy_=false;cancelMessageEdit(false);
  cleanupMessageEdits();
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
  if (feedback_) feedback_->stop();
  diagnosticRecord("lifecycle", "stop"); flushDiagnostics();
  shutdownHttp();
  db_.flush();
  const auto rescueMarker=join(dataDir_,".rescue-preserve-public");
  const auto rescueRecord=Json::parse(fileText(rescueMarker,1024),nullptr,false);
  const bool ownRescue=rescueRecord.is_object()&&rescueRecord.value("pid",-1)==int(getpid());
  const auto rescueAt=ownRescue?rescueRecord.value("time_ms",int64_t(0)):int64_t(0);
  const bool rescueExit=ownRescue&&nowMs()>=rescueAt&&nowMs()-rescueAt<300000;
  if(ownRescue)unlink(rescueMarker.c_str());
  if (transfer_ && !transfer_->finish(std::uncaught_exception()||rescueExit||db_.settings().value("persistent_export_directory", false)))
    db_.setting("export_cleanup_failed", true);
}
void App::routeTouch(const Json &m) {
  if(modal_) {
    if(m.value("cancelled",false))input_.cancel();
    else input_.touch(m.value("x",0),m.value("y",0),m.value("pressed",false));
    return;
  }
  // A captured page-to-keyboard push owns its cancellation. Selection/read
  // suppression must not swallow that terminal frame and leave the panel moved.
  if (m.value("cancelled",false) && editor_.active &&
      (editorDismissTracking_ || editorDismissing_ || editorDismissReturnAt_)) {
    editorReaderTouch_=editorPointer_=false;
    readerDown_=readerPointer_=readerVertical_=false;
    readerSettle_=true;
    editorResetDismiss();editorCancelKeys();readReset();input_.cancel();
    return;
  }
  if (imageViewer_ || view_=="crop") {
    if(!imageTouch(m))input_.touch(m.value("x",0),m.value("y",0),m.value("pressed",false));
    return;
  }
  if (composerShortcutTouch(m.value("x",0),m.value("y",0),m.value("pressed",false),m.value("cancelled",false))) return;
  if(!editor_.active)readerTouch(m.value("x",0),m.value("y",0),m.value("pressed",false),m.value("cancelled",false));
  if(readTouch(m.value("x",0),m.value("y",0),m.value("pressed",false),m.value("cancelled",false)))return;
  if (editor_.active) {
    const int x=m.value("x",0),y=m.value("y",0);
    const bool down=m.value("pressed",false),cancelled=m.value("cancelled",false);
    if(editorLayoutHold_) {
      if(!down||cancelled)editorLayoutHold_=false;
      return;
    }
    const auto inputArea=editorTextArea();
    if(editorLayout().docked&&!editorPointer_&&down&&!inputArea.contains(x,y)&&y<editorLayout().panelTop&&
       !editorExpanded_&&!editorClipboardOpen_&&!editorMenu_&&!editorLayoutMenu_&&!editorPunctuation_)
      editorReaderTouch_=true;
    if(editorReaderTouch_) {
      if(!editorDismissTouch(x,y,down,cancelled)) {
        readerTouch(x,y,down,cancelled);
        if(cancelled)input_.cancel();else input_.touch(x,y,down);
      }
      if(!down||cancelled)editorReaderTouch_=false;
    } else editorTouch(x,y,down,cancelled);
    return;
  }
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
                     navigationDistance_ <= uiMetrics_.gestures().tapSquared() && x >= screen_.width * 2 / 3;
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
      lv_obj_get_coords(chatControls_, &bounds);
      composerTapAllowed_ = x >= bounds.x1 && x <= bounds.x2 &&
                            y >= bounds.y1 && y <= bounds.y2;
    }
  }
  const int composerDx = x - composerTouchX_, composerDy = y - composerTouchY_;
  if (int64_t(composerDx) * composerDx + int64_t(composerDy) * composerDy > uiMetrics_.gestures().tapSquared() ||
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
#ifndef PENCHAT_NATIVE_DEVICE
  if(type=="update.test" && std::getenv("PENCHAT_TEST_UPDATE_CONFIG")){updater_->request(m.value("operation",std::string()),m.value("args",Json::object()));return;}
  if (type == "feedback.test" && std::getenv("PENCHAT_TEST_FEEDBACK_CONFIG")) {
    feedback_->request(m.value("operation",std::string()),m.value("args",Json::object()));
    return;
  }
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
      auto object = icon(screenObj_, names[i], x + 19, y, 48, 48, {}, theme::palette().surface);
      label(screenObj_, names[i], x, y + 53, 84, uiMetrics_.sp(12), theme::palette().muted);
      auto small =
          icon(screenObj_, names[i], x + 29, y + 76, 28, 28, {}, theme::palette().surface);
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
    const bool reattach=m.value("reattach",false);pageAttached_=true;
    if(reattach) {
      inputFocus_=InputFocus(true);windowFocusSequence_=0;windowFocused_=false;
      penObserverRequest_.clear();penObserverReady_=false;
      bridge_.generation(m.value("generation",0));
      bridge_.send("booted",{{"screen",screen_.descriptor()},{"version",PENCHAT_VERSION},{"appid",PENCHAT_APP_ID}});
      if(editor_.active)editorPolicy(!editorSelectionOnly_);
    }
    bridge_.generation(m.value("generation", 0));
    if(m.contains("screenPower"))applyScreenPower(m["screenPower"]);
    sendAppearance();
    bridge_.send("pen.capabilities",{{"observer",scanner_.available()}});
    editorOcrCapabilities(m.value("ocrCapabilities",Json::object()));
    startupReadyPending_ = true;
    syncBackPolicy();
    syncScanner();
    return;
  }
  if (m.value("generation", bridge_.generation()) != bridge_.generation())
    return;
  if(type=="screen.power") {applyScreenPower(m);return;}
  if(type=="pen.observe.result") {
    if(m.value("requestId",std::string())!=penObserverRequest_)return;
    const bool enabled=m.value("enabled",false),ok=m.value("ok",false);
    penObserverRequest_.clear();penObserverReady_=enabled&&ok;
    systemOverlayStatus_["pen_observer_code"]=m.value("code",0);
    if(enabled&&!ok)penObserverRequest_="unsupported"; // Do not spin/release an unfiltered key client.
    syncScanner();return;
  }
  if(type=="page.detach") {
    pageAttached_=false;windowFocus(false);updateInputFocus();touchInput_.close();return;
  }
  if(type == "input.focus") {
    const auto before=inputFocus_.snapshot(ticks());
    if(inputFocus_.update(m.value("sequence",uint64_t(0)),m.value("active",false),ticks())) {
      const auto after=inputFocus_.snapshot(ticks());
      if(before.required!=after.required||before.active!=after.active)
        recordInputFocus(InputFocusTrace::InputApplied,after.active,after.sequence);
      updateInputFocus();
    }
    return;
  }
  if(type=="update.open") {rollbackRequest_.clear();openUpdate(false);return;}
  if(type=="rescue.open.result") {
    if(!rollbackRequest_.empty()&&m.value("operation",std::string())==rollbackRequest_){rollbackRequest_.clear();modal("版本回退未打开","未能安全停止当前运行，请稍后重试。保存的数据未被删除。");}
    return;
  }
  if(type=="update.install.result") {updater_->request("install_result",m);return;}
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
  if(type=="touch"&&keyboard_) {
    if(officialReaderInput()&&(!touchInput_.enabled()||m.value("source",std::string())=="native-evdev"))officialReaderTouch(m);
    return;
  }
  if (type == "touch" && visible_ && !keyboard_ && inputFocused_ && inputFocus_.allowed(ticks())) {
    if (imageStoreBusy_) return;
    // Production input has one owner. Never replay Falcon or delayed IPC touches.
    if (touchInput_.enabled() && m.value("source", std::string()) != "native-evdev") return;
    lastTouch_ = m;
    if(popupTouch(m))return;
    const bool down=m.value("pressed",false)&&!m.value("cancelled",false);
    if(down&&!systemEdgeDown_)
      systemEdgeGesture_=layout().portrait()&&!editor_.active&&!imageViewer_&&view_!="crop"&&m.value("y",0)<layout().safeTop();
    systemEdgeDown_=down;
    if(systemEdgeGesture_) {
      // The portrait safe top edge belongs to the system pull-down gesture.
      // Do not scroll the chat underneath the panel during focus handoff.
      if(!down)systemEdgeGesture_=false;
      return;
    }
    if(editor_.active&&down&&!editorReaderTouch_&&editorDismissReturnAt_)editorResetDismiss();
    const bool multi=m.contains("points") && m["points"].is_array() && m["points"].size()>1;
    if(!modal_ && editor_.active && !imageViewer_ && view_!="crop" && !nonImageMulti_ && editorKeyTouch(m)) {
      resetBack();
    } else if(multi || nonImageMulti_ || imageGesture_.multi || imageGesture_.mode<0) {
      resetBack();
      if(imageViewer_ || view_=="crop") routeTouch(m);
      else {nonImageMulti_=m.value("pressed",false);auto cancel=m;cancel["cancelled"]=true;cancel["pressed"]=false;routeTouch(cancel);}
    } else if (!backTouch(m)) routeTouch(m);
  } else if (type == "visibility")
    suspend(!m.value("visible", false));
  else if(type=="window.focus") {
    const auto sequence=m.value("sequence",uint64_t(0));
    if(sequence>windowFocusSequence_){recordInputFocus(InputFocusTrace::WindowReceived,m.value("focused",false),sequence);windowFocusSequence_=sequence;windowFocus(m.value("focused",false));}
  }
  else if(type=="system.overlay.status") {
    bool changed=false;
    for(const char *key:{"window_api","window_valid","app_focused","panel_api","panel_available","panel_supported","panel_visible"})
      if(m.contains(key)&&m[key].is_boolean()) {
        if(!systemOverlayStatus_.contains(key)||systemOverlayStatus_[key]!=m[key])changed=true;
        systemOverlayStatus_[key]=m[key];
      }
    if(changed)recordInputFocus(InputFocusTrace::OverlayApplied,false);
  }
  else if(type=="keyboard.layout" || type=="keyboard.change") {
    if(!keyboard_||!officialDocked_||m.value("requestId",std::string())!=keyboardId_)return;
    if(type=="keyboard.change") {
      const auto text=m.value("text",std::string());
      if(editLength(text)>editor_.limit)return;
      const auto textEvent=m.value("inputEventTime",int64_t(0));
      if(textEvent>0&&std::abs(textEvent-nowMs())<5000)officialTextEventAt_=std::max(officialTextEventAt_,textEvent);
      const bool changed=text!=editor_.text;
      const auto previousCursor=editor_.cursor;
      const auto utf16=m.value("cursorIndex",editUtf16(text,editLength(text)));
      size_t cursor=0,units=0;for(unsigned char c:text) {if(units>=utf16)break;if((c&192)!=128){units+=c>=240?2:1;++cursor;}}
      if(!changed&&(officialTextContact_||officialEditPending_||editor_.selected()||cursor==previousCursor))return;
      if(changed)editor_.remember();
      editor_.text=text;editor_.preedit.clear();editor_.position(cursor);editorUpstream_=std::string::npos;
      editorSyncText(changed);
    } else {
      const int height=m.value("height",0);
      if(height<=0||height>=screen_.height-100)return;
      const bool resized=height!=officialHeight_;officialHeight_=height;
      officialForeground_=m.value("foreground",false);officialLayoutAt_=ticks();
      if(resized){stopOfficialReaderGlide();officialReaderCaptured_=false;officialReaderPending_=0;editorViewport();editorSyncText(true);}
      display_.topHeight(screen_.height-height);
      updateInputFocus();
    }
    auto field=editorOfficialField();
    field["requestId"]=keyboardId_;field["cursorIndex"]=editUtf16(editor_.text,editor_.cursor);
    bridge_.send("keyboard.field",field);
  }
  else if(type=="keyboard.edit.result") {
    if(!keyboard_||m.value("requestId",std::string())!=keyboardId_||m.value("sequence",uint64_t(0))!=officialEditPending_)return;
    officialEditPending_=0;
    if(!m.value("ok",false)) {
      const auto text=m.value("text",editor_.text);
      if(editLength(text)<=editor_.limit){editor_.text=text;editor_.position(std::min(editor_.cursor,editLength(text)));}
      editorMenu_=false;
    }
    editorSyncText(true);
  }
  else if(type=="keyboard.action.error") {
    if(m.value("requestId",std::string())==keyboardId_&&m.value("actionId",std::string())==officialActionId_){officialActionId_.clear();officialActionKind_.clear();}
  }
  else if (type == "keyboard.result") {
    if (!keyboard_ || m.value("requestId", std::string()) != keyboardId_)
      return;
    const auto action=!officialActionId_.empty()&&m.value("confirmed",false)&&m.value("actionId",std::string())==officialActionId_?officialActionKind_:std::string();
    const auto actionSession=officialActionSession_;
    if(action!="menu")officialMenuAction_={};
    officialActionId_.clear();officialActionKind_.clear();officialComposerHit_.clear();
    // Never carry a system-key contact into the restored native controls.
    stopOfficialReaderGlide();touchInput_.close();officialReaderDown_=officialReaderCaptured_=officialReaderDragging_=false;officialReaderPending_=0;
    // Docked editing is live: hiding/cancelling the system panel never rolls back text.
    if(officialDocked_&&m.value("retained",false)&&m.contains("text")) {
      const auto text=m.value("text",std::string());if(editLength(text)<=editor_.limit)editor_.text=text;
    }
    const bool restoreFullScreen=!officialDocked_&&officialBackup_.active;
    if(restoreFullScreen&&!m.value("confirmed",false))editor_=officialBackup_;
    officialBackup_=EditorState{};officialDocked_=false;officialHeight_=0;officialForeground_=false;officialLayoutAt_=0;
    // Withdraw the partial plane before restoring full geometry, so the old
    // frame cannot flash over a system IME that is still finishing teardown.
    display_.visible(false);keyboard_ = false;display_.topHeight(0);
    if (editor_.active) {
      const bool closedNormally=m.value("error",std::string()).empty();
      if (m.value("confirmed", false)||closedNormally) {
        closeAttachments();
        // A normal fullscreen Cancel restores the seed and ends editing. Keep
        // the snapshot for data recovery without reopening the native keyboard.
        // Docked editing still retains its latest committed live proxy text.
        auto text = restoreFullScreen&&!m.value("confirmed",false)?editor_.text:m.value("text", editor_.text);
        // A normal dismissal may omit text. Preserve the exact seed including
        // unfinished native phonetics; an explicitly returned empty value still
        // means the user deleted everything. Live proxy changes clear preedit.
        if(!restoreFullScreen&&!m.contains("text")&&!editor_.preedit.empty())
          text.insert(editByte(text,editor_.cursor),editor_.preedit);
        if (editLength(text) <= editor_.limit) { editor_.text = text; editor_.preedit.clear(); editorFinish(true); }
        else { editor_.notice = "官方输入超过长度上限，请调整";editorPolicy(true);editorBuild();editorOcrStart(); }
      } else {editorPolicy(true);editorChanged();editor_.notice=m.value("error",std::string());editorFollowCursor_=false;editorBuild();editorOcrStart();}
    }
    display_.visible(visible_ && !screenPower_.blocked(ticks()) && (inputFocused_ || display_.systemOverlaySafe()));
    if(!action.empty()&&!editor_.active&&session_==actionSession)
      events_.push("official_composer_action",{{"session",actionSession},{"action",action}});

  } else if(type=="navigation.policy") {
    if(m.value("sequence",uint64_t(0))==backPolicySequence_) {
      backPolicyOk_=m.value("ok",false); backPolicyBlocked_=m.value("blocked",false);
    }
  } else if(type=="editor.policy") {
    if(editor_.active && m.value("session",std::string())==editor_.id && m.value("sequence",uint64_t(0))==editorPolicySequence_) {
      editorTopBlocked_=m.value("blocked",false);
      if(!m.value("ok",false)){editor_.notice="当前固件无法禁用下拉面板";editorBuild();}
    }
  } else if(type=="ocr.capabilities") {
    if(editor_.active&&m.value("session",std::string())==editor_.id&&
       !editorOcrId_.empty()&&m.value("requestId",std::string())==editorOcrId_)
      editorOcrCapabilities(m);
  } else if(type=="ocr.prepared") {
    editorOcrPrepared(m);
  } else if(type=="ocr.stopped") {
    if(!editorOcrStopId_.empty() && m.value("requestId",std::string())==editorOcrStopId_ && m.value("ok",false)) {
      editorOcrStopId_.clear();editorOcrStopRetry_=0;editorOcrStart();
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
  if (balanceDirty_ && ticks() - balanceLastRender_ >= 100 && contextMenuKind_ == "balances" &&
      !touchDown_ && !lastTouch_.value("pressed",false) && contextMenuList_ && !lv_obj_is_scrolling(contextMenuList_) && !readGesture_.active() && !(readActive_&&readSelection_.selected())) {
    balanceDirty_ = false;
    balanceLastRender_ = ticks();
    balancePage();
  }
  updateInputFocus();
  editorOcrFlushStop();
  if(startupReadyPending_ && display_.presented()) {
    startupReadyPending_ = false;
    bridge_.send("ready");
  }
  const int reservedTop = display_.systemOverlaySafe() && !editor_.active && !imageViewer_ && view_ != "crop"
      ? std::max(uiMetrics_.dp(8), std::min(screen_.width, screen_.height) / 25) : 0;
  if(!officialActionId_.empty()&&ticks()-officialActionAt_>2500){officialActionId_.clear();officialActionKind_.clear();}
  const bool officialReader=officialReaderInput();
  if(!officialReader){if(officialPopupContact_)input_.interrupt();officialPopupContact_=false;officialComposerHit_.clear();officialContactIds_.clear();stopOfficialReaderGlide();officialReaderDown_=officialReaderCaptured_=officialReaderDragging_=false;officialReaderPending_=0;}
  auto touches=touchInput_.poll((visible_ && !keyboard_ && touchLeaseActive_)||officialReader, reservedTop);
  for(const auto &touch:touches){
    if(!officialReaderInput()&&(!visible_ || keyboard_ || !inputFocused_ || !inputFocus_.allowed(ticks()))) { updateInputFocus();touchInput_.close();break; }
    handle(touch);
  }
  if(officialReaderPending_) {
    const int delta=officialReaderPending_;officialReaderPending_=0;
    // One scroll/layout/render pass per UI tick, regardless of evdev batch size.
    if(officialReaderInput())lv_obj_scroll_by(body_,0,delta,LV_ANIM_OFF);
  }
  if(officialReaderMomentum_) {
    const int distance=officialReaderMomentum_;officialReaderMomentum_=0;
    if(officialReaderInput()) {
      officialReaderGlideBody_=body_;
      lv_obj_scroll_by_bounded(body_,0,distance,LV_ANIM_ON);
    }
  }
  imageReturnTick();
  for (const auto &saved : db_.saveEvents()) {
    if(saved.type=="draft_error"){notice_="草稿未保存，请检查存储空间";continue;}
    if(saved.data.value("key",std::string())=="ocr_multiline"){ocrModeSaved(saved);continue;}
    if(saved.data.value("key",std::string())=="prefer_official_keyboard"||saved.data.value("key",std::string())=="quick_official_keyboard"){officialKeyboardPreferenceSaved(saved);continue;}
    if(saved.data.value("key",std::string())=="ui_refresh_hz"){refreshRateSaved(saved);continue;}
    if(saved.data.value("key",std::string())=="theme_mode"){themeSaved(saved);continue;}
    if(saved.data.value("key",std::string())=="ui_scale_percent"){uiScaleSaved(saved);continue;}
    if(saved.data.value("key",std::string())=="reading_font_percent"){fontSizeSaved(saved);continue;}
    if(saved.data.value("key",std::string())=="onboarding"){onboardingSaved(saved);continue;}
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
          chatFinishPending_ = {{"id", message}, {"session", session},
                                {"error", saved.data.value("status", std::string()) == "error" ? notice_ : std::string()}};
        }
      }
    } else if (saved.type == "setting_error") {
      const auto message=std::string("中文布局偏好未能保存，本次选择仍保留。");
      if(editor_.active){editor_.notice=message;editorBuild();}else error(message);
    } else if (saved.type == "save_error") {
      diagnosticRecord("storage", "save_error");
      notice_ = saved.data.value("error", std::string("保存失败"));
      modal("内容尚未保存", notice_);
      button(modal_, "重试保存", screen_.width - uiMetrics_.dp(335), screen_.height - uiMetrics_.dp(66), uiMetrics_.dp(120), uiMetrics_.touchTarget(),
             [this] {
               db_.retrySaves();
               if (modal_)
                 lv_obj_delete(modal_);
             });
    }
  }
  refreshRateTick();
  themeTick();
  uiScaleTick();
  fontSizeTick();
  editorTick();
  readTick();
  if(readRebuildPending_&&!readBusy()&&visible_&&!keyboard_&&!editor_.active)build();
  if (afterScanRelease_ && scanner_.released()) {
    auto callback = std::move(afterScanRelease_);
    afterScanRelease_ = {};
    callback();
  }
  syncScanner();
  if (feedbackDirty_ && view_ == "feedback" && !editor_.active && !touchDown_ && !readBusy() && !modal_ && visible_) {
    feedbackDirty_ = false; build();
  }
  for (auto &e : events_.drain()) {
    if((e.type=="photo"||e.type=="camera_started"||e.type=="camera_error"||e.type=="camera_sensor"||e.type=="camera_orientation")&&e.data.value("generation",uint64_t(0))!=camera_.generation()) {
      if(e.type=="photo")unlink(e.data.value("path",std::string()).c_str());
      continue;
    }
    if(e.type=="onboarding_action"){if(e.data.value("step",-1)==onboardingStep_)onboardingAction(e.data.value("action",std::string()));continue;}
    if(e.type=="open_onboarding"){openOnboarding();continue;}
    if (e.type == "update_rollback_open") {openRollback();continue;}
    if (e.type == "update_release_fault") { feedback_->request("update_fault",e.data); continue; }
    if (e.type == "update_state" || e.type == "update_prepared") { updateEvent(e); continue; }
    if (e.type == "feedback_state") { feedbackEvent(e); continue; }
    if (e.type == "feedback_notice") {
      if(visible_&&!editor_.active&&!modal_)modal(e.data.value("title",std::string("反馈发送状态")),e.data.value("text",std::string()));
      continue;
    }
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
      if(ok&&e.data.value("session",std::string())==session_&&messageEdit_.empty()) {
        const auto saved=db_.draft(draftState_.at("key"));draftState_["images"]=saved.at("images");
      }
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
    if (e.type == "web_reader") {
      webEvent(e);
      continue;
    }
    if (e.type == "web_toggle") {
      const auto id=e.data.value("id",std::string());
      auto control=webControls_.find(id);
      if(control!=webControls_.end())toggleWeb(id,!control->second.expanded);
      continue;
    }
    if (e.type == "pinyin_clear_confirm") {if(view_=="keyboard_settings"){input_.cancel();modal("正在清除拼音记忆","请稍候…");pinyin_.clear();}continue;}
    if (e.type == "pinyin.cleared") {if(view_=="keyboard_settings"){build();if(e.data.value("ok",false))modal("拼音记忆","已恢复基础词典排序。");else modal("未能清除拼音记忆","文件未能保存，请检查可用空间后重试。");}continue;}
    if (e.type == "catalog_help") {
      if (visible_ && view_ == "model" && e.data.value("id", std::string()) == modelForm_.value("id", std::string()))
        modal("使用匹配值", "点击填充图标只替换对应字段；其他设置不变，保存后生效。");
      continue;
    }
    if(e.type=="editor_target_lost") {
      if(editor_.active&&!editorTarget_&&e.data.value("id",std::string())==editor_.id)editorFinish(false);
      continue;
    }
    if (e.type == "editor_action"&&keyboard_&&officialReaderInput()&&e.data.value("id",std::string())==editor_.id) {officialEditAction(e.data.value("action",std::string()));continue;}
    if (e.type == "editor_action") { if (editor_.active && visible_ && touchLeaseActive_ && !keyboard_ && e.data.value("id",std::string()) == editor_.id) editorAction(e.data.value("action",std::string())); continue; }
    if (e.type.find("scan_") == 0) {
      scanEvent(e);
      continue;
    }
    if(replyEvent(e))continue;
    if (sessionInfoEvent(e)) continue;
    if(messageEvent(e))continue;
    if(e.type=="image_scan_open") {
      if(e.data.value("scan",uint64_t(0))==scanId_ && e.data.value("generation",uint64_t(0))==scanGeneration_ &&
         e.data.value("session",std::string())==scanSession_)viewScanResult();
      continue;
    }
    if(e.type=="official_composer_action") {
      if(visible_&&view_=="chat"&&!keyboard_&&!editor_.active&&!modal_&&!attachments_&&!navigationOpen_&&e.data.value("session",std::string())==session_) {
        const auto action=e.data.value("action",std::string());
        if(action=="menu") {auto callback=std::move(officialMenuAction_);officialMenuAction_={};if(callback)callback();}
        else if(action=="attachments")toggleAttachments();
        else if(action=="effort")effortPicker();
        else if(action=="send"&&!generating_&&!contextBusy_)sendMessage();
      }
      continue;
    }
    if(e.type=="composer_send") {
      if(!editor_.active&&view_=="chat"&&e.data.value("session",std::string())==session_&&!generating_&&!contextBusy_&&canSend())sendMessage();
      continue;
    }
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
          ((!messageEdit_.empty()&&std::any_of(messageEditImages_.begin(),messageEditImages_.end(),[&](const Json &a){return a["edit_id"]==id;}))||draftImage(id)||db_.pendingImage(session_, id))) {
        input_.cancel();
        // The popup owns the opening release; retire the page's original contact now.
        touchDown_=composerTapAllowed_=readDown_=editorReaderTouch_=false;resetBack();
        readerDown_=readerPointer_=readerVertical_=false;
        const auto anchor=e.data.value("anchor",Json::array());
        if(anchor.size()!=4)continue;
        lv_point_t measured{};
        lv_text_get_size(&measured,"删除",font(uiMetrics_.fontSize(UiFont::Button)),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
        const int padding=uiMetrics_.dp(4),height=std::max(uiMetrics_.touchTarget(),int(measured.y)+uiMetrics_.dp(12));
        const int width=std::max(uiMetrics_.dp(80),int(measured.x)+uiMetrics_.dp(24));
        auto list=contextMenu("image-delete",1,width+2*padding);
        imageDeleteId_=id;imageDeleteRequest_=uid("image-delete");
        auto safe=selectionMenuSafeArea(screen_.width,screen_.height,roundedDisplay_,uiMetrics_);
        if(body_) {
          const auto reader=readClip(body_);
          safe.y=std::max(safe.y,int(reader.y1));
          safe.height=std::max(1,std::min(safe.y+safe.height,int(reader.y2)+1)-safe.y);
        }
        const LayoutRect imageArea{anchor[0],anchor[1],anchor[2].get<int>()-anchor[0].get<int>()+1,anchor[3].get<int>()-anchor[1].get<int>()+1};
        auto place=placeSelectionMenu(imageArea,safe,width+2*padding,height+2*padding,0,layout().safety(),uiMetrics_);
        if(place.position==SelectionMenu::LowerLeft) {
          place.bounds.x=std::max(safe.x,std::min(safe.x+safe.width-place.bounds.width,
              imageArea.x+(imageArea.width-place.bounds.width)/2));
          place.bounds.y=std::max(safe.y,std::min(safe.y+safe.height-place.bounds.height,
              imageArea.y+imageArea.height+uiMetrics_.dp(8)));
          place.bounds=layout().safety().fit(place.bounds);
        }
        lv_obj_set_pos(contextMenuPanel_,place.bounds.x,place.bounds.y);
        lv_obj_set_size(contextMenuPanel_,place.bounds.width,place.bounds.height);
        lv_obj_set_pos(list,padding,padding);lv_obj_set_size(list,place.bounds.width-2*padding,height);
        lv_obj_set_scrollable(list,false);
        const auto request=imageDeleteRequest_,session=session_,editToken=messageEditToken_;
        auto remove=button(list,"删除",0,0,place.bounds.width-2*padding,height,
          [this,session,id,request,editToken]{events_.push("image_delete_confirm",
            {{"session",session},{"message",id},{"request",request},{"edit_token",editToken}});},
          theme::palette().popup);
        stylePopupRow(remove);
        lv_obj_set_style_text_color(lv_obj_get_child(remove,0),lv_color_hex(theme::palette().danger),0);
        choiceActions_["delete"]=remove;
      }
    } else if (e.type == "image_delete_confirm") {
      if (view_ == "chat" && visible_ && contextMenuKind_=="image-delete" && !contextBusy_ &&
          e.data.value("session", std::string()) == session_ &&
          e.data.value("request",std::string())==imageDeleteRequest_ &&
          e.data.value("edit_token",std::string())==messageEditToken_ &&
          e.data.value("message", std::string()) == imageDeleteId_) {
        input_.cancel();
        if(!messageEdit_.empty()) {
          const auto id=e.data.value("message",std::string());
          messageEditImages_.erase(std::remove_if(messageEditImages_.begin(),messageEditImages_.end(),[&](const Json &a){return a["edit_id"]==id;}),messageEditImages_.end());build();
        } else if (db_.removeDraftImage(draftState_,e.data.value("message", std::string())) ||
                   db_.removePendingImage(session_,e.data.value("message", std::string()))) {
          draftState_["images"]=db_.draft(draftState_.at("key")).at("images");
          messageOffset_ = 0;
          build();
        } else {
          closeHistoryContext();replyToast("图片已进入发送请求，无法删除");
        }
      }
    } else if (e.type == "retry_request") {
      if (view_ == "chat" && visible_ && !keyboard_ &&
          e.data.value("session", std::string()) == session_) {
        input_.cancel();
        retryWarning(e.data.value("id", std::string()));
      }
    } else if (e.type == "reasoning_toggle") {
      const auto id=e.data.value("id",std::string());
      auto found=reasoningControls_.find(id);
      const auto source=e.data.value("session",std::string());
      if(found!=reasoningControls_.end() && ((view_=="archive_chat"&&source==archiveReaderId_) || (view_=="chat"&&source==session_))) {
        lv_obj_stop_scroll_anim(body_);
        const bool expanded=!found->second.expanded;
        (view_=="archive_chat"?archiveReasoningOverrides_:reasoningOverrides_)[id]=expanded;
        if(view_=="chat")chatFollowing_=false;
        toggleReasoning(id,expanded);
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
    } else if (e.type == "composer_shortcut_apply" && view_ == "settings" && visible_) {
      input_.cancel();
      const auto mode = e.data.value("mode", std::string("auto"));
      if (mode == "auto" || mode == "manual" || mode == "off") {
        db_.setting("landscape_composer_shortcut", mode);
        composerShortcut_.configure(mode);
        build();
      }
    } else if (e.type == "refresh_rate_apply") {
      requestRefreshRate(e.data.value("hz",0));
    } else if (e.type == "theme_apply") {
      requestTheme(e.data.value("mode",std::string()));
    } else if (e.type == "ui_scale_save") {
      saveUiScale();
    } else if (e.type == "reading_font_apply" && view_ == "font_size") {
      saveFontSize();
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
      if (db_.finishTitle(e.data["session"], e.data["token"], title)) {
        historyDirty_ = true;
        if (e.data["session"] == session_) refreshChatTitle();
      }
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
        receivingReply_ = false;
        saveInFlight_ = true;
        auto status = e.type == "done"        ? "complete"
                      : e.type == "cancelled" ? "cancelled"
                                              : "error";
        Json err = e.type == "network_error" ? e.data["error"] : Json();
        persistReply(status, err);
        if (e.type == "network_error")
          notice_ = e.data.value("error", std::string("网络错误"));
      }
    } else if (e.type == "balance_result") {
      balanceEvent(e);
    } else if (e.type == "balance_hint") {
      replyToast("账户余额");
    } else if (e.type == "models") {
      providerModelsResult(e);
    } else if (e.type == "image_ready" || e.type == "image_error") {
      if(e.type=="image_ready"&&view_=="scan"&&!scanOriginalKey_.empty()&&e.data.value("key",std::string())==scanOriginalKey_)
        scanDecodedAt_=ticks();
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
      diagnosticRecord("camera", "error");
      cameraCapturing_=false;
      cameraError_=e.data.value("error", std::string("相机失败"));
      error(cameraError_);
    }
    else if (e.type == "camera_started" && view_ == "camera") {
      diagnosticRecord("camera", "started");
      if (status_)
        lv_label_set_text(status_, "");
    } else if (e.type == "photo") {
      diagnosticRecord("camera", "photo", e.data);
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
  if(!deferredModalTitle_.empty()&&!keyboard_&&!editor_.active&&visible_&&inputFocused_) {
    auto title=std::move(deferredModalTitle_),text=std::move(deferredModalText_);
    deferredModalTitle_.clear();deferredModalText_.clear();modal(title,text);
  }
  if(modal_&&chatControls_&&contextMenuKind_!="image-delete")lv_obj_set_hidden(chatControls_,true);
  if(onboarding_)onboarding_->tick(visible_&&!keyboard_&&!editor_.active);
  updateTick();
  if(updateExit_)return;
  syncBackPolicy();
  composerShortcutTick();
  settleReader();
  if(!onboardingActive_&&!replyBusy_&&!contextRegenerating_)tickTitles();
  if (persistDirty_ && !saveInFlight_ && ticks() >= persistDue_)
    persistReply("streaming");
  flushChatRender();
  syncReplyControls();
  if (view_ == "camera" && cameraAutomatic_ && pendingCameraAngle_>=0 && !cameraCapturing_ && !touchDown_ && visible_) {
    cameraAngle_=pendingCameraAngle_;pendingCameraAngle_=-1;
    // Rotate portrait control visuals in place; their hit regions and the
    // optically corrected preview keep fixed geometry without a page rebuild.
    if (layout().portrait())cameraControlsOrientation();else build();
  }
  if (view_ == "camera" && visible_ && !keyboard_ && !navigationOpen_ &&
      ticks() - lastCameraFrame_ >= 33) {
    lastCameraFrame_ = ticks();
    auto frame = camera_.frame();
    if (frame && cameraImage_ && frame->cameraPose == cameraAngle_ &&
        (!textures_.count(cameraImage_)||textures_.at(cameraImage_)->frame!=frame)) {
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
  lv_obj_update_layout(lv_screen_active());
  lv_obj_update_layout(lv_layer_top());
  const auto powerState=screenPower_.snapshot(ticks());
  Json transferDiagnostic = Json::object();
  if (view_ == "transfer") {
    for (auto key : {"path", "added", "skipped", "count", "format_spec", "format_version", "topic"})
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
    model = resolveReasoning(model, sessionPreferences_, "");
  lv_area_t reasonHeader{};
  if (!reasoningHeaders_.empty())
    lv_obj_get_coords(reasoningHeaders_.back(), &reasonHeader);

  lv_area_t active{}, capsule{}, send{}, entry{};
  if(composer_)lv_obj_get_coords(composer_,&entry);
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
                           {"highlighted",lv_obj_has_state(entry.second,LV_STATE_CHECKED)},
                           {"x", (area.x1 + area.x2) / 2},
                           {"y", (area.y1 + area.y2) / 2}});
  }
  auto bounds=[](lv_obj_t *object)->Json {if(!object)return Json();lv_area_t a;lv_obj_get_coords(object,&a);return Json::array({a.x1,a.y1,a.x2,a.y2});};
  Json workspaceModels=Json::array();
  for(const auto &m:providerDraftModels_)workspaceModels.push_back({{"id",m.at("id")},{"name",m.value("name",std::string())},
    {"deleted",m.value("workspace_deleted",false)},{"new",m.value("workspace_new",false)}});
  Json historyCaptions=Json::object(),historyActions=Json::object(),choiceActions=Json::object();
  for(const auto &entry:choiceActions_)choiceActions[entry.first]={{"bounds",bounds(entry.second)},{"selected",lv_obj_has_state(entry.second,LV_STATE_CHECKED)}};
  for(const auto &entry:historyCaptions_) {
    auto title=entry.second.title;auto time=entry.second.time;
    historyCaptions[entry.first]={{"title_bounds",bounds(title)},{"time_bounds",bounds(time)},
       {"row_bounds",bounds(lv_obj_get_parent(title))},
       {"title_line_height",lv_obj_get_style_text_font(title,LV_PART_MAIN)->line_height},
       {"time_line_height",time?lv_obj_get_style_text_font(time,LV_PART_MAIN)->line_height:0},
       {"title_length",editLength(lv_label_get_text(title))},{"marquee",lv_label_get_long_mode(title)==LV_LABEL_LONG_MODE_SCROLL_CIRCULAR},
       {"time",time?lv_label_get_text(time):""}};
  }
  for(const auto &entry:historyActions_)historyActions[entry.first]=bounds(entry.second);
  Json reasonControls=Json::array(),footer=Json::object();
  for(const auto &entry:reasoningControls_) {
    const auto &c=entry.second;
    reasonControls.push_back({{"id",entry.first},{"arrow",bounds(c.arrow)},{"title",bounds(lv_obj_get_child(lv_obj_get_parent(c.arrow),1))},
      {"expanded",c.expanded},{"search_text",c.searchText},{"angle",disclosureProgress(c.arrow)*900/1000},
      {"detail",c.detail?bounds(c.detail):Json()}});
  }
  auto textMetrics=[](lv_obj_t *label)->Json {
    if(!label)return Json();
    lv_point_t size{};lv_text_get_size(&size,lv_label_get_text(label),lv_obj_get_style_text_font(label,LV_PART_MAIN),
      lv_obj_get_style_text_letter_space(label,LV_PART_MAIN),lv_obj_get_style_text_line_space(label,LV_PART_MAIN),LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    return {{"text",lv_label_get_text(label)},{"width",size.x},{"height",size.y}};
  };
  for(const auto &entry:navigationFooter_)footer[entry.first]={{"bounds",bounds(entry.second)},
    {"caption",bounds(lv_obj_get_child(entry.second,0))},{"text_metrics",textMetrics(lv_obj_get_child(entry.second,0))},{"glyph",bounds(lv_obj_get_child(entry.second,1))},
    {"pressed",lv_obj_has_state(entry.second,LV_STATE_PRESSED)}};
  Json attachmentActions = Json::object();
  for (const auto &entry : attachmentEntries_) attachmentActions[entry.first] = bounds(entry.second);
  Json archiveViews=Json::object();
  for(const auto &entry:archiveViewButtons_){lv_area_t a;lv_obj_get_coords(entry.second,&a);archiveViews[entry.first]={a.x1,a.y1,a.x2,a.y2};}

  return {
      {"onboarding",onboarding_?onboarding_->diagnostic():Json()},
      {"onboarding_active",onboardingActive_},{"onboarding_config",onboardingConfig_},
      {"editor", editorDiagnostic()},
      {"draft_length", editLength(draft_)},
      {"storage", db_.storageStats()},
      {"diagnostic_time", ticks()},
      {"layout_work",{{"form_batches",formLayoutBatches_},{"form_rows",formLayoutRows_}}},
      {"history_rows", historyRows},
      {"history_captions",historyCaptions},{"history_actions",historyActions},
      {"presets_layout",presetsDiagnostic()},
      {"history_context", historyContextId_},
      {"title_busy", titleBusy_.load()},
      {"title_pending", titleJobs_.size()},
      {"title_limit", titleLimit()},
      {"title_width", titleWidth()},
      {"generation", bridge_.generation()},
      {"pid", getpid()},
      {"touch", lastTouch_},
      {"input_focus_trace",inputFocusDiagnostics()},
      {"view", view_},
      {"provider_protocol", providerForm_.is_object() ? providerForm_.value("api", std::string()) : std::string()},
      {"provider_workspace",{{"active",!providerWorkspaceId_.empty()},{"dirty",providerDirty()},
        {"model_count",providerDraftModels_.size()},{"models",workspaceModels},{"deleted_count",std::count_if(providerDraftModels_.begin(),providerDraftModels_.end(),[](const Json &m){return m.value("workspace_deleted",false);})}}},
      {"anthropic_connection", view_ == "anthropic_connection" ? objectField(providerForm_, "anthropic") : Json::object()},
      {"model_catalog", view_ == "model" ? modelRecommendation(modelForm_) : Json::object()},
      {"model_parameters", view_ == "model" ? Json({{"reasoning", modelForm_["reasoning"]},
          {"maximum", anthropicProtocol(modelForm_) ? Json(modelForm_.value("max_output_tokens", anthropicMaxOutput(modelForm_))) : Json()},
          {"context", modelForm_.value("context_budget", contextBudget(modelForm_))},
          {"vision", modelForm_.value("vision", objectField(modelForm_, "capabilities_json").value("vision", Json()))}}) : Json::object()},
      {"update", [this] {
        Json actions=Json::object();for(const auto &item:updateActions_){lv_area_t a;lv_obj_get_coords(item.second,&a);actions[item.first]={a.x1,a.y1,a.x2,a.y2};}
        if(rollbackContinue_){lv_area_t a;lv_obj_get_coords(rollbackContinue_,&a);actions["rollback_continue"]={a.x1,a.y1,a.x2,a.y2};}
        return Json{{"status",updateState_.value("status",std::string())},{"current",PENCHAT_VERSION},
          {"target",updateState_.value("release",Json::object()).value("version",std::string())},
          {"weak",updateState_.value("release",Json::object()).value("weak",false)},
          {"rollback_pending",!rollbackRequest_.empty()},{"suppressed",updateState_.value("suppressed",false)},{"progress",updateState_.value("progress",Json::object())},
          {"notes",updateState_.value("notes",Json::array()).size()},{"note_index",updateState_.value("note_index",size_t(0))},
          {"note_page",updateState_.value("note_page",size_t(0))},{"actions",actions}};
      }()},
      {"feedback", [this] {
        Json actions=Json::object();
        for(const auto &item:feedbackActions_) { lv_area_t a; lv_obj_get_coords(item.second,&a); actions[item.first]={a.x1,a.y1,a.x2,a.y2}; }
        auto state=feedback_?feedback_->state():Json::object();
        return Json{{"screen",feedbackView_},{"pending",state.value("pending",size_t(0))},
                    {"collecting",state.value("collecting",false)},{"actions",actions}};
      }()},
      {"transfer_actions", [this] {
        Json actions=Json::object();
        for (const auto &entry : transferActions_) {
          lv_area_t a; lv_obj_get_coords(entry.second,&a);
          actions[entry.first]={a.x1,a.y1,a.x2,a.y2};
        }
        return actions;
      }()},
      {"transfer_screen", transferScreen_},
      {"transfer_busy", transfer_ && transfer_->busy()},
      {"transfer_error", transferError_},
      {"transfer_data", transferDiagnostic},
      {"scanner", scanner_.state()},
      {"scan_id", scanId_},
      {"scan_result", scanResult_},
      {"scan_latency",{{"finished",scanFinishedAt_},{"prepared",scanPreparedAt_},{"decoded",scanDecodedAt_},{"export_us",scanExportUs_},
        {"view_clicked",scanViewClickedAt_},{"view_loaded",scanViewLoadedAt_},
        {"confirm_clicked",scanConfirmClickedAt_},{"stored",scanStoredAt_}}},
      {"scan_preview_area", [this] {
        if(!scanHolder_)return Json::array();
        lv_area_t a;lv_obj_get_coords(scanHolder_,&a);return Json::array({a.x1,a.y1,a.x2,a.y2});
      }()},
      {"scan_actions",{{"use",bounds(scanUse_)},{"cancel",bounds(scanCancel_)}}},
      {"scan_warning", scanWarning_ && !lv_obj_is_hidden(scanWarning_)?lv_label_get_text(scanWarning_):""},
      {"scan_released", scanner_.released()},
      {"send_center_x2", send.x1 + send.x2},
      {"send_center_y2", send.y1 + send.y2},
      {"send_diameter", send.x2 - send.x1 + 1},
      {"capsule_endcap_x2", 2 * capsule.x2 - (capsule.y2 - capsule.y1)},
      {"capsule_center_y2", capsule.y1 + capsule.y2},
      {"composer_bounds", {capsule.x1,capsule.y1,capsule.x2,capsule.y2}},
      {"attachment_button",chatControls_?bounds(lv_obj_get_child(chatControls_,layout().portrait()?1:0)):Json()},
      {"composer_text_bounds", {entry.x1,entry.y1,entry.x2,entry.y2}},
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
      {"text_selection",readDiagnostic()},
      {"reply_actions",replyDiagnostic()},
      {"navigation_button", view_ == "chat" ? (messageEdit_.empty()?"menu":"close") : "back"},
      {"chat_header", {{"bounds",chatHeader_?bounds(chatHeader_):Json()},
                        {"title",chatTitle_?std::string(lv_label_get_text(chatTitle_)):std::string()},
                        {"title_bounds",chatTitle_?bounds(chatTitle_):Json()},
                        {"scan",scanHint_?std::string(lv_label_get_text(scanHint_)):std::string()},
                        {"scan_bounds",scanHint_?bounds(scanHint_):Json()},{"scan_metrics",textMetrics(scanHint_)},
                        {"reader_bounds",view_=="chat"&&body_?bounds(body_):Json()},
                        {"message_width",layout().chatMessageWidth()}}},
      {"pull_indicator_y", pullIndicator_ ? lv_obj_get_y(pullIndicator_) : -1},
      {"pull_content_y", body_ ? lv_obj_get_style_translate_y(body_, LV_PART_MAIN) : 0},
      {"following_latest", chatFollowing_},
      {"receiving_reply", receivingReply_},
      {"generation_hint_present", generationHint_ != nullptr},
      {"output_length", output_.size()},
      {"live_content_height", liveContent_ ? lv_obj_get_height(liveContent_) : 0},
      {"reader_interacting", readerInteracting()},
      {"reader_vertical", readerVertical_},
      {"at_latest_bottom", atLatestBottom()},
      {"chat_render_pending", dirty_ || chatRenderPending_ || !chatFinishPending_.empty()},
      {"attachments", attachments_ != nullptr},
      {"attachment_above_keyboard", !editorRoot_ || (attachments_ && popupKind_=="attachments" &&
        lv_obj_get_parent(attachments_)==lv_obj_get_parent(editorRoot_) && lv_obj_get_index(attachments_)>lv_obj_get_index(editorRoot_))},
      {"popup", popupKind_},
      {"popup_bounds",attachmentPanel_?bounds(attachmentPanel_):Json()},
      {"context_menu",{{"kind",contextMenuKind_},{"bounds",contextMenuPanel_?bounds(contextMenuPanel_):Json()},
        {"list",contextMenuList_?bounds(contextMenuList_):Json()},{"opening",contextMenuOpening_},{"choices",choiceActions}}},
      {"has_conversation_content", hasConversationContent_},
      {"effort_gauge", sessionPreferences_.value("gauge", !effortMemory_.empty())},
      {"session_preferences", sessionPreferences_},
      {"active_model", model.value("id", std::string())},
      {"effective_effort", model.value("effective_effort", std::string())},
      {"supported_efforts",
       model.empty() ? std::vector<std::string>() : supportedEfforts(model)},
      {"effort_choices",
       model.empty() ? std::vector<std::string>() : effortChoices(model)},
      {"reasoning_header_count", reasoningHeaders_.size()},{"reasoning_controls",reasonControls},{"navigation_footer",footer},
      {"reasoning_header_x", reasonHeader.x1 + uiMetrics_.dp(22)},
      {"reasoning_header_y", (reasonHeader.y1 + reasonHeader.y2) / 2},
      {"reasoning_observed", reasoningMetadata_.value("reasoning_observed", false)},
      {"reasoning_expanded_count", expandedReasoning_.size()},{"web_controls_count",webControls_.size()},
      {"camera_ready",
       cameraImage_ && textures_.count(cameraImage_) && cameraError_.empty()},
      {"camera_generation",camera_.generation()},
      {"camera_angle", cameraAngle_},
      {"camera_control_rotations", [this] {
        Json rotations=Json::array();
        for(auto visual:cameraRotatingControls_)rotations.push_back(lv_obj_get_style_transform_rotation(visual,LV_PART_MAIN)/10);
        return rotations;
      }()},
      {"camera_controls", [this] {
        Json controls=Json::object();
        if(view_!="camera"||!body_)return controls;
        lv_area_t body;lv_obj_get_coords(body_,&body);
        const auto workspace=layout().cameraWorkspace(lv_obj_get_width(body_),lv_obj_get_height(body_),cameraAngle_%180!=0);
        auto add=[&](const char *name,const LayoutRect &r) {
          controls[name]={body.x1+r.x,body.y1+r.y,body.x1+r.x+r.width-1,body.y1+r.y+r.height-1};
        };
        add("preview",workspace.preview);add("direction",workspace.direction);
        add("shutter",workspace.shutter);add("turn",workspace.turn);
        return controls;
      }()},
      {"camera_transforms", {{"preview", camera_.transforms(cameraAngle_).preview},
                             {"workspace", camera_.transforms(cameraAngle_).workspace},
                             {"saved", camera_.transforms(cameraAngle_).saved}}},
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
      {"network_transport", httpDiagnostics()},
      {"touch_input", touchInput_.diagnostics()},
      {"image_touch", {{"peak",imageTouchPeak_},{"pinch_updates",imagePinchUpdates_},{"mode",imageGesture_.mode},{"source",touchInput_.enabled()?"direct-read-only":"host-test"}}},
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
      {"balances", balanceDiagnostic()},
      {"ui_scale", {{"saved",uiScaleSaved_},{"applied",uiMetrics_.scalePercent()},{"draft",uiScaleDraft_},
        {"pending",!uiScaleRequest_.empty()},{"ready",uiScaleReady_},{"error",!uiScaleError_.empty()},
        {"changes",uiScaleChanges_},{"slider",bounds(uiScaleSlider_)},{"preview",bounds(uiScalePreview_)},
        {"anchor",{{"message",scaleAnchor_.message},{"region",scaleAnchor_.region},{"offset",scaleAnchor_.offset},{"y",scaleAnchor_.y},{"pending",scaleAnchor_.pending}}}}},
      {"surface_motion",motion::retiring()},{"page_slide_x",screenObj_?lv_obj_get_style_translate_x(screenObj_,LV_PART_MAIN):0},
      {"appearance",{{"theme",theme::name(theme::mode())},{"pending",!themeRequest_.empty()},{"ready",themeReady_}}},
      {"refresh_rate",{{"applied",display_.refreshRate()},
        {"period_ms",display_.refreshPeriod()},
        {"animation_period_ms",display_.animationPeriod()},
        {"input_period_ms",display_.inputPeriod()},
        {"default_hz",refresh::defaultRate(screen_.width,screen_.height)},
        {"configured",refreshSaved_},{"pending",!refreshRequest_.empty()},
        {"ready",refreshReady_},{"changes",refreshChanges_},{"error",refreshError_}}},
      {"render_timing",displayTimingInfo()},{"display_stack",display_.stacking()},{"display_info",display_.information()},{"pen_observer",{{"ready",penObserverReady_},{"code",systemOverlayStatus_.value("pen_observer_code",0)},{"supported",penObserverRequest_!="unsupported"},{"pending",!penObserverRequest_.empty()&&penObserverRequest_!="unsupported"}}},
      {"surface_snapshot_timing",{{"count",surfaceSnapshotTiming().count},{"total_us",surfaceSnapshotTiming().totalUs},{"max_us",surfaceSnapshotTiming().maxUs}}},
      {"font_cache",fontCacheInfo()},
      {"reading_font", {{"percent", readingFontPercent_}, {"size", readingFontSize()},
                        {"draft", readingFontDraft_},{"applied",uiMetrics_.textPercent()},
                        {"pending",!fontRequest_.empty()},{"ready",fontReady_},{"error",!fontError_.empty()},
                        {"changes",fontChanges_},
                        {"roles",{{"body",uiMetrics_.fontSize(UiFont::Body)},
                          {"button",uiMetrics_.fontSize(UiFont::Button)},
                          {"title",uiMetrics_.fontSize(UiFont::PageTitle)},
                          {"field_title",uiMetrics_.fontSize(UiFont::FieldTitle)},
                          {"field_value",uiMetrics_.fontSize(UiFont::FieldValue)},
                          {"composer",uiMetrics_.fontSize(UiFont::Composer)},
                          {"detail",uiMetrics_.fontSize(UiFont::Detail)},
                          {"tiny",uiMetrics_.fontSize(UiFont::Tiny)}}},
                        {"slider", bounds(fontSlider_)},
                        {"preview", bounds(fontPreview_)}}},
      {"page", pageDiagnostic()},
      {"image_viewer", imageViewerDiagnostic()},
      {"back_navigation", {{"root",navigationRoot()},{"sequence",backPolicySequence_},{"policy_ok",backPolicyOk_},{"blocked",backPolicyBlocked_},{"claimed",backClaimed_},{"edge",backGeometry(screen_.width,uiMetrics_).edge},{"slop",backGeometry(screen_.width,uiMetrics_).slop},{"distance",backGeometry(screen_.width,uiMetrics_).distance}}},
      {"scroll_bottom", body_ ? lv_obj_get_scroll_bottom(body_) : 0},
      {"composer_width", composer_ ? lv_obj_get_width(composer_) : 0},
      {"composer_hidden", !chatComposerVisible()},
      {"composer_shortcut", {{"mode", composerShortcut_.mode()},
         {"available", composerShortcutAvailable()}, {"shown", composerShortcut_.shown()},
         {"distance", composerShortcut_.distance()}, {"limit", composerShortcut_.limit()},
         {"bounds", {layout().composerShortcut().x, layout().composerShortcut().y,
                     layout().composerShortcut().x + layout().composerShortcut().width - 1,
                     layout().composerShortcut().y + layout().composerShortcut().height - 1}}}},
      {"composer_radius",chatControls_?lv_obj_get_style_radius(chatControls_,LV_PART_MAIN):0},
      {"composer_line_height",font(uiMetrics_.fontSize(UiFont::Composer))->line_height},
      {"composer_scroll", composer_ ? lv_obj_get_scroll_y(composer_) : 0},
      {"composer_scroll_bottom", composer_ ? lv_obj_get_scroll_bottom(composer_) : 0},
      {"reader_height",body_?lv_obj_get_height(body_):0},
      {"page_content_height",pageContent_?lv_obj_get_height(pageContent_):0},
      {"safe_insets", {layout().safeSide(),layout().safeTop(),layout().safeSide(),layout().safeBottom()}},
      {"screen_safety",{{"rounded",layout().safety().enabled()},{"radius",layout().safety().radius()},
        {"margin",layout().safety().margin()},{"navigation_x",layout().navigationX()}}},
      {"toolbar_y", chatControls_ ? lv_obj_get_y(chatControls_) : -1},
      {"to_bottom_visible", toBottom_ && !lv_obj_is_hidden(toBottom_)},
      {"session", session_},
      {"generating", generating_},
      {"compressing", contextBusy_},
      {"keyboard", keyboard_},
      {"display_visible", display_.visible()},{"display_top_height",display_.topHeight()},
      {"ocr", editorOcrActive_},
      {"visible", visible_},
      {"page_visible",pageVisible_},{"window_focused",windowFocused_},
      {"system_overlay",systemOverlayStatus_},
      {"screen_power",{{"available",powerState.available},{"known",powerState.known},
        {"awake",powerState.awake},{"blocked",powerState.blocked},
        {"sequence",powerState.sequence},{"age_ms",powerState.age}}},
      {"display_present_pending",display_.pending()},{"ocr_release_pending",!editorOcrStopId_.empty()},{"input_focused",touchLeaseActive_},
      {"width", screen_.width},
      {"height", screen_.height},
      {"rotation", screen_.rotation}};
}
void App::demo(const std::string &markdown) {
  db_.append(session_, "assistant", markdown);
  show("chat");
}
} // namespace penchat
