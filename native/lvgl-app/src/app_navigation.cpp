#include "app.hpp"
#include <cmath>
namespace penchat {
bool App::navigationRoot() const {
  return view_ == "chat" && !imageViewer_ && !editor_.active && !keyboard_ &&
         !navigationOpen_ && !attachments_ && !modal_ && !imageStoreBusy_ && messageEdit_.empty() && !messageEditBusy_;
}
std::string App::backContext() const {
  return messageEditToken_ + ":" + view_ + ":" + pageKey() + ":" + transferScreen_ + ":" + editor_.id + ":" +
    std::to_string(reinterpret_cast<uintptr_t>(imageViewer_)) + ":" +
    std::to_string(reinterpret_cast<uintptr_t>(modal_)) + ":" +
    std::to_string(reinterpret_cast<uintptr_t>(attachments_));
}
void App::syncBackPolicy() {
  const auto generation = bridge_.generation();
  const bool root = navigationRoot();
  const auto context = backContext();
  if (!generation || (backPolicyGeneration_ == uint64_t(generation) && backPolicyRoot_ == root && backPolicyContext_ == context)) return;
  backPolicyGeneration_ = generation; backPolicyRoot_ = root; backPolicyContext_ = context;
  backPolicyOk_ = false;
  bridge_.send("navigation.state", {{"sequence", ++backPolicySequence_}, {"root", root}, {"imageWorkspace", bool(imageViewer_) || view_=="crop"}});
}
void App::resetBack() {
  backDown_ = backCandidate_ = backClaimed_ = backAborted_ = false;
  backEditorKey_ = false;
  if (backHint_) { lv_obj_delete(backHint_); backHint_ = nullptr; }
}
void App::nativeBack() {
  if(replyBusy_){replyCancel_=true;return;}
  if(contextRegenerating_){cancelContext();build();return;}
  if(messageEditBusy_)return;
  input_.cancel(); touchDown_ = false; composerTapAllowed_ = false;
  if (imageViewer_) closeImageViewer();
  else if (editor_.active) editorFinish(false);
  else if (modal_) { auto object = modal_; modal_ = nullptr; historyContextId_.clear(); lv_obj_delete(object); }
  else if (attachments_) closeAttachments();
  else if (!navigationOpen_ && view_ != "chat") goBack();
  else if(!messageEdit_.empty())cancelMessageEdit();
  syncScanner(); syncBackPolicy();
}
void App::systemBack(const Json &m) {
  const auto gesture=m.value("gesture",uint64_t(0));
  // Some firmware takes over the pointer and emits backpressed after CANCEL.
  // Only the frozen edge-down target may accept that completion. The raw
  // release path and Falcon signal share one consumed physical gesture ID.
  if (!visible_ || keyboard_ || imageStoreBusy_ || navigationRoot() ||
      (navigationOpen_ && !modal_) || !gesture || gesture==backConsumedGesture_ ||
      gesture!=backGesture_ || backStart_.value("gesture",uint64_t(0))!=gesture ||
      m.value("sequence",uint64_t(0))!=backPolicySequence_ || backContext_!=backContext()) return;
  backConsumedGesture_=gesture; resetBack(); nativeBack();
}
bool App::backTouch(const Json &m) {
  const int x=m.value("x",0), y=m.value("y",0);
  const bool down=m.value("pressed",false), cancelled=m.value("cancelled",false);
  const int slop=std::max(8,int(std::lround(screen_.width*.0125)));
  const int edge=std::max(20,int(std::lround(screen_.width*.046)));
  const int distance=std::max(44,int(std::lround(screen_.width*.10)));
  if (cancelled) {
    const bool consumed=backCandidate_;
    resetBack();
    return consumed;
  }
  if (down && !backDown_) {
    backDown_=true;
    backEditorKey_ = editor_.active && std::any_of(editorKeys_.begin(), editorKeys_.end(),
        [x,y](const EditorKey &key) { return key.hit.contains(x,y); });
    backGesture_=m.value("gesture",uint64_t(0));
    // Reserve only the logical left edge. Drawer dismissal stays tap-only;
    // image panning, text selection and horizontal content keep their gestures.
    bool cropHandle=false;
    if(view_=="crop" && cropLoaded_ && cropViewport_ && !modal_) {
      auto p=photoPoint({x,y});lv_area_t viewport;lv_obj_get_coords(cropViewport_,&viewport);
      cropHandle=cropEdges(p.x-viewport.x1,p.y-viewport.y1)!=0;
    }
    backCandidate_=!cropHandle && !navigationRoot() && (!navigationOpen_ || modal_) && x>=0 && x<=edge;
    if (backCandidate_) { backStart_=m; backX_=x; backY_=y; backContext_=backContext(); }
  }
  if (!backCandidate_) { if(!down)backDown_=false; return false; }
  const int dx=x-backX_, dy=y-backY_;
  if (backContext_!=backContext()) backAborted_=true;
  if (backAborted_) { if(!down)resetBack(); return true; }
  if (!backClaimed_ && (std::abs(dy)>slop || dx < -slop) && dx<=std::abs(dy)*1.5) {
    // This is scrolling or a left swipe: replay the held down event once,
    // then let the existing page/editor/image handler own the whole gesture.
    backCandidate_=false; routeTouch(backStart_);
    if(!down)backDown_=false;
    return false;
  }
  // Edge-key taps often drift slightly toward the keyboard interior. Keep
  // these eligible for the normal down/up replay; a real back swipe still wins.
  const int claimSlop = backEditorKey_ ? 2 * slop : slop;
  if (!backClaimed_ && dx>claimSlop && dx>std::abs(dy)*1.5) {
    backClaimed_=true;
    backHint_=icon(lv_screen_active(),"back",8,(screen_.height-56)/2,44,56,{},theme::surface);
    lv_obj_set_clickable(backHint_,false);
  }
  if (backHint_) {
    const int opacity=std::max(80,std::min(255,80+std::max(0,dx)*175/distance));
    lv_obj_set_style_opa(backHint_,opacity,0); lv_obj_move_foreground(backHint_);
  }
  if (!down) {
    const bool commit=backClaimed_ && dx>=distance && dx>std::abs(dy)*1.5;
    const bool tap=!backClaimed_;
    const auto start=backStart_;
    resetBack();
    if (commit) { backConsumedGesture_=backGesture_; nativeBack(); }
    else if (tap) { routeTouch(start); return false; }
  }
  return true;
}
} // namespace penchat
