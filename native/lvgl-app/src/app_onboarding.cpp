#include "app.hpp"
namespace penchat {
void App::initializeOnboarding(const Json &settings) {
  const auto saved=objectField(settings,"onboarding");
  if(saved.value("version",Json()).is_number_integer()&&saved["version"].get<int64_t>()>=1&&saved.value("complete",Json())==true)return;
  const auto step=saved.value("step",Json());
  onboardingStep_=step.is_number_integer()?int(std::max<int64_t>(0,std::min<int64_t>(6,step.get<int64_t>()))):0;
  onboardingActive_=true;view_="onboarding";
}
void App::openOnboarding() {
  if(onboardingActive_)return;
  onboardingReturn_=view_;onboardingReturnScroll_=body_?std::max(0,int(lv_obj_get_scroll_y(body_))):0;
  onboardingActive_=onboardingReplay_=true;onboardingStep_=0;
  show("onboarding");
}
void App::onboardingPage() {
  onboarding_.reset(new Onboarding(screenObj_,screen_.width,screen_.height,onboardingStep_,onboardingReplay_,!db_.models().empty(),
    [this](const std::string &action){events_.push("onboarding_action",{{"action",action},{"step",onboardingStep_}});}));
  if(!onboardingPending_.empty())onboarding_->busy(true);
}
void App::onboardingAction(const std::string &action) {
  if(!onboardingActive_||view_!="onboarding"||!onboardingPending_.empty())return;
  if(action=="pause"){onboarding_->toggle();return;}
  if(action=="replay"){onboarding_->replay();return;}
  if(action=="configure"&&onboardingStep_==2){onboardingConfig_=true;show("provider");return;}
  const bool finish=(action=="finish"&&(onboardingStep_>=3||onboardingReplay_))||(action=="next"&&onboardingStep_==6);
  int target=onboardingStep_;
  if(action=="next"&&!finish)target=std::min(6,target+1);
  else if(action=="back")target=std::max(0,target-1);
  else if(!finish)return;
  if(onboardingReplay_) {
    if(finish)finishOnboarding();else{onboardingStep_=target;build();}
    return;
  }
  onboardingTarget_=target;onboardingFinishing_=finish;
  onboardingPending_=uid("guide");onboarding_->busy(true);
  db_.queueSetting("onboarding",{{"version",1},{"step",target},{"complete",finish}},onboardingPending_);
}
void App::onboardingSaved(const Event &event) {
  if(onboardingPending_.empty()||event.data.value("request",std::string())!=onboardingPending_)return;
  onboardingPending_.clear();
  if(event.type=="setting_error") {
    if(onboarding_)onboarding_->busy(false);
    modal("引导进度未保存","请关闭提示后重试；不会影响已有聊天。");return;
  }
  if(onboardingFinishing_)finishOnboarding();
  else{onboardingStep_=onboardingTarget_;if(view_=="onboarding")build();}
}
void App::finishOnboarding() {
  const auto destination=onboardingReturn_;const int scroll=onboardingReturnScroll_;
  onboardingActive_=onboardingReplay_=onboardingConfig_=false;
  show(destination);
  if(destination!="chat"&&body_)lv_obj_scroll_to_y(body_,scroll,LV_ANIM_OFF);
}
} // namespace penchat
