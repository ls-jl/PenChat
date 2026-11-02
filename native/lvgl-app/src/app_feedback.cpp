#include "app.hpp"
namespace penchat {
void App::feedbackRequest(const std::string &op, Json args) {
  if (!feedback_ || feedbackBusy_) return;
  feedbackBusy_ = true;
  feedbackOperation_ = op=="prepare"?"prepare:"+feedback_->state().value("preview_id",std::string()):op;
  feedback_->request(op, std::move(args));
}
void App::feedbackStartKeys(const std::string &step) {
  editorOcrStop();
  feedbackBusy_ = true;
  whenScannerReleased([this, step] {
    feedbackBusy_ = false;
    if (view_ == "feedback" && feedbackView_ == "adapt" && visible_)
      feedbackRequest("keys.start", {{"step", step}});
  });
}
void App::feedbackEvent(const Event &event) {
  feedbackBusy_ = false;
  const auto sequence=event.data.value("action_sequence",uint64_t(0));
  const auto action = sequence!=feedbackActionSequence_?event.data.value("action", std::string()):std::string();
  feedbackActionSequence_=sequence;
  if (feedbackOperation_.compare(0,8,"prepare:")==0 && event.data.value("preview_ready",false) &&
      event.data.value("preview_id",std::string())!=feedbackOperation_.substr(8)) {
    feedbackView_ = "preview"; feedbackPreviewPage_ = 0; feedbackOperation_.clear();
  }
  if (action == "submitted" || (feedbackOperation_ == "submit" && !event.data.value("preview_ready",false))) {
    feedbackView_ = "records"; feedbackOperation_.clear();
  }
  if (view_ == "settings" && settingsControls_.count("feedback")) {
    const auto value=std::to_string(event.data.value("pending",size_t(0)))+" 份待处理 · 诊断与机型适配";
    lv_label_set_text(settingsControls_.at("feedback").detail,value.c_str());
  }
  if (view_ == "feedback") feedbackDirty_ = true;
  if (action == "upload_result" && event.data.value("notice",std::string()).find("重试") != std::string::npos && visible_ && !editor_.active)
    events_.push("feedback_notice",{{"text",event.data.value("notice",std::string())}});
}
void App::feedbackPage() {
  const auto state = feedback_->state();
  const int width = layout().contentWidth(), gap = 16, column = (width - gap) / 2;
  auto control = [this](const std::string &key, const std::string &title, int x, int y, int w,
                        std::function<void()> action) {
    auto b = button(pageContent_, title, x, y, w, 48, std::move(action));
    feedbackActions_[key] = b;
    return b;
  };
  auto description = [this,width](const std::string &text, int y, uint32_t color = theme::muted) {
    auto object = label(pageContent_, text, 10, y, width, 18, color);
    lv_obj_update_layout(object);
    return int(lv_obj_get_height(object));
  };
  if (feedbackView_ == "preview") {
    pageHeader("诊断预览");
    const auto count = state.value("preview_pages", size_t(0));
    if (count) feedbackPreviewPage_ = std::min(feedbackPreviewPage_, count-1);
    description(std::to_string(state.value("report_bytes", size_t(0))) + " 字节 · " +
                std::to_string(feedbackPreviewPage_+1) + "/" + std::to_string(count) +
                " 页 · 仅发送此报告，不发送聊天和图片", 4);
    control("previous", "上一页", 10, 36, 104, [this] { if(feedbackPreviewPage_)--feedbackPreviewPage_;build(); });
    control("next", "下一页", 126, 36, 104, [this,count] { if(feedbackPreviewPage_+1<count)++feedbackPreviewPage_;build(); });
    auto send = control("send", "发送反馈", width-126, 36, 136, [this] {
      const auto s = feedback_->state();
      feedbackRequest("submit", {{"revision",s.value("revision",uint64_t(0))}});
    });
    if (!state.value("enabled",false) || feedbackBusy_ || !state.value("preview_ready",false))
      lv_obj_add_state(send,LV_STATE_DISABLED);
    if (!state.value("enabled",false)) description("此构建未配置上传服务，仍可查看诊断。",92);
    description(feedback_->previewPage(feedbackPreviewPage_), state.value("enabled",false)?100:130, theme::text);
  } else if (feedbackView_ == "records") {
    pageHeader("反馈记录");
    int y = 4;
    const auto records = state.value("records", Json::array());
    if (records.empty()) y += description("没有待处理反馈。发送成功后仅保留编号与时间，报告正文会清除。",y)+16;
    for (const auto &record : records) {
      const auto id = record.value("id",std::string());
      y += description(id.substr(0,8) + " · " + record.value("reason",std::string()),y,theme::text)+4;
      const auto due = record.value("due_ms",int64_t(0));
      y += description("已尝试 " + std::to_string(record.value("attempts",0)) + " 次 · " +
                       std::to_string(record.value("bytes",size_t(0))) + " 字节" +
                       (record.value("auto",false)?" · 自动重试等待 "+std::to_string(std::max<int64_t>(0,(due-nowMs()+999)/1000))+" 秒":" · 等待手动处理"),y)+8;
      const int third = (width-16)/3;
      auto retry=control("retry_"+id,"重试",10,y,third,[this,id]{feedbackRequest("retry",{{"id",id}});});
      if(record.value("state",std::string())=="sending" || record.value("state",std::string())=="invalid")lv_obj_add_state(retry,LV_STATE_DISABLED);
      control("stop_"+id,"停止自动重试",18+third,y,third,[this,id]{feedbackRequest("stop_auto",{{"id",id}});});
      control("delete_"+id,"删除",26+third*2,y,third,[this,id]{
        modal("删除本地反馈", "仅清除此设备上的记录，不删除服务器已收到的内容。");
        button(modal_,"删除",screen_.width-210,screen_.height-66,110,44,[this,id]{
          if(modal_)lv_obj_delete(modal_);feedbackRequest("delete",{{"id",id}});
        });
      });
      y += 68;
    }
    for (const auto &receipt : state.value("receipts",Json::array()))
      y += description("已发送 · "+receipt.value("id",std::string()).substr(0,8),y)+10;
  } else if (feedbackView_ == "adapt") {
    pageHeader("机型适配信息");
    int y=4;
    y+=description("采样期间临时屏蔽物理按键功能，最长30秒；完成后立即恢复。触摸不受影响。",y)+12;
    const auto steps=state.value("steps",Json::object());
    const bool collecting=state.value("collecting",false);
    const std::string current=state.value("key_step",std::string());
    for(const auto &step: {std::string("scan"),std::string("home")}) {
      const auto result=steps.value(step,Json::object());
      const auto status=result.value("status",std::string());
      const auto title=step=="scan"?"1. 扫描按钮":"2. Home 按钮";
      std::string meaning=status=="complete"?"已记录按下和松开":status=="pressed"?"已记录按下，等待松开":
        status=="skipped"?"已跳过":status=="timeout"?"等待超时":status=="release_missing"?"缺少松开事件":
        status=="ambiguous"?"检测到多个候选，请核对":status=="grab_failed"?"无法屏蔽按键，请结束其他占用后重试":
        status=="key_held"?"按键尚未松开，请松开后重试":status=="unavailable"?"设备不可读或权限不足，未开始采样":
        status=="events_lost"?"事件丢失，请重新采集":status=="cancelled"?(result.value("release_missing",false)?"已取消 · 缺少松开事件":"已取消"):"尚未采集";
      y+=description(std::string(title)+" · "+(collecting&&current==step?(status=="listening"||status=="pressed"?"按键功能已屏蔽，请按下并松开按钮":"正在准备监听，请稍候"):meaning),y,theme::text)+8;
      auto start=control("start_"+step,status.empty()?"开始采集":"重新采集",10,y,column,[this,step]{feedbackStartKeys(step);});
      auto skip=control("skip_"+step,"跳过",26+column,y,column,[this,step]{feedbackRequest("keys.skip",{{"step",step}});});
      if(collecting || (step=="home"&&!steps.count("scan"))) lv_obj_add_state(start,LV_STATE_DISABLED);
      if((collecting && current!=step) || (step=="home"&&!steps.count("scan"))) lv_obj_add_state(skip,LV_STATE_DISABLED);
      y+=60;
      if(step=="home")y+=description("仅采样时屏蔽Home功能，完成、停止或超时后恢复。请按下并松开Home键。",y)+12;
      else y+=description("仅采样时屏蔽扫描键功能；请等待提示就绪后按下并松开扫描键。",y)+12;
      for(const auto &event:result.value("events",Json::array())) {
        y+=description(event.value("device",std::string())+" · 键码 "+std::to_string(event.value("code",0))+" · "+
          std::to_string(event.value("value",0))+" · "+std::to_string(event.value("relative_ms",int64_t(0)))+"ms",y)+4;
      }
    }
    control("stop_keys","停止采集",10,y,column,[this]{feedbackRequest("keys.cancel");});
    control("review","核对完成",26+column,y,column,[this]{feedback_->request("keys.cancel");feedbackView_="form";build();});
  } else {
    pageHeader("问题反馈");
    auto text = state.value("description",std::string());
    auto field=fieldButton(pageContent_,"问题描述（选填，最多2000字）",text.empty()?"请勿填写密钥或其他凭据":text,10,4,width,[this]{
      const auto original=feedback_->state().value("description",std::string());
      edit(original,"描述遇到的问题，请勿填写凭据",[this](const std::string &value){feedbackRequest("draft",{{"description",value}});},2000,"zh-CN",{},true);
    });
    feedbackActions_["description"]=field;
    const bool adapt=state.value("adapt",false);
    settingsRow("feedback_adapt","帮助开发者适配更多机型","本次反馈可选，点击开始后才监听",10,76,width,[this,adapt]{feedbackRequest("draft",{{"adapt",!adapt}});},adapt);
    if(adapt)control("adapt","采集扫描键与 Home",10,144,width,[this]{feedbackView_="adapt";build();});
    int y=adapt?208:144;
    control("preview","诊断预览 / 发送",10,y,column,[this]{
      feedbackRequest("prepare",{{"generating",generating_},{"compressing",contextBusy_},{"camera_ready",cameraImage_!=nullptr},
          {"camera_angle",cameraAngle_},{"camera_auto",cameraAutomatic_},{"storage_unsaved",persistDirty_||saveInFlight_}});
    });
    control("records","反馈记录（"+std::to_string(state.value("pending",size_t(0)))+"）",26+column,y,column,[this]{feedbackView_="records";build();});
    y+=64;
    if(!state.value("enabled",false))y+=description("此构建未配置上传服务；可预览诊断，不能发送。",y)+12;
    description("仅主动发送白名单诊断与本次描述，不包含聊天、图片和凭据。失败报告只保存在应用私有目录。",y);
  }
  const auto message=state.value("error",std::string()).empty()?state.value("notice",std::string()):state.value("error",std::string());
  if(!message.empty()) {
    lv_obj_update_layout(pageContent_);
    const int y=lv_obj_get_height(pageContent_)+12;
    description(message,y,theme::text);
  }
}
} // namespace penchat
