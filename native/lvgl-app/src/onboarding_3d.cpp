#include "onboarding_3d.hpp"
#include "theme.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace penchat {
namespace {
constexpr float pi = 3.14159265358979323846f;
float edge(float ax, float ay, float bx, float by, float x, float y) {
  return (bx-ax)*(y-ay)-(by-ay)*(x-ax);
}
uint32_t shade(uint32_t c, float light) {
  auto channel = [light](uint32_t value) { return uint32_t(std::min(255.f,value*light)); };
  return 0xff000000u | channel((c>>16)&255)<<16 | channel((c>>8)&255)<<8 | channel(c&255);
}
}
Onboarding3D::Onboarding3D(int width, int height) : width_(width), height_(height) {
  // Bound illustration work independently of screen resolution; not a device default.
  if(width<1 || height<1 || width>640 || height>360)
    throw std::invalid_argument("Tutorial canvas exceeds its rendering budget");
  pixels_.resize(size_t(width)*height);
  depth_.resize(pixels_.size());
}
Onboarding3D::Projected Onboarding3D::project(Point p, int width, int height, float scale) {
  const float inverseZ = 1.f/(700.f-p.z);
  return {width*.5f+p.x*700.f*inverseZ*scale,
          height*.5f+p.y*700.f*inverseZ*scale, inverseZ, p.u, p.v};
}
Onboarding3D::Point Onboarding3D::transform(Point p) const {
  const float cy=std::cos(yaw_), sy=std::sin(yaw_), cx=std::cos(pitch_), sx=std::sin(pitch_);
  const float x=cy*p.x+sy*p.z, z=-sy*p.x+cy*p.z;
  return {x,cx*p.y-sx*z,sx*p.y+cx*z,p.u,p.v};
}
void Onboarding3D::triangle(Point pa, Point pb, Point pc, uint32_t color, const Texture *texture) {
  const auto a=project(transform(pa),width_,height_,scale_);
  const auto b=project(transform(pb),width_,height_,scale_);
  const auto c=project(transform(pc),width_,height_,scale_);
  const float area=edge(a.x,a.y,b.x,b.y,c.x,c.y);
  if(std::abs(area)<.001f)return;
  ++triangles_;
  const int left=std::max(0,int(std::floor(std::min({a.x,b.x,c.x}))));
  const int right=std::min(width_-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
  const int top=std::max(0,int(std::floor(std::min({a.y,b.y,c.y}))));
  const int bottom=std::min(height_-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
  for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x) {
    const float wa=edge(b.x,b.y,c.x,c.y,x+.5f,y+.5f)/area;
    const float wb=edge(c.x,c.y,a.x,a.y,x+.5f,y+.5f)/area;
    const float wc=1.f-wa-wb;
    if(wa<-.00001f || wb<-.00001f || wc<-.00001f)continue;
    const float iz=wa*a.inverseZ+wb*b.inverseZ+wc*c.inverseZ;
    const size_t index=size_t(y)*width_+x;
    if(iz<depth_[index])continue;
    uint32_t pixel=color;
    if(texture) {
      // Perspective-correct UVs keep the demonstration aligned on a real 3D face.
      const float u=(wa*a.u*a.inverseZ+wb*b.u*b.inverseZ+wc*c.u*c.inverseZ)/iz;
      const float v=(wa*a.v*a.inverseZ+wb*b.v*b.inverseZ+wc*c.v*c.inverseZ)/iz;
      const float tx=std::max(0.f,std::min(float(texture->width-1),u*(texture->width-1)));
      const float ty=std::max(0.f,std::min(float(texture->height-1),v*(texture->height-1)));
      const int ix=int(tx), iy=int(ty), dx=int((tx-ix)*256), dy=int((ty-iy)*256);
      uint32_t samples[4];
      for(int n=0;n<4;++n) {
        const int sx=std::min(texture->width-1,ix+(n&1));
        const int sy=std::min(texture->height-1,iy+(n>>1));
        std::memcpy(&samples[n],texture->pixels+size_t(sy)*texture->stride+sx*4,4);
      }
      // Bilinear sampling preserves the antialiased Chinese glyphs on the panel.
      pixel=0;
      for(int shift=0;shift<32;shift+=8) {
        const int top=((samples[0]>>shift)&255)*(256-dx)+((samples[1]>>shift)&255)*dx;
        const int bottom=((samples[2]>>shift)&255)*(256-dx)+((samples[3]>>shift)&255)*dx;
        pixel|=uint32_t((top*(256-dy)+bottom*dy+32768)>>16)<<shift;
      }
      if((pixel>>24)==0)continue;
    }
    pixels_[index]=pixel;depth_[index]=iz;
  }
}
void Onboarding3D::quad(Point a,Point b,Point c,Point d,uint32_t color,const Texture *texture) {
  triangle(a,b,c,color,texture);triangle(a,c,d,color,texture);
}
void Onboarding3D::panel(Texture texture) {
  const float h=400.f*texture.height/texture.width;
  const Point a{-200,-h/2,0,0,0}, b{200,-h/2,0,1,0};
  const Point c{200,h/2,0,1,1}, d{-200,h/2,0,0,1};
  // A beveled housing: actual side faces, not an offset 2D shadow.
  const Point aa{-202,-h/2-2,-9}, bb{202,-h/2-2,-9};
  const Point cc{202,h/2+2,-9}, dd{-202,h/2+2,-9};
  quad(a,b,bb,aa,shade(theme::border,1.15f));quad(b,c,cc,bb,shade(theme::border,1.5f));
  quad(c,d,dd,cc,shade(theme::border,1.25f));quad(d,a,aa,dd,shade(theme::border,.85f));
  quad(a,b,c,d,0xffffffff,&texture);
}
void Onboarding3D::pen(float position,float lift,uint32_t color) {
  // Tapered, ten-sided dictionary pen. Its tip follows the paper, then lifts.
  const float x=-145+position*285, y=15;
  auto point=[&](float radius,float along,float angle) {
    const float lateral=radius*std::cos(angle), z=radius*std::sin(angle);
    return Point{x+lateral+along*.30f,y-along,lift+8+along*.20f+z};
  };
  for(int n=0;n<10;++n) {
    const float a=2*pi*n/10, b=2*pi*(n+1)/10;
    const float light=.63f+.37f*std::max(0.f,std::cos(a+.8f));
    quad(point(6,10,a),point(6,10,b),point(9,76,b),point(9,76,a),shade(color,light));
    quad(point(9,76,a),point(9,76,b),point(7,83,b),point(7,83,a),shade(theme::text,light));
    triangle(point(6,10,a),point(6,10,b),point(2,0,a),shade(theme::muted,light));
    triangle(point(7,83,a),point(7,83,b),point(0,83,0),shade(theme::text,light));
  }
  // A slim raised screen on the barrel, sharing its actual 3D orientation.
  quad({x+10,y-54,lift+28},{x+19,y-54,lift+28},
       {x+24,y-69,lift+31},{x+15,y-69,lift+31},shade(theme::sidebar,1));
}
void Onboarding3D::render(Texture texture,int page,int phase,float progress,uint32_t background,uint32_t accent) {
  if(!texture.pixels || texture.width<=0 || texture.height<=0 || texture.width>640 ||
     texture.height>360 || texture.stride<texture.width*4)
    throw std::invalid_argument("Invalid tutorial texture");
  progress=std::max(0.f,std::min(1.f,progress));
  std::fill(pixels_.begin(),pixels_.end(),0xff000000u|background);
  std::fill(depth_.begin(),depth_.end(),0.f);triangles_=0;
  // Slow, small viewing-angle changes show depth without spinning text away.
  const float ease=1.f-std::pow(1.f-progress,3.f);
  const float cycle=(phase+progress)*pi/2;
  yaw_=(-8.f+2.f*std::sin(cycle))*pi/180;
  pitch_=(10.f-1.5f*std::cos(cycle))*pi/180;
  scale_=std::min(width_/424.f,height_/(400.f*texture.height/texture.width+24.f));
  panel(texture);
  if((page==0 || page==1) && phase==1)
    pen(ease,std::max(0.f,(progress-.85f)*40.f),accent);
  ++frames_;
}
} // namespace penchat
