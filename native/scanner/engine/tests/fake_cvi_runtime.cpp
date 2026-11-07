#include "cvi/runtime_abi.hpp"
#include "geometry_decoder.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
using pen::cvi::Tensor;
namespace {
Tensor input{}, output[9]{};
std::vector<float> pixels, values[9];
int live = 0, expected = 32, mode = 0;
void tensor(Tensor &t, const char *name, std::array<int,4> shape, std::vector<float> &data) {
  t = {}; t.name = const_cast<char *>(name); t.shape.dimensions = 4; t.count = 1;
  for (unsigned i=0;i<4;++i) {t.shape.dim[i]=shape[i];t.count*=shape[i];}
  data.resize(t.count); t.bytes=t.count*sizeof(float); t.system=reinterpret_cast<std::uint8_t *>(data.data());
}
}
extern "C" {
void mock_cvi_mode(int value) {mode=value;}
void mock_cvi_valid(int value) {expected=value;}
int mock_cvi_live() {return live;}
int CVI_NN_RegisterModel(const char *, void **model) {
  ++live; *model=&live;
  tensor(input,"img_blocks",{32,3,144,80},pixels);
  for(unsigned h=0;h<9;++h) {
    // Deliberately reverse runtime output order; names define the contract.
    tensor(output[8-h],pen::cvi::headNames[h],{1,32,int(pen::y07::kHeadBins[h]),1},values[h]);
    std::fill(values[h].begin(),values[h].end(),float(h));
  }
  if(mode==2) output[0].name=output[1].name;
  if(mode==3) input.shape.dim[1]=16;
  return 0;
}
int CVI_NN_CleanupModel(void *) {--live;return 0;}
int CVI_NN_SetConfig(void *, int option, ...) {return option==9?0:-1;}
int CVI_NN_GetModelVersion(void *, int *major,int *minor) {*major=1;*minor=4;return 0;}
const char *CVI_NN_GetModelTarget(void *) {return mode==1?"cv183x":"cv182x";}
int CVI_NN_GetInputOutputTensors(void *,Tensor **in,int *ni,Tensor **out,int *no) {
  *in=&input;*ni=1;*out=output;*no=9;return 0;
}
char *CVI_NN_TensorName(Tensor *t) {return t->name;}
std::size_t CVI_NN_TensorSize(Tensor *t) {return t->bytes;}
void *CVI_NN_TensorPtr(Tensor *t) {return t->system;}
int CVI_NN_Forward(void *,Tensor *,int ni,Tensor *,int no) {
  if(ni!=1||no!=9)return -1;
  constexpr float mean[]={123.675f,116.28f,103.53f},sd[]={58.395f,57.12f,57.375f};
  constexpr unsigned pad[]={124,116,104};
  for(unsigned t=0;t<32;++t)for(unsigned c=0;c<3;++c)for(unsigned p=0;p<144*80;++p) {
    const float want=(float(t<unsigned(expected)?p%256:pad[c])-mean[c])/sd[c];
    if(pixels[(t*3+c)*144*80+p]!=want)return -2;
  }
  return mode==4?-3:0;
}
}
