#include "app.hpp"
#include <stdexcept>
namespace penchat {
void App::initializeUpdates() {
  updater_.reset(new Updater(events_,dataDir_));updateState_=updater_->state();
  const auto recovery=updater_->recovery();
  if(recovery.contains("snapshot")&&recovery["snapshot"].is_object()) {
    const auto &s=recovery["snapshot"];
    if(s.value("session",Json()).is_string()&&s.value("draft",Json()).is_string()&&s["session"].get<std::string>().size()<128&&s["draft"].get<std::string>().size()<=128*1024) {
      session_=s["session"];draft_=s["draft"];
      sessionPreferences_=s.value("preferences",Json::object());
      if(!sessionPreferences_.is_object())sessionPreferences_=Json::object();
      chatScroll_=std::max(0,s.value("scroll",0));messageOffset_=std::max(0,s.value("offset",0));chatFollowing_=s.value("following",true);
      updater_->request("restore_ack");
    }
  }
}
Json App::updateSnapshot() const {
  return {{"session",session_},{"draft",draft_},{"preferences",sessionPreferences_},
    {"scroll",chatScroll_},{"offset",messageOffset_},{"following",chatFollowing_}};
}
void App::openUpdate(bool manual) {
  if(view_!="update")updateOrigin_=view_=="settings"?"settings":"chat";
  if(view_=="chat"&&body_)chatScroll_=std::max(0,int(lv_obj_get_scroll_y(body_)));
  updateSnapshot_=updateSnapshot();show("update");
  if(manual)updater_->request("check",{{"manual",true}});
}
void App::beginUpdate() {
  const auto status=updateState_.value("status",std::string());if(status!="available"&&status!="error")return;
  if(generating_||contextBusy_||imageStoreBusy_||editor_.active||keyboard_||!messageEdit_.empty()||saveInFlight_||persistDirty_){modal("暂时无法更新","请先结束生成、输入或图片处理，再开始更新。");return;}
  updateSnapshot_=updateSnapshot();
  updater_->request("download",{{"allow_weak",updateState_.value("release",Json::object()).value("weak",false)},{"snapshot",updateSnapshot_}});
}
void App::updateBack() {
  const auto status=updateState_.value("status",std::string());
  if(status=="ready"||status=="installing")return;
  if(status=="downloading"||status=="verifying"){updateReturning_=true;updater_->cancel(true);return;}
  if(status=="checking")updater_->cancel();
  if(status=="available")updater_->request("decline");
  show(updateOrigin_);
}
void App::updateEvent(const Event &event) {
  if(event.type=="update_prepared") {
    if(updatePrepareWorker_.joinable())updatePrepareWorker_.join();
    if(event.data.value("operation",std::string())!=updatePreparing_)return;
    updater_->request(event.data.value("success",false)?"install_requested":"install_result",event.data);
    return;
  }
  const auto old=updateState_;updateState_=event.data;
  if(updateReturning_&&(updateState_.value("status",std::string())=="deferred"||updateState_.value("status",std::string())=="cancelled")) {
    updateReturning_=false;show(updateOrigin_);return;
  }
  if(view_=="update") {
    const bool layout=old.value("status",std::string())!=updateState_.value("status",std::string())||
      old.value("note_index",size_t(0))!=updateState_.value("note_index",size_t(0))||old.value("note_page",size_t(0))!=updateState_.value("note_page",size_t(0))||
      old.value("note_text",std::string())!=updateState_.value("note_text",std::string());
    if(layout)updateDirty_=true;
    const auto p=updateState_.value("progress",Json::object());
    const auto bytes=p.value("downloaded",uint64_t(0)),total=p.value("total",uint64_t(0));
    if(updateProgress_&&total)lv_bar_set_value(updateProgress_,int(std::min<uint64_t>(100,bytes*100/total)),LV_ANIM_OFF);
    if(updateProgressText_){const auto text=std::to_string(total?bytes*100/total:0)+"% · "+std::to_string(bytes/1024)+" / "+std::to_string(total/1024)+" KiB";lv_label_set_text(updateProgressText_,text.c_str());}
    if(updateTransferText_){const auto text=std::to_string(p.value("speed",uint64_t(0))/1024)+" KiB/s · "+std::to_string(p.value("lanes",0))+" 路 · "+std::to_string(p.value("nodes",1))+" 个节点";lv_label_set_text(updateTransferText_,text.c_str());}
  }
}
void App::updateTick() {
  const auto status=updateState_.value("status",std::string());
  const auto release=updateState_.value("release",Json::object());
  if(status=="available"&&!updateState_.value("manual",false)&&!updateState_.value("suppressed",false)&&
     release.value("version",std::string())!=updateOffered_&&view_=="chat"&&visible_&&!editor_.active&&!keyboard_&&!generating_&&!contextBusy_&&
     !titleBusy_&&!(transfer_&&transfer_->busy())&&!persistDirty_&&!saveInFlight_&&!imageStoreBusy_&&!navigationOpen_&&!modal_&&!attachments_&&!imageViewer_&&!touchDown_&&!readerInteracting()&&
     messageEdit_.empty()&&!replyBusy_&&!contextRegenerating_&&!scanId_&&
     scanner_.state().value("type",std::string())!="started"&&scanner_.state().value("type",std::string())!="progress"&&scanner_.state().value("type",std::string())!="processing") {
    updateOffered_=release.value("version",std::string());openUpdate(false);
  }
  if(updateDirty_&&view_=="update"&&!touchDown_&&!modal_){updateDirty_=false;build();}
  if(status=="ready"&&visible_) {
    const auto op=updateState_.at("ticket").at("operation").get<std::string>();
    if(updatePreparing_!=op) {
      updatePreparing_=op;
      if(updatePrepareWorker_.joinable())updatePrepareWorker_.join();
      updatePrepareWorker_=std::thread([this,op]{try{db_.flush();events_.push("update_prepared",{{"operation",op},{"success",true}});}catch(...){events_.push("update_prepared",{{"operation",op},{"success",false}});}});
    }
  }
  if(status=="installing"&&visible_) {
    const auto ticket=updateState_.at("ticket");const auto op=ticket.at("operation").get<std::string>();
    if(updateSent_!=op) {
      updateSent_=op;
      shutdown();
      bridge_.send("update.install",ticket);
      updateExit_=true;
    }
  }
}
void App::updatePage() {
  const int width=screen_.width-118,column=(width-16)/2;
  pageHeader("软件更新");
  auto action=[&](const std::string &key,const std::string &text,int x,int y,int w,std::function<void()> callback) {
    auto b=button(pageContent_,text,x,y,w,48,std::move(callback));updateActions_[key]=b;return b;
  };
  const auto state=updateState_.value("status",std::string("idle"));const auto release=updateState_.value("release",Json::object());
  label(pageContent_,std::string("当前版本 ")+PENCHAT_VERSION+(release.empty()?"":" → "+release.value("version",std::string())),10,4,width,20);
  auto message=label(pageContent_,updateState_.value("message",std::string()),10,36,width,18,theme::muted);lv_obj_update_layout(message);
  int y=44+lv_obj_get_height(message);
  if(state=="downloading"||state=="verifying"||state=="ready"||state=="installing") {
    updateProgress_=lv_bar_create(pageContent_);lv_obj_set_pos(updateProgress_,10,y+6);lv_obj_set_size(updateProgress_,width,14);
    lv_obj_set_style_bg_color(updateProgress_,lv_color_hex(theme::accent),LV_PART_INDICATOR);lv_bar_set_range(updateProgress_,0,100);
    const auto p=updateState_.value("progress",Json::object());const auto done=p.value("downloaded",uint64_t(0)),total=p.value("total",uint64_t(0));
    lv_bar_set_value(updateProgress_,total?int(std::min<uint64_t>(100,done*100/total)):0,LV_ANIM_OFF);
    updateProgressText_=label(pageContent_,std::to_string(total?done*100/total:0)+"% · "+std::to_string(done/1024)+" / "+std::to_string(total/1024)+" KiB",10,y+32,width,20);
    updateTransferText_=label(pageContent_,std::to_string(p.value("speed",uint64_t(0))/1024)+" KiB/s · "+std::to_string(p.value("lanes",0))+" 路 · "+std::to_string(p.value("nodes",1))+" 个节点",10,y+62,width,18,theme::muted);
    if(state=="downloading"||state=="verifying")action("cancel","取消下载",10,y+98,column,[this]{updateBack();});
    label(pageContent_,"下载与校验成功后将自动安装。取消后，下一个版本再提醒。",10,y+160,width,16,theme::muted);
    return;
  }
  if(state=="available"||(state=="error"&&!release.empty())) {
    action("download",release.value("weak",false)?"仍然下载并更新":"立即下载并更新",10,y,column,[this]{beginUpdate();});
    action("later","稍后更新",26+column,y,column,[this]{updateBack();});y+=64;
  } else {
    action("check","重新检查",10,y,column,[this]{updater_->request("check",{{"manual",true}});});
    action("back","返回",26+column,y,column,[this]{updateBack();});y+=64;
  }
  if(updateState_.contains("ticket")&&state!="installed") {
    action("retry_install","重试已下载的安装包",10,y,width,[this]{updater_->request("retry_install",{{"snapshot",updateSnapshot()}});});y+=64;
  }
  const auto notes=updateState_.value("notes",Json::array());const auto index=updateState_.value("note_index",size_t(0)),page=updateState_.value("note_page",size_t(0));
  if(!notes.empty()&&index<notes.size()) {
    label(pageContent_,notes[index].value("version",std::string())+" 更新说明 · "+std::to_string(index+1)+"/"+std::to_string(notes.size()),10,y,width,20);y+=32;
    const int cell=(width-24)/4;
    auto prev=action("note_previous","上一版本",10,y,cell,[this,index]{updater_->request("note",{{"index",index-1},{"page",0}});});
    auto next=action("note_next","下一版本",18+cell,y,cell,[this,index]{updater_->request("note",{{"index",index+1},{"page",0}});});
    auto left=action("page_previous","上一页",26+2*cell,y,cell,[this,index,page]{updater_->request("note",{{"index",index},{"page",page-1}});});
    auto right=action("page_next","下一页",34+3*cell,y,cell,[this,index,page]{updater_->request("note",{{"index",index},{"page",page+1}});});
    if(!index)lv_obj_add_state(prev,LV_STATE_DISABLED);
    if(index+1>=notes.size())lv_obj_add_state(next,LV_STATE_DISABLED);
    if(!page)lv_obj_add_state(left,LV_STATE_DISABLED);
    if(page+1>=updateState_.value("note_pages",size_t(1)))lv_obj_add_state(right,LV_STATE_DISABLED);
    y+=64;auto content=box(pageContent_,10,y,width,LV_SIZE_CONTENT);lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(content,8,0);
    richMessage(content,updateState_.value("note_text",std::string()),width,false,"release-note");
  }
}
}
