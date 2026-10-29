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
  return true;
}
void App::replyToast(const std::string &text) {
  const int width=std::min(screen_.width-120,std::max(140,int(editLength(text))*20+32));
  auto toast=box(screenObj_,(screen_.width-width)/2,screen_.height-116,width,44,theme::surface);
  lv_obj_set_scrollable(toast,false);lv_obj_set_clickable(toast,false);lv_obj_set_style_radius(toast,16,0);
  auto caption=label(toast,text,8,10,width-16,18);lv_label_set_long_mode(caption,LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_height(caption,24);lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);lv_obj_delete_delayed(toast,1600);
}
void App::replyBar(lv_obj_t *parent,const Json &message,int width,bool question) {
  const auto id=message.at("id").get<std::string>(),session=session_;
  auto bar=box(parent,0,0,width,48,theme::background);lv_obj_set_scrollable(bar,false);
  lv_obj_t *buttons[3]{};
  const char *names[]={"copy","retry","branch"};
  const char *titles[]={"复制","重试","创建分支"};
  for(int i=0;i<3;++i) {
    struct Press {lv_point_t start{};int distance=0;bool held=false;};
    auto state=std::make_shared<Press>();
    const std::string action=names[i],title=titles[i];
    buttons[i]=icon(bar,action,i*48,4,44,44,[this,state,session,id,action]{
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
  if(!question)label(bar,"无对应问题，无法重试",152,15,width-156,17,theme::muted);
  replyControls_[id]={bar,buttons[0],buttons[1],buttons[2],!replyText(message).empty(),question};
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
  modal("重新生成这条回复", "这条回复及后续已发送对话将被覆盖，使用当前会话配置重新回答。草稿和待发送图片会保留，不加入本次请求。");
  replyConfirmId_=id;replyConfirmModel_=model;
  lv_obj_add_event_cb(modal_,[](lv_event_t *e){auto self=static_cast<App *>(lv_event_get_user_data(e));self->replyConfirmId_.clear();self->replyConfirmModel_=Json::object();},LV_EVENT_DELETE,this);
  auto close=lv_obj_get_child(modal_,-1);lv_label_set_text(lv_obj_get_child(close,0),"取消");
  const int width=(screen_.width-262)/2;
  for(int i=0;i<2;++i)button(modal_,i?"直接重试":"归档当前聊天并重试",14+i*(width+12),screen_.height-66,width,44,[this,id,archive=i==0]{events_.push("reply_retry_confirm",{{"session",session_},{"id",id},{"archive",archive}});});
}
void App::branchReply(const std::string &id) {
  if(generating_||contextBusy_||replyBusy_||imageStoreBusy_||saveInFlight_||messageEditBusy_||!messageEdit_.empty())return;
  auto preferences=sessionPreferences_,model=currentModel();
  if(!model.empty()) {
    preferences["model_id"]=model["id"];const auto effort=model.value("effective_effort",std::string());preferences["effort"]=effort.empty()?"api_default":effort;
  }
  closeAttachments();resetPull();replyCancel_=false;replyBusy_=true;replyToken_=uid("branch");replySession_=session_;
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
      const auto model=replyConfirmModel_;const auto id=replyConfirmId_;
      sendActionAt_=nowMs();startContext(false,id,true,e.data.value("archive",false),model);
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
    {"visible",!lv_obj_is_hidden(p.second.bar)},{"message",bounds(lv_obj_get_parent(p.second.bar))},
    {"copy_enabled",!lv_obj_has_state(p.second.copy,LV_STATE_DISABLED)},{"retry_enabled",!lv_obj_has_state(p.second.retry,LV_STATE_DISABLED)},{"branch_enabled",!lv_obj_has_state(p.second.branch,LV_STATE_DISABLED)}};
  return {{"rows",rows},{"busy",replyBusy_},{"confirm",replyConfirmId_},{"regenerating",contextRegenerating_}};
}
} // namespace penchat
