#pragma once
#include "app.hpp"
#include <cassert>
#include <iostream>
namespace penchat {
struct PaintPerformanceTest {
  static void run(App &a) {
    a.clear();
    a.view_="paint-performance";
    a.screenObj_=a.box(lv_screen_active(),0,0,a.screen_.width,a.screen_.height);
    a.body_=a.box(a.screenObj_,0,0,a.screen_.width,a.screen_.height);
    lv_obj_set_scroll_elastic(a.body_,false);
    auto content=a.box(a.body_,0,0,a.screen_.width,LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(content,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(content,false);
    std::string text;
    for(int i=0;i<120;++i)text+="Repeated English 中文正文，保持分块和选择位置。 ";
    a.richMessage(content,text,a.screen_.width,false,"paint-performance");
    lv_obj_update_layout(a.screenObj_);
    assert(a.rasters_.size()==1);
    auto &r=a.rasters_.front();
    assert(r.height>a.screen_.height*2);
    uint64_t batches=0,created=0;
    auto check=[&](int scroll) {
      lv_obj_scroll_to_y(a.body_,scroll,LV_ANIM_OFF);
      lv_obj_update_layout(a.body_);
      const auto previous=r.tiles;
      a.rich_.resetWorkStats();a.paintVisible();
      const auto work=a.rich_.workStats();
      size_t fresh=0;
      for(const auto &tile:r.tiles) {
        auto before=previous.find(tile.first);
        if(before==previous.end())++fresh;
        else assert(before->second==tile.second);
      }
      assert(work.layoutCalls==(fresh?1u:0u));
      assert(a.textures_.size()==r.tiles.size());
      batches+=work.layoutCalls;created+=fresh;
      // Each actual LVGL tile must retain the same crop and exact pixels as
      // the compatible single-window renderer, including partial edge tiles.
      for(const auto &tile:r.tiles) {
        auto expected=a.rich_.paint(r.block,r.width,tile.first.second,
            std::min(128,r.height-tile.first.second),r.fontSize,tile.first.first,512,r.subdued);
        const auto &actual=*a.textures_.at(tile.second)->frame;
        assert(actual.width==expected.width&&actual.height==expected.height);
        assert(actual.pixels==expected.pixels);
      }
      const auto stable=r.tiles;
      a.rich_.resetWorkStats();a.paintVisible();
      assert(a.rich_.workStats().layoutCalls==0&&r.tiles==stable);
    };
    for(int scroll:{0,219,701,1795,0})check(scroll);
    // Entirely clipped content releases all tiles. A partial return creates
    // only the new visible batch and keeps the normal offscreen overscan.
    lv_obj_set_y(content,a.screen_.height+256);
    lv_obj_update_layout(a.body_);a.rich_.resetWorkStats();a.paintVisible();
    assert(r.tiles.empty()&&a.textures_.empty()&&a.rich_.workStats().layoutCalls==0);
    lv_obj_set_y(content,0);lv_obj_update_layout(a.body_);check(0);
    assert(created>batches);
    std::cout<<"PAINT_VISIBLE size="<<a.screen_.width<<'x'<<a.screen_.height
             <<" new_tiles="<<created<<" layouts="<<batches
             <<" unchanged_updates_layouts=0 exact_tile_pixels=1\n";
    a.clear();assert(a.textures_.empty());
  }
};
} // namespace penchat
