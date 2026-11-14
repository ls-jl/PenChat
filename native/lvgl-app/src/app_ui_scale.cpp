#include "app.hpp"
#include <limits>
namespace penchat {
namespace {
bool under(lv_obj_t *object,lv_obj_t *root) {
  for(auto p=object;p;p=lv_obj_get_parent(p))if(p==root)return true;
  return false;
}
lv_obj_t *textLabel(lv_obj_t *root,const std::string &text) {
  if(lv_obj_check_type(root,&lv_label_class)&&text==lv_label_get_text(root))return root;
  for(uint32_t i=0;i<lv_obj_get_child_count(root);++i)
    if(auto label=textLabel(lv_obj_get_child(root,i),text))return label;
  return nullptr;
}
}
void App::uiScaleBusy(bool busy) {
  for(auto o:uiScaleControls_) {
    if(busy)lv_obj_add_state(o,LV_STATE_DISABLED);else lv_obj_remove_state(o,LV_STATE_DISABLED);
  }
  if(uiScaleStatus_) {
    lv_label_set_text(uiScaleStatus_,busy?"正在保存…":uiScaleError_.c_str());
    lv_obj_update_layout(uiScaleStatus_);
    if(uiScalePreview_)lv_obj_set_y(uiScalePreview_,lv_obj_get_y(uiScaleStatus_)+
        std::max(uiMetrics_.dp(32),int(lv_obj_get_height(uiScaleStatus_))+uiMetrics_.dp(10)));
  }
}
void App::uiScalePage() {
  pageHeader("界面缩放");
  auto save=pageAction("保存",0,uiMetrics_.dp(84),[this]{events_.push("ui_scale_save");});
  primary(save);uiScaleControls_.push_back(save);
  const int left=uiMetrics_.dp(10),width=std::max(1,layout().contentWidth()-2*left);
  auto description=label(pageContent_,"调整全应用的文字、控件和间距。保存后生效；键盘会自动适配可用空间。",
      left,uiMetrics_.dp(8),width,uiMetrics_.fontSize(UiFont::Detail),theme::muted);
  lv_obj_update_layout(description);
  int y=lv_obj_get_y(description)+lv_obj_get_height(description)+uiMetrics_.dp(12);
  const int height=std::max(44,uiMetrics_.dp(44)),side=std::max(44,uiMetrics_.dp(56));
  uiScaleCaption_=label(pageContent_,"",left+side,y,width-2*side,uiMetrics_.fontSize(UiFont::PageTitle));
  lv_obj_set_style_text_align(uiScaleCaption_,LV_TEXT_ALIGN_CENTER,0);
  auto step=[this](int delta){if(uiScaleRequest_.empty())updateUiScalePreview(uiScaleDraft_+delta);};
  auto minus=button(pageContent_,"−",left,y,side,height,[step]{step(-1);});pageActions_["减小"]=minus;
  auto plus=button(pageContent_,"+",left+width-side,y,side,height,[step]{step(1);});pageActions_["增大"]=plus;
  uiScaleControls_.push_back(minus);uiScaleControls_.push_back(plus);
  y+=height+uiMetrics_.dp(18);
  uiScaleSlider_=lv_slider_create(pageContent_);lv_obj_remove_style_all(uiScaleSlider_);
  const int inset=uiMetrics_.dp(22);
  lv_obj_set_pos(uiScaleSlider_,left+inset,y);lv_obj_set_size(uiScaleSlider_,std::max(1,width-2*inset),uiMetrics_.dp(16));
  lv_obj_set_ext_click_area(uiScaleSlider_,uiMetrics_.dp(16));lv_slider_set_range(uiScaleSlider_,75,150);
  for(auto part:{LV_PART_MAIN,LV_PART_INDICATOR,LV_PART_KNOB}) {
    lv_obj_set_style_bg_opa(uiScaleSlider_,LV_OPA_COVER,part);lv_obj_set_style_radius(uiScaleSlider_,LV_RADIUS_CIRCLE,part);
    lv_obj_set_style_bg_color(uiScaleSlider_,lv_color_hex(part==LV_PART_MAIN?theme::border:part==LV_PART_KNOB?theme::text:theme::accent),part);
  }
  lv_obj_set_style_pad_all(uiScaleSlider_,uiMetrics_.dp(7),LV_PART_KNOB);
  lv_obj_add_event_cb(uiScaleSlider_,[](lv_event_t *e){auto app=static_cast<App *>(lv_event_get_user_data(e));
    if(app->uiScaleRequest_.empty())app->updateUiScalePreview(lv_slider_get_value(app->uiScaleSlider_));
  },LV_EVENT_VALUE_CHANGED,this);
  uiScaleControls_.push_back(uiScaleSlider_);
  y+=uiMetrics_.dp(32);
  label(pageContent_,"75%",left,y,width/2,uiMetrics_.fontSize(UiFont::Detail),theme::muted);
  auto maximum=label(pageContent_,"150%",left+width/2,y,width/2,uiMetrics_.fontSize(UiFont::Detail),theme::muted);
  lv_obj_set_style_text_align(maximum,LV_TEXT_ALIGN_RIGHT,0);
  y+=uiMetrics_.dp(32);
  auto reset=button(pageContent_,"恢复默认",left,y,std::min(width,uiMetrics_.dp(144)),height,[this]{if(uiScaleRequest_.empty())updateUiScalePreview(100);});
  pageActions_["恢复默认"]=reset;uiScaleControls_.push_back(reset);
  y+=height+uiMetrics_.dp(10);
  uiScaleStatus_=label(pageContent_,"",left,y,width,uiMetrics_.fontSize(UiFont::Detail),theme::muted);
  y+=uiMetrics_.dp(32);
  uiScalePreview_=box(pageContent_,left,y,width,LV_SIZE_CONTENT,theme::surface);
  lv_obj_set_scrollable(uiScalePreview_,false);lv_obj_set_clickable(uiScalePreview_,false);
  updateUiScalePreview(uiScaleDraft_);uiScaleBusy(!uiScaleRequest_.empty());
}
void App::updateUiScalePreview(int percent) {
  uiScaleDraft_=std::max(75,std::min(150,percent));
  if(uiScaleSlider_)lv_slider_set_value(uiScaleSlider_,uiScaleDraft_,LV_ANIM_OFF);
  if(uiScaleCaption_)lv_label_set_text(uiScaleCaption_,(std::to_string(uiScaleDraft_)+"%").c_str());
  if(!uiScalePreview_)return;
  const UiMetrics preview(100,uiScaleDraft_);
  lv_obj_clean(uiScalePreview_);lv_obj_set_style_radius(uiScalePreview_,preview.dp(12),0);
  lv_obj_update_layout(uiScalePreview_);
  const int inset=preview.dp(12),width=std::max(1,int(lv_obj_get_width(uiScalePreview_))-2*inset);
  int y=inset;
  auto line=[&](const std::string &text,UiFont role,uint32_t ink) {
    auto o=label(uiScalePreview_,text,inset,y,width,preview.fontSize(role),ink);
    lv_obj_update_layout(o);y+=lv_obj_get_height(o)+preview.dp(10);
  };
  line("界面预览",UiFont::PageTitle,theme::text);
  line("中文与 English Aa 123\n文字和留白一起调整。",UiFont::Body,theme::text);
  auto sample=box(uiScalePreview_,inset,y,width,preview.dp(44),theme::text,preview);
  lv_obj_set_scrollable(sample,false);lv_obj_set_clickable(sample,false);
  lv_obj_set_style_radius(sample,preview.button().radius,0);
  auto caption=label(sample,"示例按钮",0,0,std::max(1,width-preview.button().captionPadding),preview.fontSize(UiFont::Button),theme::primaryText);
  lv_label_set_long_mode(caption,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_height(caption,font(preview.fontSize(UiFont::Button))->line_height);lv_obj_center(caption);
  y+=preview.dp(56);
  line("字段标题",UiFont::FieldTitle,theme::muted);
  line("示例内容",UiFont::FieldValue,theme::text);
  lv_obj_set_height(uiScalePreview_,y);
}
void App::saveUiScale() {
  if(view_!="ui_scale"||!visible_||!inputFocused_||!uiScaleRequest_.empty()||editor_.active||keyboard_)return;
  input_.cancel();touchDown_=false;
  uiScaleError_.clear();uiScaleRequest_=uid("ui-scale");uiScaleBusy(true);
  db_.queueSetting("ui_scale_percent",uiScaleDraft_,uiScaleRequest_);
}
void App::uiScaleSaved(const Event &event) {
  if(uiScaleRequest_.empty()||event.data.value("request",std::string())!=uiScaleRequest_)return;
  if(event.type=="setting_error") {
    uiScaleRequest_.clear();uiScaleError_="未能保存，本次调整未生效。请检查存储空间后重试。";uiScaleBusy(false);return;
  }
  if(event.type=="setting_saved"){uiScaleSaved_=uiScaleDraft_;uiScaleReady_=true;}
}
void App::uiScaleTick() {
  if(!uiScaleReady_||!visible_||!inputFocused_||!inputFocus_.allowed(ticks())||keyboard_||editor_.active||touchDown_)return;
  input_.cancel();resetBack();resetPull();readReset();composerShortcut_.pause();
  composerTapAllowed_=pullTrackActivity_=false;
  const bool changed=uiMetrics_.scalePercent()!=uiScaleSaved_;
  uiScaleReady_=false;uiScaleRequest_.clear();
  if(changed) {
    clear();uiMetrics_=UiMetrics(100,uiScaleSaved_);rich_.setMetrics(uiMetrics_);
    editorLayoutCache_.reset();++uiScaleChanges_;
    scaleAnchor_.pending=scaleAnchor_.session==session_&&!scaleAnchor_.following;
  }
  show("settings");
  auto row=settingsControls_.find("ui_scale");
  if(row!=settingsControls_.end()) {
    lv_obj_update_layout(screenObj_);
    lv_area_t item,viewport;lv_obj_get_coords(row->second.row,&item);lv_obj_get_coords(body_,&viewport);
    const int scroll=lv_obj_get_scroll_y(body_);
    const int maximum=std::max(0,scroll+int(lv_obj_get_scroll_bottom(body_)));
    lv_obj_scroll_to_y(body_,std::max(0,std::min(maximum,scroll+item.y1-viewport.y1-uiMetrics_.dp(8))),LV_ANIM_OFF);
    lv_obj_update_layout(body_);settingsScroll_=lv_obj_get_scroll_y(body_);
  }
}
void App::captureScaleAnchor() {
  scaleAnchor_={};scaleAnchor_.session=session_;scaleAnchor_.following=chatFollowing_;
  if(!body_||!messageContent_||chatFollowing_)return;
  lv_obj_update_layout(body_);lv_area_t viewport;lv_obj_get_coords(body_,&viewport);
  lv_obj_t *row=nullptr;int first=std::numeric_limits<int>::max();
  for(const auto &entry:scaleMessageRows_) {
    lv_area_t a;lv_obj_get_coords(entry.second,&a);
    if(a.y2>=viewport.y1&&a.y1<=viewport.y2&&a.y1<first) {
      row=entry.second;first=a.y1;scaleAnchor_.message=entry.first;
      scaleAnchor_.fraction=std::max(0.,std::min(1.,double(viewport.y1-a.y1)/std::max(1,int(a.y2-a.y1+1))));
      scaleAnchor_.y=std::max(0,int(a.y1-viewport.y1));
    }
  }
  if(!row)return;
  for(auto &entry:readRegions_)if(under(entry.first,row)) {
    for(auto &part:entry.second.parts) {
      const auto clip=readClip(part.object);if(clip.y2<clip.y1||clip.x2<clip.x1)continue;
      if(clip.y1>viewport.y1+uiMetrics_.dp(40))continue;
      const auto offset=readPosition(entry.second,clip.x1,clip.y1);
      scaleAnchor_.region=entry.second.key;scaleAnchor_.offset=offset;
      if(part.rich) {
        readMap(part);if(part.scalarBytes.empty())continue;const size_t local=std::min(offset>=part.begin?offset-part.begin:0,part.scalarBytes.size()-1);
        if(auto hit=part.hits.selectionEndpoint(part.scalarBytes[local],false)) {
          lv_area_t a;lv_obj_get_coords(part.object,&a);
          scaleAnchor_.y=a.y1-lv_obj_get_scroll_y(part.object)+hit->y-viewport.y1;
        }
      }
      return;
    }
  }
  for(const auto &m:rows_)if(m.value("id",std::string())==scaleAnchor_.message) {
    if(auto label=textLabel(row,m.value("output_text",std::string()))) {
      lv_area_t a;lv_obj_get_coords(label,&a);lv_point_t point{0,std::max(0,int(viewport.y1-a.y1))};
      scaleAnchor_.offset=lv_label_get_letter_on(label,&point,false);scaleAnchor_.label=true;
      lv_label_get_letter_pos(label,scaleAnchor_.offset,&point);scaleAnchor_.y=a.y1+point.y-viewport.y1;
    }
    break;
  }
}
void App::restoreScaleAnchor() {
  if(!scaleAnchor_.pending||view_!="chat")return;
  scaleAnchor_.pending=false;
  if(scaleAnchor_.session!=session_||scaleAnchor_.following)return;
  const auto row=scaleMessageRows_.find(scaleAnchor_.message);if(row==scaleMessageRows_.end())return;
  lv_area_t area,viewport;lv_obj_get_coords(row->second,&area);lv_obj_get_coords(body_,&viewport);
  int y=area.y1+int(std::lround(scaleAnchor_.fraction*(area.y2-area.y1+1)));
  if(!scaleAnchor_.region.empty())for(auto &entry:readRegions_)if(entry.second.key==scaleAnchor_.region) {
    for(auto &part:entry.second.parts) {
      const auto offset=std::min(scaleAnchor_.offset,entry.second.length);
      if(offset<part.begin||offset>part.begin+part.length||!part.rich)continue;
      readMap(part);if(part.scalarBytes.empty())continue;
      const auto local=std::min(offset-part.begin,part.scalarBytes.size()-1);
      if(auto hit=part.hits.selectionEndpoint(part.scalarBytes[local],false)) {
        lv_area_t a;lv_obj_get_coords(part.object,&a);y=a.y1-lv_obj_get_scroll_y(part.object)+hit->y;
      }
      break;
    }
    break;
  }
  if(scaleAnchor_.label)for(const auto &m:rows_)if(m.value("id",std::string())==scaleAnchor_.message) {
    const auto text=m.value("output_text",std::string());
    if(auto label=textLabel(row->second,text)) {
      lv_point_t point;lv_label_get_letter_pos(label,std::min(scaleAnchor_.offset,editLength(text)),&point);
      lv_area_t a;lv_obj_get_coords(label,&a);y=a.y1+point.y;
    }
    break;
  }
  const int maximum=std::max(0,int(lv_obj_get_scroll_y(body_)+lv_obj_get_scroll_bottom(body_)));
  lv_obj_scroll_to_y(body_,std::max(0,std::min(maximum,int(lv_obj_get_scroll_y(body_))+y-viewport.y1-scaleAnchor_.y)),LV_ANIM_OFF);
  chatScroll_=lv_obj_get_scroll_y(body_);chatFollowing_=false;
}
} // namespace penchat
