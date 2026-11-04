#include "internal.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace pen::stream;
static void require(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
struct Gate {
  std::atomic<bool> entered{false}, proceed{false};
};
class HeldInference : public Inference {
  std::shared_ptr<Gate> gate_;
public:
  explicit HeldInference(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}
  void open() override {}
  void close() noexcept override {}
  std::size_t managedBytes() const noexcept override { return 0; }
  void infer(const NetworkBatch &, Heads &heads) override {
    gate_->entered = true;
    while (!gate_->proceed)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const unsigned bins[9] = {384,384,512,512,512,512,512,512,1};
    for (unsigned h = 0; h < 9; ++h) {
      heads[h].assign(32 * bins[h], -80);
      for (unsigned t = 0; t < 32; ++t) heads[h][t*bins[h]+bins[h]/2] = 0;
    }
  }
};
static Limits plenty() {
  Limits l;
  l.memoryProbe = [] { return SystemMemory{1024*MiB, 900*MiB, true}; };
  l.idleModelMs = 0;
  return l;
}
static Config config() {
  Config c; c.width=640; c.height=480; c.diagnosticAcceptAll=true;
  auto k=std::make_shared<Calibration>(); k->version="memory-fixture";
  k->sensorWidth=c.width; k->sensorHeight=c.height; k->rotation=c.rotation; k->gain.fill(1);
  c.calibration=k; return c;
}
static void reservation() {
  auto b=std::make_shared<Budget>(plenty());
  // No fixed 48/96/240MiB barrier. Untouched pages keep this test economical.
  Pages large; large.allocate(300*MiB,b,b->frames);
  require(b->frames==300*MiB,"legacy byte quotas still active"); large.clear();
  require(b->frames==0,"pages not promptly returned");
  std::atomic<bool> valid{true}; auto limits=plenty();
  limits.memoryProbe=[&]{return SystemMemory{1024*MiB,900*MiB,valid.load()};};
  auto stale=std::make_shared<Budget>(limits); valid=false;
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  require(std::string(stale->reason())=="system_memory_unavailable","stale sample grants memory");
  auto failed=plenty(); failed.allocationProbe=[](std::size_t){return false;};
  auto f=std::make_shared<Budget>(failed); bool threw=false;
  try { Pages p; p.allocate(4096,f,f->frames); } catch(const MemoryError &e) { threw=std::string(e.reason)=="memory_allocation_failed"; }
  require(threw&&f->frames==0,"allocation failure reservation leaked");
}
static void pipeline() {
  auto gate=std::make_shared<Gate>(); Engine engine(std::make_unique<HeldInference>(gate),plenty());
  struct ReleaseGate { std::shared_ptr<Gate> gate; ~ReleaseGate(){gate->proceed=true;} } release{gate};
  auto c=config(); std::vector<uint8_t> pixels(c.width*c.height,210);
  for(int y=20;y<c.height-20;++y)for(int x=20;x<c.width-20;++x)if((x+y)%11<3)pixels[y*c.width+x]=30;
  auto started=engine.beginScan(c); require(started.code==Code::Ok,"begin");
  for(unsigned i=0;i<400;++i)require(engine.pushFrame(started.scan,pixels.data(),c.width,c.height,c.width,i,i).accepted,"ingress");
  auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
  while(engine.preview(started.scan)->counts().selected<400&&std::chrono::steady_clock::now()<deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  auto m=engine.metrics(); gate->proceed=true;
  require(m.selectedQueued>=320&&m.rawQueued==0,"selector waits for NPU");
  require(m.frames==400&&m.frameBytes==400*(307200+12288),"duplicated original pixels or filtered-model reservation");
  engine.endScan(started.scan); auto r=engine.wait(started.scan);
  require(r->counts().received==400&&r->counts().committed==400&&r->counts().pending==0,"backlog lost");
  engine.releaseResult(r);
  for(unsigned n=0;n<200&&engine.metrics().frames;++n)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  require(engine.metrics().frames==0,"completed originals retained");
  for(unsigned cycle=0;cycle<20;++cycle){
    auto s=engine.beginScan(c);require(s.code==Code::Ok,"repeat begin");
    for(unsigned n=0;n<40;++n)require(engine.pushFrame(s.scan,pixels.data(),640,480,640,n,n).accepted,"repeat push");
    if(cycle%2)engine.cancelScan(s.scan);
    engine.endScan(s.scan);auto result=engine.wait(s.scan);engine.releaseResult(result);
    for(unsigned n=0;n<200&&!engine.reclaimIdle();++n)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto after=engine.metrics();
    if(after.frames||after.canvasBytes||after.sessions)
      std::cerr<<"cycle="<<cycle<<" frames="<<after.frames<<" canvas="<<after.canvasBytes<<" sessions="<<after.sessions<<"\n";
    require(after.frames==0&&after.canvasBytes==0&&after.sessions==0,"repeat ownership leak");
  }
  engine.shutdown();
}
int main(){try{reservation();pipeline();std::cout<<"DYNAMIC_MEMORY_AND_SINGLE_OWNER_PIPELINE_OK\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
