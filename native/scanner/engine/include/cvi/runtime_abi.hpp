#pragma once
#include <cstddef>
#include <cstdint>

// Public CVI runtime 1.4 tensor ABI, independently declared for dynamic binding.
// Contract and upstream provenance: native/scanner/cvi-dependencies.lock.json.
// Never use this layout with an unverified runtime or a different model target.
namespace pen::cvi {
struct Shape { std::int32_t dim[6]; std::size_t dimensions; };
struct Tensor {
  char *name;
  Shape shape;
  int format;
  std::size_t count, bytes;
  std::uint8_t *system;
  std::uint64_t physical;
  int memory;
  float quantScale;
  int zeroPoint, pixelFormat;
  bool aligned;
  float mean[3], scale[3];
  void *owner;
  char reserved[32];
};
#if defined(__arm__) && !defined(__aarch64__)
static_assert(sizeof(Shape) == 28, "CVI ARM32 shape ABI");
static_assert(sizeof(Tensor) == 136, "CVI ARM32 tensor ABI");
static_assert(offsetof(Tensor, bytes) == 40, "CVI ARM32 tensor size offset");
static_assert(offsetof(Tensor, system) == 44, "CVI ARM32 tensor pointer offset");
static_assert(offsetof(Tensor, memory) == 56, "CVI ARM32 memory type offset");
#endif
inline constexpr const char *headNames[9] = {
  "dx_Reshape_f32", "dy_Reshape_f32", "angle_Reshape_f32", "scale_Reshape_f32",
  "x2_Reshape_f32", "y2_Reshape_f32", "x3_Reshape_f32", "y3_Reshape_f32",
  "bug_frame_Reshape_f32"
};
} // namespace pen::cvi
