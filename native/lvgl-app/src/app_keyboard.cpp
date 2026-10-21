#include "app.hpp"
#include <cctype>
namespace penchat {
void App::edit(const std::string &text, const std::string &placeholder,
               std::function<void(const std::string &)> callback, int maxLength,
               const char *inputType, std::function<void()> cancelled,
               bool multiline, bool secret) {
  if (editor_.active || keyboard_) return;
  closeNavigation(false); closeAttachments(); resetPull(); stopKeyScan();
  editor_ = EditorState{};
  editor_.active = true; editor_.text = text; editor_.cursor = editLength(text);
  editor_.id = uid("input"); editor_.placeholder = placeholder;
  editor_.limit = maxLength; editor_.multiline = multiline; editor_.secret = secret;
  editor_.chinese = !secret && std::string(inputType) == "ZhCNPreferred";
  editorExpanded_ = editorMenu_ = false; editorCandidates_ = Json::array();
  editorInFlight_ = editorDue_ = 0; editorChoosing_=false; editorDeferred_.clear();
  editCancellation_ = std::move(cancelled);
  const auto view = view_; const int scroll = body_ ? lv_obj_get_scroll_y(body_) : 0;
  editCompletion_ = [this,callback,view,scroll](const std::string &s) {
    callback(s);
    if (view_ == view && body_) { lv_obj_update_layout(body_); lv_obj_scroll_to_y(body_,scroll,LV_ANIM_OFF); paintVisible(); }
  };
  const auto id = editor_.id;
  whenScannerReleased([this,id] { if (editor_.active && editor_.id == id) editorBuild(); });
}
void App::editorBuild() {
  if (!editor_.active || keyboard_) return;
  if (editorPointer_ && editorHeld_ < 0) { editorRebuild_=true; return; }
  editorRebuild_=false;
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
  key(editor_.shift==2?"⇧锁":"⇧","shift",4,146,56);
  key("⌫","delete",474,146,106);
  key(editor_.mode==0?"123":editor_.mode==1?"#+=":"ABC","mode",4,216,68,44);
  key(",",",",76,216,46,44); key("空格","space",126,216,280,44);
  key(".",".",410,216,46,44); key("↵","enter",460,216,120,44);
  auto right=box(editorRoot_,584,0,376,266,0x111111); lv_obj_set_scrollable(right,false);
  // Controls share a fixed baseline; composition/candidates never resize the keys.
  lv_obj_move_background(right);
  control("取消",592,78,80,"cancel");
  control(editor_.chinese?"中文":"英文",678,78,104,"language");
  primary(control("完成",788,78,164,"done"));
  editorText_=lv_textarea_create(editorRoot_);
  lv_obj_set_pos(editorText_,592,128); lv_obj_set_size(editorText_,360,80);
  lv_obj_set_style_text_font(editorText_,font(20),0);
  lv_obj_set_style_text_color(editorText_,lv_color_hex(theme::text),0);
  lv_obj_set_style_bg_color(editorText_,lv_color_hex(theme::surface),0);
  lv_obj_set_style_border_width(editorText_,0,0); lv_obj_set_style_radius(editorText_,8,0);
  lv_obj_set_style_pad_all(editorText_,6,0);
  lv_obj_set_style_bg_color(editorText_,lv_color_hex(theme::border),LV_PART_SELECTED);
  lv_textarea_set_max_length(editorText_,editor_.limit);
  lv_textarea_set_one_line(editorText_,!editor_.multiline);
  lv_obj_set_height(editorText_,80);
  lv_textarea_set_password_mode(editorText_,editor_.secret&&!editor_.reveal);
  lv_textarea_set_password_show_time(editorText_,0);
  lv_textarea_set_placeholder_text(editorText_,editor_.placeholder.c_str());
  lv_textarea_set_text(editorText_,editor_.text.c_str());
  lv_textarea_set_cursor_pos(editorText_,editor_.cursor);
  lv_obj_add_state(editorText_,LV_STATE_FOCUSED);
  lv_textarea_set_cursor_click_pos(editorText_,true);
  lv_obj_add_event_cb(editorText_,[](lv_event_t *e) {
    auto a=static_cast<App *>(lv_event_get_user_data(e));
    a->events_.push("editor_action",{{"id",a->editor_.id},{"action","cursor:"+std::to_string(lv_textarea_get_cursor_pos(a->editorText_))}});
  },LV_EVENT_CLICKED,this);
  auto pre=label(editorRoot_,editor_.notice.empty()?editor_.preedit:editor_.notice,592,54,360,16,theme::muted);
  lv_label_set_long_mode(pre,LV_LABEL_LONG_MODE_DOTS); lv_obj_set_height(pre,20);
  const int candidateY=editorExpanded_?128:6, candidateH=editorExpanded_?80:44;
  if (editorExpanded_) { label(editorRoot_,"全部候选",596,16,300,18,theme::muted); auto cover=box(editorRoot_,592,128,360,80,0x111111);lv_obj_set_scrollable(cover,false); }
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
  if (editorCandidates_.empty()) label(choices,editor_.preedit.empty()?"候选词":"正在查询 · 可用官方键盘",4,10,302,16,theme::muted);
  control(editorExpanded_?"收起":"展开",908,6,44,"expand");
  control("←",592,214,44,"left"); control("→",642,214,44,"right");
  control("编辑",692,214,80,"tools"); control("官方键盘",778,214,174,"official");
  if (editor_.all) label(editorRoot_,"已全选 · 下次输入替换",600,180,330,16,theme::muted);
  if (editorMenu_) {
    auto menu=box(editorRoot_,592,128,360,80,theme::background); lv_obj_set_scroll_dir(menu,LV_DIR_HOR);
    const std::vector<std::string> names={"行首","行尾","全选","撤销",editor_.reveal?"隐藏密钥":"显示密钥"};
    const std::vector<std::string> commands={"home","end","all","undo","reveal"};
    for (size_t i=0;i<(editor_.secret?5u:4u);++i)
      button(menu,names[i],int(i)*90,18,86,44,[action,commands,i]{action(commands[i]);});
  }
  if (previousRoot) lv_obj_delete(previousRoot);
  lv_obj_update_layout(editorRoot_);
  lv_obj_move_foreground(editorRoot_);
}
void App::editorChanged() {
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
  if (editorRoot_) lv_obj_delete(editorRoot_); editorRoot_=editorText_=nullptr; editorKeys_.clear();
  auto done=std::move(editCompletion_); auto cancel=std::move(editCancellation_);
  editCompletion_={}; editCancellation_={};
  if (confirmed && done) done(editor_.text); else if (!confirmed && cancel) cancel();
  editor_=EditorState{}; editorDeferred_.clear(); syncScanner();
}
void App::editorOfficial() {
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
  if (a=="cancel") { editorFinish(false); return; }
  if (editorChoosing_) { editorDeferred_.push_back(a); return; }
  if (a=="official") { editorOfficial(); return; }
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
  } else if (a=="language" || a=="mode") {
    if (editor_.raw()) { if(a=="language") editor_.chinese=!editor_.chinese; else editor_.mode=(editor_.mode+1)%3; editorChanged(); }
  } else if (a=="shift") {
    if(editor_.mode) return;
    auto now=ticks(); editor_.shift=editor_.shift==2?0:editor_.shift==1?(now-editorShiftTime_<400?2:0):1; editorShiftTime_=now;
  } else if (a=="delete") { editor_.erase(); editorChanged(); }
  else if (a=="tools") editorMenu_=!editorMenu_;
  else if (a=="expand") editorExpanded_=!editorExpanded_;
  else if (a=="reveal") { editor_.reveal=!editor_.reveal; editorMenu_=false; }
  else if (a=="undo") { editor_.revert(); editorChanged(); editorMenu_=false; }
  else if (a=="all") { if(editor_.raw()) { editor_.all=true; editorMenu_=false; editorChanged(); } }
  else if (a.find("cursor:")==0) { if(editor_.raw()) { editor_.cursor=std::min(size_t(std::stoul(a.substr(7))),editLength(editor_.text)); editor_.all=false; editorChanged(); } }
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
      if (editLength(editor_.text)+editor_.preedit.size()<editor_.limit) { editor_.remember(); editor_.preedit+=letter; editorChanged(); }
      else editor_.notice="已达到长度上限";
    } else if (editor_.raw()) { editor_.insert(letter); editorChanged(); }
    if(editor_.shift==1 && std::isalpha(static_cast<unsigned char>(a[0]))) editor_.shift=0;
  }
  editorBuild();
}
void App::editorTouch(int x,int y,bool pressed,bool cancelled) {
  if (cancelled) { editorPointer_=false; editorHeld_=-1; editorRepeat_=0; input_.cancel(); editorBuild(); return; }
  if (pressed && !editorPointer_) {
    editorPointer_=true; editorSlid_=false; editorStartX_=x; editorStartY_=y; editorHeld_=-1; editorRepeats_=0;
    // Fill only the narrow inter-key gutters. The staggered row margins stay
    // inert; a press never transfers to another key after pointer-down.
    for(size_t i=0;i<editorKeys_.size();++i) {
      const auto &k=editorKeys_[i];
      if(x>=k.x-4 && x<k.x+k.w+4 && y>=k.y-3 && y<k.y+k.h+3) {
        editorHeld_=int(i); editorPress_=k;
        lv_obj_set_style_bg_color(k.object,lv_color_hex(theme::border),0);
        editorRepeat_=ticks()+450; break;
      }
    }
  }
  if (editorHeld_>=0) {
    const auto &k=editorPress_;
    if(x<k.x-12 || x>=k.x+k.w+12 || y<k.y-12 || y>=k.y+k.h+12) { editorSlid_=true; editorRepeat_=0; }
    if(!pressed) {
      editorPointer_=false; editorHeld_=-1; editorRepeat_=0;
      if(!editorSlid_ && !editorRepeats_) events_.push("editor_action",{{"id",editor_.id},{"action",k.value}});
      else editorBuild();
    }
    return;
  }
  // A right-panel drag may scroll candidates/text, but must not activate a button.
  if (std::abs(x-editorStartX_)>12 || std::abs(y-editorStartY_)>12) editorSlid_=true;
  if (!pressed) { editorPointer_=false; if(editorSlid_) { input_.cancel(); return; } }
  input_.touch(x,y,pressed);
}
void App::editorInterrupt() {
  editorPointer_=false; editorHeld_=-1; editorRepeat_=0; editorInFlight_=editorDue_=0; editorChoosing_=false; ++editorRevision_;
  input_.cancel();
  if(editor_.active) bridge_.send("ime.reset",{{"session",editor_.id}});
}
void App::editorTick() {
  if (!editor_.active || keyboard_ || !visible_) return;
  if(editorRebuild_ && !editorPointer_) editorBuild();
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
  return {{"controls",controls},{"active",editor_.active},{"official",keyboard_},{"session",editor_.id},{"mode",editor_.mode},
    {"chinese",editor_.chinese},{"shift",editor_.shift},{"multiline",editor_.multiline},{"secret",editor_.secret},
    {"length",editLength(editor_.text)},{"cursor",editor_.cursor},{"preedit_length",editor_.preedit.size()},
    {"query_ms",editorLatency_},{"candidate_count",editorCandidates_.size()},{"revision",editorRevision_},{"keys",keys}};
}
} // namespace penchat
