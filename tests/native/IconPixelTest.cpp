#include "stb_image.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
int main(int argc, char **argv) {
  assert(argc == 6);
  int w,h,channels;
  auto data = stbi_load(argv[1], &w, &h, &channels, 3);
  assert(data);
  const int ox=atoi(argv[2]),oy=atoi(argv[3]),size=atoi(argv[4]);
  const bool dim=atoi(argv[5]);
  assert(ox>=0 && oy>=0 && ox+size<=w && oy+size<=h);
  auto pixel=[&](int x,int y){return int(data[((y+oy)*w+x+ox)*3]);};
  const int background=pixel(0,0);
  int horizontal=0,vertical=0,peak=0;
  double mass=0,mx=0,my=0;
  for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
    horizontal=std::max(horizontal,std::abs(pixel(x,y)-pixel(size-1-x,y)));
    vertical=std::max(vertical,std::abs(pixel(x,y)-pixel(x,size-1-y)));
    const auto value=std::max(0,pixel(x,y)-background);
    mass+=value;mx+=value*x;my+=value*y;peak=std::max(peak,value);
  }
  assert(mass>500 && peak>(dim?40:120)); // a missing/blank image is not symmetry
  assert(horizontal<=1 && vertical<=1);
  assert(std::abs(mx/mass-(size-1)*.5)<.01 && std::abs(my/mass-(size-1)*.5)<.01);
  // The upright top/bottom tooth and centered clear hole must remain legible.
  assert(pixel(size/2,2)>background+20);
  assert(std::abs(pixel(size/2,size/2)-background)<=1);
  stbi_image_free(data);
  std::cout<<"Rendered gear: horizontal/vertical pixel symmetry and centered mass passed, "<<size<<"px.\n";
}
