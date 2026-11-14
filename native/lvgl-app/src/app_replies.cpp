#include "app.hpp"
namespace penchat {
namespace {
std::string replyText(const Json &row) {
  auto i=row.find("output_text");return i!=row.end()&&i->is_string()?i->get<std::string>():std::string();
}
}
bool App::switchDraft(const std::string &next) {
  if(next==session_)return true;
  size_t bytes=draft_.size();
  for(const auto &d:sessionDrafts_)if(d.first!=next&&d.first!=session_)bytes+=d.second.size();
  if(bytes>1024*1024){modal("草稿暂存已满","请先发送或清空部分草稿，再切换会话；当前输入仍保留。");return false;}
  if(draft_.empty())sessionDrafts_.erase(session_);else sessionDrafts_[session_]=draft_;
  auto found=sessionDrafts_.find(next);
  draft_=found==sessionDrafts_.end()?std::string():std::move(found->second);
  if(found!=sessionDrafts_.end())sessionDrafts_.erase(found);
  composerShortcut_.reset();
  return true;
}
void App::replyToast(const std::string &text) {
  const int width=std::min(screen_.width-uiMetrics_.dp(120),std::max(uiMetrics_.dp(140),int(editLength(text))*uiMetrics_.dp(20)+uiMetrics_.dp(32)));
  auto toast=box(screenObj_,(screen_.width-width)/2,screen_.height-uiMetrics_.dp(116),width,uiMetrics_.dp(44),theme::surface);
  lv_obj_set_scrollable(toast,false);lv_obj_set_clickable(toast,false);lv_obj_set_style_radius(toast,uiMetrics_.dp(16),0);
  auto caption=label(toast,text,uiMetrics_.dp(8),uiMetrics_.dp(10),width-uiMetrics_.dp(16),uiMetrics_.fontSize(UiFont::FormNote));lv_label_set_long_mode(caption,LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(caption,uiMetrics_.dp(24));lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);lv_obj_delete_delayed(toast,1600);
}
void App::replyBar(lv_obj_t *parent,const Json &message,int width,bool question) {
  const auto id=message.at("id").get<std::string>(),session=session_;
  const auto choice=replyChoiceRows_.value(id,Json::object());
  const int ordinal=choice.value("ordinal",1),total=choice.value("total",1);
  if(ordinal>1) {
    auto hint=label(parent,choice.value("archive",0)?"将从这条回复继续 · 发送时先归档":"将从这条回复继续",0,0,width,uiMetrics_.fontSize(UiFont::Detail),theme::muted);
    lv_label_set_long_mode(hint,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_height(hint,font(uiMetrics_.fontSize(UiFont::Detail))->line_height);
  }
  auto bar=box(parent,0,0,width,uiMetrics_.dp(48),theme::background);lv_obj_set_scrollable(bar,false);
  lv_obj_t *buttons[3]{};
  const char *names[]={"copy","retry","branch"};
  const char *titles[]={"复制","重试","创建分支"};
  for(int i=0;i<3;++i) {
    struct Press {lv_point_t start{};int distance=0;bool held=false;};
    auto state=std::make_shared<Press>();
    const std::string action=names[i],title=titles[i];
    buttons[i]=icon(bar,action,i*uiMetrics_.dp(48),uiMetrics_.dp(4),uiMetrics_.dp(44),uiMetrics_.dp(44),[this,state,session,id,action]{
      if(!state->held&&state->distance<=144)events_.push("reply_action",{{"session",session},{"id",id},{"action",action}});
    },theme::background,theme::muted);
    struct Handler {App *app;std::shared_ptr<Press> press;std::string session,id,title;};
    auto handler=new Handler{this,state,session,id,title};
    lv_obj_add_event_cb(buttons[i],[](lv_event_t *e){
      auto h=static_cast<Handler *>(lv_event_get_user_data(e));const auto code=lv_event_get_code(e);
      if(code==LV_EVENT_DELETE){delete h;return;}
      if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_LONG_PRESSED)return;
      auto indev=lv_indev_active();if(!indev)return;lv_point_t p;lv_indev_get_point(indev,&p);
      if(code==LV_EVENT_PRESSED){h->press->start=p;h->press->distance=0;h->press->held=false;}
      const int dx=p.x-h->press->start.x,dy=p.y-h->press->start.y;
      h->press->distance=std::max(h->press->distance,dx*dx+dy*dy);
      if(code==LV_EVENT_LONG_PRESSED&&h->press->distance<=144) {
        h->press->held=true;h->app->events_.push("reply_hint",{{"session",h->session},{"id",h->id},{"text",h->title}});
      }
    },LV_EVENT_ALL,handler);
  }
  if(!question)label(bar,"无对应问题，无法重试",uiMetrics_.dp(152),uiMetrics_.dp(15),width-uiMetrics_.dp(156),uiMetrics_.fontSize(UiFont::FormHint),theme::muted);
  lv_obj_t *previous=nullptr,*next=nullptr;
  if(total>1) {
    auto change=[this,id,session](const char *action){events_.push("reply_action",{{"session",session},{"id",id},{"action",action}});};
    previous=icon(bar,"chevron-left",width-uiMetrics_.dp(148),uiMetrics_.dp(4),uiMetrics_.dp(44),uiMetrics_.dp(44),[change]{change("previous");},theme::background,theme::muted);
    auto count=label(bar,std::to_string(ordinal)+" / "+std::to_string(total),width-uiMetrics_.dp(104),uiMetrics_.dp(16),uiMetrics_.dp(60),uiMetrics_.fontSize(UiFont::Detail),theme::muted);
    lv_label_set_long_mode(count,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_height(count,font(uiMetrics_.fontSize(UiFont::Detail))->line_height);lv_obj_set_style_text_align(count,LV_TEXT_ALIGN_CENTER,0);
    next=icon(bar,"chevron-right",width-uiMetrics_.dp(44),uiMetrics_.dp(4),uiMetrics_.dp(44),uiMetrics_.dp(44),[change]{change("next");},theme::background,theme::muted);
  }
  replyControls_[id]={bar,buttons[0],buttons[1],buttons[2],previous,next,!replyText(message).empty(),question,ordinal,total};
  auto owner=new std::pair<App *,std::string>(this,id);
  lv_obj_add_event_cb(bar,[](lv_event_t *e){
    auto p=static_cast<std::pair<App *,std::string> *>(lv_event_get_user_data(e));auto &map=p->first->replyControls_;
    auto i=map.find(p->second);if(i!=map.end()&&i->second.bar==lv_event_get_target_obj(e))map.erase(i);delete p;
  },LV_EVENT_DELETE,owner);
}
void App::syncReplyControls() {
  const bool busy=generating_||contextBusy_||replyBusy_||imageStoreBusy_||saveInFlight_||messageEditBusy_||!messageEdit_.empty();
  const bool previousLayout = chatLayout_;
  const bool deferLayout = readerInteracting();
  bool changed = false;
  auto enable=[](lv_obj_t *o,bool yes){if(lv_obj_has_state(o,LV_STATE_DISABLED)==!yes)return;if(yes)lv_obj_remove_state(o,LV_STATE_DISABLED);else lv_obj_add_state(o,LV_STATE_DISABLED);lv_obj_set_style_opa(o,yes?LV_OPA_COVER:LV_OPA_40,0);};
  for(auto &entry:replyControls_) {
    auto &c=entry.second;
    const bool hide = entry.first == assistant_ && receivingReply_;
    if (lv_obj_is_hidden(c.bar) != hide && !deferLayout) {
      chatLayout_ = true;
      lv_obj_set_hidden(c.bar, hide); changed = true;
    }
    const bool visible = !lv_obj_is_hidden(c.bar);
    enable(c.copy,visible&&(entry.first==assistant_? !output_.empty():c.body));
    enable(c.retry,visible&&!busy&&c.question);enable(c.branch,visible&&!busy);
    if(c.previous)enable(c.previous,visible&&!busy&&c.ordinal>1);
    if(c.next)enable(c.next,visible&&!busy&&c.ordinal<c.total);
  }
  if (changed && !previousLayout && body_ && messageContent_) {
    lv_obj_update_layout(messageContent_);
    lv_obj_scroll_to_y(body_, chatFollowing_ ? LV_COORD_MAX : chatScroll_, LV_ANIM_OFF);
    chatScroll_ = std::max(0, int(lv_obj_get_scroll_y(body_)));
  }
  chatLayout_ = previousLayout;
  if (changed && !previousLayout) {updateChatControls(true);paintVisible();}
}
void App::retryWarning(const std::string &id) {
  if(generating_||contextBusy_||replyBusy_||imageStoreBusy_||saveInFlight_||messageEditBusy_||!messageEdit_.empty())return;
  auto rows=db_.query("SELECT seq FROM chat_messages WHERE id=? AND session_id=? AND role='assistant'",{id,session_});
  if(rows.empty())return;
  auto user=db_.query("SELECT id FROM chat_messages WHERE session_id=? AND role='user' AND seq<? AND (length(trim(coalesce(output_text,'')))>0 OR message_type='image') LIMIT 1",{session_,rows[0]["seq"]});
  if(user.empty()){replyToast("没有对应的用户问题，无法重试");return;}
  auto model=currentModel();
  if(model.empty()){modal("请选择可用模型","请先选择聊天模型，再重新生成回复。");return;}
  model["system_prompt"]=sessionPreferences_.value("system_prompt",std::string());
  if(!db_.settings().value("retry_archive_prompt",false)||!db_.replyHasLater(session_,id)) {
    sendActionAt_=nowMs();startContext(false,id,true,model);return;
  }
  auto list=contextMenu("retry",3);
  replyConfirmId_=id;replyConfirmModel_=model;
  lv_obj_add_event_cb(modal_,[](lv_event_t *e){
    if(lv_event_get_target_obj(e)!=lv_event_get_current_target_obj(e))return;
    auto self=static_cast<App *>(lv_event_get_user_data(e));self->replyConfirmId_.clear();self->replyConfirmModel_=Json::object();
  },LV_EVENT_DELETE,this);
  const int row=layout().portrait()?uiMetrics_.popup().portraitRow:uiMetrics_.popup().row;
  const char *labels[]={"取消","归档当前聊天并重试","直接重试"};
  for(int i=0;i<3;++i) {
    auto item=button(list,labels[i],0,i*row,lv_obj_get_width(list),row-uiMetrics_.dp(4),[this,id,i]{
      if(!i){closeHistoryContext();return;}
      events_.push("reply_retry_confirm",{{"session",session_},{"id",id},{"archive",i==1}});
    },theme::popup,LV_TEXT_ALIGN_LEFT);stylePopupRow(item);
  }
}
void App::selectReply(const std::string &id,int ordinal) {
  if(generating_||contextBusy_||replyBusy_||saveInFlight_||messageEditBusy_||!messageEdit_.empty())return;
  auto control=replyControls_.find(id);if(control==replyControls_.end())return;
  lv_area_t area;lv_obj_get_coords(lv_obj_get_parent(control->second.bar),&area);
  replyAnchorY_=area.y1;replyAnchorScroll_=std::max(0,int(lv_obj_get_scroll_y(body_)));
  replyBusy_=true;replyToken_=uid("reply-select");const auto token=replyToken_,session=session_;
  if(replyWorker_.joinable())replyWorker_.join();
  replyWorker_=std::thread([this,id,ordinal,token,session]{
    try{db_.flush();db_.selectReplyResult(session,id,ordinal);events_.push("reply_select_done",{{"token",token},{"session",session},{"id",id}});}
    catch(...){events_.push("reply_select_done",{{"token",token},{"session",session},{"error",true}});}
  });
}
void App::branchReply(const std::string &id) {
  if(generating_||contextBusy_||replyBusy_||imageStoreBusy_||saveInFlight_||messageEditBusy_||!messageEdit_.empty())return;
  auto preferences=sessionPreferences_,model=currentModel();
  if(!model.empty()) {
    preferences["model_id"]=model["id"];const auto effort=model.value("effective_effort",std::string());preferences["effort"]=effort.empty()?"api_default":effort;
  }
  closeAttachments();resetPull();replyCancel_=false;replyBusy_=true;replyToken_=uid("branch");
  modal("正在创建分支","正在复制到所选回复，并移动草稿和待发送图片…");
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(modal_,-1),0),"取消");
  // Deleting this progress panel requests cancellation, without waiting on SQLite.
  lv_obj_add_event_cb(modal_,[](lv_event_t *e){auto self=static_cast<App *>(lv_event_get_user_data(e));if(self->replyBusy_)self->replyCancel_=true;},LV_EVENT_DELETE,this);
  if(replyWorker_.joinable())replyWorker_.join();
  const auto token=replyToken_,session=session_;
  replyWorker_=std::thread([this,preferences,id,token,session]{
    bool leased=false;
    try {
      db_.flush();auto snapshot=db_.contextSnapshot(session,true);leased=snapshot.value("leased",false);
      auto branch=db_.createBranch(snapshot,id,preferences,replyCancel_);
      if(leased){db_.releaseTransferSnapshot();leased=false;}
      events_.push("reply_branch_done",{{"token",token},{"session",session},{"result",branch}});
    }catch(...){if(leased)db_.releaseTransferSnapshot();events_.push("reply_branch_done",{{"token",token},{"session",session},{"cancelled",bool(replyCancel_)},{"error","分支未创建，原会话和未发送内容保持完整，请检查保存状态或存储空间。"}});}
  });
}
bool App::replyEvent(const Event &e) {
  if(e.type=="reply_select_done") {
    if(e.data.value("token",std::string())!=replyToken_)return true;
    if(replyWorker_.joinable())replyWorker_.join();replyBusy_=false;
    if(e.data.value("session",std::string())!=session_)return true;
    if(e.data.contains("error")){replyToast("结果切换失败，原选择仍保留");return true;}
    assistant_.clear();output_.clear();reasoning_.clear();reasoningMetadata_=Json::object();
    expandedReasoning_.clear();reasoningOverrides_.clear();tableScrolls_.clear();
    chatFollowing_=false;chatScroll_=replyAnchorScroll_;renderMessages(false);
    const auto id=e.data.value("id",std::string());auto found=replyControls_.find(id);
    if(found!=replyControls_.end()) {
      lv_obj_update_layout(messageContent_);lv_area_t area;lv_obj_get_coords(lv_obj_get_parent(found->second.bar),&area);
      lv_obj_scroll_to_y(body_,std::max(0,replyAnchorScroll_+area.y1-replyAnchorY_),LV_ANIM_OFF);
      chatScroll_=std::max(0,int(lv_obj_get_scroll_y(body_)));updateChatControls(true);paintVisible();
    }
    return true;
  }
  if(e.type=="reply_branch_done") {
    if(e.data.value("token",std::string())!=replyToken_)return true;
    if (replyWorker_.joinable()) replyWorker_.join();
    replyBusy_ = false;
    if(e.data.value("session",std::string())!=session_)return true;
    if(e.data.contains("error")){build();if(!e.data.value("cancelled",false))modal("无法创建分支",e.data["error"]);return true;}
    sessionDrafts_.erase(session_);
    session_=e.data.at("result").at("id").get<std::string>();sessionPreferences_=db_.sessionPreferences(session_);
    // draft_ deliberately moves with the active page; no attach()/auto-send path.
    assistant_.clear();output_.clear();reasoning_.clear();reasoningMetadata_=Json::object();
    expandedReasoning_.clear();reasoningOverrides_.clear();tableScrolls_.clear();
    messageOffset_=0;chatScroll_=0;chatFollowing_=true;pullNeedsPause_=pullTrackActivity_=false;
    show("chat");return true;
  }
  if(e.type=="reply_retry_confirm") {
    if(visible_&&modal_&&e.data.value("session",std::string())==session_&&e.data.value("id",std::string())==replyConfirmId_) {
      auto model=replyConfirmModel_;const auto id=replyConfirmId_;
      model["_retry_archive"]=e.data.value("archive",false);closeHistoryContext();
      sendActionAt_=nowMs();startContext(false,id,true,model);
    }
    return true;
  }
  if(e.type=="reply_action"||e.type=="reply_hint") {
    if(!visible_||view_!="chat"||keyboard_||editor_.active||imageViewer_||navigationOpen_||modal_||e.data.value("session",std::string())!=session_)return true;
    const auto id=e.data.value("id",std::string());
    if(id==assistant_&&receivingReply_)return true;
    if(e.type=="reply_hint"){replyToast(e.data.value("text",std::string()));return true;}
    const auto action=e.data.value("action",std::string());
    input_.cancel();
    if(action=="copy") {
      auto rows=db_.query("SELECT output_text FROM chat_messages WHERE id=? AND session_id=? AND role='assistant'",{id,session_});
      if (rows.empty()) return true;
      auto text=id==assistant_?output_:replyText(rows[0]);
      if(!text.empty()){if(editorClipboard_.copy(text))replyToast("已复制");else modal("无法复制","回复超过剪贴板容量，可通过导出聊天保存完整内容。");}
    }else if(action=="previous"||action=="next") {
      auto row=replyChoiceRows_.value(id,Json::object());selectReply(id,row.value("ordinal",1)+(action=="next"?1:-1));
    }else if(action=="retry")retryWarning(id);
    else if(action=="branch")branchReply(id);
    return true;
  }
  return false;
}
Json App::replyDiagnostic() const {
  Json rows=Json::object();
  auto bounds=[](lv_obj_t *o){lv_area_t a;lv_obj_get_coords(o,&a);return Json::array({a.x1,a.y1,a.x2,a.y2});};
  for(const auto &p:replyControls_)rows[p.first]={{"bar",bounds(p.second.bar)},{"copy",bounds(p.second.copy)},{"retry",bounds(p.second.retry)},{"branch",bounds(p.second.branch)},
    {"ordinal",p.second.ordinal},{"total",p.second.total},{"previous",p.second.previous?bounds(p.second.previous):Json()},{"next",p.second.next?bounds(p.second.next):Json()},{"visible",!lv_obj_is_hidden(p.second.bar)},{"message",bounds(lv_obj_get_parent(p.second.bar))},
    {"copy_enabled",!lv_obj_has_state(p.second.copy,LV_STATE_DISABLED)},{"retry_enabled",!lv_obj_has_state(p.second.retry,LV_STATE_DISABLED)},{"branch_enabled",!lv_obj_has_state(p.second.branch,LV_STATE_DISABLED)}};
  return {{"rows",rows},{"busy",replyBusy_},{"confirm",replyConfirmId_},{"regenerating",contextRegenerating_}};
}
} // namespace penchat
