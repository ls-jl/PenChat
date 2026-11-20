#include "cvi_pixels.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>
using Byte = std::uint8_t;
static void reference(const Byte *source,int w,int h,int rotation,Byte *out,int pw,int ph,
    std::size_t stride,std::size_t yBytes,std::size_t uvBytes) {
  std::fill(out,out+yBytes,16);std::fill(out+yBytes,out+yBytes+uvBytes,128);
  auto sample=[&](int x,int y,int c) {
    int sx=x,sy=y;
    if(rotation==270){sx=w-1-y;sy=x;}
    else if(rotation==90){sx=y;sy=h-1-x;}
    else if(rotation==180){sx=w-1-x;sy=h-1-y;}
    return int(source[(std::size_t(sy)*w+sx)*4+c]);
  };
  auto byte=[](int v){return Byte(std::max(0,std::min(255,v)));};
  for(int y=0;y<ph;++y)for(int x=0;x<pw;++x) {
    const int b=sample(x,y,0),g=sample(x,y,1),r=sample(x,y,2);
    out[std::size_t(y)*stride+x]=byte(((66*r+129*g+25*b+128)>>8)+16);
    if(!(x%2)&&!(y%2)) {
      int r=0,g=0,b=0;
      for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){b+=sample(x+dx,y+dy,0);g+=sample(x+dx,y+dy,1);r+=sample(x+dx,y+dy,2);}
      r=(r+2)/4;g=(g+2)/4;b=(b+2)/4;
      out[yBytes+std::size_t(y/2)*stride+x]=byte(((-38*r-74*g+112*b+128)>>8)+128);
      out[yBytes+std::size_t(y/2)*stride+x+1]=byte(((112*r-94*g-18*b+128)>>8)+128);
    }
  }
}
int main(int argc,char **) {
  for(int w:{2,18,260,640})for(int h:{2,14,260})for(int rotation:{0,90,180,270}) {
    const int pw=(rotation==90||rotation==270)?h:w,ph=(rotation==90||rotation==270)?w:h;
    const std::size_t stride=(pw+31)&~31,yBytes=stride*ph+64,uvBytes=stride*ph/2+32;
    std::vector<Byte> source(std::size_t(w)*h*4),a(yBytes+uvBytes),b(a.size());
    for(std::size_t n=0;n<source.size();++n)source[n]=Byte((n*73+n/7)%256);
    reference(source.data(),w,h,rotation,a.data(),pw,ph,stride,yBytes,uvBytes);
    camera::cvi::bgraToNv12(source.data(),w,h,rotation,b.data(),pw,ph,stride,yBytes,uvBytes);
    assert(a==b);
  }
  std::cout<<"CVI_PIXELS_ALL_ROTATIONS_STRIDE_AND_CHROMA_IDENTICAL_OK\n";
  if(argc>1) {
    int w=640,h=260,pw=h,ph=w;std::size_t stride=288,yBytes=stride*ph,uvBytes=yBytes/2;
    std::vector<Byte> source(w*h*4,73),out(yBytes+uvBytes);volatile unsigned checksum=0;
    for(int mode:{0,1}) {
      auto start=std::chrono::steady_clock::now();
      for(int n=0;n<120;++n) {
        source[0]=Byte(n);
        if(mode)camera::cvi::bgraToNv12(source.data(),w,h,270,out.data(),pw,ph,stride,yBytes,uvBytes);
        else reference(source.data(),w,h,270,out.data(),pw,ph,stride,yBytes,uvBytes);
        checksum+=out[n];
      }
      std::cout<<(mode?"block":"previous")<<"_cached_ms_per_frame="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/120<<"\n";
    }
    std::cout<<"checksum="<<checksum<<"\n";
  }
}
