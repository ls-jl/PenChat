#pragma once
#include <dlfcn.h>
namespace penchat {
// Read-only access to Falcon's exported SettingPanel (an NWindow). The stock
// SettingPanel::bringToFront uses this same NWindow::isShowing entry point.
// No object offsets, addresses, hooks or system-policy changes are used.
class SystemOverlay {
public:
  struct State { bool supported, visible; };
  using IsShowing=bool (*)(const void *);
  SystemOverlay(void **panel,IsShowing showing):panel_(panel),showing_(showing) {}
  static SystemOverlay realSystem() {
    // Some vendor hosts load Falcon locally. Inspect only the existing module;
    // never load a second framework or rely on the caller's global scope.
    void *module=dlopen("libfalcon.so",RTLD_LAZY|RTLD_NOLOAD);
    void *scope=module?module:RTLD_DEFAULT;
    auto panel=reinterpret_cast<void **>(dlsym(scope,"_ZN6falcon14_setting_panelE"));
    auto showing=reinterpret_cast<IsShowing>(dlsym(scope,"_ZNK7greenui7NWindow9isShowingEv"));
    if(module)dlclose(module);
    return {panel,showing};
  }
  State state() const {
    if(!panel_||!showing_||!*panel_)return {false,false};
    return {true,showing_(*panel_)};
  }
  bool available() const { return panel_&&showing_; }
private:
  void **panel_;
  IsShowing showing_;
};
} // namespace penchat
