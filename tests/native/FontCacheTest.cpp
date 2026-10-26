#include "platform.hpp"
#include <cassert>
#include <iostream>
using namespace penchat;
int main(int argc,char **argv) {
  assert(argc==3);lv_init();initializeFonts(argv[1],argv[2]);
  for(int size:{18,24,32}) {
    auto f=font(size);
    for(uint32_t cp=0x4e00;cp<0x4e00+10000;++cp) {
      lv_font_glyph_dsc_t d{};assert(lv_font_get_glyph_dsc(f,&d,cp,0));
      lv_font_get_glyph_static_bitmap(&d);
      if((cp%128)==0)trimFontCache();
    }
    trimFontCache();auto info=fontCacheInfo();
    assert(info["bytes"].get<size_t>()<=4*1024*1024&&info["entries"].get<size_t>()<=8192);
  }
  lv_font_glyph_dsc_t d{};assert(lv_font_get_glyph_dsc(font(18),&d,0x4e00,0));
  assert(d.adv_w>0&&lv_font_get_glyph_static_bitmap(&d));
  lv_deinit();shutdownFonts();assert(fontCacheInfo()["entries"]==0);
  std::cout<<"Font cache: 30,000 glyphs, bounded frame-end eviction/reload and complete FreeType teardown passed\n";
}
