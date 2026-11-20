#pragma once
#include <cstddef>
#include <cstdint>
// Independently declared legacy CV1826 public media ABI. Exact S6 firmware only.
namespace camera { namespace cvi {
using U32=std::uint32_t;using U64=std::uint64_t;
struct Rect {int x,y;U32 w,h;};
struct Layer {Rect rect;U32 w,h,fps;int format;};
struct Channel {U32 priority;Rect rect;};
struct Pool {U32 bytes,count,remap;char name[32];};
struct Frame {
  U32 w,h;int format,bayer,video,compress,range,gamut;U32 stride[3];
  U64 physical[3];std::uint8_t *virtualAddress[3];U32 virtualPadding[3],length[3];
  std::int16_t top,bottom,left,right;U32 time;U64 pts;void *privateData;
  U32 privatePadding,flags;
};
struct FrameInfo {Frame frame;U32 pool;};
#if defined(__arm__)
static_assert(sizeof(Frame)==144,"CVI frame ABI");
static_assert(sizeof(FrameInfo)==152,"CVI frame info ABI");
#endif
}}
