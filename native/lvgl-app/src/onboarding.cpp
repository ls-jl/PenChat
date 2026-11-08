#include "onboarding.hpp"
#include "layout.hpp"
#include "icons.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"
namespace penchat {
namespace {
const char *titles[] = {"直接扫描，得到图片", "打开键盘，扫描变成文字", "配置 API，连接你的模型", "用预设切换用途", "管理与继续聊天", "选择模型与推理强度", "长对话与数据管理"};
const char *copy[] = {
  "无需打开键盘，直接用笔头划扫纸面。\n检查扫描结果，确认后加入待发送区。\n得到的是图片，发送前可以继续检查。",
  "先点击聊天输入框，再用笔头划扫。\nOCR 将文字放入编辑区，可以修改。\n单行、多行扫描可在键盘底部切换。",
  "API 服务提供地址、密钥和模型。\n填写自己的服务信息，添加模型并保存。\nAPI Key 从你选择的服务商处获取。",
  "在设置中开启预设入口，再从加号选择。\n预设可指定提示词、模型和推理强度。\n选择“沿用会话”则保留当前选择。",
  "历史在左上角；有内容时拉满圆环，松开新建。\n长按自己的消息可以复制、选择或编辑。\nAI 回复下方可复制、重试和创建分支。",
  "从加号选择模型，打开推理强度入口。\nAPI 默认不会主动指定推理强度。\n回复的思考区域可以展开或收起。",
  "加号中查看占用并压缩，压缩前会归档。\n设置中可导入、导出聊天和预设。\n字体大小、检查更新与问题反馈也在设置。"};
const char *captions[7][4] = {
  {"聊天页：直接划扫", "笔头扫过纸面", "检查图片，再确认", "图片进入待发送区"},
  {"点击输入框", "键盘中划扫文字", "OCR 得到可编辑文字", "修改文字，确认输入"},
  {"从历史菜单进入设置", "打开模型管理", "填写自己的服务信息", "添加模型，保存配置"},
  {"设置 → 会话预设", "开启预设入口", "从加号选择预设", "沿用或指定会话配置"},
  {"左上角打开历史", "上拉并松手，新建聊天", "长按用户消息", "AI 回复的三个操作"},
  {"加号 → 模型", "打开推理强度", "API 默认或指定档位", "点击展开思考过程"},
  {"查看上下文占用", "压缩前保存归档", "摘要生效，占用降低", "设置 → 导入与导出"}};
void click(lv_obj_t *o,std::function<void()> fn) {
  auto f=new std::function<void()>(std::move(fn));
  lv_obj_set_clickable(o,true);
  lv_obj_add_event_cb(o,[](lv_event_t *e) {
    auto f=static_cast<std::function<void()> *>(lv_event_get_user_data(e));
    if(lv_event_get_code(e)==LV_EVENT_DELETE){delete f;return;}
    if(lv_event_get_code(e)==LV_EVENT_CLICKED){auto callback=*f;callback();}
  },LV_EVENT_ALL,f);
}
}
lv_obj_t *Onboarding::rect(lv_obj_t *p,int x,int y,int w,int h,uint32_t c,int radius) {
  auto o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
  lv_obj_set_scrollable(o,false);lv_obj_set_clickable(o,false);
  lv_obj_set_style_bg_color(o,lv_color_hex(c),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
  lv_obj_set_style_radius(o,radius,0);return o;
}
lv_obj_t *Onboarding::text(lv_obj_t *p,const std::string &s,int x,int y,int w,int size,uint32_t ink) {
  auto o=lv_label_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);
  lv_obj_set_style_text_font(o,font(size),0);lv_obj_set_style_text_color(o,lv_color_hex(ink),0);
  lv_label_set_text(o,s.c_str());lv_obj_set_clickable(o,false);return o;
}
lv_obj_t *Onboarding::control(const std::string &id,const std::string &s,int x,int y,int w,bool primary) {
  auto o=rect(root_,x,y,w,44,primary?theme::text:theme::surface);
  auto l=text(o,s,0,0,w-8,18,primary?theme::background:theme::text);
  lv_label_set_long_mode(l,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_height(l,font(18)->line_height);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);
  lv_obj_set_style_bg_color(o,lv_color_hex(theme::border),LV_STATE_PRESSED);
  lv_obj_set_style_opa(o,LV_OPA_50,LV_STATE_DISABLED);
  click(o,[this,id]{if(!busy_)action_(id);});actions_[id]=o;return o;
}
// These are illustration design units, never physical display coordinates.
lv_obj_t *Onboarding::part(lv_obj_t *p,int x,int y,int w,int h,uint32_t c,int r) {
  return rect(p,x*sceneWidth_/400,y*sceneHeight_/150,w*sceneWidth_/400,h*sceneHeight_/150,c,r);
}
void Onboarding::words(lv_obj_t *p,const std::string &s,int x,int y,int w,int size,uint32_t ink) {
  auto o=text(p,s,x*sceneWidth_/400,y*sceneHeight_/150,w*sceneWidth_/400,size,ink);
  lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_DOTS);lv_obj_set_height(o,font(size)->line_height);
}
void Onboarding::symbol(lv_obj_t *p,const std::string &s,int x,int y,int size,uint32_t ink) {
  auto o=part(p,x,y,size,size,theme::sidebar,0);lv_obj_set_style_bg_opa(o,LV_OPA_TRANSP,0);attachIcon(o,s,ink);
}
void Onboarding::paper(lv_obj_t *p,int x,int y,int w,int h) {
  auto o=part(p,x,y,w,h,theme::text,5);
  for(int n=0;n<3;++n)rect(o,12,10+n*12,std::max(10,w*sceneWidth_/400-28-(n==2?30:0)),3,theme::border,1);
}
void Onboarding::chat(lv_obj_t *p,bool image) {
  symbol(p,"menu",10,5);words(p,"PenChat",46,4,200,17);
  part(p,108,34,260,26);words(p,"请解释这道题",120,36,230,16);
  part(p,12,118,376,29);symbol(p,"plus",22,121,20);
  words(p,"询问 PenChat",60,121,230,16,theme::muted);symbol(p,"up",352,121,22);
  if(image)paper(p,20,66,145,42);
}
void Onboarding::scene(int phase,lv_obj_t *p) {
  const int s=step_;
  if(s==0) {
    if(phase==0||phase==3)chat(p,phase==3);
    if(phase==1) {
      words(p,"正在扫描…",16,4,280,17);
      paper(p,30,48,330,61);
      // The dictionary pen is a solid mesh rendered above this paper texture.
    } else if(phase==2) {
      paper(p,24,24,352,70);part(p,244,108,132,34,theme::accent);
      words(p,"使用图片",266,113,110,17);symbol(p,"close",26,112);
    }
  } else if(s==1) {
    if(phase==0)chat(p);
    else if(lv_obj_get_style_height(root_,LV_PART_MAIN)>lv_obj_get_style_width(root_,LV_PART_MAIN)) {
      part(p,8,3,384,67,theme::sidebar);
      words(p,phase==1?"划扫得到可编辑文字":phase==2?"请解释这道题":"请解释这道题。",20,18,360,16);
      const std::string rows[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
      for(int row=0;row<3;++row)for(int k=0;k<int(rows[row].size());++k) {
        auto key=part(p,20+(row==1?16:row==2?48:0)+k*36,78+row*21,33,19);
        auto letter=text(key,rows[row].substr(k,1),0,0,33*sceneWidth_/400,13);
        lv_obj_set_style_text_align(letter,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(letter);
      }
      if(phase==1)paper(p,30,48,330,61);
      else {auto caret=part(p,phase==2?190:218,20,2,22,theme::caret,0);motions_[phase].push_back({caret,(phase==2?190:218)*sceneWidth_/400,20*sceneHeight_/150,24,0});}
    } else {
      const std::string rows[]={"QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
      for(int row=0;row<3;++row)for(int k=0;k<int(rows[row].size());++k) {
        int x=8+(row==1?9:row==2?27:0)+k*19;
        auto key=part(p,x,8+row*29,17,25);
        auto letter=text(key,rows[row].substr(k,1),0,0,17*sceneWidth_/400,13);
        lv_obj_set_style_text_align(letter,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(letter);
      }
      part(p,20,103,174,33);words(p,"拼音",80,110,80,15);
      part(p,218,3,174,140,theme::sidebar);
      words(p,"单行扫描",226,106,100,15,theme::muted);
      words(p,"完成",332,106,60,15,theme::link);
      words(p,phase==1?"划扫后识别文字":phase==2?"请解释这道题":"请解释这道题。",226,32,164,16);
      if(phase==1)paper(p,30,48,330,61);
      else {auto caret=part(p,phase==2?350:280,57,2,22,theme::caret,0);motions_[phase].push_back({caret,(phase==2?350:280)*sceneWidth_/400,57*sceneHeight_/150,phase==2?-40:35,0});}
    }
  } else if(s==2) {
    if(phase==0) {
      part(p,0,0,268,150,theme::sidebar);words(p,"历史对话",18,18,230,18);
      words(p,"新建聊天",18,113,120,16);symbol(p,"settings",164,113);words(p,"设置",198,113,66,16);
    } else if(phase==1) {
      words(p,"设置",16,7,340,21);part(p,12,48,376,48);words(p,"模型管理",26,57,320,19);words(p,"Provider 与模型",26,82,300,14,theme::muted);
    } else {
      words(p,"添加服务商",16,4,320,19);
      const char *names[]={"服务地址","API Key","模型"};const char *values[]={"服务商提供的 API 地址","****************","你选择的模型 ID"};
      for(int n=0;n<3;++n){part(p,12,35+n*31,376,28,phase==2&&n==1?theme::border:theme::surface);words(p,names[n],22,37+n*31,100,15,theme::muted);words(p,values[n],124,37+n*31,253,15);}
      if(phase==3){part(p,300,0,88,28,theme::accent);words(p,"保存",324,3,60,17);}
    }
  } else if(s==3) {
    words(p,phase<2?"会话预设":"加号 → 预设",16,6,360,20);
    if(phase<2){part(p,12,47,376,48);words(p,"启用预设入口",24,59,260,18);part(p,318,57,52,28,phase?theme::accent:theme::border,14);part(p,phase?346:322,61,20,20,theme::text,10);}
    else {part(p,12,43,376,48,theme::border);words(p,"答题助手 · 示例",25,52,330,18);words(p,phase==2?"点击选择预设":"模型、强度：沿用会话",25,105,340,17,phase==2?theme::muted:theme::link);symbol(p,"chevron-down",350,57);}
  } else if(s==4) {
    if(phase==0){part(p,0,0,268,150,theme::sidebar);part(p,12,8,244,35);words(p,"刚才的问题",24,12,215,17);words(p,"另一段聊天",24,62,215,17);words(p,"新建聊天       设置",16,118,245,16);}
    else if(phase==1){chat(p);auto arc=lv_arc_create(p);lv_obj_set_size(arc,42,42);lv_obj_set_pos(arc,sceneWidth_/2-21,sceneHeight_/2-20);lv_arc_set_range(arc,0,100);lv_arc_set_bg_angles(arc,0,360);lv_arc_set_rotation(arc,270);lv_obj_set_style_bg_opa(arc,LV_OPA_TRANSP,LV_PART_KNOB);lv_arc_set_value(arc,100);lv_obj_remove_style(arc,nullptr,LV_PART_KNOB);lv_obj_set_style_arc_color(arc,lv_color_hex(theme::accent),LV_PART_INDICATOR);lv_obj_set_clickable(arc,false);words(p,"松开新建",151,93,170,16);}
    else if(phase==2){chat(p);part(p,45,64,310,43,theme::border);words(p,"复制   选择文本   编辑",60,77,285,17);}
    else{words(p,"这是 AI 的示例回复。",20,35,360,19);symbol(p,"copy",20,87,34);symbol(p,"retry",85,87,34);symbol(p,"branch",150,87,34);words(p,"复制    重试    创建分支",15,123,370,16,theme::muted);}
  } else if(s==5) {
    words(p,phase==0?"选择模型":phase==3?"查看思考过程":"推理强度",16,6,370,20);
    if(phase==0){part(p,12,45,376,40,theme::border);words(p,"当前模型 · 示例",24,53,330,18);words(p,"按服务商分组，选择后用于下次请求",20,106,360,16,theme::muted);}
    else if(phase==3){symbol(p,"chevron-down",16,44);words(p,"思考了 12秒",49,43,300,17,theme::reasoning);words(p,"先整理问题中的已知条件…",24,78,350,17,theme::reasoning);words(p,"再给出完整答案。",24,114,350,17);}
    else{symbol(p,"gauge",25,57,45,theme::link);part(p,88,63,284,15,theme::border,8);part(p,88,63,phase==1?30:188,15,theme::accent,8);for(int n=0;n<4;++n)part(p,92+n*83,60,20,20,theme::text,10);words(p,phase==1?"API 默认：不主动指定":"指定档位：以模型支持为准",25,112,355,17);}
  } else {
    words(p,phase==3?"导入与导出":"上下文与归档",16,6,365,20);
    if(phase==3){part(p,12,43,182,40);part(p,204,43,182,40);words(p,"聊天导入 / 导出",21,52,171,15);words(p,"预设导入 / 导出",214,52,171,15);words(p,"Favorite / PenChat",24,103,355,17,theme::link);}
    else {auto arc=lv_arc_create(p);lv_obj_set_size(arc,66,66);lv_obj_set_pos(arc,25,45);lv_arc_set_range(arc,0,100);lv_arc_set_bg_angles(arc,0,360);lv_arc_set_rotation(arc,270);lv_obj_set_style_bg_opa(arc,LV_OPA_TRANSP,LV_PART_KNOB);lv_arc_set_value(arc,phase==2?35:78);lv_obj_remove_style(arc,nullptr,LV_PART_KNOB);lv_obj_set_style_arc_color(arc,lv_color_hex(theme::accent),LV_PART_INDICATOR);lv_obj_set_clickable(arc,false);words(p,phase==2?"占用 35%":"占用 78%",119,49,250,20);words(p,phase==1?"保存压缩前副本":phase==2?"原消息和图片仍保留":"加号 → 压缩上下文",119,89,260,16,theme::muted);}
  }
  // A single moving touch indicator makes the demonstrated path visible.
  if(motions_[phase].empty() && !(step_<2 && phase==1)) {
    auto touch=part(p,phase==0?25:310,phase==0?12:112,12,12,theme::link,6);
    motions_[phase].push_back({touch,(phase==0?25:310)*sceneWidth_/400,(phase==0?12:112)*sceneHeight_/150,phase==0?45:-24,0});
  }
}
Onboarding::Onboarding(lv_obj_t *parent,int width,int height,int step,bool replaying,bool configured,std::function<void(const std::string &)> action)
  : action_(std::move(action)),step_(step) {
  root_=rect(parent,0,0,width,height,theme::background,0);
  const auto geometry=AppLayout{width,height}.guideWorkspace();
  const bool portrait=geometry.portrait;
  const int split=geometry.split,right=geometry.right,measure=geometry.measure;
  const int footer=geometry.footer,demoH=geometry.demoHeight;
  auto demo=rect(root_,16,12,portrait?width-32:split-28,demoH,theme::sidebar,14);
  text(demo,"操作演示",14,8,portrait?width-64:split-60,16,theme::muted);
  sceneWidth_=std::min(640,portrait?width-48:split-44);
  sceneHeight_=std::min(360,portrait?sceneWidth_*150/400:height-130);
  const int sceneY=portrait?42+(demoH-50-sceneHeight_)/2:42;
  for(int n=0;n<4;++n){stages_[n]=rect(demo,8,sceneY,sceneWidth_,sceneHeight_,theme::background,0);scene(n,stages_[n]);lv_obj_set_hidden(stages_[n],true);}
  // Snapshot only the active synthetic stage into one reused texture. The image
  // owns no GPU/display resources and is deleted before its backing buffers.
  try {
    const int canvasH=std::min(360,demoH-40);
    renderer_.reset(new Onboarding3D(std::min(640,sceneWidth_),canvasH));
    texture_=lv_snapshot_create_draw_buf(stages_[0],LV_COLOR_FORMAT_ARGB8888);
    if(!texture_)throw std::bad_alloc();
    renderDescriptor_.header.magic=LV_IMAGE_HEADER_MAGIC;
    renderDescriptor_.header.cf=LV_COLOR_FORMAT_XRGB8888;
    renderDescriptor_.header.w=renderer_->width();renderDescriptor_.header.h=renderer_->height();
    renderDescriptor_.header.stride=renderer_->width()*4;
    renderDescriptor_.data_size=renderer_->width()*renderer_->height()*4;
    renderDescriptor_.data=reinterpret_cast<const uint8_t *>(renderer_->pixels());
    renderImage_=lv_image_create(demo);lv_obj_set_pos(renderImage_,8,32+(demoH-40-canvasH)/2);
    lv_obj_set_clickable(renderImage_,false);lv_image_set_src(renderImage_,&renderDescriptor_);
  } catch(const std::exception &) {
    renderer_.reset();if(texture_){lv_draw_buf_destroy(texture_);texture_=nullptr;}
    renderFailed_=true;
  }
  const int top=portrait?demoH+92:13;
  text(root_,std::to_string(step+1)+" / 7"+(step<3?" · 必看":" · 功能介绍"),right,top,measure,16,theme::muted);
  text(root_,titles[step],right,top+30,measure,portrait?28:24);
  auto description=text(root_,copy[step],right,top+70,measure,portrait?21:(measure<340?16:18));
  lv_obj_set_style_text_line_space(description,measure<340&&!portrait?3:5,0);
  if(step==2&&configured)text(root_,"已配置模型，可直接继续",right,portrait?top+176:174,measure,16,theme::link);
  if(step==6)text(root_,"可在“关于 PenChat”中重看引导",right,portrait?top+176:174,measure,16,theme::muted);
  const int playbackY=portrait?demoH+28:footer;
  pause_=control("pause","暂停",16,playbackY,80);control("replay","重播",104,playbackY,80);
  auto phaseCaption=text(demo,captions[step][0],126,8,portrait?width-190:split-166,16,theme::link);actions_["caption"]=phaseCaption;
  if(step>0)control("back","上一步",width-220,footer,92);
  control("next",step==6?"开始使用":"下一步",width-120,footer,100,true);
  if(step==2)control("configure","去配置",right,footer,100);
  else if(step>=3||replaying)control("finish",replaying?"关闭引导":"结束引导",right,footer,112);
  tick(true);
}
Onboarding::~Onboarding(){
  if(renderImage_)lv_image_set_src(renderImage_,nullptr);
  lv_image_cache_drop(&renderDescriptor_);
  if(root_)lv_obj_delete(root_);
  if(texture_)lv_draw_buf_destroy(texture_);
}
void Onboarding::render(int phase,float progress,bool changed) {
  if(!renderer_)return;
  const auto started=std::chrono::steady_clock::now();
  lv_result_t result=LV_RESULT_OK;
  if(changed || texturePhase_!=phase) {
    lv_obj_set_hidden(stages_[phase],false);
    result=lv_snapshot_take_to_draw_buf(stages_[phase],LV_COLOR_FORMAT_ARGB8888,texture_);
    lv_obj_set_hidden(stages_[phase],true);
    texturePhase_=phase;++textureSnapshots_;
  }
  if(result!=LV_RESULT_OK) {
    // Keep the tutorial navigable when a bounded snapshot cannot be allocated.
    lv_image_set_src(renderImage_,nullptr);lv_obj_set_hidden(renderImage_,true);
    lv_image_cache_drop(&renderDescriptor_);renderer_.reset();renderFailed_=true;
    lv_draw_buf_destroy(texture_);texture_=nullptr;lv_obj_set_hidden(stages_[phase],false);return;
  }
  renderer_->render({texture_->data,int(texture_->header.w),int(texture_->header.h),int(texture_->header.stride)},
                    step_,phase,progress,theme::sidebar,theme::accent);
  lv_image_cache_drop(&renderDescriptor_);lv_obj_invalidate(renderImage_);
  const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
  renderMaxUs_=std::max<int64_t>(renderMaxUs_,us);renderTotalUs_+=us;++renderSamples_;
}
void Onboarding::tick(bool visible) {
  const auto now=ticks();
  if(visible&&!paused_&&last_)elapsed_+=std::min<int64_t>(now-last_,100);
  last_=visible?now:0;
  if(!visible||(paused_&&phase_>=0)||now-drawn_<34)return;
  drawn_=now;
  const int phase=int(elapsed_%6400)/1600;
  if(phase_!=phase){for(int n=0;n<4;++n)lv_obj_set_hidden(stages_[n],renderer_||n!=phase);phase_=phase;lv_label_set_text(actions_.at("caption"),captions[step_][phase]);}
  const double t=double(elapsed_%1600)/1600;
  bool changed=false;
  for(const auto &m:motions_[phase]) {
    const int x=m.x+int(m.dx*t),y=m.y+int(m.dy*t);
    if(lv_obj_get_x(m.object)!=x || lv_obj_get_y(m.object)!=y) {
      lv_obj_set_pos(m.object,x,y);changed=true;
    }
  }
  render(phase,float(t),changed);
}
void Onboarding::toggle(){paused_=!paused_;last_=0;lv_label_set_text(lv_obj_get_child(pause_,0),paused_?"继续":"暂停");}
void Onboarding::replay(){elapsed_=0;phase_=-1;last_=0;paused_=false;lv_label_set_text(lv_obj_get_child(pause_,0),"暂停");tick(true);}
void Onboarding::busy(bool b){busy_=b;for(auto &e:actions_)if(e.first!="caption"){if(b)lv_obj_add_state(e.second,LV_STATE_DISABLED);else lv_obj_remove_state(e.second,LV_STATE_DISABLED);}}
Json Onboarding::diagnostic() const {
  Json actions=Json::object();for(const auto &e:actions_){if(e.first=="caption")continue;lv_area_t a;lv_obj_get_coords(e.second,&a);actions[e.first]=Json::array({a.x1,a.y1,a.x2,a.y2});}
  return {{"step",step_},{"phase",phase_},{"elapsed",elapsed_},{"paused",paused_},{"busy",busy_},{"actions",actions},{"objects",lv_obj_get_child_count(root_)},
    {"renderer",renderer_?"software_3d":"flat"},{"render_failed",renderFailed_},
    {"render_snapshots",textureSnapshots_},{"render_frames",renderSamples_},{"render_mean_us",renderSamples_?renderTotalUs_/renderSamples_:0},
    {"render_max_us",renderMaxUs_},{"render_bytes",renderer_?renderer_->bytes()+texture_->data_size:0},
    {"render_triangles",renderer_?renderer_->triangles():0}};
}
} // namespace penchat
