#include "app.hpp"
#include <cmath>
namespace penchat {
namespace {
struct Point { std::string id; double x, y; };
bool inside(double x,double y,const lv_area_t &a) {
  return x>=a.x1 && x<=a.x2 && y>=a.y1 && y<=a.y2;
}
}
void App::finishImageGesture(bool animate) {
  imageGesture_ = {};
  viewerPointer_ = viewerCanvasTouch_ = false;
  if(imageViewer_ && viewerFrame_ && viewerZoom_<1) {
    if(animate) {viewerReturnZoom_=viewerZoom_;viewerReturnAt_=ticks();}
    else {viewerReturnAt_=0;viewerZoom_=1;updateImageViewer();}
  }
}
void App::imageReturnTick() {
  if(!imageViewer_ || !viewerReturnAt_)return;
  const double t=std::min(1.,double(ticks()-viewerReturnAt_)/160.);
  const double ease=1-std::pow(1-t,3);
  viewerZoom_=viewerReturnZoom_+(1-viewerReturnZoom_)*ease;
  if(t>=1){viewerZoom_=1;viewerReturnAt_=0;}
  updateImageViewer();
}
bool App::imageTouch(const Json &m) {
  const bool viewer=imageViewer_!=nullptr;
  if(!viewer && view_!="crop")return false;
  auto &g=imageGesture_;
  if(modal_ || navigationOpen_ || editor_.active) {finishImageGesture(false);return false;}
  std::vector<Point> points;
  bool cancelled=m.value("cancelled",false);
  if(m.contains("points")) {
    const auto &list=m.at("points");
    if(!list.is_array() || list.size()>2)cancelled=true;
    else for(const auto &p:list) {
      if(!p.is_object() || !p.contains("x") || !p.contains("y") || !p["x"].is_number() || !p["y"].is_number() || !p.contains("id")) {cancelled=true;break;}
      const double x=p["x"].get<double>(),y=p["y"].get<double>();
      if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>1000000||std::abs(y)>1000000) {cancelled=true;break;}
      const auto id=p["id"].dump();
      if(!points.empty() && points[0].id==id){cancelled=true;break;}
      points.push_back({id,x,y});
    }
  } else if(m.value("pressed",false))points.push_back({"legacy",double(m.value("x",0)),double(m.value("y",0))});
  const int count=int(points.size());
  imageTouchPeak_=std::max(imageTouchPeak_,count);
  const auto serial=m.value("gesture",uint64_t(0));
  if(serial && serial<imageGestureSerial_)return true;
  if(serial && serial!=imageGestureSerial_) {
    imageGestureSerial_=serial;
    if(g.mode) {input_.cancel();finishImageGesture(false);}
  }
  // A lifecycle interruption invalidates that physical gesture, without
  // advancing a counter past future real touches after repeated hide/show.
  if(g.mode==-2) {
    if(serial)return true;
    finishImageGesture(false); // Legacy single-pointer packets have no identity.
  }
  if(cancelled) {
    input_.cancel();resetBack();viewerLastTap_=0;
    finishImageGesture(false);
    if(m.value("pressed",false))g.mode=-1;
    return true;
  }
  if(g.mode==-1){if(!count)finishImageGesture();return true;}
  if(!count) {
    const int mode=g.mode;
    if(viewer && mode==1 && !g.multi && viewerDistance_<=144 && viewerFrame_) {
      const auto now=ticks();
      const int x=viewerLastX_,y=viewerLastY_;
      if(viewerLastTap_ && now-viewerLastTap_<350 && std::abs(x-viewerLastTapX_)<24 && std::abs(y-viewerLastTapY_)<24)
        zoomImageViewer(viewerZoom_>1.001?1:2,x,y);
      else {viewerLastTap_=now;viewerLastTapX_=x;viewerLastTapY_=y;}
    }
    finishImageGesture();
    return mode!=4 && mode!=0;
  }
  if((!viewer && (cropBusy_ || !cropViewport_)) || (viewer && !viewerCanvas_))return true;
  lv_area_t area;lv_obj_get_coords(viewer?viewerCanvas_:cropViewport_,&area);
  bool controls=false;
  for(auto &p:points) {
    if(!viewer) {auto local=photoPoint({int32_t(std::lround(p.x)),int32_t(std::lround(p.y))});p.x=local.x;p.y=local.y;}
    if(!inside(p.x,p.y,area))controls=true;
    if(viewer && viewerClose_) {lv_area_t close;lv_obj_get_coords(viewerClose_,&close);if(inside(p.x,p.y,close))controls=true;}
    p.x-=area.x1;p.y-=area.y1;
  }
  if((viewer&&!viewerFrame_) || (!viewer&&!cropLoaded_)) {
    if(!controls){input_.cancel();g.mode=-1;return true;}
    return false;
  }
  auto snapshot=[&](double x,double y){
    g.x=x;g.y=y;g.panX=viewer?viewerPanX_:cropPanX_;g.panY=viewer?viewerPanY_:cropPanY_;
    g.zoom=viewer?viewerZoom_:cropZoom_;
    g.left=cropX_;g.top=cropY_;g.right=cropX_+cropW_;g.bottom=cropY_+cropH_;
  };
  if(count==2) {
    if(g.mode==4 || (!g.mode && controls)) {input_.cancel();resetBack();g.mode=-1;return true;}
    input_.cancel();resetBack();viewerReturnAt_=0;viewerLastTap_=0;
    if(points[0].id==g.second && points[1].id==g.first)std::swap(points[0],points[1]);
    const double x=(points[0].x+points[1].x)/2,y=(points[0].y+points[1].y)/2;
    const double distance=std::hypot(points[1].x-points[0].x,points[1].y-points[0].y);
    if(g.mode!=3 || g.first!=points[0].id || g.second!=points[1].id) {
      snapshot(x,y);g.distance=std::max(8.,distance);g.mode=3;g.multi=true;
      g.first=points[0].id;g.second=points[1].id;g.count=2;
      return true;
    }
    const double maximum=std::max(1.,std::min(16.,255./(viewer?imageViewerFit():cropBaseScale_)));
    const double zoom=std::max(viewer?.75:1.,std::min(maximum,g.zoom*std::max(8.,distance)/g.distance));
    const double ratio=zoom/g.zoom;
    if(viewer){viewerZoom_=zoom;viewerPanX_=x-(g.x-g.panX)*ratio;viewerPanY_=y-(g.y-g.panY)*ratio;updateImageViewer();}
    else {cropZoom_=zoom;cropPanX_=x-(g.x-g.panX)*ratio;cropPanY_=y-(g.y-g.panY)*ratio;updateCrop();}
    ++imagePinchUpdates_;return true;
  }
  auto p=points[0];
  if(!g.mode) {
    if(controls){g.mode=4;return false;}
    input_.cancel();viewerReturnAt_=0;
    snapshot(p.x,p.y);g.first=p.id;g.mode=1;g.count=1;viewerDistance_=0;
    if(!viewer) {g.edges=cropEdges(p.x,p.y);if(g.edges)g.mode=2;}
  } else if(g.mode==4)return false;
  else if(g.mode==3 || g.first!=p.id) {
    // Rebase the remaining finger; it may pan, never resize or click.
    snapshot(p.x,p.y);g.first=p.id;g.mode=1;g.multi=true;g.count=1;
  }
  if(viewer){viewerLastX_=int(std::lround(p.x));viewerLastY_=int(std::lround(p.y));}
  const double dx=p.x-g.x,dy=p.y-g.y;
  viewerDistance_=std::max(viewerDistance_,int(std::min(1000000000.,dx*dx+dy*dy)));
  if(g.mode==2) {
    double l=g.left,t=g.top,r=g.right,b=g.bottom;
    const double imageRight=cropPanX_+cropPreviewW_*cropZoom_, imageBottom=cropPanY_+cropPreviewH_*cropZoom_;
    const double minW=std::min(44.,cropPreviewW_*cropZoom_),minH=std::min(44.,cropPreviewH_*cropZoom_);
    if(g.edges&1)l=std::max(std::max(0.,cropPanX_),std::min(r-minW,g.left+dx));
    if(g.edges&2)r=std::min(std::min(double(cropViewportW_),imageRight),std::max(l+minW,g.right+dx));
    if(g.edges&4)t=std::max(std::max(0.,cropPanY_),std::min(b-minH,g.top+dy));
    if(g.edges&8)b=std::min(std::min(double(cropViewportH_),imageBottom),std::max(t+minH,g.bottom+dy));
    cropX_=l;cropY_=t;cropW_=r-l;cropH_=b-t;updateCrop();
  } else if(g.multi || viewerDistance_>144) {
    viewerLastTap_=0;
    if(viewer){viewerPanX_=g.panX+dx;viewerPanY_=g.panY+dy;updateImageViewer();}
    else {cropPanX_=g.panX+dx;cropPanY_=g.panY+dy;updateCrop();}
  }
  return true;
}
} // namespace penchat
