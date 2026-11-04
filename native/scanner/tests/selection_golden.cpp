#include <pen_stream/stream.hpp>
#include "geometry_decoder.hpp"
#include <cstdio>
#include <vector>
#include <mutex>
using namespace pen::stream;
class Synthetic : public Inference {
public:
 uint64_t inputs=1469598103934665603ull;unsigned calls=0;
 void open()override{}void close()noexcept override{}size_t managedBytes()const noexcept override{return 0;}
 void infer(const NetworkBatch&b,Heads&h)override{
  ++calls;for(unsigned t=0;t<b.validCount;++t)for(unsigned j=0;j<144*80;++j)inputs=(inputs^b.gray[t][j])*1099511628211ull;
  unsigned bins[9]={384,384,512,512,512,512,512,512,1};
  for(unsigned k=0;k<9;++k){h[k].assign(32*bins[k],-80);for(unsigned t=0;t<32;++t)h[k][t*bins[k]+bins[k]/2+(k==0?2:0)]=0;}
 }
};
static uint64_t hash(uint64_t a,const std::vector<uint8_t>&v){for(auto x:v)a=(a^x)*1099511628211ull;return a;}
int main(){for(int rot=0;rot<4;++rot)for(unsigned n:{0u,1u,31u,32u,33u,56u,57u,80u}){
 auto backend=std::make_unique<Synthetic>();auto *network=backend.get();Limits limits;limits.idleModelMs=0;Engine e(std::move(backend),limits);
 Config c;c.width=640;c.height=480;c.rotation=Rotation(rot);auto k=std::make_shared<Calibration>();k->version="golden";k->sensorWidth=640;k->sensorHeight=480;k->rotation=c.rotation;for(unsigned i=0;i<k->gain.size();++i)k->gain[i]=.8f+(i%31)*.02f;c.calibration=k;
 std::vector<uint64_t> selected;auto s=e.beginScan(c,{[&](const Event&v){if(v.kind==Event::Kind::Selected)selected.push_back(v.sequence);}});if(s.code!=Code::Ok)return 2;
 std::vector<uint8_t> raw(640*480,210);
 for(unsigned i=0;i<n;++i){for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x){unsigned xx=x+i*3,z=(xx/5)*1664525u+(y/7)*1013904223u;raw[y*640+x]=(y%48>8&&y%48<34&&xx%30<20&&z%7<4)?35:210;}if(!e.pushFrame(s.scan,raw.data(),640,480,640,i,i*8400).accepted)return 3;}
 e.endScan(s.scan);auto r=e.wait(s.scan);auto b=r->bounds();auto count=r->counts();uint64_t pixels=1469598103934665603ull,mask=pixels,seq=pixels;std::vector<uint8_t> a(b.width),v(b.width);
 for(int y=0;y<b.height;++y){if(r->readRow(y,a.data(),v.data(),a.size())!=Code::Ok)return 4;pixels=hash(pixels,a);mask=hash(mask,v);}for(auto id:selected)seq=(seq^id)*1099511628211ull;
 printf("rot=%d n=%u status=%d received=%llu filtered=%llu selected=%llu committed=%llu unlocated=%llu dims=%dx%d seq=%llu gray=%llu mask=%llu network=%llu calls=%u\n",rot,n,int(r->status()),(unsigned long long)count.received,(unsigned long long)count.filtered,(unsigned long long)count.selected,(unsigned long long)count.committed,(unsigned long long)count.unlocated,b.width,b.height,(unsigned long long)seq,(unsigned long long)pixels,(unsigned long long)mask,(unsigned long long)network->inputs,network->calls);fflush(stdout);e.releaseResult(r);e.shutdown();
 }return 0;}
