#include "app.hpp"
#include "icons.hpp"
#include "editor_layout.hpp"
#include "chat_text.hpp"
#include <cctype>
namespace penchat {
namespace {
constexpr uint64_t layoutHoldMs=500;
int keyboardClusterWidth(lv_obj_t *label,size_t first,size_t last) {
  const char *text=lv_label_get_text(label),*begin=text,*end=text;
  for(size_t index=0;*end&&index<last;++index) {
    if(index==first)begin=end;
    ++end;while(*end&&(static_cast<unsigned char>(*end)&192)==128)++end;
  }
  const std::string cluster(begin,end);
  lv_point_t size{};lv_text_get_size(&size,cluster.c_str(),lv_obj_get_style_text_font(label,LV_PART_MAIN),
      lv_obj_get_style_text_letter_space(label,LV_PART_MAIN),0,LV_COORD_MAX,LV_TEXT_FLAG_EXPAND);
  return size.x;
}
std::string keyboardPunctuation(const std::string &key,bool chinese) {
  if(!chinese)return key;
  static const std::map<std::string,std::string> punctuation={
    {",","，"},{".","。"},{"?","？"},{"!","！"},{":","："},{";","；"},
    {"(","（"},{")","）"},{"[","【"},{"]","】"}
  };
  auto found=punctuation.find(key);
  return found==punctuation.end()?key:found->second;
}
}
void App::edit(const std::string &text, const std::string &placeholder,
               std::function<void(const std::string &)> callback, int maxLength,
               const char *inputType, std::function<void()> cancelled,
               bool multiline, bool secret, bool selectionOnly, bool inlineChat) {
  if (editor_.active || keyboard_ || replyBusy_ || contextRegenerating_) return;
  editorSwapSides_=wideKeyboardSwap_;
  const bool reuse=editorTransfer_&&editorRoot_&&this->layout().portrait()&&!selectionOnly;
  const bool englishOnly=secret||std::string(inputType)!="ZhCNPreferred";
  const bool sameKeys=reuse&&englishOnly==editorEnglishOnly_&&secret==editor_.secret;
  const int oldLayout=editor_.layout,oldMode=editor_.mode,oldShift=editor_.shift;
  closeNavigation(false); closeAttachments(); resetPull();
  editorSelectionOnly_=selectionOnly;editorInlineChat_=inlineChat&&this->layout().portrait();editorReaderTouch_=false;
  if(this->layout().portrait()&&!selectionOnly) {
    editorTarget_=inputSource_?inputSource_:(view_=="chat"?composer_:nullptr);
    auto target=inputTargets_.find(editorTarget_);
    if(target==inputTargets_.end()) {editorTarget_=nullptr;modal("无法输入","请重新点按要编辑的字段。");return;}
    editorTargetKey_=target->second.key;editorTargetView_=view_;
    editorTargetHeight_=lv_obj_get_height(editorTarget_);
    editorInlineChat_=editorTarget_==composer_;
    editorPlaceCursor_=inputSource_!=nullptr;editorInitialPoint_=inputSourcePoint_;
    editorSendAssets_=false;
    if(editorInlineChat_) {
      if(!messageEdit_.empty())editorSendAssets_=!messageEditImages_.empty();
      else {
        const auto latest=db_.messages(session_,0,1);
        editorSendAssets_=!latest.empty()&&latest[0].value("role",std::string())=="user"&&
          (!latest[0].value("stored_path",std::string()).empty()||!latest[0].value("original_path",std::string()).empty());
      }
    }
  }
  const int preferred=editor_.preferredChinese;
  editor_ = EditorState{};editor_.preferredChinese=preferred;
  editorUpstream_=std::string::npos;editorAffinityPhase_=-1;
  editor_.active = true; editor_.text = text; editor_.position(editLength(text));
  editor_.id = uid("input"); editor_.placeholder = placeholder;
  editor_.limit = maxLength; editor_.multiline = multiline; editor_.secret = secret;
  editor_.layout = !secret && std::string(inputType)=="ZhCNPreferred"?editor_.preferredChinese:1;
  if(sameKeys){editor_.layout=oldLayout;editor_.mode=oldMode;editor_.shift=oldShift;}
  editor_.chinese = editor_.layout!=1;editorEnglishOnly_=englishOnly;
  if(!reuse)editorKeyboardId_=uid("keyboard");
  editorTransfer_=false;
  editorLearning_=editorExact_=Json::array();editorPinyinPending_=Json();
  editorLayoutMenu_=editorPunctuation_=false;
  editorClipboardOpen_=editorClipboardConfirm_=editorUndoMenu_=false;
  if(!reuse)editorOcrMulti_=false;
  editorOcrActive_=editorOcrBusy_=editorOcrShown_=false; editorOcrCapture_=editorOcrInserted_=0;
  editorTextTouch_=editorCursorDrag_=false; editorHandle_=-1; editorLastTap_=0;editorTextScroll_=0;editorFollowCursor_=true;
  editorPolicy(!editorSelectionOnly_);
  editorExpanded_ = editorMenu_ = false; editorCandidates_ = editorCandidateDetails_ = Json::array();
  const auto layout=editorLayout();
  editorMenuAnchor_={layout.textX+layout.textWidth/2,layout.textY+52};
  editorInFlight_ = editorDue_ = 0; editorChoosing_=editorSelectPending_=false; editorDeferred_.clear();editorChosenWord_.clear();
  if(!reuse)editorBuildCount_=editorRefreshCount_=0;
  editorRefreshPending_=false;editorChoicesDirty_=true;
  editCancellation_ = std::move(cancelled);
  const auto view = view_; const int scroll = body_ ? lv_obj_get_scroll_y(body_) : 0;
  editCompletion_ = [this,callback,view,scroll](const std::string &s) {
    const int restore=editorLayout().docked&&body_?lv_obj_get_scroll_y(body_):scroll;
    callback(s);
    if (view_ == view && body_) { lv_obj_update_layout(body_); lv_obj_scroll_to_y(body_,restore,LV_ANIM_OFF); paintVisible(); }
  };
  const auto id = editor_.id;
  if(reuse){editorViewport();editorAttachTarget();}
  if(this->layout().portrait()&&!selectionOnly)editorOcrStart();
  else whenScannerReleased([this,id] { if (editor_.active && editor_.id == id) { editorBuild();editorOcrStart(); } });
  if(reuse)editorRefresh();
}
void App::editorBuild() {
  if (!editor_.active || keyboard_) return;
  if (editorPointer_) { editorRebuild_=true; return; }
  ++editorBuildCount_;
  if (!editorCanExpand()) editorExpanded_ = false;
  const auto layout=editorLayout();
  editorRebuild_=editorRefreshPending_=false;
  editorPreeditLabel_=editorChoices_=editorExpand_=editorExactList_=editorQueryLabel_=nullptr;
  editorMagnify(false);
  const bool preserveTextScroll=editorText_&&(!editorFollowCursor_||
      editor_.text==lv_textarea_get_text(editorText_));
  if(editorText_) editorTextScroll_=lv_obj_get_scroll_y(editorText_);
  const int preservedTextScroll=editorTextScroll_;
  auto previousRoot = editorRoot_;
  editorKeys_.clear(); editorLocalActions_.clear(); if(!editorInPlace())editorText_ = nullptr;
  editorRoot_ = box(lv_screen_active(),0,0,editorLayout().width,editorLayout().height,theme::background);
  lv_obj_set_scrollable(editorRoot_,false);
  if(layout.scaled) {
    lv_obj_set_style_transform_pivot_x(editorRoot_,0,0);
    lv_obj_set_style_transform_pivot_y(editorRoot_,0,0);
    lv_obj_set_style_transform_scale_x(editorRoot_,layout.scaleX(),0);
    lv_obj_set_style_transform_scale_y(editorRoot_,layout.scaleY(),0);
  }
  if(layout.docked) {
    lv_obj_set_style_bg_opa(editorRoot_,LV_OPA_TRANSP,0);
    lv_obj_set_clickable(editorRoot_,false);
    auto panel=box(editorRoot_,0,layout.panelTop,editorLayout().width,editorLayout().height-layout.panelTop,theme::background);
    lv_obj_set_scrollable(panel,false);lv_obj_set_clickable(panel,true);
  }
  auto action = [this](const std::string &s) { events_.push("editor_action",{{"id",editor_.id},{"action",s}}); };
  auto control = [&](const std::string &title,int x,int y,int w,const std::string &a) {
    auto b=button(editorRoot_,title,x,y,w,44,[action,a] { action(a); },theme::surface);
    auto l=lv_obj_get_child(b,0); lv_obj_set_width(l,w-4); lv_obj_set_style_text_font(l,font(17),0);
    return b;
  };
  auto key = [&](std::string title,std::string value,int x,int y,int w,int h) {
    x+=layout.keyInset;
    auto b=box(editorRoot_,x,y,w,h,theme::surface); lv_obj_set_scrollable(b,false);
    lv_obj_set_style_radius(b,9,0);
    if (value=="shift" && editor_.shift)
      lv_obj_set_style_bg_color(b,lv_color_hex(theme::text),0);
    if (editorPointer_ && editorHeld_>=0 && !editorSlid_ && x==editorPress_.x && y==editorPress_.y && value==editorPress_.value)
      lv_obj_set_style_bg_color(b,lv_color_hex(theme::border),0);
    if (value == "shift" || value == "delete" || value == "enter") {
      attachIcon(b, value == "shift" && editor_.shift == 2 ? "shift-lock" : value,
                 value == "shift" && editor_.shift ? theme::background : theme::text);
    } else {
      auto l=label(b,title,0,0,w,23);
      lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
      lv_obj_align(l,LV_ALIGN_CENTER,0,0);
    }
    editorKeys_.push_back({x,y,w,h,value,b,layout.keyTouchBounds({x,y,w,h})});
  };
  if(!editorSelectionOnly_) {
  const bool nine=editor_.mode==0&&editor_.layout==2&&!editor_.secret;
  if(nine) {
    const int rail=88, gridX=rail+8;
    const double cell=(layout.keySpan-gridX-12)/3.0;
    const char *groups[]={"1 标点","2 ABC","3 DEF","4 GHI","5 JKL","6 MNO","7 PQRS","8 TUV","9 WXYZ"};
    for(int n=0;n<9;++n)
      key(groups[n],n?"t9:"+std::to_string(n+1):"punctuation",layout.px(gridX+n%3*(cell+4)),layout.keyTop+n/3*(layout.rowHeight+6),layout.px(cell),layout.rowHeight);
    auto filter=box(editorRoot_,layout.keyInset+4,layout.keyTop,rail,3*layout.rowHeight+12,theme::background);
    lv_obj_set_scroll_dir(filter,LV_DIR_VER);lv_obj_set_scroll_elastic(filter,false);
    editorExactList_=filter;
    int x=4;
    auto bottom=[&](const std::string &title,const std::string &value,int w){key(title,value,x,layout.bottomY,w,layout.bottomHeight);x+=w+4;};
    bottom("123","mode",68);
    if(!layout.portrait){bottom("","language",64);attachIcon(editorKeys_.back().object,"globe",theme::text);}
    bottom("拼音","space",layout.keySpan-(layout.portrait?280:348));bottom("","delete",88);bottom("","enter",104);
  } else {
  std::vector<std::string> rows = editor_.mode==0 ? std::vector<std::string>{"qwertyuiop","asdfghjkl","zxcvbnm"} :
      editor_.mode==1 ? std::vector<std::string>{"1234567890","@#%&*()-+","/=:;!?_"} : std::vector<std::string>{"[]{}<>^~|\\","$&*_+=`\"'","!?:;/-@"};
  for (int r=0;r<3;++r) {
    const double start = r==0 ? 4 : r==1 ?
      (layout.keySpan - (rows[r].size() * layout.keyWidth + (rows[r].size()-1)*4))/2 : layout.keyWidth+10;
    for (size_t i=0;i<rows[r].size();++i) {
      std::string v(1,rows[r][i]);
      if (!editor_.mode && editor_.shift) v[0]=std::toupper(v[0]);
      key(keyboardPunctuation(v,editor_.chinese&&!editor_.secret),v,layout.px(start+i*(layout.keyWidth+4)),layout.keyTop+r*(layout.rowHeight+6),layout.px(layout.keyWidth),layout.rowHeight);
      if (!editor_.mode && r == 1) {
        auto &k = editorKeys_.back();
        k.hit = layout.keyTouchBounds({k.x,k.y,k.w,k.h}, i == 0, i + 1 == rows[r].size());
      }
    }
  }
  key(editor_.mode ? (editor_.mode==1?"#+=":"123") : "",editor_.mode?"symbols":"shift",4,layout.keyTop+2*(layout.rowHeight+6),layout.px(layout.keyWidth+2),layout.rowHeight);
  key("","delete",layout.px(layout.keySpan-2-2*layout.keyWidth),layout.keyTop+2*(layout.rowHeight+6),layout.px(2*layout.keyWidth-2),layout.rowHeight);
  const int modeWidth=layout.px(layout.keyWidth+14), punctuationWidth=layout.px(layout.keyWidth+10), enterWidth=layout.px(2*layout.keyWidth-4);
  const int spaceWidth=layout.keySpan-8-(layout.portrait?16:20)-modeWidth-(layout.portrait?2:3)*punctuationWidth-enterWidth;
  int bottomX=4;
  auto bottomKey=[&](const std::string &title,const std::string &value,int width) {
    key(title,value,bottomX,layout.bottomY,width,layout.bottomHeight); bottomX+=width+4;
  };
  bottomKey(editor_.mode==0?"123":"ABC","mode",modeWidth);
  bottomKey(keyboardPunctuation(",",editor_.chinese&&!editor_.secret),",",punctuationWidth);
  if(!layout.portrait){bottomKey("","language",punctuationWidth);attachIcon(editorKeys_.back().object,"globe",theme::text);}
  bottomKey(editor_.chinese?"拼音":"English","space",spaceWidth);
  bottomKey(keyboardPunctuation(".",editor_.chinese&&!editor_.secret),".",punctuationWidth);
  bottomKey("","enter",enterWidth);
  }
  if(layout.portrait) {
    const auto globe=layout.globeKey();
    key("","language",globe.x,globe.y,globe.width,globe.height);
    auto &language=editorKeys_.back();
    lv_obj_set_style_bg_opa(language.object,LV_OPA_TRANSP,0);
    // Its invisible hit area must not steal the bottom punctuation row.
    const int end=language.hit.y+language.hit.height;
    language.hit.y=std::max(language.hit.y,layout.bottomY+layout.bottomHeight+3);
    language.hit.height=end-language.hit.y;
    attachIcon(language.object,"globe-large",theme::text);
  }
  auto editorPanel=box(editorRoot_,layout.editorPanelX(),layout.panelTop,layout.editorPanelWidth(),layout.portrait?layout.keyTop-6-layout.panelTop:layout.height,0x111111); lv_obj_set_scrollable(editorPanel,false);
  // Composition/candidates never resize the keys; actions share a bottom baseline.
  if(!layout.docked)lv_obj_move_background(editorPanel);
  for(const auto &a:layout.actions(editorInlineChat_)) {
    const std::string actionName=a.name;
    if(editorCandidateRow()&&actionName!="done")continue;
    const std::string title=actionName=="cancel"?"取消":
      actionName=="done"?(layout.portrait?"":editorInlineChat_?"收起":"完成"):actionName=="ocr-mode"?
      (!editorOcrActive_&&!editorOcrId_.empty()?"重试":editorOcrMulti_?(layout.portrait?"多行扫描":"多行"):(layout.portrait?"单行扫描":"单行")):"";
    auto b=control(title,a.x,a.y,a.width,actionName);
    if(actionName=="done"&&layout.portrait) {
      lv_obj_set_style_bg_opa(b,LV_OPA_TRANSP,0);attachIcon(b,"keyboard-done",theme::text);
      // Grow only the invisible target; leave space below the Enter key.
      lv_obj_set_ext_click_area(b,8);
    } else if(actionName=="done"&&!editorInlineChat_)primary(b);
    if(actionName=="clipboard")attachIcon(b,"clipboard",theme::text);
  }
  } else {
    const int inset=layout.portrait?layout.keyInset:0;
    const int top=layout.portrait?this->layout().safeTop():0;
    auto back=control("",layout.portrait?inset:16,8+top,48,"cancel");attachIcon(back,"back",theme::text);
    label(editorRoot_,editor_.notice.empty()?"选择文本 · 双击选择，拖动手柄调整":editor_.notice,96,14+top,editorLayout().width-338-inset,19,theme::muted);
    control("全选",editorLayout().width-220-inset,6+top,96,"all");primary(control("复制",editorLayout().width-116-inset,6+top,104,"copy"));
  }
  if(editorInPlace()) {editorViewport();editorAttachTarget();}
  else {
  editorText_=lv_textarea_create(editorRoot_);
  editorBindCaret();
  lv_obj_set_pos(editorText_,layout.textX,layout.textY); lv_obj_set_size(editorText_,layout.textAreaWidth,layout.textHeight);
  lv_obj_set_style_text_font(editorText_,font(20),0);
  lv_obj_set_style_text_color(editorText_,lv_color_hex(theme::text),0);
  lv_obj_set_style_bg_color(editorText_,lv_color_hex(theme::surface),0);
  lv_obj_set_style_border_width(editorText_,0,0); lv_obj_set_style_radius(editorText_,8,0);
  lv_obj_set_style_border_color(editorText_,lv_color_hex(theme::caret),LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_scroll_elastic(editorText_,false);
  lv_obj_set_scroll_momentum(editorText_,false);
  lv_obj_set_style_pad_all(editorText_,6,0);
  lv_obj_set_style_bg_color(editorText_,lv_color_hex(theme::border),LV_PART_SELECTED);
  lv_textarea_set_max_length(editorText_,editor_.limit);
  lv_textarea_set_one_line(editorText_,!editor_.multiline);
  lv_obj_set_height(editorText_,layout.textHeight);
  lv_textarea_set_password_mode(editorText_,editor_.secret&&!editor_.reveal);
  lv_textarea_set_password_show_time(editorText_,0);
  lv_textarea_set_placeholder_text(editorText_,editor_.placeholder.c_str());
  lv_textarea_set_text(editorText_,editor_.text.c_str());
  lv_textarea_set_cursor_pos(editorText_,editor_.cursor);
  lv_obj_add_state(editorText_,LV_STATE_FOCUSED);
  lv_textarea_set_cursor_click_pos(editorText_,false);
  lv_obj_set_style_text_color(editorText_,lv_color_hex(theme::text),LV_PART_SELECTED);
  if(!editorFollowCursor_)lv_obj_scroll_to_y(editorText_,editorTextScroll_,LV_ANIM_OFF);
  editorFollowCursor_=false;
  }
  if(!editorSelectionOnly_) {
  auto pre=label(editorRoot_,editorCompositionText(),layout.textX,layout.compositionY,layout.textWidth,16,theme::muted);
  editorPreeditLabel_=pre;
  if(layout.portrait&&editorOcrShown_&&editor_.preedit.empty()&&editor_.notice.empty())
    lv_label_set_text(pre,editorOcrBusy_?"正在识别…":"扫描已插入 · 可继续划扫");
  lv_label_set_long_mode(pre,LV_LABEL_LONG_MODE_DOTS); lv_obj_set_height(pre,20);
  const int candidateY=editorExpanded_?layout.overlayY()+(layout.portrait?44:0):layout.candidateY, candidateH=editorExpanded_?layout.overlayHeight()-(layout.portrait?44:0):44;
  if (editorExpanded_) { auto cover=box(editorRoot_,layout.textX,layout.overlayY(),layout.textWidth,layout.overlayHeight(),0x111111);lv_obj_set_scrollable(cover,false); label(editorRoot_,"全部候选",layout.textX+4,layout.portrait?layout.overlayY():16,layout.textWidth-60,18,theme::muted); }
  auto choices=box(editorRoot_,layout.textX,candidateY,editorExpanded_?layout.textWidth:layout.textWidth-50,candidateH,0x111111);
  editorChoices_=choices;
  if(layout.portrait&&!editorCandidateRow())lv_obj_set_hidden(choices,true);
  lv_obj_set_scroll_dir(choices,editorExpanded_?LV_DIR_VER:LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(choices,LV_SCROLLBAR_MODE_AUTO);
  editorExpand_ = control(editorExpanded_?"收起":"展开",layout.textRight()-44,layout.portrait?(editorExpanded_?layout.overlayY():layout.candidateY):6,44,"expand");
  editorChoicesEmpty_=false;editorChoicesDirty_=true;editorRefreshCandidates();
  if(editor_.secret) attachIcon(control("",layout.textRight()-102,layout.portrait?layout.candidateY:6,44,"reveal"),editor_.reveal?"eye-off":"eye",theme::muted);
  if(editorOcrShown_&&!layout.portrait) {
    auto scan=box(editorRoot_,layout.textX,layout.candidateY,layout.textWidth-(editor_.secret?110:50),44,0x111111);
    lv_obj_set_scrollable(scan,false);
    label(scan,editorOcrBusy_?"正在识别…":editor_.notice.empty()?"扫描已插入 · 可继续划扫":"扫描未完成，请重试",4,10,layout.textWidth-(editor_.secret?110:50)-8,17,theme::muted);
  }
  }
  for(int i=0;i<2;++i) {
    editorHandles_[i]=box(editorRoot_,0,0,44,44,theme::surface);
    lv_obj_set_scrollable(editorHandles_[i],false);
    lv_obj_set_style_bg_opa(editorHandles_[i],LV_OPA_TRANSP,0);
    lv_obj_set_clickable(editorHandles_[i],false);
    attachIcon(editorHandles_[i],"selection-handle",theme::link);
  }
  if(editorUndoMenu_||editorMenu_) {
    const auto popup=editorUndoMenu_?
      layout.undoPopup(editorMenuAnchor_.x,editorMenuAnchor_.y,!editorClipboard_.current.empty()):
      layout.selectionPopup(editorMenuAnchor_.x,editorMenuAnchor_.y,editorSelectionOnly_);
    auto menu=box(editorRoot_,popup.x,popup.y,popup.width,popup.height,theme::sidebar);
    lv_obj_set_scrollable(menu,false);
    lv_obj_set_style_radius(menu,10,0);
    lv_obj_set_style_shadow_color(menu,lv_color_hex(theme::scrim),0);
    lv_obj_set_style_shadow_opa(menu,LV_OPA_40,0);
    lv_obj_set_style_shadow_width(menu,12,0);
    lv_obj_set_style_shadow_offset_y(menu,3,0);
    std::vector<std::pair<std::string,std::string>> commands;
    if(editorUndoMenu_) {
      commands={{"撤销","undo"}};
      if(!editorClipboard_.current.empty())commands.push_back({"粘贴","paste"});
    } else commands=editorSelectionOnly_?
      std::vector<std::pair<std::string,std::string>>{{"全选","all"},{"复制","copy"}}:
      std::vector<std::pair<std::string,std::string>>{{"全选","all"},{"复制","copy"},{"粘贴","paste"},{"剪切","cut"}};
    for(size_t i=0;i<commands.size();++i) {
      auto b=button(menu,commands[i].first,4+int(i)*92,4,88,44,[action,commands,i]{action(commands[i].second);});
      if((commands[i].second=="undo"&&editor_.undo.empty())||
         ((commands[i].second=="copy"||commands[i].second=="cut")&&editor_.secret&&!editor_.reveal)||
         (commands[i].second=="paste"&&editorClipboard_.current.empty()))lv_obj_add_state(b,LV_STATE_DISABLED);
    }
  }
  if(editorClipboardOpen_) {
    // Docked clipboard replaces the keyboard, never the focused page above it.
    const int x=layout.portrait?this->layout().safeSide():layout.textX;
    const int width=layout.portrait?screen_.width-2*x:layout.textWidth;
    const int top=layout.portrait?layout.panelTop:0;
    if(layout.portrait) {
      auto cover=box(editorRoot_,0,top,screen_.width,screen_.height-top,0x111111);
      lv_obj_set_scrollable(cover,false);lv_obj_set_clickable(cover,true);
    }
    auto head=box(editorRoot_,x,top,width,74,0x111111);lv_obj_set_scrollable(head,false);
    label(head,"剪贴板 · "+std::to_string(editorClipboard_.records.size()),4,12,width-188,19);
    editorLocalActions_["clip-clear"]=button(head,editorClipboardConfirm_?"确认清空":"清空",width-182,6,98,44,[action]{action("clip-clear");});
    editorLocalActions_["clip-close"]=button(head,"关闭",width-78,6,74,44,[action]{action("clip-close");});
    const int listTop=layout.portrait?top+74:layout.overlayY();
    const int listHeight=layout.portrait?screen_.height-this->layout().safeBottom()-listTop:layout.overlayHeight();
    auto list=box(editorRoot_,x,listTop,width,listHeight,0x111111);lv_obj_set_scroll_dir(list,LV_DIR_VER);
    int cy=0;
    // A sensitive explicit copy may be pasted once from the current slot but is never a history row.
    if(editorClipboard_.sensitiveCurrent) {
      button(list,"临时复制内容 · 点击粘贴",0,cy,width-8,44,[action]{action("paste");},theme::surface,LV_TEXT_ALIGN_LEFT);cy+=50;
    }
    if(editorClipboard_.records.empty()&&!editorClipboard_.sensitiveCurrent) label(list,"暂无记录",8,12,width-30,18,theme::muted);
    for(size_t i=0;i<editorClipboard_.records.size();++i) {
      auto item=editorClipboard_.records[i];std::replace(item.begin(),item.end(),'\n',' ');
      button(list,item,0,cy,width-58,44,[action,i]{action("clip-paste:"+std::to_string(i));},theme::surface,LV_TEXT_ALIGN_LEFT);
      auto del=button(list,"",width-52,cy,44,44,[action,i]{action("clip-delete:"+std::to_string(i));});attachIcon(del,"trash",theme::muted);cy+=50;
    }
  }
  if(editorLayoutMenu_||editorPunctuation_) {
    auto backdrop=box(editorRoot_,0,0,editorLayout().width,editorLayout().height,theme::background);
    lv_obj_set_style_bg_opa(backdrop,LV_OPA_TRANSP,0);lv_obj_set_scrollable(backdrop,false);
    lv_obj_set_clickable(backdrop,true);
    lv_obj_add_event_cb(backdrop,[](lv_event_t *event){
      auto app=static_cast<App *>(lv_event_get_user_data(event));
      app->events_.push("editor_action",{{"id",app->editor_.id},{"action","layout-close"}});
    },LV_EVENT_CLICKED,this);
    const bool layouts=editorLayoutMenu_;
    auto popup=layout.floatingPopup(layout.keyInset+100,layout.bottomY-160,304,156);
    const int rowHeight=layout.portrait?64:48,padding=layout.portrait?8:4;
    if(layouts) {
      auto globe=layout.globeKey();
      for(const auto &key:editorKeys_)if(key.value=="language")globe={key.x,key.y,key.w,key.h};
      const int width=layout.portrait?std::min(320,screen_.width-2*this->layout().safeSide()):224;
      const int height=std::min(layout.height-8,4*rowHeight+2*padding);
      popup=layout.floatingPopup(layout.portrait?this->layout().safeSide():globe.x-16,
                                  globe.y-12-height,width,height);
    }
    auto panel=box(editorRoot_,popup.x,popup.y,popup.width,popup.height,theme::sidebar);
    lv_obj_set_style_radius(panel,layouts?(layout.portrait?24:16):12,0);
    lv_obj_set_scrollable(panel,layouts);lv_obj_set_scroll_dir(panel,LV_DIR_VER);
    lv_obj_set_scroll_elastic(panel,false);lv_obj_set_scroll_chain_ver(panel,false);
    if(layouts){lv_obj_set_style_border_width(panel,1,0);lv_obj_set_style_border_color(panel,lv_color_hex(theme::border),0);}
    if(layouts) {
      const char *names[]={"26键拼音","English","9键拼音","有道输入法"};
      for(int i=0;i<4;++i) {
        const auto command=i==3?std::string("official"):"layout:"+std::to_string(i);
        const int itemHeight=rowHeight-(layout.portrait?8:4);
        auto item=button(panel,names[i],padding,padding+i*rowHeight,popup.width-2*padding,itemHeight,
                         [action,command]{action(command);},i<3&&editor_.layout==i?theme::surface:theme::sidebar,LV_TEXT_ALIGN_LEFT);
        lv_obj_set_style_radius(item,itemHeight/2,0);
        auto caption=lv_obj_get_child(item,0);lv_obj_set_style_text_font(caption,font(layout.portrait?22:20),0);
        lv_obj_set_width(caption,popup.width-2*padding-24);lv_obj_align(caption,LV_ALIGN_LEFT_MID,16,0);
        editorLocalActions_[command]=item;
      }
    } else {
      const char *marks[]={"，","。","？","！","、","：","；","“","”","（","）","'"};
      for(int i=0;i<12;++i) {
        const auto command="punct:"+std::string(marks[i]);
        auto item=button(panel,marks[i],4+i%4*74,4+i/4*50,70,44,[action,command]{action(command);});
        editorLocalActions_[command]=item;
      }
    }
  }
  if (previousRoot) lv_obj_delete(previousRoot);
  lv_obj_update_layout(editorRoot_);
  lv_obj_move_foreground(editorRoot_);
  editorSyncText();editorViewport();
  if(preserveTextScroll&&editorText_) {
    // Textarea SIZE_CHANGED starts an animated scroll-to-caret while the new
    // label is being laid out. Restore only after final geometry, cancelling
    // that animation so a menu rebuild or failed handoff cannot move reading.
    editorScroll(preservedTextScroll);editorSyncText();
  }
  editorPaintedKey_=editorPaintKey();
}
uint64_t App::editorPaintKey() const {
  uint64_t key=uint64_t(editor_.layout+editor_.mode*4+editor_.shift*16);
  const bool flags[]={editorSelectionOnly_,editor_.secret,editor_.reveal,editorExpanded_,editorClipboardOpen_,
    editorMenu_,editorUndoMenu_,editorLayoutMenu_,editorPunctuation_,editorCandidateRow(),
    editorOcrShown_,editorOcrBusy_,editorOcrActive_,!editor_.notice.empty()};
  for(size_t i=0;i<sizeof(flags)/sizeof(flags[0]);++i)if(flags[i])key|=uint64_t(1)<<(8+i);
  return key;
}
std::string App::editorCompositionText() const {
  if(!editor_.notice.empty())return editor_.notice;
  std::string text=editor_.preedit+(editor_.fixed.empty()?"":" · "+editor_.fixed);
  if(editor_.layout==1&&(!editor_.parked[0].raw.empty()||!editor_.parked[1].raw.empty()))text+="拼音已暂存";
  if(!editorDeferred_.empty()) {
    std::string pending;
    for(const auto &key:editorDeferred_) {
      if(key.size()==1)pending+=key;
      else if(key.find("t9:")==0)pending+=key.substr(3);
      else if(key=="space")pending+=' ';
      else if(key=="delete"&&!pending.empty())pending.pop_back();
      if(pending.size()>80){pending+="…";break;}
    }
    text+=" · "+(pending.empty()?"正在选词":pending);
  }
  return text;
}
void App::editorRefresh() {
  if(!editor_.active||keyboard_||!editorRoot_)return;
  if(editorPointer_&&editorHeld_<0){editorRefreshPending_=true;return;}
  const bool rebuild=editorRebuild_||editorPaintedKey_!=editorPaintKey()||editorSelectionOnly_||editorExpanded_||editorClipboardOpen_||
    editorMenu_||editorUndoMenu_||editorLayoutMenu_||editorPunctuation_;
  if(rebuild&&!editorPointer_){editorBuild();return;}
  // A held delete may cross the composition/text boundary. Keep editing text
  // responsive, but retain every key and defer structural work until release.
  if(rebuild)editorRebuild_=true;
  editorRefreshPending_=false;++editorRefreshCount_;
  if(editorPreeditLabel_) {
    const auto text=editorCompositionText();
    if(text!=lv_label_get_text(editorPreeditLabel_))lv_label_set_text(editorPreeditLabel_,text.c_str());
  }
  // Composing pinyin does not change the text widget or its geometry.
  const bool textChanged=editorText_&&editor_.text!=lv_textarea_get_text(editorText_);
  if(textChanged||editor_.selected()||(editorText_&&lv_textarea_get_cursor_pos(editorText_)!=int(editor_.cursor)))
    editorSyncText(textChanged);
  if(editorChoicesDirty_&&!editorPointer_)editorRefreshCandidates();
}
void App::editorRefreshCandidates() {
  if(!editorChoices_)return;
  const auto layout=editorLayout();
  auto action=[this](const std::string &value){events_.push("editor_action",{{"id",editor_.id},{"action",value}});};
  const bool empty=editorCandidates_.empty()&&editorExact_.empty();
  if(editorExpand_) {
    lv_obj_set_hidden(editorExpand_,!editorCanExpand());
    if(editorCanExpand())editorLocalActions_["expand"]=editorExpand_;else editorLocalActions_.erase("expand");
  }
  editorChoicesDirty_=false;
  if(empty&&editorChoicesEmpty_&&!editorExpanded_&&(editor_.preedit.empty()||editorQueryLabel_)) {
    if(editorQueryLabel_) {
      const char *text=editorInFlight_||editorDue_?"正在查询…":"无匹配候选";
      if(std::string(lv_label_get_text(editorQueryLabel_))!=text)lv_label_set_text(editorQueryLabel_,text);
    }
    return;
  }
  editorChoicesEmpty_=empty;
  for(auto i=editorLocalActions_.begin();i!=editorLocalActions_.end();) {
    if(i->first.find("choose:")==0||i->first.find("exact:")==0||i->first=="exact-all"||i->first=="pinyin-retry")i=editorLocalActions_.erase(i);else ++i;
  }
  lv_obj_clean(editorChoices_);editorQueryLabel_=nullptr;
  if(editorExactList_) {
    lv_obj_clean(editorExactList_);
    const int rail=88;
    editorLocalActions_["exact-all"]=button(editorExactList_,"全部",0,0,rail-4,44,[action]{action("exact-all");},editor_.fixed.empty()?theme::border:theme::surface);
    for(size_t i=0;i<editorExact_.size();++i) {
      const auto name="exact:"+std::to_string(editorRevision_)+":"+std::to_string(i);
      auto item=button(editorExactList_,editorExact_[i].get<std::string>(),0,48+int(i)*48,rail-4,44,[action,name]{action(name);});
      lv_obj_set_style_text_font(lv_obj_get_child(item,0),font(17),0);lv_obj_set_width(lv_obj_get_child(item,0),rail-12);editorLocalActions_[name]=item;
    }
  }
  auto choices=editorChoices_;
  int cx=0,cy=0;
  for (size_t i=0;i<editorCandidates_.size();++i) {
    const bool corrected=i<editorCandidateDetails_.size()&&editorCandidateDetails_[i].value("corrected",false);
    std::string text=editorCandidates_[i].get<std::string>(); int w=std::max(52,std::min(layout.textWidth-8,int(editLength(text))*22+(corrected?34:20)));
    if (editorExpanded_ && cx+w>layout.textWidth) { cx=0; cy+=48; }
    auto held=corrected?std::make_shared<bool>(false):std::shared_ptr<bool>();
    auto b=button(choices,text,cx,cy,w,44,[action,i,revision=editorRevision_,held] { if(!held||!*held)action("choose:"+std::to_string(revision)+":"+std::to_string(i)); },theme::surface);
    lv_obj_set_width(lv_obj_get_child(b,0),w-(corrected?22:8));
    if(corrected) {
      auto mark=box(b,w-13,7,5,5,theme::accent);lv_obj_set_style_radius(mark,LV_RADIUS_CIRCLE,0);lv_obj_set_scrollable(mark,false);lv_obj_set_clickable(mark,false);
      struct Hint { std::shared_ptr<bool> held; std::function<void()> show; lv_point_t start{}; int distance=0; };
      auto hint=new Hint{held,[action,i,revision=editorRevision_]{action("correction-info:"+std::to_string(revision)+":"+std::to_string(i));}};
      lv_obj_add_event_cb(b,[](lv_event_t *event){
        auto h=static_cast<Hint*>(lv_event_get_user_data(event));const auto code=lv_event_get_code(event);
        if(code==LV_EVENT_DELETE){delete h;return;}
        if(code==LV_EVENT_PRESS_LOST){*h->held=true;return;}
        if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_LONG_PRESSED)return;
        auto input=lv_indev_active();if(!input)return;lv_point_t point;lv_indev_get_point(input,&point);
        if(code==LV_EVENT_PRESSED){h->start=point;h->distance=0;*h->held=false;}
        const int dx=point.x-h->start.x,dy=point.y-h->start.y;h->distance=std::max(h->distance,dx*dx+dy*dy);
        if(code==LV_EVENT_LONG_PRESSED&&h->distance<=144){*h->held=true;h->show();}
      },LV_EVENT_ALL,hint);
    }
    editorLocalActions_["choose:"+std::to_string(editorRevision_)+":"+std::to_string(i)]=b;
    cx+=w+5;
  }
  if (editorCandidates_.empty()&&!editor_.preedit.empty()) {
    editorQueryLabel_=label(choices,editorInFlight_||editorDue_?"正在查询…":"无匹配候选",4,10,layout.textWidth-150,16,theme::muted);
    if(!editor_.notice.empty())editorLocalActions_["pinyin-retry"]=button(choices,"重试",layout.textWidth-148,0,88,44,[action]{action("pinyin-retry");});
  }
  lv_obj_scroll_to(choices,0,0,LV_ANIM_OFF);
}
void App::editorMoveDismiss(int offset) {
  editorDismissOffset_=std::max(0,offset);
  if(editorRoot_)lv_obj_set_style_translate_y(editorRoot_,editorDismissOffset_,0);
  if(editorInlineChat_&&chatControls_)lv_obj_set_style_translate_y(chatControls_,editorDismissOffset_,0);
  if(view_=="chat"&&toBottom_)lv_obj_set_style_translate_y(toBottom_,editorDismissOffset_,0);
  if(body_&&editor_.active&&editorLayout().docked) {
    const bool previous=chatLayout_;chatLayout_=true;
    const int scroll=lv_obj_get_scroll_y(body_);
    const int base=editorDismissTracking_||editorDismissing_||editorDismissReturnAt_?editorDismissBodyHeight_:
        view_=="chat"?chatComposerY()-8:editorLayout().panelTop;
    lv_obj_set_height(body_,base+editorDismissOffset_);lv_obj_update_layout(body_);
    const int maximum=std::max(0,int(lv_obj_get_scroll_y(body_)+lv_obj_get_scroll_bottom(body_)));
    lv_obj_scroll_to_y(body_,std::min(scroll,maximum),LV_ANIM_OFF);
    chatLayout_=previous;paintVisible();
  }
}
void App::editorResetDismiss() {
  editorDismissTracking_=editorDismissing_=false;editorDismissReturnAt_=0;
  editorMoveDismiss(0);
}
bool App::editorDismissTouch(int x,int y,bool down,bool cancelled) {
  if(!editor_.active||!editorLayout().docked)return false;
  if(down&&!editorDismissTracking_) {
    editorDismissTracking_=true;editorDismissStartX_=x;editorDismissStartY_=y;
    const auto l=editorLayout();editorDismissBoundary_=l.panelTop;
    editorDismissBodyHeight_=lv_obj_get_height(body_);
    editorDismissThreshold_=std::max(64,std::min(120,(screen_.height-l.panelTop)/4));
  }
  const int dy=y-editorDismissStartY_,dx=x-editorDismissStartX_;
  if(!cancelled&&down&&!editorDismissing_&&y>=editorDismissBoundary_&&dy>12&&dy>std::abs(dx)) {
    editorDismissing_=true;editorPointer_=true;
    input_.cancel();lv_obj_stop_scroll_anim(body_);
  }
  if(editorDismissing_) {
    if(down&&!cancelled)editorMoveDismiss(std::min(screen_.height-editorDismissBoundary_,std::max(0,y-editorDismissBoundary_)));
    if(!down||cancelled) {
      const bool finish=!cancelled&&editorDismissOffset_>=editorDismissThreshold_;
      editorDismissTracking_=editorDismissing_=editorPointer_=false;
      readerDown_=readerPointer_=readerVertical_=false;readerSettle_=true;
      if(finish) {editorResetDismiss();editorAction("dismiss");}
      else {editorDismissReturnFrom_=editorDismissOffset_;editorDismissReturnAt_=ticks();}
    }
    return true;
  }
  if(!down||cancelled)editorDismissTracking_=false;
  return false;
}
void App::editorViewport() {
  if(!body_)return;
  const auto l=editorLayout();const bool docked=editor_.active&&l.docked;
  const int composerY=chatComposerY();
  if(view_=="chat")layoutChatComposer();
  if(view_=="chat"&&chatControls_) {
    const int target=chatComposerVisible()?composerY:screen_.height+2;
    lv_anim_delete(chatControls_,nullptr);lv_obj_set_y(chatControls_,target);
    chatControlsTarget_=target;
  }
  if(toBottom_)lv_obj_set_y(toBottom_,composerY-56);
  if(view_=="chat"&&messageContent_)lv_obj_set_style_pad_bottom(messageContent_,docked?16:chatComposerHeight()+16+this->layout().safeBottom(),0);
  const int height=docked?(view_=="chat"?composerY-8:l.panelTop)+editorDismissOffset_:screen_.height;
  const bool previous=chatLayout_;chatLayout_=true;
  const int scroll=lv_obj_get_scroll_y(body_);const bool resized=lv_obj_get_height(body_)!=height;
  lv_obj_set_height(body_,height);
  const auto target=inputTargets_.find(editorTarget_);bool fieldResized=false;
  if(docked&&target!=inputTargets_.end()&&editor_.multiline&&!editorInlineChat_&&
     target->second.style!=3&&lv_obj_get_height(editorTarget_)<144) {
    lv_obj_set_height(editorTarget_,144);
    fieldResized=true;
  }
  if(pageContent_&&this->layout().portrait()&&(resized||fieldResized)) {
    lv_obj_set_style_min_height(pageContent_,std::max(0,height-int(lv_obj_get_y(pageContent_))-12),0);
    portraitPage();
  }
  lv_obj_update_layout(body_);
  if(docked&&editorTarget_&&view_!="chat"&&!modal_) {
    lv_area_t area,viewport;lv_obj_get_coords(editorTarget_,&area);lv_obj_get_coords(body_,&viewport);
    int offset=std::max(0,int(area.y2-viewport.y2+8));
    if(area.y1<viewport.y1+8)offset=area.y1-viewport.y1-8;
    if(offset)lv_obj_scroll_to_y(body_,scroll+offset,LV_ANIM_OFF);
  } else if(resized) {
    if(view_=="chat"&&chatFollowing_)lv_obj_scroll_to_y(body_,scroll+lv_obj_get_scroll_bottom(body_),LV_ANIM_OFF);
    else lv_obj_scroll_to_y(body_,scroll,LV_ANIM_OFF);
  }
  const int maximum=std::max(0,int(lv_obj_get_scroll_y(body_)+lv_obj_get_scroll_bottom(body_)));
  if(lv_obj_get_scroll_y(body_)>maximum)lv_obj_scroll_to_y(body_,maximum,LV_ANIM_OFF);
  chatLayout_=previous;
  if(docked)editorAttachTarget();
  if(editorRoot_)lv_obj_set_style_translate_y(editorRoot_,editorDismissOffset_,0);
  if(editorInlineChat_&&chatControls_)lv_obj_set_style_translate_y(chatControls_,editorDismissOffset_,0);
  if(view_=="chat"&&toBottom_)lv_obj_set_style_translate_y(toBottom_,editorDismissOffset_,0);
  if(resized){paintVisible();lv_obj_invalidate(screenObj_);}
}
bool App::editorCandidateRow() const {
  return layout().portrait()&&!editorSelectionOnly_&&!editorClipboardOpen_&&
    (!editor_.secret&&editor_.chinese&&editor_.mode==0&&!editor_.preedit.empty());
}
bool App::editorCanExpand() const {
  return editor_.active && !editorSelectionOnly_ && !editor_.secret &&
      editor_.chinese && editor_.mode == 0 && !editor_.preedit.empty() &&
      !editorCandidates_.empty();
}
void App::editorChanged() {
  editorUpstream_=std::string::npos;
  editorFollowCursor_=true;
  ++editorRevision_; editorChoicesDirty_=true;editorRefreshPending_=true; editorCandidates_=editorExact_=editorCandidateDetails_=Json::array(); editorDue_=ticks()+35; editorExpanded_=false; editor_.notice.clear();
}
void App::editorRememberLayout() {
  if(editor_.secret||editor_.layout==1||editor_.preferredChinese==editor_.layout)return;
  editor_.preferredChinese=editor_.layout;
  db_.queueSetting("pinyin_chinese_layout",editor_.layout==2?"zh9":"zh26");
}
void App::editorUseLayout(int layout,bool cycle) {
  if(editor_.secret) {editor_.notice="密钥使用英文输入";editorLayoutMenu_=false;editorBuild();return;}
  if(cycle)editor_.globe(ticks());else {editor_.changeLayout(layout);editor_.globeChain=false;}
  editorLayoutMenu_=editorPunctuation_=false;
  editorChanged();editorBuild();
}
void App::editorQuery(const std::string &operation,int candidate) {
  if (editorSelectionOnly_ || editorChoosing_ || (editorInFlight_&&operation=="choose") || !visible_ || keyboard_ || editor_.secret || editor_.layout==1) return;
  editorChoosing_=operation=="choose";
  if(editorChoosing_&&candidate>=0&&size_t(candidate)<editorCandidates_.size())editorChosenWord_=editorCandidates_[candidate].get<std::string>();
  editorInFlight_=++editorRequest_; editorSent_=ticks(); editorDue_=0;
  if(!pinyin_.request({{"session",editor_.id},{"sequence",editorInFlight_},{"revision",editorRevision_},
      {"operation",operation},{"text",editor_.preedit},{"fixed",editor_.fixed},
      {"layout",editor_.layout==2?"zh9":"zh26"},{"candidate",candidate},{"candidate_version",editorCandidateVersion_},{"correction",db_.settings().value("pinyin_correction",true)}})) {
    editorInFlight_=0;editorChoosing_=editorSelectPending_=false;editor_.notice="本地拼音暂时繁忙，请重试";editorBuild();
  }
}
void App::editorResult(const Json &m) {
  if (!editor_.active || keyboard_ || !visible_ || m.value("session",std::string())!=editor_.id ||
      m.value("sequence",uint64_t(0))!=editorInFlight_) return;
  if(editorPointer_) {editorPinyinPending_=m;return;}
  editorLatency_=ticks()-editorSent_;
  editorInFlight_=0; editorChoosing_=false;
  if (m.value("revision",uint64_t(0))!=editorRevision_) return;
  if (!m.value("ok",false)) { editorSelectPending_=false;editor_.notice=m.value("error",std::string("本地拼音状态已变化，请重试")); editorBuild(); return; }
  if (m.contains("committed")) {
    editorSelectPending_=false;editorChosenWord_.clear();
    const auto text=m.value("committed",std::string()), remaining=m.value("remaining",std::string()), py=m.value("pinyin",std::string());
    if (!text.empty() && editor_.insert(text)) {
      editorLearning_.push_back({{"word",text},{"pinyin",py}});
      editor_.chainWord+=text;editor_.chainPinyin+=(editor_.chainPinyin.empty()?"":"'")+py;++editor_.chainPieces;
      if(remaining.empty()) {
        if(editor_.chainPieces>1)editorLearning_.push_back({{"word",editor_.chainWord},{"pinyin",editor_.chainPinyin}});
        editor_.chainWord.clear();editor_.chainPinyin.clear();editor_.chainPieces=0;
      }
      if(editorLearning_.size()>256)editorLearning_.erase(editorLearning_.begin(),editorLearning_.end()-256);
      editor_.preedit=remaining;editor_.fixed=m.value("fixed",std::string());
      editor_.contentChanged();editorRememberLayout();editorChanged();
    } else if (text.empty()) editor_.notice="未能选词，请调整拼音或重试";
  } else {editorCandidateVersion_=m.value("candidate_version",uint64_t(0));editorCandidates_=m.value("candidates",Json::array());editorCandidateDetails_=m.value("candidate_details",Json::array());editorExact_=m.value("exact",Json::array());}
  editorChoicesDirty_=true;
  if(editorSelectPending_) {
    editorSelectPending_=false;
    int selected=editorChosenWord_.empty()?0:-1;
    if(!editorChosenWord_.empty())for(size_t i=0;i<editorCandidates_.size();++i)if(editorCandidates_[i]==editorChosenWord_){selected=int(i);break;}
    if(selected>=0&&size_t(selected)<editorCandidates_.size())editorQuery("choose",selected);
    else { editorChosenWord_.clear();editor_.notice="未找到候选，后续按键已保留；可按回车保留拼音"; }
  }
  editorRefresh();
}
void App::editorFinish(bool confirmed) {
  const bool transfer=confirmed&&editorFocusSwitch_&&editorInPlace();
  if(confirmed&&!editor_.secret&&db_.settings().value("pinyin_learning",true))pinyin_.commit(editorLearning_,editor_.text);
  editorLearning_=editorExact_=Json::array();editorPinyinPending_=Json();
  if(!transfer)editorLocalActions_.clear();
  editorLayoutHold_=false;editorLayoutMenu_=editorPunctuation_=false;
  if(transfer) {
    editorMagnify(false);editorCancelKeys();editorPointer_=editorTextTouch_=editorCursorDrag_=false;
    editorHandle_=editorHeld_=-1;editorRepeat_=0;editorInFlight_=editorDue_=0;
    editorChoosing_=editorSelectPending_=false;editorDeferred_.clear();++editorRevision_;
  } else editorInterrupt();
  editorDetachTarget();editor_.active=false; keyboard_=false;editorReaderTouch_=false;
  if(!transfer) {
    editorViewport();
    if (editorRoot_) lv_obj_delete(editorRoot_);
    editorRoot_=editorText_=nullptr; editorHandles_[0]=editorHandles_[1]=nullptr;editorKeys_.clear();
    editorKeyboardId_.clear();
  }
  auto done=std::move(editCompletion_); auto cancel=std::move(editCancellation_);
  editCompletion_={}; editCancellation_={};
  auto target=editorTarget_;auto beforeModal=modal_;
  const auto previous=inputTargets_.find(target);
  const auto serial=previous==inputTargets_.end()?uint64_t(0):previous->second.serial;
  editorTransfer_=transfer;
  if (confirmed && done) done(editor_.text); else if (!confirmed && cancel) cancel();
  const auto current=inputTargets_.find(target);
  if(confirmed&&target&&target==editorTarget_&&modal_==beforeModal&&
     current!=inputTargets_.end()&&current->second.serial==serial) {
    const auto style=current->second.style;auto value=lv_obj_get_child(target,style==2?1:0);
    if(value&&lv_obj_check_type(value,&lv_label_class))
      lv_label_set_text(value,editor_.secret?(editor_.text.empty()?"点击填写":"••••••••"):editor_.text.c_str());
  }
  editorTarget_=nullptr;editorTargetKey_.clear();editorTargetView_.clear();
  if(transfer){editorTransfer_=true;return;}
  const auto preferred=editor_.preferredChinese;editor_=EditorState{};editor_.preferredChinese=preferred; editorSelectionOnly_=false; editorDeferred_.clear(); syncScanner();
}
void App::editorOfficial() {
  if(editorText_)editorTextScroll_=lv_obj_get_scroll_y(editorText_);
  std::string text=editor_.text; text.insert(editByte(text,editor_.cursor),editor_.preedit);
  if (editLength(text)>editor_.limit) { editor_.notice="已达到长度上限"; editorBuild(); return; }
  const auto officialField=editorTextArea();
  editorInterrupt();officialDocked_=editorLayout().docked&&display_.supportsTopRegion();officialHeight_=0;
  if(officialDocked_)officialBackup_=editor_;
  keyboard_=true;keyboardId_=uid("official");
  if(officialDocked_&&editorRoot_)lv_obj_set_hidden(editorRoot_,true);
  display_.visible(false);
  const bool sent = bridge_.send("keyboard.open",{{"requestId",keyboardId_},{"text",text},{"placeholder",editor_.placeholder},
    {"maxlength",editor_.limit},{"multiline",editor_.multiline},{"sensitive",editor_.secret},{"inline",officialDocked_},
    {"cursorIndex",editUtf16(text,editor_.cursor+editor_.preedit.size())},
    {"inputType",editor_.chinese?"ZhCNPreferred":"EnUSOnly"},
    {"field",{{"x",officialField.x},{"y",officialField.y},{"width",officialField.width},{"height",officialField.height}}}});
  if (!sent) {
    keyboard_ = false;officialDocked_=false;officialHeight_=0;officialBackup_=EditorState{};
    keyboardId_.clear();display_.topHeight(0);
    display_.visible(visible_);
    editorPolicy(true);
    editorChanged();
    editor_.notice = "未能打开官方键盘，输入已保留，可继续编辑或重试";
    editorFollowCursor_ = false;
    editorBuild();
    editorOcrStart();
  }
}
void App::editorAction(const std::string &a) {
  if ((editorChoosing_||editorSelectPending_)&&a!="cancel") {
    if(editorDeferred_.size()<4096)editorDeferred_.push_back(a);
    else editor_.notice="待处理按键过多，请暂停输入后重试";
    editorRefreshPending_=true;return;
  }
  if(editorSelectionOnly_) {
    if(a=="cancel" || a=="done") {editorFinish(false);return;}
    if(a=="all") {editor_.select(0,editLength(editor_.text));editorBuild();}
    else if(a=="copy") {
      if(!editor_.selected())return;
      if(editorClipboard_.copy(editor_.selection())) {editor_.notice="已复制选中文字";editorMenu_=editorUndoMenu_=false;editorBuild();}
      else editor_.notice="选区过长，无法复制";
    }
    return;
  }
  if(a=="layout-close") {editorLayoutMenu_=editorPunctuation_=false;editorBuild();return;}
  if(a=="layout-menu") {editorLayoutMenu_=true;editorBuild();return;}
  if(a.find("layout:")==0) {editorUseLayout(std::stoi(a.substr(7)));return;}
  if(a=="language") {editorUseLayout(0,true);return;}
  if(a.find("correction-info:")==0) {
    auto split=a.find(':',16);
    if(split!=std::string::npos&&std::stoull(a.substr(16,split-16))==editorRevision_&&!editorInFlight_) {
      auto i=std::stoul(a.substr(split+1));if(i<editorCandidateDetails_.size()&&editorCandidateDetails_[i].value("corrected",false)) {
        editor_.notice="纠错："+editorCandidateDetails_[i].value("pinyin",std::string());editorBuild();
      }
    }
    return;
  }
  if(a=="pinyin-retry") {editorInFlight_=0;editorChoosing_=false;editorChanged();editorBuild();return;}
  if(a=="punctuation") {editorPunctuation_=true;editorBuild();return;}
  if(a.find("punct:")==0) {if(editor_.commit(a.substr(6))) {editor_.contentChanged();editorChanged();}editorPunctuation_=false;editorBuild();return;}
  if(a=="exact-all") {editor_.remember();editor_.fixed.clear();editor_.contentChanged();editorChanged();editorBuild();return;}
  if(a.find("exact:")==0) {
    auto split=a.find(':',6);
    if(split!=std::string::npos&&std::stoull(a.substr(6,split-6))==editorRevision_&&!editorInFlight_) {
      const auto i=std::stoul(a.substr(split+1));if(i<editorExact_.size()) {
        editor_.remember();editor_.fixed+=(editor_.fixed.empty()?"":"'")+editorExact_[i].get<std::string>();
        editor_.contentChanged();editorChanged();editorBuild();
      }
    }
    return;
  }
  const auto beforeText=editor_.text,beforePreedit=editor_.preedit;
  if(a=="ocr-mode") {
    if(editorOcrActive_){editorOcrMulti_=!editorOcrMulti_;editorOcrInserted_=0;}
    // Replace a continuous registration without an unregister/re-register gap.
    if(layout().portrait()) {editorOcrActive_=false;editorOcrCapture_=0;editorOcrPending_.clear();}
    else editorOcrStop();
    editorOcrStart();editorBuild();return;
  }
  if(a=="clipboard") {editorClipboardOpen_=!editorClipboardOpen_;editorClipboardConfirm_=false;editorMenu_=editorUndoMenu_=false;editorBuild();return;}
  if(a=="clip-close") {editorClipboardOpen_=false;editorBuild();return;}
  if(a=="clip-clear") {if(editorClipboardConfirm_) {editorClipboard_.clear();editorClipboardConfirm_=false;}else editorClipboardConfirm_=true;editorBuild();return;}
  if(a.find("clip-delete:")==0) {auto i=std::stoul(a.substr(12));if(i<editorClipboard_.records.size()) {auto old=editorClipboard_.records[i];editorClipboard_.records.erase(editorClipboard_.records.begin()+i);if(editorClipboard_.current==old)editorClipboard_.current=editorClipboard_.records.empty()?"":editorClipboard_.records.front();}editorBuild();return;}
  if(a=="chat-send" && editorInlineChat_) {
    if(generating_||contextBusy_)return;
    if(!editor_.preedit.empty()) {editorAction("done");return;}
    if(!hasChatText(editor_.text)&&!editorSendAssets_)return;
    const auto session=session_;editorFinish(true);
    events_.push("composer_send",{{"session",session}});return;
  }
  if(a=="dismiss") {editorAccept();return;}
  if(a=="cancel") { editorFinish(false); return; }
  if (a=="official") {
    if(editorLayoutMenu_) {editorLayoutMenu_=false;editorOfficial();}
    return;
  }
  if(a=="copy"||a=="cut") {
    if(editor_.selected() && (!editor_.secret||editor_.reveal)) {
      if(editorClipboard_.copy(editor_.selection(),editor_.secret)) {if(a=="cut"){editor_.insert("");editorChanged();}}
      else editor_.notice="选区过长，无法复制";
    }
    if(editor_.text!=beforeText||editor_.preedit!=beforePreedit){editor_.contentChanged();editorFollowCursor_=true;}
    editorMenu_=editorUndoMenu_=false;editorBuild();return;
  }
  if(a=="paste"||a.find("clip-paste:")==0) {
    std::string text=editorClipboard_.current;
    if(a!="paste") {auto i=std::stoul(a.substr(11));if(i>=editorClipboard_.records.size())return;text=editorClipboard_.records[i];}
    if(!editor_.multiline)std::replace(text.begin(),text.end(),'\n',' ');
    // Validate both pieces before committing pending pinyin or replacing selection.
    if(editor_.retained()+editLength(editor_.preedit)+editLength(text)>editor_.limit) editor_.notice="粘贴内容超过长度上限";
    else if(editor_.commit(text)){editor_.contentChanged();editorChanged();editorClipboardOpen_=false;}
    editorMenu_=editorUndoMenu_=false;editorBuild();return;
  }
  if (a=="done" || a=="enter" || a=="space") {
    if (!editor_.preedit.empty()) {
      if (a=="enter") {
        if (editor_.raw()) {editor_.contentChanged();editorChanged();}
        editorBuild();
        return;
      }
      if (!editorCandidates_.empty() && !editorInFlight_) editorQuery("choose",0);
      else if(editorInFlight_||editorDue_) {
        editorSelectPending_=true;editorChosenWord_.clear();
        if(!editorInFlight_)editorQuery();
        editorRefreshPending_=true;
      } else { editor_.notice="无匹配候选，可按回车保留拼音"; editorBuild(); }
      return;
    }
    if (a=="done" || (a=="enter"&&!editor_.multiline)) { editorFinish(true); return; }
    editor_.insert(a=="space"?" ":"\n");
  } else if (a.find("choose:")==0) {
    auto split=a.find(':',7);
    if (split!=std::string::npos && std::stoull(a.substr(7,split-7))==editorRevision_ && !editorInFlight_ && !editorCandidates_.empty()) editorQuery("choose",std::stoi(a.substr(split+1)));
    return;
  } else if (a=="mode" || a=="symbols") {
    if (editor_.raw()) { if(a=="symbols") editor_.mode=editor_.mode==1?2:1;else editor_.mode=editor_.mode?0:1; editorChanged(); }
  } else if(a.find("t9:")==0&&editor_.layout==2&&editor_.mode==0) {
    if(editor_.retained()+editor_.preedit.size()<editor_.limit) {editor_.remember();editor_.preedit+=a.substr(3);editorRememberLayout();editorChanged();}
    else editor_.notice="已达到长度上限";
  } else if (a=="shift") {
    if(editor_.mode) return;
    auto now=ticks(); editor_.shift=editor_.shift==2?0:editor_.shift==1?(now-editorShiftTime_<400?2:0):1; editorShiftTime_=now;
  } else if (a=="delete") { editor_.erase(); editorChanged(); }
  else if (a=="expand") editorExpanded_=editorCanExpand()&&!editorExpanded_;
  else if (a=="reveal") { editor_.reveal=!editor_.reveal; editorMenu_=false; }
  else if (a=="undo") { editor_.revert(); editorChanged(); editorMenu_=editorUndoMenu_=false; }
  else if (a=="all") { if(editor_.raw()) { editor_.all=true; editorMenu_=false; editorChanged(); } }
  else if (a.size()==1) {
    std::string letter=keyboardPunctuation(a,editor_.chinese&&!editor_.secret);
    if(editor_.mode==0 && std::isalpha(static_cast<unsigned char>(letter[0])))
      letter[0]=editor_.shift?std::toupper(letter[0]):std::tolower(letter[0]);
    if(editor_.chinese && editor_.mode==0 && letter[0]>='a' && letter[0]<='z' && !editor_.secret) {
      if (editor_.retained()+editor_.preedit.size()<editor_.limit) { editor_.remember(); editor_.preedit+=letter; editorRememberLayout(); editorChanged(); }
      else editor_.notice="已达到长度上限";
    } else if (editor_.commit(letter)) { editorChanged(); }
    if(editor_.shift==1 && std::isalpha(static_cast<unsigned char>(a[0]))) editor_.shift=0;
  }
  if(editor_.text!=beforeText||editor_.preedit!=beforePreedit){editor_.contentChanged();editorFollowCursor_=true;}
  if(a!="expand"&&a!="reveal")editorOcrShown_=false;
  editorRefreshPending_=true;
}
lv_point_t App::editorCaretPosition(size_t index) const {
  lv_point_t point{};
  if(!editorText_)return point;
  auto text=lv_textarea_get_label(editorText_);lv_label_get_letter_pos(text,index,&point);
  if(index!=editorUpstream_||!index)return point;
  const size_t previous=editBoundary(editor_.text,index-1);
  const auto cluster=editor_.text.substr(editByte(editor_.text,previous),editByte(editor_.text,index)-editByte(editor_.text,previous));
  if(cluster.empty()||cluster[0]=='\n'||cluster[0]=='\r')return point;
  lv_label_get_letter_pos(text,previous,&point);
  point.x+=keyboardClusterWidth(text,previous,index);
  return point;
}
lv_point_t App::editorPoint(size_t index) const {
  auto point=editorCaretPosition(index);
  if(!editorText_)return point;
  auto text=lv_textarea_get_label(editorText_);lv_area_t area;lv_obj_get_coords(text,&area);
  point.x+=area.x1;
  point.y+=area.y1+lv_obj_get_style_text_font(text,LV_PART_MAIN)->line_height+8;
  if(index==editorUpstream_) {
    const auto bounds=editorTextArea();
    if(bounds.width)point.x=std::max(bounds.x,std::min(bounds.x+bounds.width-1,int(point.x)));
  }
  return point;
}
size_t App::editorPositionAt(int x,int y) {
  editorUpstream_=std::string::npos;editorAffinityTime_=ticks();editorAffinityPhase_=-1;
  if(!editorText_)return editor_.cursor;
  auto text=lv_textarea_get_label(editorText_);lv_area_t a;lv_obj_get_coords(text,&a);
  lv_area_t bounds;lv_obj_get_content_coords(editorText_,&bounds);
  // A captured finger can leave either side of the field. Resolve the nearest
  // text insertion boundary, independently of the floating lens placement.
  // Keep vertical hit testing on real text rather than inventing empty lines.
  y=std::max(int(bounds.y1),std::min(int(bounds.y2),y));
  lv_point_t end{};lv_label_get_letter_pos(text,editLength(editor_.text),&end);
  lv_point_t point{x-a.x1,std::max(0,std::min(y-a.y1,int(end.y)+lv_obj_get_style_text_font(text,LV_PART_MAIN)->line_height-1))};
  const size_t length=editLength(editor_.text);
  const size_t start=editBoundary(editor_.text,std::min(size_t(lv_label_get_letter_on(text,&point,true)),length));
  if(start==length) {if(length)editorUpstream_=length;return length;}
  const size_t next=editBoundary(editor_.text,start+1,true);
  const auto cluster=editor_.text.substr(editByte(editor_.text,start),editByte(editor_.text,next)-editByte(editor_.text,start));
  if(cluster[0]=='\n'||cluster[0]=='\r')return start;
  lv_point_t left{},right{};lv_label_get_letter_pos(text,start,&left);lv_label_get_letter_pos(text,next,&right);
  const bool wrapped=left.y!=right.y;
  if(wrapped) {
    // A soft wrap moves the next caret onto the next line. The current glyph
    // still has a right half, including when its advance reaches the field edge.
    right.x=left.x+keyboardClusterWidth(text,start,next);
  }
  const bool after=std::abs(point.x-right.x)<=std::abs(point.x-left.x);
  if(after&&(wrapped||next==length))editorUpstream_=next;
  return after?next:start;
}
void App::editorBindCaret() {
  lv_obj_add_event_cb(editorText_,[](lv_event_t *event) {
    static_cast<App *>(lv_event_get_user_data(event))->editorDrawCaret(event);
  },LV_EVENT_DRAW_POST,this);
}
void App::editorSyncCaret() {
  if(!editorText_)return;
  const auto selector=LV_PART_CURSOR|LV_STATE_FOCUSED;
  if(editorUpstream_==editor_.cursor) {
    lv_obj_set_style_border_opa(editorText_,LV_OPA_TRANSP,selector);
    lv_obj_set_style_bg_opa(editorText_,LV_OPA_TRANSP,selector);
  } else {
    lv_obj_remove_local_style_prop(editorText_,LV_STYLE_BORDER_OPA,selector);
    lv_obj_remove_local_style_prop(editorText_,LV_STYLE_BG_OPA,selector);
  }
  lv_obj_invalidate(editorText_);
}
void App::editorDrawCaret(lv_event_t *event) {
  if(!editor_.active||lv_event_get_target_obj(event)!=editorText_||editorUpstream_!=editor_.cursor||keyboard_)return;
  if(!editorPointer_&&((ticks()-editorAffinityTime_)/500)%2)return;
  const auto clip=editorTextArea();if(clip.width<2||clip.height<1)return;
  const auto point=editorPoint(editor_.cursor);
  const int height=lv_obj_get_style_text_font(lv_textarea_get_label(editorText_),LV_PART_MAIN)->line_height;
  const int x=std::max(clip.x,std::min(clip.x+clip.width-2,int(point.x)));
  lv_area_t area{x,std::max(clip.y,int(point.y)-height-8),x+1,std::min(clip.y+clip.height-1,int(point.y)-9)};
  if(area.y1>area.y2)return;
  lv_draw_rect_dsc_t draw;lv_draw_rect_dsc_init(&draw);draw.bg_color=lv_color_hex(theme::caret);
  lv_draw_rect(lv_event_get_layer(event),&draw,&area);
}
void App::editorScroll(int y) {
  if(!editorText_)return;
  lv_obj_update_layout(editorText_);
  auto text=lv_textarea_get_label(editorText_);
  const int limit=std::max(0,int(lv_obj_get_height(text)-lv_obj_get_content_height(editorText_)));
  lv_obj_scroll_to_y(editorText_,std::max(0,std::min(limit,y)),LV_ANIM_OFF);
}
void App::editorMagnify(bool show) {
  const auto layout=editorLayout();
  if(!show || !editorText_ || !editorRoot_) {
    if(editorMagnifier_)lv_obj_delete(editorMagnifier_);
    editorMagnifier_=editorMagnifierImage_=editorMagnifierCaret_=nullptr;
    editorMagnifierBuffer_=nullptr;
    return;
  }
  auto point=editorPoint(editor_.cursor);point.y-=font(20)->line_height+8-font(20)->line_height/2;
  const auto area=editorTextArea();
  const auto popup=layout.magnifierPopup(point.x,point.y,area.x,area.x+area.width);
  if(!editorMagnifier_) {
    auto buffer=lv_snapshot_create_draw_buf(editorText_,LV_COLOR_FORMAT_ARGB8888);
    if(!buffer)return;
    editorMagnifierBuffer_=buffer;
    editorMagnifier_=box(editorRoot_,0,0,popup.width,popup.height,theme::surface);
    lv_obj_set_scrollable(editorMagnifier_,false);lv_obj_set_clickable(editorMagnifier_,false);
    lv_obj_set_style_radius(editorMagnifier_,12,0);lv_obj_set_style_clip_corner(editorMagnifier_,true,0);
    lv_obj_set_style_border_width(editorMagnifier_,1,0);lv_obj_set_style_border_color(editorMagnifier_,lv_color_hex(theme::border),0);
    editorMagnifierImage_=lv_image_create(editorMagnifier_);
    // Detach the image source before freeing its bounded snapshot on deletion.
    lv_obj_add_event_cb(editorMagnifierImage_,[](lv_event_t *e) {
      lv_image_set_src(lv_event_get_target_obj(e),nullptr);
      lv_draw_buf_destroy(static_cast<lv_draw_buf_t *>(lv_event_get_user_data(e)));
    },LV_EVENT_DELETE,buffer);
    lv_image_set_pivot(editorMagnifierImage_,0,0);lv_image_set_scale(editorMagnifierImage_,448);
    editorMagnifierCaret_=box(editorMagnifier_,89,0,2,font(20)->line_height*7/4,theme::caret);
    lv_obj_set_scrollable(editorMagnifierCaret_,false);lv_obj_set_clickable(editorMagnifierCaret_,false);
  }
  if(lv_snapshot_take_to_draw_buf(editorText_,LV_COLOR_FORMAT_ARGB8888,editorMagnifierBuffer_)!=LV_RESULT_OK) {
    editorMagnify(false);return;
  }
  lv_image_set_src(editorMagnifierImage_,editorMagnifierBuffer_);
  lv_obj_set_size(editorMagnifier_,popup.width,popup.height);
  lv_area_t a;lv_obj_get_coords(editorText_,&a);
  const int ext=(int(editorMagnifierBuffer_->header.w)-lv_obj_get_width(editorText_))/2;
  const int lensX=popup.width/2,lensY=popup.height/2;
  lv_obj_set_pos(editorMagnifierImage_,lensX-(point.x-a.x1+ext)*7/4,lensY-(point.y-a.y1+ext)*7/4);
  lv_obj_set_width(editorMagnifierCaret_,std::min(2,popup.width));
  lv_obj_set_height(editorMagnifierCaret_,std::max(1,std::min(font(20)->line_height*7/4,popup.height-4)));
  lv_obj_set_pos(editorMagnifierCaret_,std::max(0,lensX-1),(popup.height-lv_obj_get_height(editorMagnifierCaret_))/2);
  lv_obj_set_pos(editorMagnifier_,popup.x,popup.y);
  lv_obj_move_foreground(editorMagnifier_);
}
void App::editorSyncText(bool text) {
  if(!editorText_)return;
  if(editorInlineChat_&&editorInPlace())editorViewport();
  const int scroll=lv_obj_get_scroll_y(editorText_);
  if(text)lv_textarea_set_text(editorText_,editor_.text.c_str());
  if(text||lv_textarea_get_cursor_pos(editorText_)!=editor_.cursor)lv_textarea_set_cursor_pos(editorText_,editor_.cursor);
  auto label=lv_textarea_get_label(editorText_);auto r=editor_.range();
  lv_label_set_text_selection_start(label,editor_.selected()?r.first:LV_LABEL_TEXT_SELECTION_OFF);
  lv_label_set_text_selection_end(label,editor_.selected()?r.second:LV_LABEL_TEXT_SELECTION_OFF);
  lv_obj_update_layout(editorText_);
  editorSyncCaret();
  if(editorCursorDrag_||editorHandle_>=0)editorScroll(scroll);
  for(int i=0;i<2;++i)if(editorHandles_[i]) {
    auto point=editorPoint(i?r.second:r.first);
    const int lineBottom=point.y-8;
    const auto area=editorTextArea();
    const bool lineVisible=lineBottom>area.y&&lineBottom-font(20)->line_height<area.y+area.height;
    lv_obj_set_hidden(editorHandles_[i],!editor_.selected()||editorClipboardOpen_||editorExpanded_||editorMenu_||editorUndoMenu_||!lineVisible);
    lv_obj_set_pos(editorHandles_[i],int(point.x)-22,int(point.y)-22);
    lv_obj_move_foreground(editorHandles_[i]);
  }
  editorMagnify(editorPointer_&&(editorCursorDrag_||editorHandle_>=0));
}
void App::editorPolicy(bool active) {
  editorTopBlocked_=false;
  bridge_.send("editor.state",{{"session",editor_.id},{"sequence",++editorPolicySequence_},{"active",active}});
}
void App::editorOcrStop() {
  if(!editorOcrId_.empty()){editorOcrStopId_=editorOcrId_;editorOcrStopRetry_=0;editorOcrFlushStop();}
  editorOcrPreparing_=false;editorOcrPrepareSent_=0;
  editorOcrActive_=editorOcrBusy_=false;editorOcrId_.clear();editorOcrCapture_=0;editorOcrPending_.clear();
}
void App::editorOcrFlushStop() {
  if(editorOcrStopId_.empty() || ticks()<editorOcrStopRetry_)return;
  bridge_.send("ocr.cancel",{{"requestId",editorOcrStopId_}});
  editorOcrStopRetry_=ticks()+250; // Idempotent cleanup; remain blocked until acknowledged.
}
void App::editorOcrStart() {
  if(editorSelectionOnly_||!editor_.active||keyboard_||!visible_||!inputFocused_||editorOcrActive_||!editorOcrStopId_.empty())return;
  const auto session=editor_.id;
  if(scanner_.nativeOcrKeys()) {
    editorOcrActive_=editorOcrPreparing_=true;editorOcrPrepareSent_=ticks();
    editorOcrId_=uid("editor-ocr");editorOcrCapture_=0;syncScanner();
    if(!bridge_.send("ocr.prepare",{{"nativePen",true},{"requestId",editorOcrId_},{"session",session},{"mode",editorOcrMulti_?"multi":"single"},{"continuous",true}}))
      editorOcrPrepared({{"requestId",editorOcrId_},{"session",session},{"ok",false}});
    return;
  }
  whenScannerReleased([this,session] {
    if(editorSelectionOnly_||!editor_.active||editor_.id!=session||keyboard_||!visible_||!inputFocused_||editorOcrActive_||!editorOcrStopId_.empty())return;
    editorOcrActive_=true;editorOcrId_=uid("editor-ocr");editorOcrCapture_=0;
    if(!bridge_.send("ocr.start",{{"requestId",editorOcrId_},{"session",session},{"mode",editorOcrMulti_?"multi":"single"},{"continuous",true}}))
      editorOcrResult({{"requestId",editorOcrId_},{"session",session},{"mode",editorOcrMulti_?"multi":"single"},
        {"capture",1},{"confirmed",false},{"terminal",true},{"error","OCR请求未送达，请重试或手动输入"}});
  });
}
void App::editorOcrPrepared(const Json &m) {
  if(!editor_.active||!editorOcrActive_||!editorOcrPreparing_||keyboard_||!visible_||
     m.value("session",std::string())!=editor_.id||m.value("requestId",std::string())!=editorOcrId_)return;
  editorOcrPrepareSent_=0;
  if(!m.value("ok",false)) {
    editorOcrActive_=false;editor_.notice="OCR未就绪，笔头已暂停；可重试或手动输入";
    editorBuild();return;
  }
  editorOcrPreparing_=false;
  if(editor_.notice.find("OCR")==0)editor_.notice.clear();
  const auto session=editor_.id,request=editorOcrId_;
  whenScannerReleased([this,session,request] {
    if(!editor_.active||!visible_||keyboard_||editor_.id!=session||editorOcrId_!=request)return;
    if(editorRoot_)editorRefresh();else editorBuild();
    if(!bridge_.send("ocr.start",{{"nativePen",true},{"requestId",request},{"session",session},{"mode",editorOcrMulti_?"multi":"single"},{"continuous",true}})) {
      editorOcrActive_=false;editorOcrPreparing_=true;syncScanner();
      editor_.notice="OCR启动未完成，笔头已暂停；请重试";editorBuild();
    }
  });
}
void App::editorOcrResult(const Json &m) {
  if(!editor_.active||keyboard_||!visible_||!editorOcrActive_||m.value("session",std::string())!=editor_.id||
      m.value("requestId",std::string())!=editorOcrId_||m.value("mode",std::string())!=(editorOcrMulti_?"multi":"single"))return;
  auto capture=m.value("capture",uint64_t(0));
  const bool terminal=m.value("terminal",false);
  // A route failure may refer to the last delivered stroke. It must still
  // withdraw OCR readiness; treating it as duplicate leaves a dead session armed.
  if(capture<=editorOcrCapture_&&!terminal)return;
  if(editorPointer_) {
    for(const auto &pending:editorOcrPending_)
      if((pending.value("capture",uint64_t(0))==capture&&pending.value("terminal",false)==terminal)||pending.value("overflow",false))return;
    if(editorOcrPending_.size()<63)editorOcrPending_.push_back(m);
    else {
      auto failure=m;failure.erase("text");failure["confirmed"]=false;failure["overflow"]=true;
      failure["error"]="扫描暂存已满，本次未插入，请松手后重新划扫";editorOcrPending_.push_back(std::move(failure));
    }
    return;
  }
  editorOcrCapture_=std::max(editorOcrCapture_,capture);editorOcrBusy_=false;editorOcrShown_=true;
  if(!m.value("confirmed",false)) {
    editor_.notice=m.value("error",std::string("未识别到文字，请重试"));
    if(m.value("terminal",false)){editorOcrActive_=false;if(layout().portrait()){editorOcrPreparing_=true;syncScanner();}}
    editorBuild();return;
  }
  std::string text=m.value("text",std::string());
  text.erase(std::remove(text.begin(),text.end(),'\r'),text.end());
  if(!editor_.multiline||!editorOcrMulti_)std::replace(text.begin(),text.end(),'\n',' ');
  if(text.empty())return;
  if(editorOcrMulti_&&editorOcrInserted_>0&&!editor_.text.empty()&&!editor_.selected())text=(editor_.multiline?"\n":" ")+text;
  if(editor_.retained()+editLength(editor_.preedit)+editLength(text)>editor_.limit)editor_.notice="扫描内容超过长度上限，请缩短内容后重试";
  else if(editor_.commit(text)){++editorOcrInserted_;editor_.contentChanged();editorChanged();}
  editorBuild();
}
// Key-only two-finger rollover. Text, candidates and other controls retain the
// ordinary multitouch cancellation policy. Contact identity, not points[0], owns
// each key; release order cannot reorder the sequence of intended key presses.
void App::editorCancelKeys() {
  for(const auto &c:editorContacts_)
    lv_obj_set_style_bg_color(c.key.object,lv_color_hex(c.key.value=="shift"&&editor_.shift?theme::text:theme::surface),0);
  editorContacts_.clear();
}
bool App::editorKeyTouch(const Json &m) {
  if(!m.contains("points")||!m["points"].is_array())return false;
  const auto &points=m["points"];
  if(editorContacts_.empty()&&(editorPointer_||points.empty()||editorExpanded_||editorClipboardOpen_||
      editorMenu_||editorUndoMenu_||editorLayoutMenu_||editorPunctuation_||editorReaderTouch_))return false;
  if(m.value("cancelled",false)||points.size()>2) {
    if(editorContacts_.empty())return false;
    editorCancelKeys();editorTouch(m.value("x",0),m.value("y",0),false,true);
    nonImageMulti_=m.value("pressed",false);return true;
  }
  // Validate the complete frame before accepting any new press. An additional
  // touch on the text/candidates must never become a letter or activate a button.
  std::vector<std::string> ids;
  for(const auto &p:points) {
    if(!p.is_object()||!p.contains("id")||!p.contains("x")||!p["x"].is_number_integer()||
        !p.contains("y")||!p["y"].is_number_integer())return false;
    const auto id=p["id"].dump();
    if(std::find(ids.begin(),ids.end(),id)!=ids.end())return false;
    ids.push_back(id);
    if(editorContacts_.size()>=256) {
      editor_.notice="同时按住的按键过多，请松手后继续";
      editorCancelKeys();return false;
    }
    const auto held=std::find_if(editorContacts_.begin(),editorContacts_.end(),[&](const EditorContact &c){return c.id==id&&!c.released;});
    if(held!=editorContacts_.end())continue;
    const int x=editorLayout().fromNativeX(p["x"]),y=editorLayout().fromNativeY(p["y"]);
    if(std::none_of(editorKeys_.begin(),editorKeys_.end(),[&](const EditorKey &k){return k.hit.contains(x,y);})) {
      editorCancelKeys();return false;
    }
  }
  for(auto &c:editorContacts_) {
    const auto point=std::find_if(points.begin(),points.end(),[&](const Json &p){return p["id"].dump()==c.id;});
    if(point==points.end())c.released=true;
    else {
      const int x=editorLayout().fromNativeX((*point)["x"]),y=editorLayout().fromNativeY((*point)["y"]);const auto &b=c.key.hit;
      if(x<b.x-12||x>=b.x+b.width+12||y<b.y-12||y>=b.y+b.height+12)c.slid=true;
    }
    if(c.slid||c.released)
      lv_obj_set_style_bg_color(c.key.object,lv_color_hex(c.key.value=="shift"&&editor_.shift?theme::text:theme::surface),0);
  }
  for(const auto &p:points) {
    const auto id=p["id"].dump();
    if(std::any_of(editorContacts_.begin(),editorContacts_.end(),[&](const EditorContact &c){return c.id==id;}))continue;
    const int x=editorLayout().fromNativeX(p["x"]),y=editorLayout().fromNativeY(p["y"]);
    const auto key=std::find_if(editorKeys_.begin(),editorKeys_.end(),[&](const EditorKey &k){return k.hit.contains(x,y);});
    editorContacts_.push_back({id,*key,false,false,0,uint64_t(ticks())+(key->value=="language"?layoutHoldMs:450)});
    lv_obj_set_style_bg_color(key->object,lv_color_hex(theme::border),0);
  }
  editorPointer_=!points.empty();
  editorHeld_=-1;editorRepeat_=0;
  // Mark the keyboard as held for partial repaint, without handing these
  // contacts to LVGL's single-pointer reader or its text gesture recognizer.
  if(editorPointer_) {
    const auto active=std::find_if(editorContacts_.begin(),editorContacts_.end(),[](const EditorContact &c){return !c.released;});
    editorPress_=active->key;editorHeld_=0;
  }
  while(!editorContacts_.empty()&&editorContacts_.front().released) {
    auto c=std::move(editorContacts_.front());editorContacts_.erase(editorContacts_.begin());
    if(!c.slid&&!c.repeats)events_.push("editor_action",{{"id",editor_.id},{"action",c.key.value}});
  }
  return true;
}
void App::editorTouch(int x,int y,bool pressed,bool cancelled) {
  const auto layout=editorLayout();
  const int nativeX=x,nativeY=y;
  x=layout.fromNativeX(x);y=layout.fromNativeY(y);
  editorTouchX_=x;editorTouchY_=y;
  if(cancelled) {editorCancelKeys();editorLastTap_=0;editorPointer_=editorTextTouch_=editorCursorDrag_=false;editorHandle_=-1;editorHeld_=-1;editorRepeat_=0;input_.cancel();editorBuild();return;}
  if(pressed&&!editorPointer_) {
    editorPointer_=true;editorSlid_=editorTextMoved_=editorCursorDrag_=false;
    editorStartX_=x;editorStartY_=y;editorHeld_=-1;editorRepeats_=0;editorPressTime_=ticks();editorHandle_=-1;
    const auto undo=layout.undoPopup(editorMenuAnchor_.x,editorMenuAnchor_.y,!editorClipboard_.current.empty());
    const auto selection=layout.selectionPopup(editorMenuAnchor_.x,editorMenuAnchor_.y,editorSelectionOnly_);
    const bool popupHit=(editorMenu_&&selection.contains(x,y))||(editorUndoMenu_&&undo.contains(x,y));
    bool overlay=editorClipboardOpen_||editorExpanded_||editorLayoutMenu_||editorPunctuation_||popupHit;
    if(!overlay && editor_.selected()) {
      auto r=editor_.range();int distance=100000;
      for(int i=0;i<2;++i) {
        if(!editorHandles_[i]||lv_obj_is_hidden(editorHandles_[i]))continue;
        lv_area_t a;lv_obj_get_coords(editorHandles_[i],&a);int dx=x-(a.x1+a.x2)/2,dy=y-(a.y1+a.y2)/2;
        if(y>=a.y1+12&&std::abs(dx)<=22&&std::abs(dy)<=22&&dx*dx+dy*dy<distance) {distance=dx*dx+dy*dy;editorHandle_=i;}
      }
      if(editorHandle_>=0)editorDragAnchor_=editorHandle_?r.first:r.second;
    }
    editorTextTouch_=editorHandle_>=0||(!overlay&&editorTextArea().contains(x,y));
    if(editorTextTouch_) {
      if(!editor_.preedit.empty()&&editor_.raw()) {editorChanged();editorSyncText(true);}
      editorTextScroll_=lv_obj_get_scroll_y(editorText_);input_.cancel();return;
    }
    if(!popupHit&&!editorLayoutMenu_&&!editorPunctuation_&&!(editorClipboardOpen_&&layout.portrait))for(size_t i=0;i<editorKeys_.size();++i) {
      const auto &k=editorKeys_[i];
      if(k.hit.contains(x,y)) {
        editorHeld_=int(i);editorPress_=k;lv_obj_set_style_bg_color(k.object,lv_color_hex(theme::border),0);editorRepeat_=ticks()+(k.value=="language"?layoutHoldMs:450);break;
      }
    }
  }
  if(editorTextTouch_) {
    const bool moved=std::abs(x-editorStartX_)>12||std::abs(y-editorStartY_)>12;
    const bool held=ticks()-editorPressTime_>=450;
    if(pressed&&moved) {
      if(editorHandle_>=0) {editor_.select(editorDragAnchor_,editorPositionAt(x,y-16));editorSyncText();}
      else if(editorCursorDrag_||(!editorTextMoved_&&held)) {editorCursorDrag_=true;editor_.position(editorPositionAt(x,y));editorSyncText();}
      else editorScroll(editorTextScroll_+editorStartY_-y);
      editorTextMoved_=true;
    }
    if(!pressed) {
      editorPointer_=editorTextTouch_=false;
      if(editorTextMoved_)editorLastTap_=0;
      if(editorHandle_>=0||editorCursorDrag_) {editorHandle_=-1;editorCursorDrag_=false;editorLastTap_=0;editorBuild();return;}
      if(!editorTextMoved_) {
        auto index=editorPositionAt(x,y);
        if(held) {
          editorMenuAnchor_={x,y};editorLastTap_=0;
          auto r=editor_.range();editorMenu_=editor_.selected()&&index>=r.first&&index<=r.second;editorUndoMenu_=!editorMenu_;if(editorUndoMenu_)editor_.position(index);if(editorSelectionOnly_){if(!editor_.selected())editor_.word(index);editorMenu_=true;editorUndoMenu_=false;}
        } else if(ticks()-editorLastTap_<350&&std::abs(x-editorLastTapX_)<=12&&std::abs(y-editorLastTapY_)<=12) {
          editor_.word(index);editorMenu_=editorUndoMenu_=false;editorLastTap_=0;
        } else {
          editor_.position(index);editorMenu_=editorUndoMenu_=false;editorLastTap_=ticks();editorLastTapX_=x;editorLastTapY_=y;
        }
      }
      editorTextScroll_=lv_obj_get_scroll_y(editorText_);editorBuild();
    }
    return;
  }
  if(editorHeld_>=0) {
    const auto k=editorPress_;
    if(x<k.hit.x-12||x>=k.hit.x+k.hit.width+12||y<k.hit.y-12||y>=k.hit.y+k.hit.height+12){editorSlid_=true;editorRepeat_=0;}
    if(!pressed) {
      lv_obj_set_style_bg_color(k.object,lv_color_hex(k.value=="shift"&&editor_.shift?theme::text:theme::surface),0);
      editorPointer_=false;editorHeld_=-1;editorRepeat_=0;
      if(!editorSlid_&&!editorRepeats_)events_.push("editor_action",{{"id",editor_.id},{"action",k.value}});else editorBuild();
    }
    return;
  }
  if(std::abs(x-editorStartX_)>12||std::abs(y-editorStartY_)>12)editorSlid_=true;
  if(!pressed){editorPointer_=false;if(editorSlid_){input_.cancel();return;}}
  input_.touch(nativeX,nativeY,pressed);
}
void App::editorInterrupt(bool releaseOcr) {
  editorLayoutHold_=false;editorResetDismiss();editorReaderTouch_=false;
  editorCancelKeys();
  editorMagnify(false);
  if(releaseOcr){editorPolicy(false);editorOcrStop();}
  editorTextTouch_=editorCursorDrag_=false;editorHandle_=-1;
  editorPointer_=false; editorHeld_=-1; editorRepeat_=0; editorInFlight_=editorDue_=0; editorSelectPending_=editorSelectPending_||editorChoosing_;editorChoosing_=false;editorRefreshPending_=false; ++editorRevision_;
  input_.cancel();
  editor_.globeChain=false;editorLayoutMenu_=editorPunctuation_=false;editorPinyinPending_=Json();
}
void App::editorTick() {
  const auto layout=editorLayout();
  if (!editor_.active || keyboard_ || !visible_ || !touchLeaseActive_) { editorAutoScrollAt_=0;return; }
  const auto now=ticks();
  if(editorDismissReturnAt_) {
    const double progress=std::min(1.,double(now-editorDismissReturnAt_)/160.);
    const double remaining=1.-progress;
    editorMoveDismiss(int(std::lround(editorDismissReturnFrom_*remaining*remaining*remaining)));
    if(progress>=1.){editorDismissReturnAt_=0;editorMoveDismiss(0);}
  }
  if(editorText_&&editorUpstream_==editor_.cursor) {
    const int phase=editorPointer_?0:int((now-editorAffinityTime_)/500)%2;
    if(phase!=editorAffinityPhase_){editorAffinityPhase_=phase;lv_obj_invalidate(editorText_);}
  }
  if(editorOcrPreparing_&&editorOcrActive_&&editorOcrPrepareSent_&&now-editorOcrPrepareSent_>2000)
    editorOcrPrepared({{"requestId",editorOcrId_},{"session",editor_.id},{"ok",false}});
  if(editorInlineChat_&&chatControls_) {
    auto send=lv_obj_get_child(chatControls_,-1);
    const bool enabled=generating_||contextBusy_||hasChatText(editor_.text)||editorSendAssets_;
    lv_obj_set_clickable(send,enabled);
    lv_obj_set_style_bg_color(send,lv_color_hex(enabled?theme::text:theme::border),0);
  }
  const auto area=editorTextArea();
  if(editorInPlace()&&(area.x!=editorLastTextArea_.x||area.y!=editorLastTextArea_.y||
      area.width!=editorLastTextArea_.width||area.height!=editorLastTextArea_.height)) {
    editorLastTextArea_=area;editorSyncText();
  }
  const int scrollStep=std::max(1,int(editorAutoScrollAt_?std::min<int64_t>(32,now-editorAutoScrollAt_):8)/2);
  editorAutoScrollAt_=now;
  if(!editorPointer_&&!editorPinyinPending_.is_null()) {auto pending=std::move(editorPinyinPending_);editorPinyinPending_=Json();editorResult(pending);}
  if(!editorPointer_&&!editorOcrPending_.empty()) {auto pending=std::move(editorOcrPending_);editorOcrPending_.clear();for(const auto &m:pending)editorOcrResult(m);}
  if(editorRebuild_ && !editorPointer_) editorBuild();
  if(editorTextTouch_&&(editorCursorDrag_||editorHandle_>=0)&&(editorTouchY_<area.y+14||editorTouchY_>area.y+area.height-14)) {
    const bool up=editorTouchY_<area.y+14;
    const bool boundary=up?editor_.cursor==0:editor_.cursor==editLength(editor_.text);
    const int before=lv_obj_get_scroll_y(editorText_);
    if(!boundary)editorScroll(before+(up?-scrollStep:scrollStep));
    if(lv_obj_get_scroll_y(editorText_)!=before) {
      if(editorHandle_>=0)editor_.select(editorDragAnchor_,editorPositionAt(editorTouchX_,editorTouchY_-16));else editor_.position(editorPositionAt(editorTouchX_,editorTouchY_));
      editorSyncText();
    }
  }
  if (!editorChoosing_ && !editorSelectPending_ && !editorDeferred_.empty()) { auto pending=std::move(editorDeferred_); editorDeferred_.clear(); for(const auto &a:pending) { if(!editor_.active) break; editorAction(a); } }
  if (!editor_.active || keyboard_) return;
  bool openLayouts=false;
  for(auto &c:editorContacts_) {
    if(&c!=&editorContacts_.front()||c.released||c.slid||!c.repeatAt||uint64_t(ticks())<c.repeatAt)continue;
    if(c.key.value=="delete") {
      ++c.repeats;editorAction("delete");c.repeatAt=ticks()+std::max(60,180-c.repeats*20);
    } else if(c.key.value=="language"&&!c.repeats) {
      ++c.repeats;c.repeatAt=0;openLayouts=true;break;
    }
  }
  if(editorHeld_>=0&&editorRepeat_&&uint64_t(ticks())>=editorRepeat_&&editorPress_.value=="language"&&!editorSlid_&&!editorRepeats_) {
    ++editorRepeats_;editorRepeat_=0;openLayouts=true;
  }
  if(openLayouts) {
    // The opening contact is cancelled before rebuilding; its later release
    // cannot become a short globe tap or a click on the new floating list.
    editorCancelKeys();editorHeld_=-1;editorRepeat_=0;editorPointer_=false;
    editorLayoutHold_=true;editorLayoutMenu_=true;input_.cancel();editorBuild();return;
  }
  if(editorHeld_>=0 && editorRepeat_ && uint64_t(ticks())>=editorRepeat_ && editorPress_.value=="delete") {
    const int held=editorHeld_; ++editorRepeats_; editorAction("delete"); editorHeld_=held;
    editorRepeat_=ticks()+std::max(60,180-editorRepeats_*20);
  }
  if(editorInFlight_ && ticks()-editorSent_>1500) { editorInFlight_=0; editorChoosing_=editorSelectPending_=false; editor_.notice="本地拼音查询超时，可重试或使用官方键盘"; editorBuild(); }
  if(editorDue_ && uint64_t(ticks())>=editorDue_ && !editorChoosing_) {
    if(editor_.preedit.empty()||editor_.layout==1||editor_.secret) { editorDue_=0; }
    else editorQuery();
  }
  if(editorRefreshPending_)editorRefresh();
}
Json App::editorDiagnostic() const {
  const auto layout=editorLayout();
  Json keys=Json::array(); for(const auto &k:editorKeys_) keys.push_back({{"key",k.value},{"x",k.x+k.w/2},{"y",k.y+k.h/2},
      {"bounds",{k.x,k.y,k.x+k.w,k.y+k.h}},
      {"hit_bounds",{k.hit.x,k.hit.y,k.hit.x+k.hit.width,k.hit.y+k.hit.height}}});
  Json actions=Json::object();
  for(const auto &entry:editorLocalActions_){lv_area_t a;lv_obj_get_coords(entry.second,&a);actions[entry.first]={a.x1,a.y1,a.x2,a.y2};}
  if(editor_.active&&!editorSelectionOnly_)for(const auto &a:layout.actions(editorInlineChat_)) {
    if(editorCandidateRow()&&std::string(a.name)!="done")continue;
    actions[a.name]={a.x,a.y,a.x+a.width-1,a.y+43};
  }
  if(editor_.active&&editorInlineChat_&&chatControls_) {
    lv_area_t a;lv_obj_get_coords(lv_obj_get_child(chatControls_,-1),&a);actions["chat-send"]={a.x1,a.y1,a.x2,a.y2};
  }
  Json undoPopup=Json::array(),selectionPopup=Json::array(),magnifierBounds=Json::array(),magnifierCaret=Json::array(),textBounds=Json::array();
  if(editorMagnifierCaret_){lv_area_t a;lv_obj_get_coords(editorMagnifierCaret_,&a);magnifierCaret={a.x1,a.y1,a.x2,a.y2};}
  if(editorMagnifier_){lv_area_t a;lv_obj_get_coords(editorMagnifier_,&a);magnifierBounds={a.x1,a.y1,a.x2,a.y2};}
  if(editorText_) {lv_area_t a;lv_obj_get_coords(editorText_,&a);textBounds={a.x1,a.y1,a.x2,a.y2};}
  if(editor_.active&&editorUndoMenu_) {
    const auto p=layout.undoPopup(editorMenuAnchor_.x,editorMenuAnchor_.y,!editorClipboard_.current.empty());
    undoPopup={p.x,p.y,p.x+p.width-1,p.y+p.height-1};
    actions["undo"]={p.x+4,p.y+4,p.x+91,p.y+47};
    if(!editorClipboard_.current.empty())actions["paste"]={p.x+96,p.y+4,p.x+183,p.y+47};
  }
  if(editor_.active&&editorMenu_) {
    const auto p=layout.selectionPopup(editorMenuAnchor_.x,editorMenuAnchor_.y,editorSelectionOnly_);
    selectionPopup={p.x,p.y,p.x+p.width-1,p.y+p.height-1};
    const std::vector<std::string> commands=editorSelectionOnly_?std::vector<std::string>{"all","copy"}:std::vector<std::string>{"all","copy","paste","cut"};
    for(size_t i=0;i<commands.size();++i){int x=p.x+4+int(i)*92;actions[commands[i]]={x,p.y+4,x+87,p.y+47};}
  }
  Json controls=Json::array();
  if(editorRoot_) for(uint32_t i=0;i<lv_obj_get_child_count(editorRoot_);++i) {
    auto o=lv_obj_get_child(editorRoot_,i); lv_area_t a;lv_obj_get_coords(o,&a);
    if(a.x1>=layout.editorPanelX()&&a.x2<layout.editorPanelX()+layout.editorPanelWidth()) { Json item={{"bounds",{a.x1,a.y1,a.x2,a.y2}},{"scroll",{lv_obj_get_scroll_x(o),lv_obj_get_scroll_y(o)}}};
      if(lv_obj_get_child_count(o) && lv_obj_check_type(lv_obj_get_child(o,0),&lv_label_class)) {auto l=lv_obj_get_child(o,0);lv_obj_get_coords(l,&a);item["label_bounds"]={a.x1,a.y1,a.x2,a.y2}; } controls.push_back(item); }
  }
  Json handles=Json::array();
  const auto range=editor_.range();
  for(int i=0;i<2;++i)if(editorHandles_[i]&&!lv_obj_is_hidden(editorHandles_[i])) {
    lv_area_t a;lv_obj_get_coords(editorHandles_[i],&a);const auto anchor=editorPoint(i?range.second:range.first);
    handles.push_back({{"endpoint",i},{"x",(a.x1+a.x2)/2},{"y",(a.y1+a.y2)/2},{"anchor",{anchor.x,anchor.y}},{"bounds",{a.x1,a.y1,a.x2,a.y2}}});
  }
  Json layoutPopup=Json::array();
  const auto layoutItem=editorLocalActions_.find("layout:0");
  if(editorLayoutMenu_&&layoutItem!=editorLocalActions_.end()) {
    lv_area_t area;lv_obj_get_coords(lv_obj_get_parent(layoutItem->second),&area);
    layoutPopup={area.x1,area.y1,area.x2,area.y2};
  }
  auto caret=editorPoint(editor_.cursor);
  Json targets=Json::object();
  for(const auto &entry:inputTargets_)if(entry.second.style>0||entry.second.key=="chat-composer") {
    lv_area_t a;lv_obj_get_coords(entry.first,&a);targets[entry.second.key]={a.x1,a.y1,a.x2,a.y2};
  }
  Json anchor=Json::array();
  if(editorTarget_){lv_area_t a;lv_obj_get_coords(editorTarget_,&a);anchor={a.x1,a.y1,a.x2,a.y2};}
  return {{"keyboard_session",editorKeyboardId_},{"build_count",editorBuildCount_},{"refresh_count",editorRefreshCount_},{"deferred_keys",editorDeferred_.size()},{"select_pending",editorSelectPending_},{"shared_candidate_row",editorCandidateRow()},{"candidate_y",layout.candidateY},{"function_y",layout.actionsY},{"targets",targets},{"target_bounds",anchor},{"text_in_target",editorTarget_&&editorText_&&lv_obj_get_parent(editorText_)==editorTarget_},{"magnifier",editorMagnifier_!=nullptr},{"magnifier_bounds",magnifierBounds},{"magnifier_caret",magnifierCaret},{"selection_popup",selectionPopup},{"cursor_drag",editorCursorDrag_},{"selection_handles",handles},{"caret",{caret.x,caret.y}},{"text_scroll",editorText_?lv_obj_get_scroll_y(editorText_):0},{"controls",controls},{"actions",actions},{"in_place",editor_.active&&editorInPlace()&&editorTarget_!=nullptr},{"input_anchor",editorTargetKey_},{"docked",layout.docked},{"dismiss_offset",editorDismissOffset_},{"dismiss_threshold",editorDismissThreshold_},{"dismissing",editorDismissing_},{"dismiss_returning",editorDismissReturnAt_!=0},{"inline_chat",editorInlineChat_},{"panel_top",layout.panelTop},{"selection_only",editorSelectionOnly_},{"active",editor_.active},{"official",keyboard_},{"session",editor_.id},{"mode",editor_.mode},
    {"layout",editor_.layout==0?"zh26":editor_.layout==1?"en26":"zh9"},{"preferred_layout",editor_.preferredChinese==2?"zh9":"zh26"},
    {"layout_menu",editorLayoutMenu_},{"layout_popup",layoutPopup},{"layout_hold",editorLayoutHold_},{"expanded",editorExpanded_},{"punctuation_menu",editorPunctuation_},{"exact_count",editorExact_.size()},
    {"parked_count",int(!editor_.parked[0].raw.empty())+int(!editor_.parked[1].raw.empty())},
    {"pending",editorInFlight_!=0||editorDue_!=0},{"chinese",editor_.chinese},{"shift",editor_.shift},{"multiline",editor_.multiline},{"secret",editor_.secret},
    {"length",editLength(editor_.text)},{"painted_length",editorText_?editLength(lv_textarea_get_text(editorText_)):0},{"cursor",editor_.cursor},{"caret_upstream",editorUpstream_==editor_.cursor},
    {"line_height",editorText_?lv_obj_get_style_text_font(lv_textarea_get_label(editorText_),LV_PART_MAIN)->line_height:0},{"preedit_length",editor_.preedit.size()},
    {"selection_start",editor_.range().first},{"selection_end",editor_.range().second},{"selected",editor_.selected()},
    {"clipboard_count",editorClipboard_.records.size()},{"clipboard_open",editorClipboardOpen_},{"menu",editorMenu_},{"undo_menu",editorUndoMenu_},{"undo_popup",undoPopup},{"text_bounds",textBounds},
    {"notice_present",!editor_.notice.empty()},{"ocr_request",editorOcrId_},{"ocr_preparing",editorOcrPreparing_},{"ocr_active",editorOcrActive_},{"ocr_multi",editorOcrMulti_},{"ocr_busy",editorOcrBusy_},{"top_panel_blocked",editorTopBlocked_},
    {"coordinate_space",{{"width",layout.width},{"height",layout.height},{"native_width",layout.nativeWidth},{"native_height",layout.nativeHeight},{"scaled",layout.scaled}}},{"keyboard_split",layout.split},{"keyboard_swapped",layout.swapped},{"keyboard_origin",layout.keyInset},{"query_ms",editorLatency_},{"candidate_count",editorCandidates_.size()},{"corrected_count",std::count_if(editorCandidateDetails_.begin(),editorCandidateDetails_.end(),[](const Json &v){return v.value("corrected",false);})},{"revision",editorRevision_},{"keys",keys}};
}
} // namespace penchat

namespace penchat {
void App::keyboardSettingsPage() {
  pageHeader("键盘与输入");
  const int width=layout().contentWidth(), column=(width-16)/2;
  const bool learning=db_.settings().value("pinyin_learning",true);
  label(pageContent_,"本地拼音",10,4,width,16,theme::muted);
  settingsRow("pinyin_learning","拼音学习",learning?"完成输入后记住词频和新词":"暂停学习，保留已有记忆",10,28,column,[this,learning]{db_.setting("pinyin_learning",!learning);build();},learning);
  settingsRow("clear_pinyin","清除拼音记忆","恢复基础词典排序",26+column,28,column,[this]{
    modal("清除拼音记忆","将清除已学习的词频和新词。基础词典、聊天和输入偏好都会保留。");
    auto confirm=button(modal_,"清除记忆",16,screen_.height-66,200,44,[this]{events_.push("pinyin_clear_confirm");});
    lv_obj_set_style_text_color(lv_obj_get_child(confirm,0),lv_color_hex(theme::danger),0);
  });
  const bool correction=db_.settings().value("pinyin_correction",true);
  settingsRow("pinyin_correction","拼音纠错","为26键拼音补充错键候选",10,92,column,[this,correction]{db_.setting("pinyin_correction",!correction);build();},correction);
  const bool swapSides=wideKeyboardSwap_;
  settingsRow("wide_keyboard_swap","横屏键盘左右互换","仅长横屏：键盘在右，编辑区在左",26+column,92,column,[this,swapSides]{wideKeyboardSwap_=!swapSides;db_.queueSetting("wide_keyboard_swap",wideKeyboardSwap_);build();},swapSides);
  const auto preferred=editor_.preferredChinese==2?std::string("zh9"):std::string("zh26");
  label(pageContent_,std::string("常用中文布局：")+(preferred=="zh9"?"9键拼音":"26键拼音")+"\n实际输入后自动记住；长按地球可直接选择布局。",10,168,width-20,18,theme::muted);
}
} // namespace penchat
