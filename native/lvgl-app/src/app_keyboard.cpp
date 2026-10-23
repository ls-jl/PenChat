#include "app.hpp"
#include "icons.hpp"
#include <cctype>
namespace penchat {
void App::edit(const std::string &text, const std::string &placeholder,
               std::function<void(const std::string &)> callback, int maxLength,
               const char *inputType, std::function<void()> cancelled,
               bool multiline, bool secret) {
  if (editor_.active || keyboard_) return;
  closeNavigation(false); closeAttachments(); resetPull();
  editor_ = EditorState{};
  editor_.active = true; editor_.text = text; editor_.position(editLength(text));
  editor_.id = uid("input"); editor_.placeholder = placeholder;
  editor_.limit = maxLength; editor_.multiline = multiline; editor_.secret = secret;
  editor_.chinese = !secret && std::string(inputType) == "ZhCNPreferred";
  editorClipboardOpen_=editorClipboardConfirm_=editorUndoMenu_=false;
  editorOcrMulti_=editorOcrBusy_=editorOcrShown_=false; editorOcrCapture_=editorOcrInserted_=0;
  editorTextTouch_=editorCursorDrag_=false; editorHandle_=-1; editorLastTap_=0;editorTextScroll_=0;editorFollowCursor_=true;
  editorPolicy(true);
  editorExpanded_ = editorMenu_ = false; editorCandidates_ = Json::array();
  editorInFlight_ = editorDue_ = 0; editorChoosing_=false; editorDeferred_.clear();
  editCancellation_ = std::move(cancelled);
  const auto view = view_; const int scroll = body_ ? lv_obj_get_scroll_y(body_) : 0;
  editCompletion_ = [this,callback,view,scroll](const std::string &s) {
    callback(s);
    if (view_ == view && body_) { lv_obj_update_layout(body_); lv_obj_scroll_to_y(body_,scroll,LV_ANIM_OFF); paintVisible(); }
  };
  const auto id = editor_.id;
  whenScannerReleased([this,id] { if (editor_.active && editor_.id == id) { editorBuild();editorOcrStart(); } });
}
void App::editorBuild() {
  if (!editor_.active || keyboard_) return;
  if (editorPointer_ && editorHeld_ < 0) { editorRebuild_=true; return; }
  editorRebuild_=false;
  if(editorText_) editorTextScroll_=lv_obj_get_scroll_y(editorText_);
  auto previousRoot = editorRoot_;
  editorKeys_.clear(); editorText_ = nullptr;
  editorRoot_ = box(lv_screen_active(),0,0,960,266,theme::background);
  lv_obj_set_scrollable(editorRoot_,false);
  auto action = [this](const std::string &s) { events_.push("editor_action",{{"id",editor_.id},{"action",s}}); };
  auto control = [&](const std::string &title,int x,int y,int w,const std::string &a) {
    auto b=button(editorRoot_,title,x,y,w,44,[action,a] { action(a); },theme::surface);
    auto l=lv_obj_get_child(b,0); lv_obj_set_width(l,w-4); lv_obj_set_style_text_font(l,font(17),0);
    return b;
  };
  auto key = [&](std::string title,std::string value,int x,int y,int w,int h = 64) {
    auto b=box(editorRoot_,x,y,w,h,theme::surface); lv_obj_set_scrollable(b,false);
    lv_obj_set_style_radius(b,9,0);
    auto l=label(b,title,0,0,w,23); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0); lv_obj_align(l,LV_ALIGN_CENTER,0,0);
    if (value=="shift" && editor_.shift) { lv_obj_set_style_bg_color(b,lv_color_hex(theme::text),0); lv_obj_set_style_text_color(l,lv_color_hex(theme::background),0); }
    if (editorPointer_ && editorHeld_>=0 && !editorSlid_ && x==editorPress_.x && y==editorPress_.y && value==editorPress_.value)
      lv_obj_set_style_bg_color(b,lv_color_hex(theme::border),0);
    if (value == "shift" || value == "delete" || value == "enter") {
      lv_label_set_text(l, "");
      attachIcon(b, value == "shift" && editor_.shift == 2 ? "shift-lock" : value,
                 value == "shift" && editor_.shift ? theme::background : theme::text);
    }
    editorKeys_.push_back({x,y,w,h,value,b});
  };
  std::vector<std::string> rows = editor_.mode==0 ? std::vector<std::string>{"qwertyuiop","asdfghjkl","zxcvbnm"} :
      editor_.mode==1 ? std::vector<std::string>{"1234567890","@#%&*()-+","/=:;!?_"} : std::vector<std::string>{"[]{}<>^~|\\","$&*_+=`\"'","!?:;/-@"};
  for (int r=0;r<3;++r) {
    const int start = r==0 ? 4 : r==1 ? 33 : 64;
    for (size_t i=0;i<rows[r].size();++i) {
      std::string v(1,rows[r][i]);
      if (!editor_.mode && editor_.shift) v[0]=std::toupper(v[0]);
      key(v,v,start+int(i)*58,6+r*70,54);
    }
  }
  key(editor_.mode ? (editor_.mode==1?"#+=":"123") : (editor_.shift==2?"⇧锁":"⇧"),editor_.mode?"symbols":"shift",4,146,56);
  key("⌫","delete",474,146,106);
  key(editor_.mode==0?"123":"ABC","mode",4,216,68,44);
  key(",",",",76,216,46,44);key("","language",126,216,46,44);
  attachIcon(editorKeys_.back().object,"globe",theme::text);
  key(editor_.chinese?"拼音":"English","space",176,216,230,44);
  key(".",".",410,216,46,44); key("↵","enter",460,216,120,44);
  auto right=box(editorRoot_,584,0,376,266,0x111111); lv_obj_set_scrollable(right,false);
  // Controls share a fixed baseline; composition/candidates never resize the keys.
  lv_obj_move_background(right);
  control("取消",592,214,80,"cancel");
  control(!editorOcrActive_&&!editorOcrId_.empty()?"重试扫描":editorOcrMulti_?"多行扫描":"单行扫描",678,214,104,"ocr-mode");
  control("官方",788,214,64,"official");
  primary(control("完成",856,214,48,"done"));
  attachIcon(control("",908,214,44,"clipboard"),"clipboard",theme::text);
  editorText_=lv_textarea_create(editorRoot_);
  lv_obj_set_pos(editorText_,592,78); lv_obj_set_size(editorText_,360,130);
  lv_obj_set_style_text_font(editorText_,font(20),0);
  lv_obj_set_style_text_color(editorText_,lv_color_hex(theme::text),0);
  lv_obj_set_style_bg_color(editorText_,lv_color_hex(theme::surface),0);
  lv_obj_set_style_border_width(editorText_,0,0); lv_obj_set_style_radius(editorText_,8,0);
  lv_obj_set_style_border_color(editorText_,lv_color_hex(theme::caret),LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_pad_all(editorText_,6,0);
  lv_obj_set_style_bg_color(editorText_,lv_color_hex(theme::border),LV_PART_SELECTED);
  lv_textarea_set_max_length(editorText_,editor_.limit);
  lv_textarea_set_one_line(editorText_,!editor_.multiline);
  lv_obj_set_height(editorText_,130);
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
  auto pre=label(editorRoot_,editor_.notice.empty()?editor_.preedit:editor_.notice,592,54,360,16,theme::muted);
  lv_label_set_long_mode(pre,LV_LABEL_LONG_MODE_DOTS); lv_obj_set_height(pre,20);
  const int candidateY=editorExpanded_?78:6, candidateH=editorExpanded_?130:44;
  if (editorExpanded_) { label(editorRoot_,"全部候选",596,16,300,18,theme::muted); auto cover=box(editorRoot_,592,78,360,130,0x111111);lv_obj_set_scrollable(cover,false); }
  auto choices=box(editorRoot_,592,candidateY,editorExpanded_?360:310,candidateH,0x111111);
  lv_obj_set_scroll_dir(choices,editorExpanded_?LV_DIR_VER:LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(choices,LV_SCROLLBAR_MODE_AUTO);
  int cx=0,cy=0;
  for (size_t i=0;i<editorCandidates_.size();++i) {
    std::string text=editorCandidates_[i].get<std::string>(); int w=std::max(52,std::min(300,int(editLength(text))*22+20));
    if (editorExpanded_ && cx+w>360) { cx=0; cy+=48; }
    auto b=button(choices,text,cx,cy,w,44,[action,i,revision=editorRevision_] { action("choose:"+std::to_string(revision)+":"+std::to_string(i)); },theme::surface);
    lv_obj_set_width(lv_obj_get_child(b,0),w-8); cx+=w+5;
  }
  if (editorCandidates_.empty()&&!editor_.preedit.empty()) label(choices,"正在查询 · 可用官方键盘",4,10,302,16,theme::muted);
  control(editorExpanded_?"收起":"展开",908,6,44,"expand");
  if(editor_.secret) attachIcon(control("", 850,6,44,"reveal"),editor_.reveal?"eye-off":"eye",theme::muted);
  if(editorOcrShown_) {
    auto scan=box(editorRoot_,592,6,editor_.secret?250:310,44,0x111111);
    lv_obj_set_scrollable(scan,false);
    label(scan,editorOcrBusy_?"正在识别…":editor_.notice.empty()?"扫描已插入 · 可继续划扫":"扫描未完成，请重试",4,10,300,17,theme::muted);
  }
  for(int i=0;i<2;++i) {
    editorHandles_[i]=box(editorRoot_,0,0,44,44,theme::surface);
    lv_obj_set_scrollable(editorHandles_[i],false);
    lv_obj_set_style_bg_opa(editorHandles_[i],LV_OPA_TRANSP,0);
    lv_obj_set_clickable(editorHandles_[i],false);
    attachIcon(editorHandles_[i],"selection-handle",theme::link);
  }
  if(editorMenu_ || editorUndoMenu_) {
    auto menu=box(editorRoot_,592,128,360,52,theme::background);
    lv_obj_set_scroll_dir(menu,LV_DIR_HOR);
    std::vector<std::pair<std::string,std::string>> commands=editorUndoMenu_?
      std::vector<std::pair<std::string,std::string>>{{"撤销","undo"}}:
      std::vector<std::pair<std::string,std::string>>{{"全选","all"},{"复制","copy"},{"粘贴","paste"},{"剪切","cut"}};
    for(size_t i=0;i<commands.size();++i) {
      auto b=button(menu,commands[i].first,int(i)*90,4,86,44,[action,commands,i]{action(commands[i].second);});
      if((commands[i].second=="undo"&&editor_.undo.empty()) ||
         ((commands[i].second=="copy"||commands[i].second=="cut")&&editor_.secret&&!editor_.reveal) ||
         (commands[i].second=="paste"&&editorClipboard_.current.empty())) lv_obj_add_state(b,LV_STATE_DISABLED);
    }
  }
  if(editorClipboardOpen_) {
    auto head=box(editorRoot_,592,0,360,74,0x111111);lv_obj_set_scrollable(head,false);
    label(head,"剪贴板 · "+std::to_string(editorClipboard_.records.size()),4,12,172,19);
    button(head,editorClipboardConfirm_?"确认清空":"清空",178,6,98,44,[action]{action("clip-clear");});
    button(head,"关闭",282,6,74,44,[action]{action("clip-close");});
    auto list=box(editorRoot_,592,78,360,130,0x111111);lv_obj_set_scroll_dir(list,LV_DIR_VER);
    int cy=0;
    // A sensitive explicit copy may be pasted once from the current slot but is never a history row.
    if(editorClipboard_.sensitiveCurrent) {
      button(list,"临时复制内容 · 点击粘贴",0,cy,352,44,[action]{action("paste");},theme::surface,LV_TEXT_ALIGN_LEFT);cy+=50;
    }
    if(editorClipboard_.records.empty()&&!editorClipboard_.sensitiveCurrent) label(list,"暂无记录",8,12,330,18,theme::muted);
    for(size_t i=0;i<editorClipboard_.records.size();++i) {
      auto item=editorClipboard_.records[i];std::replace(item.begin(),item.end(),'\n',' ');
      button(list,item,0,cy,302,44,[action,i]{action("clip-paste:"+std::to_string(i));},theme::surface,LV_TEXT_ALIGN_LEFT);
      auto del=button(list,"",308,cy,44,44,[action,i]{action("clip-delete:"+std::to_string(i));});attachIcon(del,"trash",theme::muted);cy+=50;
    }
  }
  if (previousRoot) lv_obj_delete(previousRoot);
  lv_obj_update_layout(editorRoot_);
  lv_obj_move_foreground(editorRoot_);
  editorSyncText();
}
void App::editorChanged() {
  editorFollowCursor_=true;
  ++editorRevision_; editorCandidates_=Json::array(); editorDue_=ticks()+35; editorExpanded_=false; editor_.notice.clear();
}
void App::editorQuery(const std::string &operation,int candidate) {
  if (editorInFlight_ || !visible_ || keyboard_) return;
  editorChoosing_=operation=="choose";
  editorInFlight_=++editorRequest_; editorSent_=ticks(); editorDue_=0;
  bridge_.send("ime.request",{{"session",editor_.id},{"sequence",editorInFlight_},{"revision",editorRevision_},
    {"operation",operation},{"text",editor_.preedit},{"candidate",candidate}});
}
void App::editorResult(const Json &m) {
  if (!editor_.active || keyboard_ || !visible_ || m.value("session",std::string())!=editor_.id ||
      m.value("sequence",uint64_t(0))!=editorInFlight_) return;
  editorLatency_=ticks()-editorSent_;
  editorInFlight_=0; editorChoosing_=false;
  if (m.value("revision",uint64_t(0))!=editorRevision_) return;
  if (!m.value("ok",false)) { editor_.notice="拼音引擎不可用，请用官方键盘"; editorBuild(); return; }
  if (m.contains("committed")) {
    auto text=m.value("committed",std::string()); auto remaining=m.value("remaining",std::string());
    if (!text.empty() && editor_.insert(text)) { editor_.preedit=remaining; editorChanged(); }
    else if (text.empty()) editor_.notice="未能选词，请调整拼音或使用官方键盘";
  } else editorCandidates_=m.value("candidates",Json::array());
  editorBuild();
}
void App::editorFinish(bool confirmed) {
  editorInterrupt(); editor_.active=false; keyboard_=false;
  if (editorRoot_) lv_obj_delete(editorRoot_); editorRoot_=editorText_=nullptr; editorHandles_[0]=editorHandles_[1]=nullptr;editorKeys_.clear();
  auto done=std::move(editCompletion_); auto cancel=std::move(editCancellation_);
  editCompletion_={}; editCancellation_={};
  if (confirmed && done) done(editor_.text); else if (!confirmed && cancel) cancel();
  editor_=EditorState{}; editorDeferred_.clear(); syncScanner();
}
void App::editorOfficial() {
  if(editorText_)editorTextScroll_=lv_obj_get_scroll_y(editorText_);
  std::string text=editor_.text; text.insert(editByte(text,editor_.cursor),editor_.preedit);
  if (editLength(text)>editor_.limit) { editor_.notice="已达到长度上限"; editorBuild(); return; }
  editorInterrupt(); keyboard_=true; keyboardId_=uid("official");
  display_.visible(false);
  bridge_.send("keyboard.open",{{"requestId",keyboardId_},{"text",text},{"placeholder",editor_.placeholder},
    {"maxlength",editor_.limit},{"multiline",editor_.multiline},{"sensitive",editor_.secret},
    {"cursorIndex",editUtf16(text,editor_.cursor+editor_.preedit.size())},
    {"inputType",editor_.chinese?"ZhCNPreferred":"EnUSOnly"}});
}
void App::editorAction(const std::string &a) {
  if(a=="ocr-mode") { if(editorOcrActive_){editorOcrMulti_=!editorOcrMulti_;editorOcrInserted_=0;}editorOcrStop();editorOcrStart();editorBuild();return; }
  if(a=="clipboard") {editorClipboardOpen_=!editorClipboardOpen_;editorClipboardConfirm_=false;editorMenu_=editorUndoMenu_=false;editorBuild();return;}
  if(a=="clip-close") {editorClipboardOpen_=false;editorBuild();return;}
  if(a=="clip-clear") {if(editorClipboardConfirm_) {editorClipboard_.clear();editorClipboardConfirm_=false;}else editorClipboardConfirm_=true;editorBuild();return;}
  if(a.find("clip-delete:")==0) {auto i=std::stoul(a.substr(12));if(i<editorClipboard_.records.size()) {auto old=editorClipboard_.records[i];editorClipboard_.records.erase(editorClipboard_.records.begin()+i);if(editorClipboard_.current==old)editorClipboard_.current=editorClipboard_.records.empty()?"":editorClipboard_.records.front();}editorBuild();return;}
  if(a=="cancel") { editorFinish(false); return; }
  if (editorChoosing_) { editorDeferred_.push_back(a); return; }
  if (a=="official") { editorOfficial(); return; }
  if(a=="copy"||a=="cut") {
    if(editor_.selected() && (!editor_.secret||editor_.reveal)) {
      if(editorClipboard_.copy(editor_.selection(),editor_.secret)) {if(a=="cut"){editor_.insert("");editorChanged();}}
      else editor_.notice="选区过长，无法复制";
    }
    editorMenu_=editorUndoMenu_=false;editorBuild();return;
  }
  if(a=="paste"||a.find("clip-paste:")==0) {
    std::string text=editorClipboard_.current;
    if(a!="paste") {auto i=std::stoul(a.substr(11));if(i>=editorClipboard_.records.size())return;text=editorClipboard_.records[i];}
    if(!editor_.multiline)std::replace(text.begin(),text.end(),'\n',' ');
    // Validate both pieces before committing pending pinyin or replacing selection.
    if(editor_.retained()+editLength(editor_.preedit)+editLength(text)>editor_.limit) editor_.notice="粘贴内容超过长度上限";
    else if(editor_.commit(text)){editorChanged();editorClipboardOpen_=false;}
    editorMenu_=editorUndoMenu_=false;editorBuild();return;
  }
  if (a=="done" || a=="enter" || a=="space") {
    if (!editor_.preedit.empty()) {
      if (a=="enter") {
        if (editor_.raw()) editorChanged();
        editorBuild();
        return;
      }
      if (!editorCandidates_.empty() && !editorInFlight_) editorQuery("choose",0);
      else { editor_.notice="请等待有效候选，或切换英文保留拼音"; editorBuild(); }
      return;
    }
    if (a=="done" || (a=="enter"&&!editor_.multiline)) { editorFinish(true); return; }
    editor_.insert(a=="space"?" ":"\n");
  } else if (a.find("choose:")==0) {
    auto split=a.find(':',7);
    if (split!=std::string::npos && std::stoull(a.substr(7,split-7))==editorRevision_ && !editorInFlight_ && !editorCandidates_.empty()) editorQuery("choose",std::stoi(a.substr(split+1)));
    return;
  } else if (a=="language" || a=="mode" || a=="symbols") {
    if (editor_.raw()) { if(a=="language") editor_.chinese=!editor_.chinese; else if(a=="symbols") editor_.mode=editor_.mode==1?2:1;else editor_.mode=editor_.mode?0:1; editorChanged(); }
  } else if (a=="shift") {
    if(editor_.mode) return;
    auto now=ticks(); editor_.shift=editor_.shift==2?0:editor_.shift==1?(now-editorShiftTime_<400?2:0):1; editorShiftTime_=now;
  } else if (a=="delete") { editor_.erase(); editorChanged(); }
  else if (a=="tools") editorMenu_=!editorMenu_;
  else if (a=="expand") editorExpanded_=!editorExpanded_;
  else if (a=="reveal") { editor_.reveal=!editor_.reveal; editorMenu_=false; }
  else if (a=="undo") { editor_.revert(); editorChanged(); editorMenu_=editorUndoMenu_=false; }
  else if (a=="all") { if(editor_.raw()) { editor_.all=true; editorMenu_=false; editorChanged(); } }
  else if (a.find("cursor:")==0) { if(editor_.raw()) { editor_.position(std::min(size_t(std::stoul(a.substr(7))),editLength(editor_.text))); editorChanged(); } }
  else if (a=="left" || a=="right" || a=="home" || a=="end") {
    if (editor_.raw()) {
      editor_.all=false;
      if(a=="left" && editor_.cursor) --editor_.cursor;
      if(a=="right") editor_.cursor=std::min(editor_.cursor+1,editLength(editor_.text));
      if(a=="home") { auto b=editByte(editor_.text,editor_.cursor); auto pos=b?editor_.text.rfind('\n',b-1):std::string::npos; editor_.cursor=pos==std::string::npos?0:editLength(editor_.text.substr(0,pos+1)); }
      if(a=="end") { auto pos=editor_.text.find('\n',editByte(editor_.text,editor_.cursor)); editor_.cursor=pos==std::string::npos?editLength(editor_.text):editLength(editor_.text.substr(0,pos)); }
      editorMenu_=false; editorChanged();
    }
  } else if (a.size()==1) {
    std::string letter=a;
    if(editor_.mode==0 && std::isalpha(static_cast<unsigned char>(letter[0])))
      letter[0]=editor_.shift?std::toupper(letter[0]):std::tolower(letter[0]);
    if(editor_.chinese && editor_.mode==0 && letter[0]>='a' && letter[0]<='z' && !editor_.secret) {
      if (editor_.retained()+editor_.preedit.size()<editor_.limit) { editor_.remember(); editor_.preedit+=letter; editorChanged(); }
      else editor_.notice="已达到长度上限";
    } else if (editor_.commit(letter)) { editorChanged(); }
    if(editor_.shift==1 && std::isalpha(static_cast<unsigned char>(a[0]))) editor_.shift=0;
  }
  if(a!="expand"&&a!="reveal")editorOcrShown_=false;
  editorBuild();
}
lv_point_t App::editorPoint(size_t index) const {
  lv_point_t point{};
  if(!editorText_)return point;
  auto text=lv_textarea_get_label(editorText_);lv_area_t area;lv_obj_get_coords(text,&area);
  lv_label_get_letter_pos(text,index,&point);point.x+=area.x1;point.y+=area.y1+font(20)->line_height+8;
  return point;
}
size_t App::editorPositionAt(int x,int y) const {
  if(!editorText_)return editor_.cursor;
  auto text=lv_textarea_get_label(editorText_);lv_area_t a;lv_obj_get_coords(text,&a);
  lv_point_t point{x-a.x1,y-a.y1};
  return editBoundary(editor_.text,std::min(size_t(lv_label_get_letter_on(text,&point,true)),editLength(editor_.text)));
}
void App::editorSyncText(bool text) {
  if(!editorText_)return;
  if(text)lv_textarea_set_text(editorText_,editor_.text.c_str());
  if(text||lv_textarea_get_cursor_pos(editorText_)!=editor_.cursor)lv_textarea_set_cursor_pos(editorText_,editor_.cursor);
  auto label=lv_textarea_get_label(editorText_);auto r=editor_.range();
  lv_label_set_text_selection_start(label,editor_.selected()?r.first:LV_LABEL_TEXT_SELECTION_OFF);
  lv_label_set_text_selection_end(label,editor_.selected()?r.second:LV_LABEL_TEXT_SELECTION_OFF);
  lv_obj_update_layout(editorText_);
  for(int i=0;i<2;++i)if(editorHandles_[i]) {
    auto point=editorPoint(i?r.second:r.first);
    lv_obj_set_hidden(editorHandles_[i],!editor_.selected()||editorClipboardOpen_||editorExpanded_||editorMenu_||editorUndoMenu_||point.y<90||point.y>220);
    lv_obj_set_pos(editorHandles_[i],std::max(584,std::min(916,int(point.x)-22)),std::min(164,int(point.y)-22));
  }
}
void App::editorPolicy(bool active) {
  editorTopBlocked_=false;
  bridge_.send("editor.state",{{"session",editor_.id},{"sequence",++editorPolicySequence_},{"active",active}});
}
void App::editorOcrStop() {
  if(editorOcrActive_)bridge_.send("ocr.cancel",{{"requestId",editorOcrId_}});
  editorOcrActive_=editorOcrBusy_=false;editorOcrId_.clear();editorOcrCapture_=0;editorOcrPending_.clear();
}
void App::editorOcrStart() {
  if(!editor_.active||keyboard_||!visible_||editorOcrActive_)return;
  const auto session=editor_.id;
  whenScannerReleased([this,session] {
    if(!editor_.active||editor_.id!=session||keyboard_||!visible_||editorOcrActive_)return;
    editorOcrActive_=true;editorOcrId_=uid("editor-ocr");editorOcrCapture_=0;
    bridge_.send("ocr.start",{{"requestId",editorOcrId_},{"session",session},{"mode",editorOcrMulti_?"multi":"single"},{"continuous",true}});
  });
}
void App::editorOcrResult(const Json &m) {
  if(!editor_.active||keyboard_||!visible_||!editorOcrActive_||m.value("session",std::string())!=editor_.id||
      m.value("requestId",std::string())!=editorOcrId_||m.value("mode",std::string())!=(editorOcrMulti_?"multi":"single"))return;
  auto capture=m.value("capture",uint64_t(0));
  if(capture<=editorOcrCapture_)return;
  if(editorPointer_) {
    for(const auto &pending:editorOcrPending_)if(pending.value("capture",uint64_t(0))==capture||pending.value("overflow",false))return;
    if(editorOcrPending_.size()<63)editorOcrPending_.push_back(m);
    else {
      auto failure=m;failure.erase("text");failure["confirmed"]=false;failure["overflow"]=true;
      failure["error"]="扫描暂存已满，本次未插入，请松手后重新划扫";editorOcrPending_.push_back(std::move(failure));
    }
    return;
  }
  editorOcrCapture_=capture;editorOcrBusy_=false;editorOcrShown_=true;
  if(!m.value("confirmed",false)) {
    editor_.notice=m.value("error",std::string("未识别到文字，请重试"));
    if(m.value("terminal",false))editorOcrActive_=false;
    editorBuild();return;
  }
  std::string text=m.value("text",std::string());
  text.erase(std::remove(text.begin(),text.end(),'\r'),text.end());
  if(!editor_.multiline||!editorOcrMulti_)std::replace(text.begin(),text.end(),'\n',' ');
  if(text.empty())return;
  if(editorOcrMulti_&&editorOcrInserted_>0&&!editor_.text.empty()&&!editor_.selected())text=(editor_.multiline?"\n":" ")+text;
  if(editor_.retained()+editLength(editor_.preedit)+editLength(text)>editor_.limit)editor_.notice="扫描内容超过长度上限，请缩短内容后重试";
  else if(editor_.commit(text)){++editorOcrInserted_;editorChanged();}
  editorBuild();
}
void App::editorTouch(int x,int y,bool pressed,bool cancelled) {
  editorTouchX_=x;editorTouchY_=y;
  if(cancelled) {editorLastTap_=0;editorPointer_=editorTextTouch_=editorCursorDrag_=false;editorHandle_=-1;editorHeld_=-1;editorRepeat_=0;input_.cancel();editorBuild();return;}
  if(pressed&&!editorPointer_) {
    editorPointer_=true;editorSlid_=editorTextMoved_=editorCursorDrag_=false;
    editorStartX_=x;editorStartY_=y;editorHeld_=-1;editorRepeats_=0;editorPressTime_=ticks();editorHandle_=-1;
    bool overlay=editorClipboardOpen_||editorExpanded_||((editorMenu_||editorUndoMenu_)&&y>=128&&y<182);
    if(!overlay && editor_.selected()) {
      auto r=editor_.range();int distance=100000;
      for(int i=0;i<2;++i) {
        if(!editorHandles_[i]||lv_obj_is_hidden(editorHandles_[i]))continue;
        lv_area_t a;lv_obj_get_coords(editorHandles_[i],&a);int dx=x-(a.x1+a.x2)/2,dy=y-(a.y1+a.y2)/2;
        if(y>=a.y1+12&&std::abs(dx)<=22&&std::abs(dy)<=22&&dx*dx+dy*dy<distance) {distance=dx*dx+dy*dy;editorHandle_=i;}
      }
      if(editorHandle_>=0)editorDragAnchor_=editorHandle_?r.first:r.second;
    }
    editorTextTouch_=editorHandle_>=0||(!overlay&&x>=592&&x<952&&y>=78&&y<208);
    if(editorTextTouch_) {
      if(!editor_.preedit.empty()&&editor_.raw()) {editorChanged();editorSyncText(true);}
      editorTextScroll_=lv_obj_get_scroll_y(editorText_);input_.cancel();return;
    }
    for(size_t i=0;i<editorKeys_.size();++i) {
      const auto &k=editorKeys_[i];
      if(x>=k.x-4&&x<k.x+k.w+4&&y>=k.y-3&&y<k.y+k.h+3) {
        editorHeld_=int(i);editorPress_=k;lv_obj_set_style_bg_color(k.object,lv_color_hex(theme::border),0);editorRepeat_=ticks()+450;break;
      }
    }
  }
  if(editorTextTouch_) {
    const bool moved=std::abs(x-editorStartX_)>12||std::abs(y-editorStartY_)>12;
    const bool held=ticks()-editorPressTime_>=450;
    if(pressed&&moved) {
      if(editorHandle_>=0) {editor_.select(editorDragAnchor_,editorPositionAt(x,y-16));editorSyncText();}
      else if(editorCursorDrag_||(!editorTextMoved_&&held)) {editorCursorDrag_=true;editor_.position(editorPositionAt(x,y));editorSyncText();}
      else lv_obj_scroll_to_y(editorText_,editorTextScroll_+editorStartY_-y,LV_ANIM_OFF);
      editorTextMoved_=true;
    }
    if(!pressed) {
      editorPointer_=editorTextTouch_=false;
      if(editorTextMoved_)editorLastTap_=0;
      if(editorHandle_>=0||editorCursorDrag_) {editorHandle_=-1;editorCursorDrag_=false;editorLastTap_=0;editorBuild();return;}
      if(!editorTextMoved_) {
        auto index=editorPositionAt(x,y);
        if(held) {
          auto r=editor_.range();editorMenu_=editor_.selected()&&index>=r.first&&index<=r.second;editorUndoMenu_=!editorMenu_;if(editorUndoMenu_)editor_.position(index);
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
    if(x<k.x-12||x>=k.x+k.w+12||y<k.y-12||y>=k.y+k.h+12){editorSlid_=true;editorRepeat_=0;}
    if(!pressed) {
      editorPointer_=false;editorHeld_=-1;editorRepeat_=0;
      if(!editorSlid_&&!editorRepeats_)events_.push("editor_action",{{"id",editor_.id},{"action",k.value}});else editorBuild();
    }
    return;
  }
  if(std::abs(x-editorStartX_)>12||std::abs(y-editorStartY_)>12)editorSlid_=true;
  if(!pressed){editorPointer_=false;if(editorSlid_){input_.cancel();return;}}
  input_.touch(x,y,pressed);
}
void App::editorInterrupt() {
  editorPolicy(false);editorOcrStop();editorTextTouch_=editorCursorDrag_=false;editorHandle_=-1;
  editorPointer_=false; editorHeld_=-1; editorRepeat_=0; editorInFlight_=editorDue_=0; editorChoosing_=false; ++editorRevision_;
  input_.cancel();
  if(editor_.active) bridge_.send("ime.reset",{{"session",editor_.id}});
}
void App::editorTick() {
  if (!editor_.active || keyboard_ || !visible_) return;
  if(!editorPointer_&&!editorOcrPending_.empty()) {auto pending=std::move(editorOcrPending_);editorOcrPending_.clear();for(const auto &m:pending)editorOcrResult(m);}
  if(editorRebuild_ && !editorPointer_) editorBuild();
  if(editorTextTouch_&&(editorCursorDrag_||editorHandle_>=0)&&(editorTouchY_<92||editorTouchY_>194)) {
    lv_obj_scroll_by(editorText_,0,editorTouchY_<92?4:-4,LV_ANIM_OFF);
    if(editorHandle_>=0)editor_.select(editorDragAnchor_,editorPositionAt(editorTouchX_,editorTouchY_-16));else editor_.position(editorPositionAt(editorTouchX_,editorTouchY_));
    editorSyncText();
  }
  if (!editorChoosing_ && !editorDeferred_.empty()) { auto pending=std::move(editorDeferred_); editorDeferred_.clear(); for(const auto &a:pending) { if(!editor_.active) break; editorAction(a); } }
  if (!editor_.active || keyboard_) return;
  if(editorHeld_>=0 && editorRepeat_ && ticks()>=editorRepeat_ && editorPress_.value=="delete") {
    const int held=editorHeld_; ++editorRepeats_; editorAction("delete"); editorHeld_=held;
    editorRepeat_=ticks()+std::max(60,180-editorRepeats_*20);
  }
  if(editorInFlight_ && ticks()-editorSent_>1500) { editorInFlight_=0; editorChoosing_=false; editor_.notice="拼音查询超时，请用官方键盘"; editorBuild(); }
  if(editorDue_ && ticks()>=editorDue_ && !editorInFlight_) {
    if(editor_.preedit.empty()) { editorDue_=0; bridge_.send("ime.reset",{{"session",editor_.id}}); }
    else editorQuery();
  }
}
Json App::editorDiagnostic() const {
  Json keys=Json::array(); for(const auto &k:editorKeys_) keys.push_back({{"key",k.value},{"x",k.x+k.w/2},{"y",k.y+k.h/2}});
  Json controls=Json::array();
  if(editorRoot_) for(uint32_t i=0;i<lv_obj_get_child_count(editorRoot_);++i) {
    auto o=lv_obj_get_child(editorRoot_,i); lv_area_t a;lv_obj_get_coords(o,&a);
    if(a.x1>=584) { Json item={{"bounds",{a.x1,a.y1,a.x2,a.y2}},{"scroll",{lv_obj_get_scroll_x(o),lv_obj_get_scroll_y(o)}}};
      if(lv_obj_get_child_count(o) && lv_obj_check_type(lv_obj_get_child(o,0),&lv_label_class)) {auto l=lv_obj_get_child(o,0);lv_obj_get_coords(l,&a);item["label_bounds"]={a.x1,a.y1,a.x2,a.y2}; } controls.push_back(item); }
  }
  Json handles=Json::array();
  for(int i=0;i<2;++i)if(editorHandles_[i]&&!lv_obj_is_hidden(editorHandles_[i])) {lv_area_t a;lv_obj_get_coords(editorHandles_[i],&a);handles.push_back({{"endpoint",i},{"x",(a.x1+a.x2)/2},{"y",(a.y1+a.y2)/2}});}
  auto caret=editorPoint(editor_.cursor);
  return {{"selection_handles",handles},{"caret",{caret.x,caret.y}},{"text_scroll",editorText_?lv_obj_get_scroll_y(editorText_):0},{"controls",controls},{"active",editor_.active},{"official",keyboard_},{"session",editor_.id},{"mode",editor_.mode},
    {"chinese",editor_.chinese},{"shift",editor_.shift},{"multiline",editor_.multiline},{"secret",editor_.secret},
    {"length",editLength(editor_.text)},{"cursor",editor_.cursor},{"preedit_length",editor_.preedit.size()},
    {"selection_start",editor_.range().first},{"selection_end",editor_.range().second},{"selected",editor_.selected()},
    {"clipboard_count",editorClipboard_.records.size()},{"clipboard_open",editorClipboardOpen_},{"menu",editorMenu_},{"undo_menu",editorUndoMenu_},
    {"ocr_request",editorOcrId_},{"ocr_active",editorOcrActive_},{"ocr_multi",editorOcrMulti_},{"ocr_busy",editorOcrBusy_},{"top_panel_blocked",editorTopBlocked_},
    {"query_ms",editorLatency_},{"candidate_count",editorCandidates_.size()},{"revision",editorRevision_},{"keys",keys}};
}
} // namespace penchat
