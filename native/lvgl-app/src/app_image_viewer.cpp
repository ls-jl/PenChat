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
  viewerFailed_=false;viewerZoom_=1;viewerPanX_=viewerPanY_=0;viewerFrame_.reset();
  imageViewer_=box(lv_screen_active(),0,0,screen_.width,screen_.height,theme::sidebar);
  lv_obj_set_scrollable(imageViewer_,false);lv_obj_set_clickable(imageViewer_,true);
  viewerCanvas_=box(imageViewer_,8,8,screen_.width-148,screen_.height-16,theme::sidebar);
  lv_obj_set_scrollable(viewerCanvas_,false);lv_obj_set_clickable(viewerCanvas_,false);
  viewerImage_=lv_image_create(viewerCanvas_);
  lv_obj_set_clickable(viewerImage_,false);lv_image_set_pivot(viewerImage_,0,0);
  viewerStatus_=label(viewerCanvas_,"正在加载原图…",12,0,screen_.width-172,20,theme::muted);
  lv_obj_set_style_text_align(viewerStatus_,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(viewerStatus_);
  icon(imageViewer_,"close",screen_.width-60,8,44,44,[this]{events_.push("image_view_close");},theme::surface);
  viewerZoomLabel_=label(imageViewer_,"全图",screen_.width-132,56,124,20,theme::muted);
  lv_obj_set_style_text_align(viewerZoomLabel_,LV_TEXT_ALIGN_CENTER,0);
  viewerMinus_=icon(imageViewer_,"minus",screen_.width-132,92,56,44,[this]{zoomImageViewer(viewerZoom_/2);},theme::surface);
  viewerPlus_=icon(imageViewer_,"plus",screen_.width-68,92,56,44,[this]{zoomImageViewer(viewerZoom_*2);},theme::surface);
  button(imageViewer_,"适应屏幕",screen_.width-132,144,124,44,[this]{zoomImageViewer(1);});
  if(screen_.height>=246) {
    auto hint=label(imageViewer_,"双击放大\n拖动查看",screen_.width-132,screen_.height-54,124,16,theme::muted);
    lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
  }
  lv_obj_add_state(viewerPlus_,LV_STATE_DISABLED);lv_obj_add_state(viewerMinus_,LV_STATE_DISABLED);
  viewerKey_=images_.request(path,32768,32768,false,true);
  lv_obj_update_layout(imageViewer_);lv_obj_move_foreground(imageViewer_);
  scanner_.eligible(false,session_);
  refreshImageViewer();
}
void App::closeImageViewer(bool restore) {
  if(!imageViewer_)return;
  auto root=imageViewer_;imageViewer_=nullptr;
  viewerCanvas_=viewerImage_=viewerStatus_=viewerZoomLabel_=viewerPlus_=viewerMinus_=nullptr;
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
void App::updateImageViewer() {
  if(!imageViewer_ || !viewerFrame_)return;
  const double fit=std::max(1.,std::floor(std::min(double(lv_obj_get_width(viewerCanvas_))/viewerFrame_->width,
       double(lv_obj_get_height(viewerCanvas_))/viewerFrame_->height)*256))/256;
  const int scale=std::max(1,std::min(65535,int(std::lround(fit*viewerZoom_*256))));
  const double width=viewerFrame_->width*double(scale)/256,height=viewerFrame_->height*double(scale)/256;
  const int vw=lv_obj_get_width(viewerCanvas_),vh=lv_obj_get_height(viewerCanvas_);
  viewerPanX_=width<=vw?(vw-width)/2:std::max(vw-width,std::min(0.,viewerPanX_));
  viewerPanY_=height<=vh?(vh-height)/2:std::max(vh-height,std::min(0.,viewerPanY_));
  lv_image_set_scale(viewerImage_,scale);lv_obj_set_pos(viewerImage_,std::lround(viewerPanX_),std::lround(viewerPanY_));
  const auto text=viewerZoom_<=1.001?std::string("全图"):std::to_string(int(std::lround(viewerZoom_*100)))+"%";
  lv_label_set_text(viewerZoomLabel_,text.c_str());
  if(viewerZoom_<=1.001)lv_obj_add_state(viewerMinus_,LV_STATE_DISABLED);else lv_obj_remove_state(viewerMinus_,LV_STATE_DISABLED);
  if(viewerZoom_>=std::min(16.,255./fit)-.001)lv_obj_add_state(viewerPlus_,LV_STATE_DISABLED);else lv_obj_remove_state(viewerPlus_,LV_STATE_DISABLED);
}
void App::zoomImageViewer(double zoom,int x,int y) {
  if(!imageViewer_ || !viewerFrame_)return;
  const int vw=lv_obj_get_width(viewerCanvas_),vh=lv_obj_get_height(viewerCanvas_);
  const double fit=std::max(1.,std::floor(std::min(double(vw)/viewerFrame_->width,double(vh)/viewerFrame_->height)*256))/256;
  zoom=std::max(1.,std::min(std::max(1.,std::min(16.,255./fit)),zoom));
  const double ratio=zoom/viewerZoom_;
  if(x<0)x=vw/2;if(y<0)y=vh/2;
  viewerPanX_=x-(x-viewerPanX_)*ratio;viewerPanY_=y-(y-viewerPanY_)*ratio;
  viewerZoom_=zoom;viewerLastTap_=0;
  updateImageViewer();
}
void App::imageViewerTouch(int x,int y,bool pressed,bool cancelled) {
  if(cancelled) {viewerPointer_=viewerCanvasTouch_=false;viewerLastTap_=0;input_.cancel();return;}
  viewerLastX_=x;viewerLastY_=y;
  if(pressed&&!viewerPointer_) {
    viewerPointer_=true;viewerStartX_=x;viewerStartY_=y;viewerDistance_=0;
    lv_area_t a;lv_obj_get_coords(viewerCanvas_,&a);
    viewerCanvasTouch_=x>=a.x1&&x<=a.x2&&y>=a.y1&&y<=a.y2;
    viewerStartPanX_=viewerPanX_;viewerStartPanY_=viewerPanY_;
  }
  if(viewerCanvasTouch_) {
    const int dx=x-viewerStartX_,dy=y-viewerStartY_;
    viewerDistance_=std::max(viewerDistance_,dx*dx+dy*dy);
    if(pressed && viewerDistance_>144 && viewerFrame_) {
      viewerLastTap_=0;viewerPanX_=viewerStartPanX_+dx;viewerPanY_=viewerStartPanY_+dy;updateImageViewer();
    }
    if(!pressed) {
      viewerPointer_=viewerCanvasTouch_=false;
      const auto now=ticks();
      if(viewerFrame_ && viewerDistance_<=144) {
        if(viewerLastTap_ && now-viewerLastTap_<350 && std::abs(x-viewerLastTapX_)<24 && std::abs(y-viewerLastTapY_)<24) {
          lv_area_t a;lv_obj_get_coords(viewerCanvas_,&a);
          zoomImageViewer(viewerZoom_>1.001?1:2,x-a.x1,y-a.y1);
        } else {viewerLastTap_=now;viewerLastTapX_=x;viewerLastTapY_=y;}
      }
    }
    return;
  }
  if(!pressed)viewerPointer_=false;
  input_.touch(x,y,pressed);
}
Json App::imageViewerDiagnostic() const {
  Json state={{"open",imageViewer_!=nullptr}};
  if(!imageViewer_)return state;
  state["loaded"]=bool(viewerFrame_);state["failed"]=viewerFailed_;
  state["zoom"]=viewerZoom_;state["pan"]={viewerPanX_,viewerPanY_};
  state["frame"]=viewerFrame_?Json::array({viewerFrame_->width,viewerFrame_->height}):Json::array({0,0});
  state["viewport"]={8,8,screen_.width-148,screen_.height-16};
  return state;
}
} // namespace penchat
