#include "app.hpp"
#include "reasoning.hpp"
#include <unistd.h>
#include <dirent.h>
#include <regex>
#include <sys/stat.h>
namespace penchat {
namespace {
std::string messageText(const Json &row) {
  const auto i=row.find("output_text");
  return i!=row.end()&&i->is_string()?i->get<std::string>():std::string();
}
}
void App::initializeMessageEdits() {
  const auto path=join(dataDir_,".message-edit");
  messageEditWorker_=std::thread([path]{
    struct stat st{};if(lstat(path.c_str(),&st)||!S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode))return;
    auto dir=opendir(path.c_str());if(!dir)return;
    const std::regex owned("image_[0-9]+_[0-9]+\\.png(\\.part)?");
    while(auto entry=readdir(dir)) {
      const std::string name=entry->d_name;if(!std::regex_match(name,owned))continue;
      const auto file=join(path,name);
      if(!lstat(file.c_str(),&st)&&S_ISREG(st.st_mode)&&!S_ISLNK(st.st_mode))unlink(file.c_str());
    }
    closedir(dir);
  });
}
void App::messageLongPress(lv_obj_t *object, const std::string &id) {
  struct Press {App *app; std::string session,id; lv_point_t start{}; int distance=0;};
  auto state=new Press{this,session_,id};
  userBubbles_[id]=object;
  lv_obj_set_clickable(object,true);
  lv_obj_add_event_cb(object,[](lv_event_t *e) {
    auto p=static_cast<Press *>(lv_event_get_user_data(e));
    const auto code=lv_event_get_code(e);
    if(code==LV_EVENT_DELETE) {
      auto &rows=p->app->userBubbles_;auto i=rows.find(p->id);
      if(i!=rows.end()&&i->second==lv_event_get_target_obj(e))rows.erase(i);
      delete p;return;
    }
    if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_LONG_PRESSED)return;
    auto indev=lv_indev_active();if(!indev)return;
    lv_point_t point;lv_indev_get_point(indev,&point);
    if(code==LV_EVENT_PRESSED){p->start=point;p->distance=0;}
    int dx=point.x-p->start.x,dy=point.y-p->start.y;
    p->distance=std::max(p->distance,dx*dx+dy*dy);
    if(code==LV_EVENT_LONG_PRESSED&&p->distance<=144)
      p->app->events_.push("message_menu",{{"session",p->session},{"id",p->id}});
  },LV_EVENT_ALL,state);
}
void App::messageMenu(const std::string &id) {
  auto rows=db_.query("SELECT output_text FROM chat_messages WHERE session_id=? AND id=? AND role='user'",{session_,id});
  if(rows.empty())return;
  closeAttachments();resetPull();input_.cancel();
  modal("消息","");messageContextId_=id;
  auto layer=modal_;messageActions_.clear();
  lv_obj_add_event_cb(layer,[](lv_event_t *e){auto app=static_cast<App *>(lv_event_get_user_data(e));app->messageActions_.clear();app->messageContextId_.clear();},LV_EVENT_DELETE,this);
  // One action per row. The close control stays visible in the right column.
  auto list=box(layer,12,42,screen_.width-238,screen_.height-54,theme::surface);
  lv_obj_set_scroll_dir(list,LV_DIR_VER);
  const auto text=messageText(rows[0]);
  const std::vector<std::pair<std::string,std::string>> actions={{"copy","复制"},{"select","选择文本"},{"edit","编辑消息"}};
  int y=0;
  for(const auto &a:actions) {
    auto b=button(list,a.second,0,y,screen_.width-254,48,[this,id,key=a.first]{events_.push("message_action",{{"session",session_},{"id",id},{"action",key}});},theme::background,LV_TEXT_ALIGN_LEFT);
    messageActions_[a.first]=b;y+=54;
    if((a.first!="edit"&&text.empty())||(a.first=="edit"&&(generating_||contextBusy_||saveInFlight_||!messageEdit_.empty())))lv_obj_add_state(b,LV_STATE_DISABLED);
  }
}
void App::selectMessageText(const std::string &text) {
  const auto session=session_;const auto offset=messageOffset_;const auto scroll=chatScroll_;const bool follow=chatFollowing_;
  edit(text,"选择消息文本",[](const std::string &){},std::max(8192,int(editLength(text))),"EnUSOnly",
       [this,session,offset,scroll,follow]{if(session!=session_)return;messageOffset_=offset;chatScroll_=scroll;chatFollowing_=follow;build();},true,false,true);
}
void App::messageEditWarning(const std::string &id) {
  if(generating_||contextBusy_||imageStoreBusy_||saveInFlight_||messageEditBusy_||!messageEdit_.empty())return;
  auto mode=db_.settings().value("message_edit_default",std::string("ask"));
  if(mode=="archive"||mode=="overwrite"){beginMessageEdit(id,mode=="archive");return;}
  modal("从这条消息继续", "发送修改后的消息时，这条消息及后续对话将被覆盖。发送前可随时取消；也可以先保留一份归档。");
  auto close=lv_obj_get_child(modal_,-1);lv_label_set_text(lv_obj_get_child(close,0),"取消");
  const int width=(screen_.width-262)/2;
  button(modal_,"归档当前聊天并继续",14,screen_.height-66,width,44,[this,id]{events_.push("message_begin",{{"session",session_},{"id",id},{"archive",true}});});
  button(modal_,"继续",26+width,screen_.height-66,width,44,[this,id]{events_.push("message_begin",{{"session",session_},{"id",id},{"archive",false}});});
}
void App::messageEditDefaultPicker() {
  modal("编辑消息", "仅在点击发送时覆盖后续对话。取消编辑不会修改原记录。选择默认处理方式后，编辑前不再询问。");
  auto selected=db_.settings().value("message_edit_default",std::string("ask"));
  const std::vector<std::pair<std::string,std::string>> modes={{"ask","每次询问"},{"archive","发送时先归档"},{"overwrite","发送时直接覆盖"}};
  const int w=(screen_.width-162)/3;
  for(size_t i=0;i<modes.size();++i) {
    auto b=button(modal_,modes[i].second,16+i*(w+12),screen_.height-120,w,44,[this,key=modes[i].first]{db_.setting("message_edit_default",key);build();});
    if(modes[i].first==selected)primary(b);
  }
}
void App::beginMessageEdit(const std::string &id, bool archive) {
  if(generating_||contextBusy_||imageStoreBusy_||messageEditBusy_||!messageEdit_.empty())return;
  messageEditArchive_=archive;messageEditToken_=uid("message-edit");
  messageEditOffset_=messageOffset_;messageEditScroll_=chatScroll_;messageEditFollowing_=chatFollowing_;
  messageEditPreferences_=sessionPreferences_;messageEditMemory_=effortMemory_;
  messageEditBusy_=true;
  modal("准备编辑", "正在读取原消息和图片…");
  messagePrepareModal_=modal_;
  lv_obj_add_event_cb(modal_,[](lv_event_t *e){static_cast<App *>(lv_event_get_user_data(e))->messagePrepareModal_=nullptr;},LV_EVENT_DELETE,this);
  // Preparation may be cancelled. A late snapshot is released, never applied.
  lv_label_set_text(lv_obj_get_child(lv_obj_get_child(modal_,-1),0),"取消");
  if(messageEditWorker_.joinable())messageEditWorker_.join();
  const auto token=messageEditToken_,session=session_;
  messageEditWorker_=std::thread([this,token,session,id]{
    try {db_.flush();auto snapshot=db_.messageEditSnapshot(session,id);events_.push("message_prepared",{{"token",token},{"session",session},{"snapshot",std::move(snapshot)}});}
    catch(...){events_.push("message_prepared",{{"token",token},{"session",session},{"error","无法读取原消息，请检查保存状态后重试"}});}
  });
}
void App::cancelMessageEdit(bool restore) {
  if(messageEditBusy_)return;
  if(messageEdit_.empty())return;
  if(messageEdit_.value("leased",false))db_.releaseTransferSnapshot();
  messageEdit_=Json::object();messageEditImages_=Json::array();messageEditCursors_.clear();
  for(const auto &path:messageEditFiles_) {unlink(path.c_str());unlink((path+".part").c_str());}
  messageEditFiles_.clear();messageEditToken_.clear();
  sessionPreferences_=messageEditPreferences_;effortMemory_=messageEditMemory_;draft_.clear();
  if(restore){messageOffset_=messageEditOffset_;chatScroll_=messageEditScroll_;chatFollowing_=messageEditFollowing_;show("chat");}
}
void App::commitMessageEdit() {
  if(messageEdit_.empty()||messageEditBusy_||imageStoreBusy_||generating_||contextBusy_)return;
  if(currentModel().empty()){modal("请选择可用模型","请先选择聊天模型，再发送修改后的消息。");return;}
  messageEditBusy_=true;sendActionAt_=nowMs();
  modal("正在更新会话",messageEditArchive_?"正在保存完整归档并更新消息…":"正在更新消息…");
  lv_obj_set_hidden(lv_obj_get_child(modal_,-1),true);
  if(messageEditWorker_.joinable())messageEditWorker_.join();
  const auto snapshot=messageEdit_,images=messageEditImages_,preferences=sessionPreferences_;
  const auto text=draft_,token=messageEditToken_;const bool archive=messageEditArchive_;const auto sentAt=sendActionAt_;
  messageEditWorker_=std::thread([this,snapshot,images,preferences,text,token,archive,sentAt]{
    try{auto result=db_.commitMessageEdit(snapshot,images,text,preferences,archive,sentAt);events_.push("message_committed",{{"token",token},{"result",result}});}
    catch(...){events_.push("message_committed",{{"token",token},{"error","未能保存修改，原会话保持完整。请检查存储空间或取消后重新编辑。"}});}
  });
}
Json App::editedMessages() {
  const auto pivot=messageEdit_.at("pivot").get<int64_t>();
  if(messageOffset_==0){messageEditCursors_.clear();messageEditCursors_.push_back(pivot);}
  const size_t page=std::max(0,messageOffset_)/30;
  while(messageEditCursors_.size()<=page) {
    auto rows=db_.messagesBefore(session_,messageEditCursors_.back(),30);
    messageEditCursors_.push_back(rows.empty()?int64_t(0):rows.front()["seq"].get<int64_t>());
  }
  auto rows=db_.messagesBefore(session_,messageEditCursors_[page],30);
  if(page==0)for(const auto &asset:messageEditImages_) {
    auto row=asset;row["id"]=asset.at("edit_id");row["role"]="user";row["seq"]=pivot;row["output_text"]="";row["edit_virtual"]=true;
    rows.push_back(std::move(row));
  }
  return rows;
}
bool App::messageEvent(const Event &e) {
  if(e.type=="message_prepared") {
    if (messageEditWorker_.joinable()) messageEditWorker_.join();
    messageEditBusy_ = false;
    if(e.data.value("token",std::string())!=messageEditToken_||e.data.value("session",std::string())!=session_||!messagePrepareModal_||modal_!=messagePrepareModal_||!visible_||editor_.active) {
      if(e.data.contains("snapshot")&&e.data["snapshot"].value("leased",false))db_.releaseTransferSnapshot();
      if(messagePrepareModal_&&modal_==messagePrepareModal_)lv_obj_delete(modal_);
      messageEditToken_.clear();return true;
    }
    if(e.data.contains("error")){modal("无法编辑",e.data["error"]);return true;}
    messageEdit_=e.data.at("snapshot");messageEditImages_=messageEdit_.at("images");
    messageOffset_=0;chatFollowing_=true;chatScroll_=0;
    const auto token=messageEditToken_;const auto text=messageEdit_.value("text",std::string());
    show("chat");
    edit(text,"编辑消息 · 完成后仍需点击发送",[this,token](const std::string &s){if(token!=messageEditToken_)return;draft_=s;build();},std::max(8192,int(editLength(text))),"ZhCNPreferred",[this,token]{if(token==messageEditToken_)cancelMessageEdit();},true);
    return true;
  }
  if(e.type=="message_committed") {
    if (messageEditWorker_.joinable()) messageEditWorker_.join();
    messageEditBusy_ = false;
    if(e.data.value("token",std::string())!=messageEditToken_)return true;
    if(e.data.contains("error")){modal("修改未保存",e.data["error"]);return true;}
    const auto prefs=sessionPreferences_;const auto memory=effortMemory_;
    cancelMessageEdit(false);sessionPreferences_=prefs;effortMemory_=memory;
    if(prefs.value("model_id",std::string())!=messageEditPreferences_.value("model_id",std::string()))db_.selectModel(prefs.value("model_id",std::string()));
    assistant_.clear();output_.clear();reasoning_.clear();reasoningOverrides_.clear();tableScrolls_.clear();
    messageOffset_=0;chatFollowing_=true;draft_.clear();show("chat");startContext(false);return true;
  }
  if(e.type=="message_menu") {
    if(view_=="chat"&&visible_&&!keyboard_&&!editor_.active&&!modal_&&!navigationOpen_&&!imageViewer_&&!messageEditBusy_&&e.data.value("session",std::string())==session_)
      messageMenu(e.data.value("id",std::string()));
    return true;
  }
  if(e.type=="message_action") {
    const auto id=e.data.value("id",std::string());
    if(!modal_||id!=messageContextId_||e.data.value("session",std::string())!=session_)return true;
    auto rows=db_.query("SELECT output_text FROM chat_messages WHERE session_id=? AND id=? AND role='user'",{session_,id});
    if(rows.empty())return true;
    const auto text=messageText(rows[0]),action=e.data.value("action",std::string());
    input_.cancel();auto layer=modal_;lv_obj_delete(layer);
    if(action=="copy"){if(editorClipboard_.copy(text)) {
      auto toast=box(screenObj_,screen_.width/2-70,screen_.height-116,140,44,theme::surface);
      lv_obj_set_scrollable(toast,false);lv_obj_set_clickable(toast,false);lv_obj_set_style_radius(toast,16,0);
      auto caption=label(toast,"已复制",8,8,124,20);lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);
      lv_obj_delete_delayed(toast,1200);
    }else modal("无法复制","消息过长，请选择需要的部分复制。");}
    else if(action=="select")selectMessageText(text);
    else if(action=="edit")messageEditWarning(id);
    return true;
  }
  if(e.type=="message_begin") {
    if(visible_&&modal_&&e.data.value("session",std::string())==session_)
      beginMessageEdit(e.data.value("id",std::string()),e.data.value("archive",false));
    return true;
  }
  return false;
}
Json App::messageDiagnostic() const {
  auto bounds=[](lv_obj_t *o){lv_area_t a;lv_obj_get_coords(o,&a);return Json::array({a.x1,a.y1,a.x2,a.y2});};
  Json bubbles=Json::object(),actions=Json::object();
  for(const auto &p:userBubbles_)bubbles[p.first]=bounds(p.second);
  for(const auto &p:messageActions_)actions[p.first]=bounds(p.second);
  return {{"bubbles",bubbles},{"actions",actions},{"context_id",messageContextId_},{"editing",!messageEdit_.empty()},
          {"busy",messageEditBusy_},{"pivot",messageEdit_.value("pivot",int64_t(0))},{"images",messageEditImages_.size()},{"archive",messageEditArchive_}};
}
} // namespace penchat
