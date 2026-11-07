#pragma once
#include "platform.hpp"
#include "onboarding_3d.hpp"
#include "theme.hpp"
#include <array>
#include <map>
namespace penchat {
// A self-contained, synthetic demonstration. It has no business-service handles.
class Onboarding {
  struct Motion { lv_obj_t *object; int x, y, dx, dy; };
  lv_obj_t *root_ = nullptr, *pause_ = nullptr;
  std::array<lv_obj_t *, 4> stages_{};
  std::array<std::vector<Motion>, 4> motions_;
  std::map<std::string, lv_obj_t *> actions_;
  std::function<void(const std::string &)> action_;
  std::unique_ptr<Onboarding3D> renderer_;
  lv_obj_t *renderImage_ = nullptr;
  lv_draw_buf_t *texture_ = nullptr;
  lv_image_dsc_t renderDescriptor_{};
  int texturePhase_ = -1;
  unsigned textureSnapshots_ = 0;
  int64_t renderMaxUs_ = 0, renderTotalUs_ = 0;
  unsigned renderSamples_ = 0;
  bool renderFailed_ = false;
  void render(int phase, float progress, bool changed);
  int step_ = 0, phase_ = -1, sceneWidth_ = 0, sceneHeight_ = 0;
  int64_t elapsed_ = 0, last_ = 0, drawn_ = 0;
  bool paused_ = false, busy_ = false;
  lv_obj_t *rect(lv_obj_t *,int,int,int,int,uint32_t,int=8);
  lv_obj_t *text(lv_obj_t *,const std::string &,int,int,int,int=16,uint32_t=theme::text);
  lv_obj_t *control(const std::string &,const std::string &,int,int,int,bool=false);
  lv_obj_t *part(lv_obj_t *,int,int,int,int,uint32_t=theme::surface,int=8);
  void words(lv_obj_t *,const std::string &,int,int,int,int=16,uint32_t=theme::text);
  void symbol(lv_obj_t *,const std::string &,int,int,int=24,uint32_t=theme::text);
  void paper(lv_obj_t *,int,int,int,int);
  void chat(lv_obj_t *,bool=false);
  void scene(int,lv_obj_t *);
public:
  Onboarding(lv_obj_t *,int,int,int,bool,bool,std::function<void(const std::string &)>);
  ~Onboarding();
  void tick(bool visible);
  void toggle();
  void replay();
  void busy(bool);
  Json diagnostic() const;
};
} // namespace penchat
