#include "app.hpp"
#include "effort_slider.hpp"
#include "ui_motion.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <fstream>
namespace penchat {
struct MenusMotionTest {
  static uint32_t clock;
  static void advance(int milliseconds) {clock+=milliseconds;lv_timer_handler();}
  static void components(App &a) {
    a.clear();
    auto root=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(root);
    lv_obj_set_size(root,a.screen_.width,a.screen_.height);lv_obj_set_scrollable(root,false);
    struct Pointer {lv_point_t point{};lv_indev_state_t state=LV_INDEV_STATE_RELEASED;} pointer;
    auto input=lv_indev_create();lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);lv_indev_set_user_data(input,&pointer);
    lv_indev_set_read_cb(input,[](lv_indev_t *input,lv_indev_data_t *data){
      auto p=static_cast<Pointer *>(lv_indev_get_user_data(input));data->point=p->point;data->state=p->state;
    });
    for(int count:{1,2,8}) {
      int preview=-1,committed=-1,cancelled=-1;
      auto slider=createEffortSlider(root,10,10,a.screen_.width-20,count,0,a.uiMetrics_,
          [&](int index,EffortSliderEvent event){
            if(event==EffortSliderEvent::Preview)preview=index;
            if(event==EffortSliderEvent::Commit)committed=index;
            if(event==EffortSliderEvent::Cancel)cancelled=index;
          });
      lv_obj_update_layout(root);lv_area_t area;lv_obj_get_coords(slider,&area);
      for(int index=0;index<count;++index) {
        const int x=effortSliderCenter(area,count,index);
        assert(effortSliderIndex(area,count,x)==index);
        pointer.point={x,(area.y1+area.y2)/2};pointer.state=LV_INDEV_STATE_PRESSED;lv_indev_read(input);
        assert(preview==index);
        pointer.state=LV_INDEV_STATE_RELEASED;lv_indev_read(input);assert(committed==index);
      }
      assert(effortSliderIndex(area,count,area.x1-100)==0);
      assert(effortSliderIndex(area,count,area.x2+100)==count-1);
      // A real pointer updates preview, and a cancelled contact restores the
      // prior committed stop without committing the preview to preferences.
      pointer.point={effortSliderCenter(area,count,0),(area.y1+area.y2)/2};
      pointer.state=LV_INDEV_STATE_PRESSED;lv_indev_read(input);
      lv_obj_send_event(slider,LV_EVENT_PRESS_LOST,nullptr);
      assert(cancelled==count-1&&effortSliderData(slider)->index==count-1);
      pointer.state=LV_INDEV_STATE_RELEASED;lv_indev_read(input);lv_obj_delete(slider);
    }
    // Continuous movement is independent of discrete preview; click and release
    // must paint intermediate frames, and cancellation restores the commitment.
    int commit=-1;
    auto slider=createEffortSlider(root,10,10,a.screen_.width-20,4,0,a.uiMetrics_,
        [&](int index,EffortSliderEvent event){if(event==EffortSliderEvent::Commit)commit=index;});
    lv_obj_update_layout(root);lv_area_t area;lv_obj_get_coords(slider,&area);
    auto data=effortSliderData(slider);
    const int first=effortSliderCenter(area,4,0),last=effortSliderCenter(area,4,3),cy=(area.y1+area.y2)/2;
    pointer.point={first,cy};pointer.state=LV_INDEV_STATE_PRESSED;lv_indev_read(input);
    pointer.point={first+(last-first)/2,cy};lv_indev_read(input);
    assert(data->position>4500&&data->position<5500&&commit==-1);
    Json frames=Json::array();
    auto frame=[&](const char *name){a.display_.screenshot(join(a.dataDir_,std::string(name)+".png"));frames.push_back({{"name",name},{"position",data->position}});};
    frame("slider-drag");
    pointer.state=LV_INDEV_STATE_RELEASED;lv_indev_read(input);
    const int released=data->position;frame("slider-release");advance(45);assert(data->position!=released);frame("slider-snap-mid");advance(100);frame("slider-snap-end");
    assert(commit==data->index&&data->position==data->index*10000/3);
    {std::ofstream output(join(a.dataDir_,"slider-frames.json"));output<<Json{{"area",{area.x1,area.y1,area.x2,area.y2}},{"frames",frames},{"color",theme::palette().text},{"theme",theme::mode()==theme::Mode::Light?"light":"dark"}}.dump(2);}
    const int original=commit;
    pointer.point={first,cy};pointer.state=LV_INDEV_STATE_PRESSED;lv_indev_read(input);
    advance(65);assert(data->position>0&&data->position<10000);
    lv_obj_send_event(slider,LV_EVENT_PRESS_LOST,nullptr);advance(120);
    assert(commit==original&&data->index==original&&data->position==original*10000/3);
    pointer.state=LV_INDEV_STATE_RELEASED;lv_indev_read(input);
    pointer.point={last,cy};pointer.state=LV_INDEV_STATE_PRESSED;lv_indev_read(input);advance(35);
    lv_obj_delete(slider);advance(200); // No animation may retain the freed data.
    pointer.state=LV_INDEV_STATE_RELEASED;lv_indev_read(input);
    lv_indev_delete(input);
    auto gauge=lv_obj_create(root);lv_obj_remove_style_all(gauge);lv_obj_set_size(gauge,44,44);
    attachIcon(gauge,"gauge",theme::palette().text,1.f,a.uiMetrics_);
    const auto callbacks=lv_obj_get_event_count(gauge);
    for(int count:{1,2,8})for(int index=0;index<count;++index) {
      setGaugePosition(gauge,index,count);
      assert(std::fabs(gaugePosition(gauge)-(count==1?.5f:float(index)/(count-1)))<.0001f);
      assert(lv_obj_get_event_count(gauge)==callbacks);
    }
    auto content=lv_obj_create(root);lv_obj_remove_style_all(content);lv_obj_set_size(content,100,0);
    auto arrow=lv_obj_create(root);attachIcon(arrow,"chevron-right",theme::palette().text);
    motion::disclosure(content,arrow,true,100);advance(65);lv_obj_update_layout(root);
    const int interrupted=lv_obj_get_height(content);assert(interrupted>0&&interrupted<100);
    motion::disclosure(content,arrow,false,100);lv_obj_update_layout(root);
    assert(lv_obj_get_height(content)==interrupted);advance(190);lv_obj_update_layout(root);
    assert(lv_obj_get_height(content)==0&&disclosureProgress(arrow)==0);
    motion::disclosure(content,arrow,true,100);lv_obj_delete(content);lv_obj_delete(arrow);advance(200);
    lv_obj_delete(root);motion::finishAll();
  }
  static void balances(App &a) {
    a.view_="chat";a.build();a.visible_=false;
    a.openBalances();a.balancePage();motion::finishAll();lv_obj_update_layout(a.screenObj_);
    const int empty=lv_obj_get_height(a.contextMenuPanel_);
    assert(empty>0);a.closeHistoryContext();motion::finishAll();
    std::vector<std::string> ids;
    for(int i=0;i<5;++i) {
      Json form={{"name",std::string("本地余额Fixture ")+std::to_string(i)},
        {"baseURL","https://example.invalid/v1"},{"apiKey","fake-local-fixture"},{"api","responses"},{"balance_vendor","deepseek"}};
      ids.push_back(a.db_.saveProvider(form,{},false));
      if(i==0) {
        a.openBalances();a.balancePage();motion::finishAll();lv_obj_update_layout(a.screenObj_);
        assert(lv_obj_get_height(a.contextMenuPanel_)>0);
        if(a.layout().portrait())assert(lv_obj_get_height(a.contextMenuPanel_)<=a.screen_.height/3);
        a.closeHistoryContext();motion::finishAll();
      }
    }
    for(const auto &p:a.db_.providers()) {
      const auto id=p.at("id").get<std::string>();
      a.balanceResults_[id]={{"identity",balanceIdentity(p)},{"status","ready"},{"queried_at",int64_t(1760000000000LL)},
        {"balances",Json::array({{{"amount","12.345"},{"unit","CNY"},{"details",Json::array({{{"label","充值余额"},{"amount","10"}},{{"label","赠送余额"},{"amount","2.345"}}})}},
          {{"amount","0.08"},{"unit","USD"},{"details",Json::array()}}})}};
    }
    a.openBalances();a.balancePage();motion::finishAll();lv_obj_update_layout(a.screenObj_);
    const auto safe=popupSafeArea(a.screen_.width,a.screen_.height,a.roundedDisplay_,a.uiMetrics_);
    const int cap=a.layout().portrait()?a.screen_.height/3:safe.height;
    assert(lv_obj_get_height(a.contextMenuPanel_)<=cap);
    assert(lv_obj_get_scroll_bottom(a.contextMenuList_)>0);
    auto arrow=a.balanceDetailActions_.at(ids.front());
    const auto panel=a.contextMenuPanel_,reader=a.contextMenuList_;
    lv_obj_send_event(arrow,LV_EVENT_CLICKED,nullptr);advance(200);lv_obj_update_layout(panel);
    assert(a.balanceExpanded_.count(ids.front())&&a.contextMenuPanel_==panel&&a.contextMenuList_==reader);
    assert(disclosureProgress(arrow)==1000);
    lv_obj_scroll_to_y(reader,30,LV_ANIM_OFF);const int scroll=lv_obj_get_scroll_y(reader);
    a.balanceResults_[ids.front()]["status"]="error";
    a.balanceResults_[ids.front()]["error"]="本地夹具：刷新失败，保留上次结果";
    a.balancePage();lv_obj_update_layout(panel);
    assert(lv_obj_get_height(panel)<=cap&&lv_obj_get_scroll_y(reader)==scroll);
    // Replacement completion must not erase the new popup's pointers.
    a.closeHistoryContext();auto choice=a.contextMenu("fixture",1);advance(200);
    assert(a.contextMenuList_==choice&&a.contextMenuKind_=="fixture");
    a.closeHistoryContext();motion::finishAll();a.visible_=true;a.clear();
  }
  static void historyFooter(App &a) {
    a.view_="chat";a.build();a.historyDrawer();motion::finishAll();
    lv_obj_update_layout(a.screenObj_);
    auto newChat=a.navigationFooter_.at("newchat");
    auto settings=a.navigationFooter_.at("settings");
    auto panel=lv_obj_get_parent(settings);
    assert(lv_obj_get_parent(newChat)==panel);
    lv_area_t panelArea,newArea,settingsArea;
    lv_obj_get_coords(panel,&panelArea);lv_obj_get_coords(newChat,&newArea);
    lv_obj_get_coords(settings,&settingsArea);
    if(a.layout().portrait()&&newArea.y1!=settingsArea.y1)assert(newArea.y2<settingsArea.y1);
    else assert(newArea.x1<settingsArea.x1&&newArea.y1==settingsArea.y1);
    for(auto control:{newChat,settings}) {
      lv_area_t area;lv_obj_get_coords(control,&area);
      assert(area.x1>=0&&area.y1>=0&&area.x2<a.screen_.width&&area.y2<a.screen_.height);
      assert(lv_obj_has_flag(control,LV_OBJ_FLAG_CLICKABLE));
      // The icon and one-line caption belong to the same action target;
      // neither child may intercept its pointer events.
      assert(lv_obj_get_child_count(control)==2);
      auto caption=lv_obj_get_child(control,0),glyph=lv_obj_get_child(control,1);
      assert(lv_obj_check_type(caption,&lv_label_class));
      const std::string text=lv_label_get_text(caption);
      assert(!text.empty());
      if(!a.layout().portrait())assert(text==(control==newChat?"新聊天":"设置"));
      assert(lv_label_get_long_mode(caption)==LV_LABEL_LONG_MODE_DOTS);
      for(auto child:{caption,glyph}) {
        assert(!lv_obj_has_flag(child,LV_OBJ_FLAG_CLICKABLE));
        lv_area_t childArea;lv_obj_get_coords(child,&childArea);
        if(!a.layout().portrait()) {
          assert(childArea.x1>=area.x1&&childArea.x2<=area.x2);
          assert(childArea.y1>=area.y1&&childArea.y2<=area.y2);
        }
      }
    }
    if(!a.layout().portrait()) {
      assert(newArea.x1-panelArea.x1==a.uiMetrics_.dp(12));
      assert(panelArea.x2-settingsArea.x2==a.uiMetrics_.dp(12));
      assert(newArea.x2<settingsArea.x1);
      auto caption=lv_obj_get_child(settings,0);
      lv_point_t text{};
      lv_text_get_size(&text,"设置",font(a.uiMetrics_.fontSize(UiFont::SettingsTitle)),
          0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
      // Widening the drawer must leave Settings at its text/icon natural
      // width, rather than stretching it across the spare footer space.
      assert(lv_obj_get_width(caption)==text.x);
      assert(lv_obj_get_width(settings)==text.x+a.uiMetrics_.dp(56));
      if(lv_obj_get_width(panel)>2*lv_obj_get_width(settings))
        assert(2*lv_obj_get_width(settings)<lv_obj_get_width(panel));
    }
    a.clear();
  }
  static void centeredEffort(App &a) {
    a.roundedDisplay_=a.layout().portrait();
    const auto provider=a.db_.providers().front().at("id");
    a.db_.query("INSERT INTO model_configs(id,provider_id,name,display_name,model_type,is_default,created_at,updated_at) VALUES(?,?,?,?, 'chat',1,1,1)",
               {"centered-effort",provider,"fixture","Fixture"});
    a.db_.query("UPDATE model_configs SET capabilities_json=? WHERE id=?",{Json{{"reasoning",{{"mode","effort"},{"levels",{{"low","low"},{"high","high"}}}}}}.dump(),"centered-effort"});
    a.sessionPreferences_["model_id"]="centered-effort";
    a.view_="chat";a.build();a.effortPicker();motion::finishAll();
    lv_obj_update_layout(a.attachments_);
    lv_area_t area;lv_obj_get_coords(a.attachmentPanel_,&area);
    assert(std::abs(area.x1+area.x2-(a.screen_.width-1))<=1);
    assert(std::abs(area.y1+area.y2-(a.screen_.height-1))<=1);
    assert(lv_obj_get_style_bg_opa(a.attachmentPanel_,LV_PART_MAIN)==0);
    auto title=lv_obj_get_child(a.attachmentPanel_,0);
    auto capsule=lv_obj_get_child(a.attachmentPanel_,1);
    auto slider=lv_obj_get_child(capsule,0);
    assert(std::string(lv_label_get_text(title))=="跟随服务商默认 · 推理强度");
    assert(lv_obj_get_height(capsule)==a.uiMetrics_.dp(60));
    assert(lv_obj_get_y(slider)>=0&&lv_obj_get_y(slider)+lv_obj_get_height(slider)<=lv_obj_get_height(capsule));
    a.closeAttachments();motion::finishAll();a.clear();
  }
  static void run(App &a) {
    const std::vector<std::string> choices{"none","api_default","low","high"};
    auto automatic=effortGaugePlacement(choices,1);
    assert(automatic.isDefault&&automatic.index==1&&automatic.count==3);
    for(int i:{0,2,3}) {
      const auto placement=effortGaugePlacement(choices,i);
      assert(!placement.isDefault&&placement.count==3);
      assert(placement.index==(i?i-1:0));
    }
    assert(effortCaption("high")=="高 · 推理强度");
    assert(effortGaugePlacement({"api_default"},0).index==1);
    assert(effortGaugePlacement({"api_default","high"},1).count==1);

    for(bool portrait:{false,true}) {
      assert(historyTimeDisplay(Json::object(),portrait)==(portrait?"off":"time"));
      assert(historyTimeDisplay({{"history_time_display",123}},portrait)==(portrait?"off":"time"));
      assert(historyTimeDisplay({{"history_time_display","invalid"}},portrait)==(portrait?"off":"time"));
      for(const auto &mode:{"off","time","title"})assert(historyTimeDisplay({{"history_time_display",mode}},portrait)==mode);
    }
    components(a);balances(a);centeredEffort(a);historyFooter(a);
  }
};
uint32_t MenusMotionTest::clock=1000;
}
int main(int argc,char **argv) {
  using namespace penchat;assert(argc==6||argc==7);lv_init();
  lv_tick_set_cb([]()->uint32_t{return MenusMotionTest::clock;});
  initializeFonts(join(argv[1],".deps/chinese.ttf"),join(argv[1],"assets/fonts/DejaVuSans.ttf"));
  {
    Screen screen;screen.width=std::stoi(argv[4]);screen.height=std::stoi(argv[5]);
    Display display(screen,true);Input input;Bridge bridge(-1);
    ensureDirectory(argv[2]);Database db(join(argv[2],"penchat.db"));
    const auto requestedTheme=argc==7?theme::parse(argv[6]):theme::Mode::Dark;
    if(argc==7)db.setting("theme_mode",argv[6]);
    // Production initializes the process palette before constructing App.
    theme::setMode(requestedTheme);
    App app(db,bridge,display,input,screen,argv[1],argv[2],join(argv[1],".deps/chinese.ttf"),
        join(argv[1],".deps/microtex/res"),false,Json::object(),UiMetrics(100,std::stoi(argv[3])));
    assert(theme::mode()==requestedTheme);
    MenusMotionTest::run(app);
  }
  lv_deinit();shutdownFonts();std::cout<<"MENU_SLIDER_GAUGE_MOTION_BALANCE_FIXTURES_OK\n";
}
