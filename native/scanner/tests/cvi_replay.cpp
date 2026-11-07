// Explicit diagnostic for the pinned CVI adapter; no hardware capture or UI.
#include "pen_stream/stream.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
int main(int argc,char **argv) {
  try {
    if(argc!=5)throw std::runtime_error("usage: cvi-replay MODEL RUNTIME GRAY32 OUTPUT_PREFIX");
    std::array<std::uint8_t,32*144*80> pixels;
    std::ifstream file(argv[3],std::ios::binary);
    if(!file.read(reinterpret_cast<char *>(pixels.data()),pixels.size()) ||
       file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("invalid_gray32_input");
    auto backend=pen::stream::makeCvi32(argv[1],argv[2]);backend->open();
    pen::stream::NetworkBatch batch;batch.validCount=32;
    for(unsigned t=0;t<32;++t)batch.gray[t]=pixels.data()+t*144*80;
    pen::stream::Heads heads;backend->infer(batch,heads);
    for(unsigned h=0;h<9;++h){
      std::ofstream output(std::string(argv[4])+"_"+std::to_string(h)+".f32",std::ios::binary);
      if(!output.write(reinterpret_cast<const char *>(heads[h].data()),heads[h].size()*sizeof(float)))
        throw std::runtime_error("output_write_failed");
    }
    backend->close();std::cout<<"CVI32_NATIVE_ADAPTER_REPLAY_OK\n";
  }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
