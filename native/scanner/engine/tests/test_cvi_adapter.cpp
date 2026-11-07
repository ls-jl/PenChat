#include "pen_stream/stream.hpp"
#include "geometry_decoder.hpp"
#include <array>
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
static void require(bool value) {if(!value)throw std::runtime_error("CVI adapter assertion failed");}
int main(int argc,char **argv) {
  try {
    require(argc==2);
    void *lib=dlopen(argv[1],RTLD_NOW);require(lib);
    auto mode=reinterpret_cast<void(*)(int)>(dlsym(lib,"mock_cvi_mode"));
    auto valid=reinterpret_cast<void(*)(int)>(dlsym(lib,"mock_cvi_valid"));
    auto live=reinterpret_cast<int(*)()>(dlsym(lib,"mock_cvi_live"));
    require(mode&&valid&&live);
    auto adapter=pen::stream::makeCvi32("fixture",argv[1]);
    for(int m=1;m<=3;++m) {
      mode(m);bool rejected=false;
      try {adapter->open();}catch(const std::exception &){rejected=true;}
      require(rejected&&live()==0);
    }
    mode(0);adapter->open();adapter->open();require(live()==1);
    require(adapter->managedBytes()==0);
    std::array<std::uint8_t,144*80> pixels;
    for(unsigned p=0;p<pixels.size();++p)pixels[p]=p%256;
    pen::stream::NetworkBatch batch;batch.gray.fill(pixels.data());
    pen::stream::Heads heads;
    for(unsigned n=1;n<=32;++n) {
      valid(n);batch.validCount=n;adapter->infer(batch,heads);
      for(unsigned h=0;h<9;++h) {
        require(heads[h].size()==32*pen::y07::kHeadBins[h]);
        for(float value:heads[h])require(value==float(h));
      }
    }
    for(unsigned invalid:{0u,33u}) {
      batch.validCount=invalid;bool rejected=false;
      try{adapter->infer(batch,heads);}catch(const std::exception &){rejected=true;}
      require(rejected);
    }
    batch.validCount=1;batch.gray[0]=nullptr;bool nullRejected=false;
    try{adapter->infer(batch,heads);}catch(const std::exception &){nullRejected=true;}
    require(nullRejected);batch.gray[0]=pixels.data();
    batch.validCount=1;valid(1);mode(4);bool failed=false;
    try{adapter->infer(batch,heads);}catch(const std::exception &){failed=true;}
    require(failed);adapter->close();adapter->close();require(live()==0);
    dlclose(lib);std::cout<<"CVI32_RGB_PADDING_NAMED_HEADS_CLEANUP_OK\n";
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
