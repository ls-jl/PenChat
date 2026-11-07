#include "onboarding_3d.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>

using penchat::Onboarding3D;
int main() {
  const auto far=Onboarding3D::project({40,20,0},400,200,1);
  const auto near=Onboarding3D::project({40,20,100},400,200,1);
  assert(near.x>far.x && near.y>far.y && near.inverseZ>far.inverseZ);
  bool rejected=false;
  try{Onboarding3D invalid(641,360);}catch(const std::invalid_argument &){rejected=true;}
  assert(rejected);
  std::vector<uint32_t> texture(436*136,0xff303030);
  for(int y=45;y<85;++y)for(int x=24;x<400;++x)texture[y*436+x]=0xffececec;
  Onboarding3D::Texture source{reinterpret_cast<uint8_t *>(texture.data()),436,136,436*4};
  for(const auto &size: {std::pair<int,int>{436,146},{424,160},{520,360}}) {
    Onboarding3D scene(size.first,size.second);
    const auto bytes=scene.bytes();const auto *address=scene.pixels();
    auto started=std::chrono::steady_clock::now();
    for(int page=0;page<7;++page)for(int phase=0;phase<4;++phase)for(int t=0;t<4;++t) {
      scene.render(source,page,phase,t/3.f,0x171717,0x3464ef);
      assert(scene.pixels()==address && scene.bytes()==bytes && bytes<=640*360*8);
      assert(scene.triangles()>=10 && scene.triangles()<=80);
      assert(scene.pixels()[0]==0xff171717); // projection remains within its canvas
      assert(std::count_if(address,address+size.first*size.second,
        [](uint32_t c){return c==0xffececec;})>100);
    }
    assert(scene.frames()==112);
    scene.render(source,0,1,0,0x171717,0x3464ef);
    std::vector<uint32_t> first(address,address+size.first*size.second);
    scene.render(source,0,1,1,0x171717,0x3464ef);
    assert(!std::equal(first.begin(),first.end(),address)); // mesh really moves
    scene.render(source,0,1,0,0x171717,0x3464ef);
    assert(std::equal(first.begin(),first.end(),address)); // repeatable replay
    rejected=false;
    try{scene.render({nullptr,0,0,0},0,0,0,0,0);}catch(const std::invalid_argument &){rejected=true;}
    assert(rejected);
    const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
    std::cout<<size.first<<"x"<<size.second<<": "<<bytes<<" bytes, "<<us/115<<" us/frame\n";
  }
}
