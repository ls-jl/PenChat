#include "app.hpp"
#include <unistd.h>
#include <ctime>
namespace penchat {
bool App::backupCanStart(){
  if(!visible_||!inputFocused_||!windowFocused_)return false;
  const auto state=updateState_.value("status",std::string());
  if(generating_||contextBusy_||replyBusy_||contextRegenerating_||titleBusy_||imageStoreBusy_||editor_.active||keyboard_||!messageEdit_.empty()||saveInFlight_||persistDirty_||state=="ready"||state=="installing"||state=="downloading"||state=="verifying"){
    modal("暂时无法还原","请先结束生成、编辑、图片处理或更新，再还原备份。");return false;
  }
  return true;
}
void App::openBackupRestore(){
  saveDraft();backupScreen_="loading";backupMessage_.clear();backupPlan_=Json::object();backupDecisions_=Json::object();backupAll_=false;backupResult_=Json::object();
  backupRequest("list");show("backup_restore");
}
void App::backupReturn(const Json &packet){
  openBackupRestore();const auto actual=BackupRestore::lastResult(dataDir_,packet.value("operation",std::string()));
  const auto status=actual.empty()?packet:actual;const bool ok=status.value("ok",false);
  backupResult_={{"ok",ok},{"counts",status.value("result",Json::object())}};
  if(ok){const auto counts=status.value("result",Json::object());backupMessage_="还原完成：新增 "+std::to_string(counts.value("added",size_t(0)))+" 项，替换 "+std::to_string(counts.value("replaced",size_t(0)))+" 项，跳过 "+std::to_string(counts.value("skipped",size_t(0)))+" 项。";}
  else if(status.value("recovered",false))backupMessage_="上次还原未提交，现有数据已保留。";
  else backupMessage_=packet.value("message",std::string("还原结果待核对，请重新打开检查"));
}
void App::backupRequest(const std::string &operation,Json args){
  backupMessage_=operation=="list"?"正在读取备份列表…":"正在严格校验备份…";
  if(!backups_->start(operation,std::move(args))){backupReload_=true;backupMessage_="正在结束上次校验，请稍候…";return;}
  backupScreen_="loading";if(view_=="backup_restore")build();
}
void App::backupEvent(const Event &event){
  if(event.data.value("token",uint64_t(0))!=backups_->token()||view_!="backup_restore")return;
  if(event.type=="backup_progress"){backupMessage_=event.data.value("message",std::string());const auto total=event.data.value("total",size_t(0));if(total)backupMessage_+=" "+std::to_string(event.data.value("done",size_t(0)))+"/"+std::to_string(total);if(backupProgress_)lv_label_set_text(backupProgress_,backupMessage_.c_str());return;}
  if(event.type=="backup_error"){backupScreen_="list";backupMessage_=event.data.value("message",std::string("备份校验失败"));build();return;}
  const auto operation=event.data.value("operation",std::string());
  if(operation=="list"){backupItems_=event.data.value("items",Json::array());backupScreen_="list";if(backupResult_.empty())backupMessage_=backupItems_.empty()?"没有备份。选择版本回退时使用“备份并安装”，之后可在这里还原。":"选择一份备份，先校验再处理冲突。";}
  else if(operation=="preview"){backupPlan_=event.data;backupDecisions_=Json::object();backupConflict_=0;backupAll_=false;backupScreen_=backupPlan_.value("conflicts",size_t(0))?"conflict":"summary";backupMessage_.clear();}
  if(view_=="backup_restore")build();
}
void App::backupAction(const std::string &action,const Json &args){
  if(action=="open"){if(view_=="settings")openBackupRestore();return;}
  if(view_!="backup_restore"||backupExit_||args.value("ui",uint64_t(0))!=backupUiEpoch_)return;
  if(action=="cancel"){backups_->cancel();backupReload_=false;show("settings");return;}
  if(action=="refresh"){backups_->cancel();backupReload_=true;return;}
  if(action=="select"){
    if(backupScreen_!="list"||backups_->busy()||!backupCanStart())return;
    saveDraft();backupRequest("preview",{{"id",args.value("id",std::string())}});return;
  }
  if(action=="replace"||action=="skip"){
    if(backupScreen_!="conflict")return;
    Json conflicts=Json::array();for(const auto &u:backupPlan_.at("items"))if(u.at("conflict").get<bool>())conflicts.push_back(u);
    if(backupConflict_>=conflicts.size())return;
    if(backupAll_){for(const auto &u:conflicts)backupDecisions_[u.at("key").get<std::string>()]=action;backupConflict_=conflicts.size();}
    else {backupDecisions_[conflicts[backupConflict_].at("key").get<std::string>()]=action;++backupConflict_;}
    backupAll_=false;if(backupConflict_==conflicts.size())backupScreen_="summary";build();return;
  }
  if(action=="apply"){
    bool modifies=backupPlan_.value("new",size_t(0))!=0;for(const auto &v:backupDecisions_)if(v=="replace")modifies=true;
    if(!modifies&&backupScreen_=="summary"){backups_->cancel();backupScreen_="list";backupMessage_="全部冲突已跳过，当前数据保持不变。";build();return;}
    if(backupScreen_!="summary"||!backupCanStart()||backups_->busy())return;
    backupApply_={{"plan",backupPlan_.at("plan")},{"actions",backupDecisions_}};
    atomicText(join(dataDir_,".rescue-preserve-public"),Json{{"pid",int(getpid())},{"time_ms",nowMs()}}.dump());
    backupExit_=true;bridge_.send("backup.applying",{{"operation",backupPlan_.at("plan")}});
  }
}
void App::backupTick(){if(backupReload_&&view_=="backup_restore"&&!backups_->busy()){backupReload_=false;backupRequest("list");}}
void App::backupPage(){
  const auto epoch=++backupUiEpoch_;
  pageHeader("还原备份");pageAction("取消",0,uiMetrics_.dp(84),[this,epoch]{events_.push("backup_action",{{"action","cancel"},{"ui",epoch}});});
  const int left=uiMetrics_.dp(10),width=layout().contentWidth(),inner=std::max(1,width-uiMetrics_.dp(20));int y=uiMetrics_.dp(4);
  auto text=[&](const std::string &value,UiFont role=UiFont::FormNote,uint32_t color=0){auto o=readableLabel(pageContent_,value,left,y,inner,uiMetrics_.fontSize(role),color?color:theme::palette().text);lv_obj_update_layout(o);y+=lv_obj_get_height(o)+uiMetrics_.dp(12);return o;};
  auto control=[&](const std::string &key,const std::string &caption,std::function<void()> callback){auto b=button(pageContent_,caption,left,y,inner,uiMetrics_.touchTarget(48),std::move(callback));backupControls_[key]=b;y+=lv_obj_get_style_height(b,LV_PART_MAIN)+uiMetrics_.dp(12);return b;};
  if(!backupMessage_.empty())backupProgress_=text(backupMessage_,UiFont::FormNote,theme::palette().muted);
  if(backupScreen_=="loading")return;
  if(backupScreen_=="list"){
    control("refresh","刷新备份列表",[this,epoch]{events_.push("backup_action",{{"action","refresh"},{"ui",epoch}});});
    for(const auto &item:backupItems_){const std::string id=item.at("id"),version=item.value("version",std::string());
      const auto seconds=item.value("time",int64_t(0))/1000;time_t stamp=time_t(seconds);tm calendar{};char date[48]{};if(seconds>0&&localtime_r(&stamp,&calendar))strftime(date,sizeof(date),"%Y-%m-%d %H:%M",&calendar);
      const auto caption=std::string(*date?date:"日期未知")+" · "+(version.empty()?"旧格式备份":version);
      const auto b=control("backup:"+id,caption,[this,id,epoch]{events_.push("backup_action",{{"action","select"},{"id",id},{"ui",epoch}});});
      if(!item.value("valid",false)){lv_obj_add_state(b,LV_STATE_DISABLED);text("备份清单无效，不能还原",UiFont::FormNote,theme::palette().danger);}
    }
    return;
  }
  if(backupScreen_=="conflict"){
    Json conflicts=Json::array();for(const auto &u:backupPlan_.at("items"))if(u.at("conflict").get<bool>())conflicts.push_back(u);
    if(backupConflict_>=conflicts.size())return;
    text("处理冲突 "+std::to_string(backupConflict_+1)+" / "+std::to_string(conflicts.size()),UiFont::SettingsTitle);
    text(conflicts[backupConflict_].at("name"),UiFont::PageTitle);
    text("当前已有这个项目。替换会用备份中的整条聊天或整份配置覆盖它；跳过会保留当前内容。",UiFont::FormNote,theme::palette().muted);
    auto check=lv_checkbox_create(pageContent_);backupControls_["all"]=check;lv_checkbox_set_text(check,"全部冲突");lv_obj_set_pos(check,left,y);lv_obj_set_width(check,inner);
    lv_obj_set_style_text_font(check,font(uiMetrics_.fontSize(UiFont::SettingsTitle)),0);lv_obj_set_style_pad_all(check,uiMetrics_.dp(6),0);lv_obj_set_ext_click_area(check,uiMetrics_.dp(6));const int checkHeight=std::max(uiMetrics_.touchTarget(),int(font(uiMetrics_.fontSize(UiFont::SettingsTitle))->line_height)+2*uiMetrics_.dp(6));lv_obj_set_height(check,checkHeight);
    lv_obj_set_style_text_color(check,lv_color_hex(theme::palette().text),0);
    lv_obj_set_style_bg_color(check,lv_color_hex(theme::palette().surface),LV_PART_INDICATOR);
    lv_obj_set_style_border_color(check,lv_color_hex(theme::palette().muted),LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(check,lv_color_hex(theme::palette().accent),LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_border_color(check,lv_color_hex(theme::palette().accent),LV_PART_INDICATOR|LV_STATE_CHECKED);
    lv_obj_set_style_radius(check,uiMetrics_.dp(5),LV_PART_INDICATOR);
    if(backupAll_)lv_obj_add_state(check,LV_STATE_CHECKED);
    lv_obj_add_event_cb(check,[](lv_event_t *e){auto app=static_cast<App*>(lv_event_get_user_data(e));const auto found=app->backupControls_.find("all");if(found!=app->backupControls_.end()&&found->second==lv_event_get_target_obj(e))app->backupAll_=lv_obj_has_state(lv_event_get_target_obj(e),LV_STATE_CHECKED);},LV_EVENT_VALUE_CHANGED,this);
    y+=checkHeight+uiMetrics_.dp(12);
    control("replace","替换",[this,epoch]{events_.push("backup_action",{{"action","replace"},{"ui",epoch}});});
    control("skip","跳过",[this,epoch]{events_.push("backup_action",{{"action","skip"},{"ui",epoch}});});
    text("勾选后，本次选择会应用于全部冲突；缺少的新项目仍会添加。",UiFont::FormNote,theme::palette().muted);return;
  }
  if(backupScreen_=="summary"){
    size_t replace=0,skip=0;for(const auto &v:backupDecisions_){if(v=="replace")++replace;else ++skip;}
    text("即将还原",UiFont::PageTitle);text("新增 "+std::to_string(backupPlan_.value("new",size_t(0)))+" 项 · 替换 "+std::to_string(replace)+" 项 · 跳过 "+std::to_string(skip)+" 项",UiFont::SettingsTitle);
    text("保留当前其他内容。还原完成后会重新加载界面；不会重新发送历史模型请求。",UiFont::FormNote,theme::palette().muted);
    primary(control("apply","开始还原",[this,epoch]{events_.push("backup_action",{{"action","apply"},{"ui",epoch}});}));
  }
}
void App::finishBackupRestore(){
  const auto operation=backupApply_.value("plan",std::string());
  try{auto result=backups_->commit(backupApply_,[this,operation](const Json &state){bridge_.send("backup.progress",{{"operation",operation},{"message",state.value("message",std::string("正在还原…"))}});});bridge_.send("backup.finished",{{"operation",operation},{"ok",true},{"result",result}});}
  catch(const BackupRestoreError &e){bridge_.send("backup.finished",{{"operation",operation},{"ok",false},{"message",e.what()}});}
  catch(...){bridge_.send("backup.finished",{{"operation",operation},{"ok",false},{"message","还原结果待核对，重新打开后将检查事务状态"}});}
}
} // namespace penchat
