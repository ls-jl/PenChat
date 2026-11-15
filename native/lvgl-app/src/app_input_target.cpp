#include "app.hpp"
#include <algorithm>
namespace penchat {
void App::inputTarget(lv_obj_t *object,const std::string &key,int style) {
  const bool fresh=inputTargets_.count(object)==0;
  inputTargets_[object]={key,style,++inputTargetSerial_};
  if(fresh)lv_obj_add_event_cb(object,[](lv_event_t *e) {
    auto self=static_cast<App *>(lv_event_get_user_data(e));auto object=lv_event_get_target_obj(e);
    if(self->inputSource_==object)self->inputSource_=nullptr;
    if(self->editorTarget_==object) {
      self->editorTarget_=nullptr;
      if(self->editor_.active)self->events_.push("editor_target_lost",{{"id",self->editor_.id}});
    }
    self->inputTargets_.erase(object);
  },LV_EVENT_DELETE,this);
  if(editor_.active&&editorInPlace()&&view_==editorTargetView_&&key==editorTargetKey_&&editorTarget_!=object) {
    editorTarget_=object;editorTargetHeight_=lv_obj_get_height(object);
  }
}
void App::inputAction(lv_obj_t *object,const std::function<void()> &action) {
  auto found=inputTargets_.find(object);if(found==inputTargets_.end())return;
  const auto serial=found->second.serial;const auto view=view_;auto previousModal=modal_;
  lv_point_t point{};auto input=lv_indev_active();if(input)lv_indev_get_point(input,&point);
  lv_area_t area;lv_obj_get_coords(object,&area);const lv_point_t relative{point.x-area.x1,point.y-area.y1};
  bool picker=false;
  for(auto parent=object;parent;parent=lv_obj_get_parent(parent))if(parent==attachments_)picker=true;
  if(keepChatEditor()&&picker) {action();return;}
  if(editor_.active&&editorInPlace()) {
    if(object==editorTarget_)return;
    // Commit first while retaining the existing page objects. The copied action
    // may then focus another field or save the form without using freed widgets.
    editorFocusSwitch_=true;
    try {if(!editorAccept()){editorFocusSwitch_=false;return;}}catch(...){editorFocusSwitch_=false;editorDiscardTransfer();throw;}
    editorFocusSwitch_=false;
    found=inputTargets_.find(object);
    if(found==inputTargets_.end()||found->second.serial!=serial||view_!=view||modal_!=previousModal) {
      editorDiscardTransfer();if(editorPendingBuild_&&!modal_)build();return;
    }
  }
  inputSource_=object;
  inputSourcePoint_=relative;
  try {action();}catch(...){inputSource_=nullptr;editorDiscardTransfer();throw;}
  if(inputSource_==object)inputSource_=nullptr;
  editorDiscardTransfer();
  if(editorPendingBuild_&&!editor_.active&&!modal_&&!attachments_)build();
}
void App::editorDiscardTransfer() {
  if(!editorTransfer_)return;
  editorTransfer_=false;editorInterrupt();editorViewport();
  editorLocalActions_.clear();
  editorPreeditLabel_=editorChoices_=editorExpand_=editorExactList_=editorQueryLabel_=nullptr;
  if(editorRoot_)lv_obj_delete(editorRoot_);
  editorRoot_=editorText_=nullptr;editorHandles_[0]=editorHandles_[1]=nullptr;editorKeys_.clear();
  const int preferred=editor_.preferredChinese;editor_=EditorState{};editor_.preferredChinese=preferred;
  editorKeyboardId_.clear();editorSelectionOnly_=false;syncScanner();
}
EditorLayout::Popup App::editorTextArea() const {
  if(!editorText_||lv_obj_is_hidden(editorText_))return {0,0,0,0};
  const auto geometry=editorLayout();
  if(geometry.compactFull)return {geometry.textX,0,geometry.textAreaWidth,geometry.compactMetrics.headerHeight};
  lv_area_t a;lv_obj_get_coords(editorText_,&a);
  bool inBody=false;for(auto p=editorTarget_;p;p=lv_obj_get_parent(p))if(p==body_){inBody=true;break;}
  if(editorInPlace()&&inBody&&body_&&!modal_) {
    lv_area_t clip;lv_obj_get_coords(body_,&clip);
    a.x1=std::max(a.x1,clip.x1);a.x2=std::min(a.x2,clip.x2);
    a.y1=std::max(a.y1,clip.y1);a.y2=std::min(a.y2,clip.y2);
  }
  return {a.x1,a.y1,std::max(0,int(a.x2-a.x1+1)),std::max(0,int(a.y2-a.y1+1))};
}
int App::chatComposerHeight() const {
  const auto geometry=layout();
  const auto c=uiMetrics_.composer();
  const auto textFont=font(uiMetrics_.fontSize(UiFont::Composer));
  const int base=geometry.composerHeight();
  const bool editing=editor_.active&&editorInPlace()&&editorInlineChat_;
  const auto &text=editing?editor_.text:draft_;
  if(!editing&&text.empty())return base;
  if(!geometry.portrait()) {
    const bool gauge=sessionPreferences_.value("gauge",!effortMemory_.empty());
    const int width=std::max(1,geometry.composerWidth()-geometry.composerEntryLeft()-geometry.composerEntryRight(gauge)-c.textPadding());
    lv_point_t size{};lv_text_get_size(&size,text.c_str(),textFont,0,0,width,LV_TEXT_FLAG_NONE);
    const int inset=std::max(0,(c.control-int(textFont->line_height))/2);
    return std::min(base*2,std::max(base,int(size.y)+2*inset+2*c.landscapeInset));
  }
  // Text has its own inset; the tool row keeps the resting pill's arc center.
  const int leftPad=c.expandedPad+c.textInset,rightPad=c.expandedPad,topPad=c.textTop;
  const int toolsTop=geometry.composerHeight()/2+geometry.composerSendDiameter()/2;
  const int width=std::max(1,geometry.composerWidth()-leftPad-rightPad);
  lv_point_t size{};lv_text_get_size(&size,text.c_str(),textFont,0,0,width,LV_TEXT_FLAG_NONE);
  const int minimum=std::max(topPad+textFont->line_height+c.textToolsGap+toolsTop,
                             editing?editorComposerOpenHeight_:0);
  return std::min(base*3,
                  std::max(minimum,topPad+std::max(int(textFont->line_height),int(size.y))+c.textToolsGap+toolsTop));
}
int App::chatComposerY() const {
  return chatComposerY(chatComposerHeight());
}
int App::chatComposerY(int composerHeight) const {
  const auto geometry=layout();
  const bool docked=editor_.active&&editorInPlace()&&view_=="chat";
  const int bottom=docked?editorLayout().panelTop-uiMetrics_.composer().dockGap:screen_.height-geometry.safeBottom();
  return bottom-composerHeight;
}
void App::syncChatBottomPadding() {
  if(view_!="chat"||!messageContent_)return;
  const auto geometry=layout();
  const bool docked=editor_.active&&editorLayout().docked;
  // Reserve resting composer space even while it is hidden. Using visibility
  // here feeds the changed scroll extent back into bottom detection and makes
  // the composer and return button repeatedly reverse at the end of a scroll.
  // Pull and keyboard-push offsets remain viewport-only transforms.
  const int tail=uiMetrics_.dp(16);
  // The reader continues behind the floating capsule. Reserve its occluded
  // tail for last-line reachability, rather than clipping away the backdrop.
  const int padding=tail+chatComposerHeight()+(docked?
      screen_.height-editorLayout().panelTop+uiMetrics_.composer().dockGap+uiMetrics_.composer().readerGap:geometry.safeBottom());
  if(lv_obj_get_style_pad_bottom(messageContent_,LV_PART_MAIN)!=padding) {
    const bool previous=chatLayout_;chatLayout_=true;
    lv_obj_set_style_pad_bottom(messageContent_,padding,0);
    lv_obj_update_layout(messageContent_);
    if(chatFollowing_&&body_) {
      lv_obj_scroll_to_y(body_,LV_COORD_MAX,LV_ANIM_OFF);
      chatScroll_=std::max(0,int(lv_obj_get_scroll_y(body_)));
    }
    chatBottomState_.pause();
    chatLayout_=previous;
  }
}
void App::layoutChatComposer() {
  if(!chatControls_||!composer_)return;
  const auto geometry=layout();
  const auto c=uiMetrics_.composer();
  const auto textFont=font(uiMetrics_.fontSize(UiFont::Composer));
  const bool editing=editor_.active&&editorInPlace()&&editorInlineChat_;
  const bool expanded=geometry.portrait()&&(editing||!draft_.empty());
  const bool previous=chatLayout_;chatLayout_=true;
  const int oldHeight=lv_obj_get_height(chatControls_);
  const int height=chatComposerHeight(),width=geometry.composerWidth();
  const int pad=expanded?c.expandedPad:geometry.composerInset();
  lv_obj_set_height(chatControls_,height);
  lv_obj_set_style_radius(chatControls_,geometry.composerHeight()/2,0);
  if(toBottom_)lv_obj_set_y(toBottom_,chatComposerY(height)-c.returnOffset());
  const bool gauge=sessionPreferences_.value("gauge",!effortMemory_.empty());
  const int x=expanded?pad+c.textInset:geometry.composerEntryLeft();
  const int y=expanded?c.textTop:pad;
  const int w=expanded?width-x-pad:width-x-geometry.composerEntryRight(gauge);
  const int toolsTop=geometry.composerHeight()/2+geometry.composerSendDiameter()/2;
  const int h=expanded?height-y-c.textToolsGap-toolsTop:height-2*pad;
  lv_obj_set_pos(composer_,x,y);lv_obj_set_size(composer_,w,h);
  for(uint32_t i=0;i<lv_obj_get_child_count(chatControls_);++i) {
    auto child=lv_obj_get_child(chatControls_,i);if(child==composer_||!lv_obj_is_clickable(child))continue;
    const bool send=i+1==lv_obj_get_child_count(chatControls_);
    const int grow=send?(geometry.composerSendDiameter()-c.control)/2:0;
    const int toolPad=geometry.composerInset();
    const int cx=send?width-toolPad-c.control-grow:i==(geometry.portrait()?1u:0u)?pad:width-toolPad-c.gaugeTrailing;
    lv_obj_set_align(child,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(child,cx,height-toolPad-c.control-grow);
  }
  auto value=lv_obj_get_child(composer_,0);
  if(value) {
    const bool wrapping=expanded||!geometry.portrait();
    lv_obj_set_style_text_font(value,textFont,0);
    lv_label_set_long_mode(value,wrapping?LV_LABEL_LONG_MODE_WRAP:LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_align(value,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(value,expanded?0:c.textInset,expanded?0:std::max(0,(std::min(c.control,h)-textFont->line_height)/2));
    lv_obj_set_size(value,expanded?w:std::max(0,w-c.textPadding()),expanded?h:wrapping?LV_SIZE_CONTENT:textFont->line_height);
    lv_obj_set_scrollable(composer_,!geometry.portrait()&&height==geometry.composerHeight()*2);
    lv_obj_set_scroll_dir(composer_,LV_DIR_VER);
    lv_obj_set_scroll_elastic(composer_,false);lv_obj_set_scroll_chain_ver(composer_,false);
  }
  if(oldHeight!=height&&chatComposerVisible()&&(editing||!hasConversationContent_||chatFollowing_||atLatestBottom())) {
    lv_anim_delete(chatControls_,nullptr);chatControlsTarget_=chatComposerY(height);
    lv_obj_set_y(chatControls_,chatControlsTarget_);
  }
  syncChatBottomPadding();
  lv_obj_update_layout(chatControls_);chatLayout_=previous;
}
void App::editorAttachTarget() {
  if(!editor_.active||!editorInPlace()||!editorTarget_)return;
  auto found=inputTargets_.find(editorTarget_);if(found==inputTargets_.end())return;
  const int style=found->second.style;
  auto value=lv_obj_get_child(editorTarget_,style==2?1:0);
  if(value)lv_obj_set_hidden(value,true);
  const bool chat=editorInlineChat_;
  const auto field=fieldMetrics(uiMetrics_);
  const int y=chat?0:style==1?field.valueTop:style==2?uiMetrics_.dp(28):uiMetrics_.dp(2);
  const int x=chat?0:style==1?field.inset:style==2?uiMetrics_.dp(12):uiMetrics_.dp(4);
  const int h=chat?lv_obj_get_height(editorTarget_):style==3?uiMetrics_.dp(44):std::max(uiMetrics_.dp(24),int(lv_obj_get_height(editorTarget_))-y-uiMetrics_.dp(2));
  if(!editorText_) {
    editorText_=lv_textarea_create(editorTarget_);
    editorBindCaret();
    lv_obj_add_event_cb(editorText_,[](lv_event_t *e){
      auto self=static_cast<App *>(lv_event_get_user_data(e));
      if(self->editorText_==lv_event_get_target_obj(e))self->editorText_=nullptr;
    },LV_EVENT_DELETE,this);
    lv_obj_set_style_text_font(editorText_,font(uiMetrics_.fontSize(chat?UiFont::Composer:style==1?UiFont::FieldValue:UiFont::Button)),0);
    lv_obj_set_style_text_color(editorText_,lv_color_hex(keyboardDesign().style(KeyboardRole::Input).foreground),0);
    lv_obj_set_style_bg_opa(editorText_,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(editorText_,0,0);lv_obj_set_style_radius(editorText_,0,0);
    lv_obj_set_style_pad_all(editorText_,0,0);
    if(chat) {
      lv_obj_set_style_pad_top(editorText_,0,0);lv_obj_set_style_pad_bottom(editorText_,0,0);
      lv_obj_set_style_text_line_space(editorText_,0,0);lv_obj_set_style_text_letter_space(editorText_,0,0);
    }
    lv_obj_set_style_border_color(editorText_,lv_color_hex(keyboardDesign().caret),LV_PART_CURSOR|LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(editorText_,lv_color_hex(keyboardDesign().selection),LV_PART_SELECTED);
    lv_obj_set_style_text_color(editorText_,lv_color_hex(keyboardDesign().style(KeyboardRole::Input).foreground),LV_PART_SELECTED);
    lv_obj_set_scroll_elastic(editorText_,false);lv_obj_set_scroll_momentum(editorText_,false);
    lv_textarea_set_cursor_click_pos(editorText_,false);
    lv_textarea_set_max_length(editorText_,editor_.limit);
    lv_textarea_set_one_line(editorText_,!editor_.multiline);
    lv_textarea_set_password_show_time(editorText_,0);
    lv_textarea_set_placeholder_text(editorText_,editor_.placeholder.c_str());
    lv_obj_add_state(editorText_,LV_STATE_FOCUSED);
  }
  lv_obj_set_pos(editorText_,x,y);lv_obj_set_size(editorText_,std::max(uiMetrics_.dp(44),int(lv_obj_get_width(editorTarget_))-2*x),h);
  lv_textarea_set_password_mode(editorText_,editor_.secret&&!editor_.reveal);
  if(editor_.text!=lv_textarea_get_text(editorText_))lv_textarea_set_text(editorText_,editor_.text.c_str());
  lv_textarea_set_cursor_pos(editorText_,editor_.cursor);
  if(editorFollowCursor_&&chat) {
    // Resizing is immediate; do not leave a cursor-scroll animation that the
    // second layout pass would cancel before revealing the new last line.
    lv_obj_update_layout(editorText_);lv_point_t point{};
    lv_label_get_letter_pos(lv_textarea_get_label(editorText_),editor_.cursor,&point);
    const int visible=lv_obj_get_content_height(editorText_);
    int scroll=lv_obj_get_scroll_y(editorText_);
    if(point.y<scroll)scroll=point.y;
    else if(point.y+editorTextFont()->line_height>scroll+visible)scroll=point.y+editorTextFont()->line_height-visible;
    editorScroll(scroll);
  } else if(!editorFollowCursor_)lv_obj_scroll_to_y(editorText_,editorTextScroll_,LV_ANIM_OFF);
  editorFollowCursor_=false;lv_obj_update_layout(editorText_);
  editorTextScroll_=lv_obj_get_scroll_y(editorText_);
  if(editorPlaceCursor_) {
    lv_area_t a;lv_obj_get_coords(editorTarget_,&a);
    editor_.position(editorPositionAt(a.x1+editorInitialPoint_.x,a.y1+editorInitialPoint_.y));
    lv_textarea_set_cursor_pos(editorText_,editor_.cursor);editorPlaceCursor_=false;
  }
  editorSyncCaret();
}
bool App::editorAccept() {
  if(!editor_.preedit.empty()&&!editor_.raw()) {editor_.notice="已达到长度上限";editorBuild();return false;}
  editorFinish(true);return true;
}
void App::editorDetachTarget() {
  if(!editorInPlace())return;
  if(editorText_)lv_obj_delete(editorText_);
  if(!editorTarget_)return;
  auto found=inputTargets_.find(editorTarget_);if(found==inputTargets_.end())return;
  auto value=lv_obj_get_child(editorTarget_,found->second.style==2?1:0);
  if(value)lv_obj_set_hidden(value,false);
  if(editorTargetHeight_>0)lv_obj_set_height(editorTarget_,editorTargetHeight_);
  if(pageContent_&&lv_obj_get_parent(editorTarget_)==pageContent_)portraitPage();
}
} // namespace penchat
