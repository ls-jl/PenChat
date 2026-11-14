#include "app.hpp"
#include <cassert>
#include <iostream>
#include <unistd.h>
namespace penchat {
struct UiControlsTest {
  static void run(App &a) {
    a.clear();
    const auto &m = a.uiMetrics_;
    a.screenObj_ = a.box(lv_screen_active(), 0, 0, a.screen_.width, a.screen_.height);
    a.body_ = a.box(a.screenObj_, 0, 0, a.screen_.width, a.screen_.height);
    a.pageHeader("度量测试", 0);
    int clicks = 0;
    auto action = a.pageAction("动作", 0, m.dp(80), [&] { ++clicks; });
    auto button = a.button(a.pageContent_, "很长的按钮标题 Long caption", 7, 9, m.dp(180), m.dp(44), [&] { ++clicks; });
    auto caption = lv_obj_get_child(button, 0);
    auto field = a.fieldButton(a.pageContent_, "字段", "值 value", 7, m.dp(60), m.dp(200), [&] { ++clicks; });
    a.settingsRow("fixture", "设置", "说明", 7, m.dp(130), m.dp(230), [&] { ++clicks; }, 1);
    a.settingsRow("arrow", "更多", "说明", 7, m.dp(190), m.dp(230), [&] { ++clicks; }, -1);
    auto &setting = a.settingsControls_.at("fixture");
    auto narrow = a.button(a.pageContent_, "长标题", 0, 0, 8, m.dp(44), [] {});
    auto explicitPixels = a.label(a.pageContent_, "pixels", 3, 4, 80, 19);
    auto automaticSize = a.box(a.pageContent_, 0, 0, LV_PCT(50), LV_SIZE_CONTENT);
    lv_obj_update_layout(a.screenObj_);
    assert(lv_obj_get_x(button) == 7 && lv_obj_get_y(button) == 9);
    assert(lv_obj_get_width(button) == m.dp(180) && lv_obj_get_height(button) == m.dp(44));
    assert(lv_obj_get_style_radius(button, LV_PART_MAIN) == m.button().radius);
    assert(lv_obj_get_width(caption) == m.dp(180) - m.button().captionPadding);
    assert(lv_label_get_long_mode(caption) == LV_LABEL_LONG_MODE_DOTS);
    assert(lv_obj_get_height(caption) == font(m.fontSize(UiFont::Button))->line_height);
    assert(lv_obj_get_style_text_font(caption, LV_PART_MAIN) == font(m.fontSize(UiFont::Button)));
    assert(lv_obj_get_width(lv_obj_get_child(narrow, 0)) == 1);
    assert(lv_obj_get_style_text_font(explicitPixels, LV_PART_MAIN) == font(19));
    assert(lv_obj_get_style_width(automaticSize, LV_PART_MAIN) == LV_PCT(50));
    assert(lv_obj_get_style_height(automaticSize, LV_PART_MAIN) == LV_SIZE_CONTENT);
    const auto f = m.field();
    const bool compact = a.layout().compact() || !m.identity();
    const int titleHeight = compact ? font(m.fontSize(UiFont::FieldTitle))->line_height : f.captionHeight;
    const int valueHeight = compact ? font(m.fontSize(UiFont::FieldValue))->line_height : f.valueHeight;
    const int valueTop = compact ? f.top + titleHeight + f.gap : f.valueTop;
    assert(lv_obj_get_height(field) == (compact ? std::max(f.height, valueTop + valueHeight + f.top) : f.height));
    assert(lv_obj_get_y(lv_obj_get_child(field, 0)) == valueTop);
    assert(lv_obj_get_style_text_font(lv_obj_get_child(field, 1), LV_PART_MAIN) == font(m.fontSize(UiFont::FieldTitle)));
    assert(lv_obj_get_height(a.pageHeader_) == m.page().headerHeight);
    assert(lv_obj_get_y(a.pageContent_) == a.layout().safeTop() + m.page().headerHeight);
    assert(lv_obj_get_height(action) == m.page().actionHeight);
    assert(lv_obj_get_height(setting.row) == m.settings().height);
    assert(lv_obj_get_x(setting.indicator) == m.dp(230) - m.settings().toggleTrailing);
    assert(lv_obj_get_width(setting.title) == m.dp(230) - m.settings().textTrailing);
    assert(lv_obj_get_width(setting.indicator) == m.settings().toggleWidth);
    assert(!lv_obj_is_clickable(setting.indicator));
    assert(!lv_obj_is_clickable(setting.title));
    assert(lv_obj_get_x(a.settingsControls_.at("arrow").indicator) == m.dp(230) - m.settings().arrowTrailing);
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(field, LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(setting.row, LV_EVENT_CLICKED, nullptr);
    lv_obj_send_event(action, LV_EVENT_CLICKED, nullptr);
    assert(clicks == 4);
    // Existing compact/portrait form reflow must not restore unscaled internals.
    a.view_ = "provider";
    a.providerForm_ = {{"name", "fixture"}};
    a.portraitPage();
    lv_obj_update_layout(a.screenObj_);
    assert(lv_obj_get_width(setting.title) == lv_obj_get_width(setting.row) - m.settings().textTrailing);
    // Keyboard helpers must remain in their existing design coordinate space.
    a.editorRoot_ = a.box(a.screenObj_, 0, 0, 240, 120);
    auto key = a.button(a.editorRoot_, "a", 0, 0, 80, 44, [] {});
    lv_obj_update_layout(a.screenObj_);
    assert(lv_obj_get_style_text_font(lv_obj_get_child(key, 0), LV_PART_MAIN) == font(a.editorMetrics().fontSize(UiFont::Button)));
    assert(lv_obj_get_style_radius(key, LV_PART_MAIN) == a.editorMetrics().dp(10));
    assert(lv_obj_get_width(lv_obj_get_child(key, 0)) == 80-a.editorMetrics().dp(24));
    lv_obj_delete(a.editorRoot_);
    a.editorRoot_ = nullptr;
    a.clear();
    a.view_="chat";
    for (bool gauge : {false,true}) for (const auto &draft : {std::string(),std::string("short"),std::string(1000,'x')}) {
      a.sessionPreferences_["gauge"]=gauge;
      a.draft_=draft;
      a.build();
      lv_obj_update_layout(a.screenObj_);
      const auto layout=a.layout();const auto c=m.composer();
      assert(lv_obj_get_x(a.chatControls_)==layout.composerX());
      assert(lv_obj_get_width(a.chatControls_)==layout.composerWidth());
      assert(lv_obj_get_height(a.chatControls_)==a.chatComposerHeight());
      assert(lv_obj_get_style_radius(a.chatControls_,LV_PART_MAIN)==layout.composerHeight()/2);
      assert(lv_obj_get_y(a.toBottom_)==a.chatComposerY()-c.returnOffset());
      assert(lv_obj_get_width(a.toBottom_)==c.control);
      assert(lv_obj_get_style_text_font(lv_obj_get_child(a.composer_,0),LV_PART_MAIN)==font(m.fontSize(UiFont::Composer)));
      const int count=lv_obj_get_child_count(a.chatControls_);
      auto send=lv_obj_get_child(a.chatControls_,count-1);
      assert(lv_obj_get_width(send)==layout.composerSendDiameter());
      for (int i=0;i<count;++i) {
        auto child=lv_obj_get_child(a.chatControls_,i);
        assert(lv_obj_get_x(child)>=0 && lv_obj_get_y(child)>=0);
        assert(lv_obj_get_x(child)+lv_obj_get_width(child)<=layout.composerWidth());
        assert(lv_obj_get_y(child)+lv_obj_get_height(child)<=a.chatComposerHeight());
      }
      if(gauge) {
        auto gaugeButton=lv_obj_get_child(a.chatControls_,1);
        assert(lv_obj_get_x(gaugeButton)+lv_obj_get_width(gaugeButton)<=lv_obj_get_x(send));
      }
    }
    a.clear();
    a.show("provider");a.beginProvider();a.providerAdvanced_=true;a.build();
    lv_obj_update_layout(a.screenObj_);
    for (const auto &key : {"name","baseURL","apiKey"}) {
      auto field=a.pageActions_.at(std::string("field:")+key);
      auto value=lv_obj_get_child(field,0),title=lv_obj_get_child(field,1);
      assert(lv_obj_get_style_text_font(title,LV_PART_MAIN)==font(m.fontSize(UiFont::FieldTitle)));
      assert(lv_obj_get_style_text_font(value,LV_PART_MAIN)==font(m.fontSize(UiFont::FieldValue)));
      if(!m.identity()) {
        assert(lv_obj_get_height(title)>=font(m.fontSize(UiFont::FieldTitle))->line_height);
        assert(lv_obj_get_height(value)>=font(m.fontSize(UiFont::FieldValue))->line_height);
        assert(lv_obj_get_y(title)+lv_obj_get_height(title)<=lv_obj_get_y(value));
        assert(lv_obj_get_y(value)+lv_obj_get_height(value)<=lv_obj_get_height(field));
      }
    }
    for (const auto &protocol : {"responses","anthropic"}) {
      a.providerForm_["api"]=protocol;
      a.editModel({{"id","fixture"},{"name","gpt-5"},{"display_name","测试模型"},
                   {"capabilities_json",{{"reasoning",{{"levels",{{"high","high"}}},{"summary",true}}}}}});
      for (bool advanced : {false,true}) {
        a.modelAdvanced_=advanced;a.build();lv_obj_update_layout(a.screenObj_);
        for(const auto &entry:a.settingsControls_) {
          const auto &control=entry.second;
          assert(lv_obj_get_style_text_font(control.title,LV_PART_MAIN)==font(m.fontSize(UiFont::SettingsTitle)));
          assert(lv_obj_get_style_text_font(control.detail,LV_PART_MAIN)==font(m.fontSize(UiFont::Detail)));
          assert(lv_obj_get_y(control.detail)+lv_obj_get_height(control.detail)<=lv_obj_get_height(control.row));
        }
        for(unsigned i=0;i<lv_obj_get_child_count(a.pageContent_);++i) {
          auto object=lv_obj_get_child(a.pageContent_,i);
          if(lv_obj_is_floating(object)&&lv_obj_get_user_data(object)) {
            auto anchor=static_cast<lv_obj_t *>(lv_obj_get_user_data(object));
            assert(lv_obj_get_x(object)>=lv_obj_get_x(anchor)+lv_obj_get_width(anchor));
            assert(lv_obj_get_y(object)==lv_obj_get_y(anchor)+m.form().actionTop);
            assert(lv_obj_get_width(object)==m.form().actionSize);
          }
        }
        if(a.layout().portrait()&&!m.identity()) {
          for(const auto &entry:a.pageActions_)if(lv_obj_get_parent(entry.second)==a.pageHeader_)
            assert(lv_obj_get_x(entry.second)>=a.layout().navigationX()+m.page().actionHeight);
        }
      }
    }
    a.clear();
  }
};
}
int main(int argc, char **argv) {
  using namespace penchat;
  assert(argc == 6);
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return uint32_t(ticks()); });
  initializeFonts(join(argv[1], ".deps/chinese.ttf"), join(argv[1], "assets/fonts/DejaVuSans.ttf"));
  {
    const UiMetrics metrics(100, std::stoi(argv[3]));
    {
      const auto size = std::make_pair(std::stoi(argv[4]), std::stoi(argv[5]));
      const auto directory = join(argv[2], std::to_string(size.first));
      ensureDirectory(directory);
      Screen screen; screen.width=size.first; screen.height=size.second;
      Display display(screen, true);
      Input input;
      Bridge bridge(-1);
      Database db(join(directory, "penchat.db"));
      App app(db, bridge, display, input, screen, argv[1], directory,
              join(argv[1], ".deps/chinese.ttf"), join(argv[1], ".deps/microtex/res"), false, Json::object(), metrics);
      UiControlsTest::run(app);
    }
  }
  lv_deinit();
  shutdownFonts();
  std::cout << "UI_CONTROLS_LAYOUT_FONT_HIT_AND_KEYBOARD_ISOLATION_OK\n";
}
