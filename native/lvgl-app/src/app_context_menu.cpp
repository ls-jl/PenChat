#include "app.hpp"
#include "icons.hpp"
#include <cmath>
namespace penchat {
bool App::keepChatEditor() const {
  return layout().portrait() && view_=="chat" && editor_.active && editorInlineChat_ && editorInPlace();
}
LayoutRect App::pickerArea(int desiredWidth) const {
  if(!keepChatEditor())return {20,12,desiredWidth,screen_.height-78};
  const int top=std::max(12,layout().safeTop());
  const int height=std::max(44,chatComposerY()-top-8);
  return placeContextPopup(screen_.width,screen_.height,roundedDisplay_,
                           {layout().safeSide(),chatComposerY(),1,1},desiredWidth,height);
}
bool App::finishChatEditorForNavigation() {
  return !keepChatEditor() || editorAccept();
}
void App::selected(lv_obj_t *object,bool value) {
  lv_obj_set_style_bg_color(object,lv_color_hex(theme::surface),0);
  if(value)lv_obj_add_state(object,LV_STATE_CHECKED);
  else lv_obj_remove_state(object,LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(object,lv_color_hex(theme::surface),LV_STATE_CHECKED);
  auto caption=lv_obj_get_child(object,0);
  if(caption) {
    // Newly created controls have not necessarily resolved their width yet.
    // Measuring before layout collapsed captions to the 24px minimum ("...").
    lv_obj_update_layout(object);
    lv_obj_set_width(caption,std::max(24,int(lv_obj_get_width(object))-52));
    lv_obj_align(caption,LV_ALIGN_LEFT_MID,36,0);
    lv_obj_set_style_text_color(caption,lv_color_hex(theme::text),0);
  }
  if(value) {
    auto mark=box(object,8,(lv_obj_get_height(object)-24)/2,24,24,theme::surface);
    lv_obj_set_scrollable(mark,false);lv_obj_set_clickable(mark,false);
    attachIcon(mark,"check",theme::accent);
  }
}
void App::choiceMenu(const std::string &kind,
                     const std::vector<std::pair<std::string,std::string>> &options,
                     const std::string &current,
                     std::function<void(const std::string &)> apply) {
  int width=224;
  for(const auto &option:options) {
    lv_point_t measured{};
    lv_text_get_size(&measured,option.second.c_str(),font(20),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
    width=std::max(width,int(measured.x)+72);
  }
  auto list=contextMenu("choice:"+kind,int(options.size()),std::min(440,width));
  const int rowHeight=layout().portrait()?theme::portraitPopupRowHeight:theme::popupRowHeight;
  int y=0;
  for(const auto &option:options) {
    const auto key=option.first;
    auto row=button(list,option.second,0,y,lv_obj_get_width(list),rowHeight-4,
      [this,key,apply]{closeHistoryContext();apply(key);},theme::popup,LV_TEXT_ALIGN_LEFT);
    selected(row,key==current);stylePopupRow(row);
    choiceActions_[key]=row;
    y+=rowHeight;
  }
}
lv_obj_t *App::contextMenu(const std::string &kind,int rows,int width) {
  readReset();closeAttachments();resetPull();
  if(modal_)lv_obj_delete(modal_);
  contextMenuKind_=kind;
  contextMenuOpening_=lastTouch_.value("pressed",false);
  input_.cancel();
  auto layer=box(screenObj_,0,0,screen_.width,screen_.height,theme::scrim);
  modal_=layer;modalScroll_=nullptr;modalButtons_.clear();
  lv_obj_set_style_bg_opa(layer,LV_OPA_TRANSP,0);
  lv_obj_set_scrollable(layer,false);lv_obj_set_clickable(layer,true);
  lv_obj_add_event_cb(layer,[](lv_event_t *event){
    auto self=static_cast<App *>(lv_event_get_user_data(event));
    if(lv_event_get_code(event)==LV_EVENT_DELETE && lv_event_get_target_obj(event)==lv_event_get_current_target_obj(event)) {
      if(self->contextMenuKind_=="balances"){self->readReset();self->cancelBalances();}
      self->balanceRefresh_=nullptr;self->balanceDetailActions_.clear();
      self->modal_=self->modalScroll_=self->contextMenuPanel_=self->contextMenuList_=nullptr;
      self->contextMenuKind_.clear();self->contextMenuOpening_=false;
      self->choiceActions_.clear();
      self->historyActions_.clear();self->historyContextId_.clear();
      self->refreshHistoryHighlight();
      self->messageActions_.clear();self->messageContextId_.clear();self->messageContextText_.clear();
    }
  },LV_EVENT_DELETE,this);
  const bool portrait=layout().portrait();
  const int rowHeight=portrait?theme::portraitPopupRowHeight:theme::popupRowHeight;
  const int padding=portrait?8:4;
  const int availableHeight=keepChatEditor()?editorLayout().panelTop:screen_.height;
  const LayoutRect anchor{lastTouch_.value("x",screen_.width/2),lastTouch_.value("y",screen_.height/2),1,1};
  const auto area=placeContextPopup(screen_.width,screen_.height,roundedDisplay_,anchor,
                                   width>0?width:portrait?320:224,rows*rowHeight+2*padding,availableHeight);
  auto panel=box(layer,area.x,area.y,area.width,area.height,theme::popup);
  contextMenuPanel_=panel;stylePopupPanel(panel,portrait);lv_obj_set_scrollable(panel,false);
  auto list=box(panel,padding,padding,area.width-2*padding,
                std::max(44,area.height-2*padding),theme::popup);
  contextMenuList_=list;lv_obj_set_scroll_dir(list,LV_DIR_VER);
  lv_obj_set_style_width(list,3,LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(list,lv_color_hex(theme::border),LV_PART_SCROLLBAR);
  lv_obj_set_scroll_elastic(list,false);lv_obj_set_scroll_chain_ver(list,false);
  // Callers size their rows from this reader immediately. Resolve percentage
  // and content geometry before they inspect its width.
  lv_obj_update_layout(layer);
  return list;
}
bool App::popupTouch(const Json &message) {
  const bool down=message.value("pressed",false)&&!message.value("cancelled",false);
  if(popupConsumeTouch_||contextMenuOpening_) {
    if(!down){popupConsumeTouch_=contextMenuOpening_=false;input_.cancel();}
    return true;
  }
  if(contextMenuKind_=="balances" && readTouch(message.value("x",0),message.value("y",0),down,message.value("cancelled",false)))return true;
  auto panel=contextMenuPanel_?contextMenuPanel_:((keepChatEditor()||popupKind_=="attachments")?attachmentPanel_:nullptr);
  if(!panel)return false;
  if(message.contains("points")&&message.at("points").is_array()&&message.at("points").size()>1) {
    popupConsumeTouch_=true;editorCancelKeys();input_.cancel();
    if(contextMenuKind_=="balances")readTouch(message.value("x",0),message.value("y",0),false,true);
    return true;
  }
  lv_area_t bounds;lv_obj_get_coords(panel,&bounds);
  const int x=message.value("x",0),y=message.value("y",0);
  const bool inside=x>=bounds.x1&&x<=bounds.x2&&y>=bounds.y1&&y<=bounds.y2;
  if(down&&!inside) {
    popupConsumeTouch_=true;editorCancelKeys();input_.cancel();
    if(contextMenuPanel_)closeHistoryContext();else closeAttachments();
    return true;
  }
  // These controls are on the page layer, not the retained keyboard root.
  // Route inside touches directly to LVGL, before key/caret arbitration.
  if(message.value("cancelled",false))input_.cancel();else input_.touch(x,y,down);
  return true;
}
} // namespace penchat
