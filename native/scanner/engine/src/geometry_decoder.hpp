#pragma once

#include <array>
#include <cstddef>
#include <vector>

// Independently written from the observed, hash-pinned Y07 model contract.
// No vendor library, RKNN runtime, OpenCV, or GPU is required by this module.
namespace pen::y07 {

inline constexpr std::array<std::size_t, 9> kHeadBins{384, 384, 512, 512, 512, 512, 512, 512, 1};

struct FloatHead {
    const float *data = nullptr;
    std::size_t elements = 0;
};

struct DecodedFrame {
    float dx, dy;
    float angle_degrees;
    float scale_delta;
    float x2, y2, x3, y3;
    float bug_logit;
};

// A standalone DFL primitive. bins must be in [1, 512]; inputs must be finite.
// Uses float32 sequential reductions to match the observed original parser.
float centeredExpectation(const float *logits, std::size_t bins);

// Input is already dequantized float32, laid out as contiguous T x bins.
// T must be one of 16/32/48/64, including padding. All nine heads are mandatory.
// Values are batch-local model coordinates, NOT sensor-image homographies.
// In particular, bug_logit remains a raw score and angle is not accumulated.
std::vector<DecodedFrame> decodeBatch(const std::array<FloatHead, 9> &heads, std::size_t frames);

} // namespace pen::y07
