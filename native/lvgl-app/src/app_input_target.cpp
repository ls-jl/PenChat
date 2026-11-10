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
  const int base=layout().composerHeight();
  const bool editing=editor_.active&&editorInPlace()&&editorInlineChat_;
  const auto &text=editing?editor_.text:draft_;
  if(!editing&&text.empty())return base;
  if(!layout().portrait()) {
    const bool gauge=sessionPreferences_.value("gauge",!effortMemory_.empty());
    const int width=std::max(44,layout().composerWidth()-layout().composerEntryLeft()-layout().composerEntryRight(gauge)-24);
    lv_point_t size{};lv_text_get_size(&size,text.c_str(),font(20),0,0,width,LV_TEXT_FLAG_NONE);
    const int inset=std::max(0,(44-int(font(20)->line_height))/2);
    return std::min(base*2,std::max(base,int(size.y)+2*inset+4));
  }
  // The expanded portrait composer uses text above its tools. The
  // larger portrait pill inset belongs only to the collapsed single row.
  const int pad=12;
  const int width=std::max(44,layout().composerWidth()-2*pad);
  lv_point_t size{};lv_text_get_size(&size,text.c_str(),font(20),0,0,width,LV_TEXT_FLAG_NONE);
  const int minimum=std::max(2*pad+font(20)->line_height+8+44,
                             editing?editorComposerOpenHeight_:0);
  return std::min(std::max(minimum,base*3),
                  std::max(minimum,2*pad+std::max(int(font(20)->line_height),int(size.y))+8+44));
}
int App::chatComposerY() const {
  const bool docked=editor_.active&&editorInPlace()&&view_=="chat";
  const auto keyboard=editorLayout();
  const bool official=officialDocked_&&keyboard_&&officialHeight_>0;
  const int bottom=docked?(official?keyboard.panelTop-4:keyboard.compositionTop(font(16)->line_height)):
    screen_.height-layout().safeBottom();
  return bottom-chatComposerHeight();
}
void App::layoutChatComposer() {
  if(!chatControls_||!composer_)return;
  const bool editing=editor_.active&&editorInPlace()&&editorInlineChat_;
  const bool expanded=layout().portrait()&&(editing||!draft_.empty());
  const bool previous=chatLayout_;chatLayout_=true;
  const int oldHeight=lv_obj_get_height(chatControls_);
  const int height=chatComposerHeight(),width=layout().composerWidth();
  const int pad=expanded?12:layout().composerInset();
  lv_obj_set_height(chatControls_,height);
  lv_obj_set_style_radius(chatControls_,layout().portrait()?(expanded?28:height/2):layout().composerHeight()/2,0);
  if(toBottom_)lv_obj_set_y(toBottom_,chatComposerY()-56);
  const bool gauge=sessionPreferences_.value("gauge",!effortMemory_.empty());
  const int x=expanded?pad:layout().composerEntryLeft();
  const int y=pad;
  const int w=expanded?width-2*pad:width-x-layout().composerEntryRight(gauge);
  const int h=expanded?height-2*pad-8-44:height-2*pad;
  lv_obj_set_pos(composer_,x,y);lv_obj_set_size(composer_,w,h);
  for(uint32_t i=0;i<lv_obj_get_child_count(chatControls_);++i) {
    auto child=lv_obj_get_child(chatControls_,i);if(child==composer_)continue;
    const bool send=i+1==lv_obj_get_child_count(chatControls_);
    const int grow=send?(layout().composerSendDiameter()-44)/2:0;
    const int cx=send?width-pad-44-grow:i==0?pad:width-pad-96;
    lv_obj_set_align(child,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(child,cx,height-pad-44-grow);
  }
  auto value=lv_obj_get_child(composer_,0);
  if(value) {
    const bool wrapping=expanded||!layout().portrait();
    lv_label_set_long_mode(value,wrapping?LV_LABEL_LONG_MODE_WRAP:LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_align(value,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(value,expanded?0:12,expanded?0:std::max(0,(std::min(44,h)-font(20)->line_height)/2));
    lv_obj_set_size(value,expanded?w:std::max(0,w-24),expanded?h:wrapping?LV_SIZE_CONTENT:font(20)->line_height);
    lv_obj_set_scrollable(composer_,!layout().portrait()&&height==layout().composerHeight()*2);
    lv_obj_set_scroll_dir(composer_,LV_DIR_VER);
    lv_obj_set_scroll_elastic(composer_,false);lv_obj_set_scroll_chain_ver(composer_,false);
  }
  if(oldHeight!=height&&chatComposerVisible()&&(editing||!hasConversationContent_||chatFollowing_||atLatestBottom())) {
    lv_anim_delete(chatControls_,nullptr);chatControlsTarget_=chatComposerY();
    lv_obj_set_y(chatControls_,chatControlsTarget_);
  }
  lv_obj_update_layout(chatControls_);chatLayout_=previous;
}
void App::editorAttachTarget() {
  if(!editor_.active||!editorInPlace()||!editorTarget_)return;
  auto found=inputTargets_.find(editorTarget_);if(found==inputTargets_.end())return;
  const int style=found->second.style;
  auto value=lv_obj_get_child(editorTarget_,style==2?1:0);
  if(value)lv_obj_set_hidden(value,true);
  const bool chat=editorInlineChat_;
  const int y=chat?0:style==1?23:style==2?28:2;
  const int x=chat?0:style==1?12:style==2?12:4;
  const int h=chat?lv_obj_get_height(editorTarget_):style==3?44:std::max(24,int(lv_obj_get_height(editorTarget_))-y-(chat?layout().composerInset():2));
  if(!editorText_) {
    editorText_=lv_textarea_create(editorTarget_);
    editorBindCaret();
    lv_obj_add_event_cb(editorText_,[](lv_event_t *e){
      auto self=static_cast<App *>(lv_event_get_user_data(e));
      if(self->editorText_==lv_event_get_target_obj(e))self->editorText_=nullptr;
    },LV_EVENT_DELETE,this);
    lv_obj_set_style_text_font(editorText_,font(style==1?19:20),0);
    lv_obj_set_style_text_color(editorText_,lv_color_hex(theme::text),0);
    lv_obj_set_style_bg_opa(editorText_,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(editorText_,0,0);lv_obj_set_style_radius(editorText_,0,0);
    lv_obj_set_style_pad_all(editorText_,0,0);
    if(chat) {
      lv_obj_set_style_pad_top(editorText_,0,0);lv_obj_set_style_pad_bottom(editorText_,0,0);
      lv_obj_set_style_text_line_space(editorText_,0,0);lv_obj_set_style_text_letter_space(editorText_,0,0);
    }
    lv_obj_set_style_border_color(editorText_,lv_color_hex(theme::caret),LV_PART_CURSOR|LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(editorText_,lv_color_hex(theme::border),LV_PART_SELECTED);
    lv_obj_set_style_text_color(editorText_,lv_color_hex(theme::text),LV_PART_SELECTED);
    lv_obj_set_scroll_elastic(editorText_,false);lv_obj_set_scroll_momentum(editorText_,false);
    lv_textarea_set_cursor_click_pos(editorText_,false);
    lv_textarea_set_max_length(editorText_,editor_.limit);
    lv_textarea_set_one_line(editorText_,!editor_.multiline);
    lv_textarea_set_password_show_time(editorText_,0);
    lv_textarea_set_placeholder_text(editorText_,editor_.placeholder.c_str());
    lv_obj_add_state(editorText_,LV_STATE_FOCUSED);
  }
  lv_obj_set_pos(editorText_,x,y);lv_obj_set_size(editorText_,std::max(44,int(lv_obj_get_width(editorTarget_))-2*x),h);
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
    else if(point.y+font(20)->line_height>scroll+visible)scroll=point.y+font(20)->line_height-visible;
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
