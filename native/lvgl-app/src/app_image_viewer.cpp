#include "app.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <cmath>
namespace penchat {
void App::requestImageViewer(const std::string &path) {
  events_.push("image_view_request",{{"session",view_=="archive_chat"?archiveReaderId_:session_},{"path",path}});
}
void App::openImageViewer(const std::string &path) {
  if (path.empty() || imageViewer_) return;
  closeAttachments();resetPull();input_.cancel();touchDown_=false;
  lv_obj_stop_scroll_anim(body_);
  viewerScroll_=std::max(0,int(lv_obj_get_scroll_y(body_)));
  if(view_=="chat")chatFollowing_=false;
  viewerPointer_=viewerCanvasTouch_=false;viewerLastTap_=0;
  imageGesture_={};imageTouchPeak_=imagePinchUpdates_=0;viewerReturnAt_=0;
  viewerFailed_=false;viewerZoom_=1;viewerPanX_=viewerPanY_=0;viewerFrame_.reset();
  imageViewer_=box(lv_screen_active(),0,0,screen_.width,screen_.height,theme::sidebar);
  lv_obj_set_scrollable(imageViewer_,false);lv_obj_set_clickable(imageViewer_,true);
  viewerCanvas_=box(imageViewer_,0,0,screen_.width,screen_.height,theme::sidebar);
  lv_obj_set_scrollable(viewerCanvas_,false);lv_obj_set_clickable(viewerCanvas_,false);
  viewerImage_=lv_image_create(viewerCanvas_);
  lv_obj_set_clickable(viewerImage_,false);lv_image_set_pivot(viewerImage_,0,0);
  viewerStatus_=label(viewerCanvas_,"正在加载原图…",12,0,screen_.width-24,20,theme::muted);
  lv_obj_set_style_text_align(viewerStatus_,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(viewerStatus_);
  viewerClose_=icon(imageViewer_,"close",screen_.width-60,8,44,44,[this]{events_.push("image_view_close");},theme::surface);
  viewerKey_=images_.request(path,32768,32768,false,true);
  lv_obj_update_layout(imageViewer_);lv_obj_move_foreground(imageViewer_);
  scanner_.eligible(false,session_);
  refreshImageViewer();
}
void App::closeImageViewer(bool restore) {
  if(!imageViewer_)return;
  auto root=imageViewer_;imageViewer_=nullptr;
  viewerCanvas_=viewerImage_=viewerStatus_=viewerClose_=nullptr;
  imageGesture_={};viewerReturnAt_=0;
  viewerPointer_=viewerCanvasTouch_=false;viewerLastTap_=0;viewerKey_.clear();
  // The image owns its descriptor/frame until its DELETE event, independently
  // of chat rebuilds and the chat thumbnail texture map.
  lv_obj_delete(root);viewerFrame_.reset();
  input_.cancel();composerTapAllowed_=false;
  if(restore&&view_=="archive_chat"&&body_){lv_obj_update_layout(body_);lv_obj_scroll_to_y(body_,viewerScroll_,LV_ANIM_OFF);paintVisible();}
  if(restore && view_=="chat" && body_) {
    const bool previous=chatLayout_;chatLayout_=true;
    lv_obj_update_layout(body_);lv_obj_scroll_to_y(body_,viewerScroll_,LV_ANIM_OFF);
    chatScroll_=std::max(0,int(lv_obj_get_scroll_y(body_)));
    chatFollowing_=messageOffset_==0 && lv_obj_get_scroll_bottom(body_)<=10;
    chatLayout_=previous;updateChatControls(false);paintVisible();
    syncScanner();
  }
}
void App::refreshImageViewer() {
  if(!imageViewer_ || viewerFrame_)return;
  auto frame=images_.get(viewerKey_);
  if(!frame) {
    if(images_.failed(viewerKey_)) {viewerFailed_=true;lv_label_set_text(viewerStatus_,"图片无法读取\n文件可能已移动、损坏或超出大小限制");}
    return;
  }
  viewerFrame_=frame;
  auto t=new Texture;t->frame=frame;
  auto &d=t->descriptor;d.header.magic=LV_IMAGE_HEADER_MAGIC;d.header.cf=LV_COLOR_FORMAT_ARGB8888;
  d.header.w=frame->width;d.header.h=frame->height;d.header.stride=frame->width*4;
  d.data_size=frame->pixels.size();d.data=frame->pixels.data();
  lv_obj_add_event_cb(viewerImage_,[](lv_event_t *e) {
    auto t=static_cast<Texture *>(lv_event_get_user_data(e));
    lv_image_set_src(lv_event_get_target_obj(e),nullptr);
    lv_image_cache_drop(&t->descriptor);delete t;
  },LV_EVENT_DELETE,t);
  lv_image_set_src(viewerImage_,&t->descriptor);lv_obj_set_hidden(viewerStatus_,true);
  zoomImageViewer(1);
  viewerStartX_=viewerLastX_;viewerStartY_=viewerLastY_;
  viewerStartPanX_=viewerPanX_;viewerStartPanY_=viewerPanY_;
}
double App::imageViewerFit() const {
  if(!viewerFrame_)return 1;
  return std::max(1.,std::floor(std::min(double(lv_obj_get_width(viewerCanvas_))/viewerFrame_->width,
       double(lv_obj_get_height(viewerCanvas_))/viewerFrame_->height)*256))/256;
}
void App::updateImageViewer() {
  if(!imageViewer_ || !viewerFrame_)return;
  const int scale=std::max(1,std::min(65535,int(std::lround(imageViewerFit()*viewerZoom_*256))));
  const double width=viewerFrame_->width*double(scale)/256,height=viewerFrame_->height*double(scale)/256;
  const int vw=lv_obj_get_width(viewerCanvas_),vh=lv_obj_get_height(viewerCanvas_);
  viewerPanX_=width<=vw?(vw-width)/2:std::max(vw-width,std::min(0.,viewerPanX_));
  viewerPanY_=height<=vh?(vh-height)/2:std::max(vh-height,std::min(0.,viewerPanY_));
  lv_image_set_scale(viewerImage_,scale);lv_obj_set_pos(viewerImage_,std::lround(viewerPanX_),std::lround(viewerPanY_));
}
void App::zoomImageViewer(double zoom,int x,int y) {
  if(!imageViewer_ || !viewerFrame_)return;
  const int vw=lv_obj_get_width(viewerCanvas_),vh=lv_obj_get_height(viewerCanvas_);
  const double fit=std::max(1.,std::floor(std::min(double(vw)/viewerFrame_->width,double(vh)/viewerFrame_->height)*256))/256;
  viewerReturnAt_=0;
  zoom=std::max(1.,std::min(std::max(1.,std::min(16.,255./fit)),zoom));
  const double ratio=zoom/viewerZoom_;
  if(x<0)x=vw/2;if(y<0)y=vh/2;
  viewerPanX_=x-(x-viewerPanX_)*ratio;viewerPanY_=y-(y-viewerPanY_)*ratio;
  viewerZoom_=zoom;viewerLastTap_=0;
  updateImageViewer();
}
Json App::imageViewerDiagnostic() const {
  Json state={{"open",imageViewer_!=nullptr}};
  if(!imageViewer_)return state;
  state["loaded"]=bool(viewerFrame_);state["failed"]=viewerFailed_;
  state["zoom"]=viewerZoom_;state["pan"]={viewerPanX_,viewerPanY_};
  state["frame"]=viewerFrame_?Json::array({viewerFrame_->width,viewerFrame_->height}):Json::array({0,0});
  state["viewport"]={0,0,screen_.width,screen_.height};
  state["returning"]=viewerReturnAt_!=0;
  state["touch_peak"]=imageTouchPeak_;state["pinch_updates"]=imagePinchUpdates_;
  return state;
}
} // namespace penchat
