#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace pen_photo {
// Same white-paper mask, median brightness normalization and clipped gains as
// the tested Python flatfield experiment. No OpenCV, NN, GPU, or frame history.
// gain is the frozen clip(220/background, .8, 1.4) sensor map.
inline float brightness(const uint8_t *gray, int width, int height, const float *gain,
                        std::vector<float> &scratch) {
    scratch.clear();
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int i = y * width + x, value = gray[i];
            if (value <= 135 || value >= 250)
                continue;
            int lo = value, hi = value;
            for (int yy = std::max(0, y - 1); yy <= std::min(height - 1, y + 1); ++yy)
                for (int xx = std::max(0, x - 1); xx <= std::min(width - 1, x + 1); ++xx) {
                    int v = gray[yy * width + xx];
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            if (hi - lo < 12)
                scratch.push_back(value * gain[i]);
        }
    if (scratch.empty())
        throw std::runtime_error("insufficient_paper_background");
    auto middle = scratch.begin() + scratch.size() / 2;
    std::nth_element(scratch.begin(), middle, scratch.end());
    float median = *middle;
    if (!(scratch.size() & 1))
        median = (median + *std::max_element(scratch.begin(), middle)) * .5f;
    return std::clamp(220.f / median, .9f, 1.1f);
}

inline void apply(const uint8_t *gray, uint8_t *output, std::size_t n, const float *gain,
                  float scalar) {
    for (std::size_t i = 0; i < n; ++i) {
        float g = std::clamp(gain[i] * scalar, .8f, 1.4f);
        float v = std::min(gray[i] * g, 250.f);
        output[i] = static_cast<uint8_t>(v + .5f);
    }
}

#if defined(__aarch64__)
// Exactly the same mask and sample order as brightness(), using CPU NEON to
// evaluate local ranges. Median selection and all floating-point gains agree.
inline float brightness_neon(const uint8_t *gray, int width, int height, const float *gain,
                             std::vector<float> &scratch) {
    scratch.clear();
    for (int y = 0; y < height; ++y) {
        const auto *top = gray + std::max(0, y - 1) * width, *row = gray + y * width,
                   *bottom = gray + std::min(height - 1, y + 1) * width;
        auto scalar = [&](int x) {
            int value = row[x];
            if (value <= 135 || value >= 250)
                return;
            int lo = value, hi = value;
            for (int yy = std::max(0, y - 1); yy <= std::min(height - 1, y + 1); ++yy)
                for (int xx = std::max(0, x - 1); xx <= std::min(width - 1, x + 1); ++xx) {
                    int v = gray[yy * width + xx];
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            if (hi - lo < 12)
                scratch.push_back(value * gain[y * width + x]);
        };
        scalar(0);
        int x = 1;
        for (; x + 16 < width; x += 16) {
            auto lo = vld1q_u8(row + x), hi = lo;
            for (const auto *p : {top, row, bottom})
                for (int dx = -1; dx <= 1; ++dx) {
                    auto v = vld1q_u8(p + x + dx);
                    lo = vminq_u8(lo, v);
                    hi = vmaxq_u8(hi, v);
                }
            auto values = vld1q_u8(row + x);
            auto mask = vandq_u8(
                vcltq_u8(vsubq_u8(hi, lo), vdupq_n_u8(12)),
                vandq_u8(vcgtq_u8(values, vdupq_n_u8(135)), vcltq_u8(values, vdupq_n_u8(250))));
            uint8_t keep[16];
            vst1q_u8(keep, mask);
            for (int k = 0; k < 16; ++k)
                if (keep[k])
                    scratch.push_back(row[x + k] * gain[y * width + x + k]);
        }
        for (; x < width; ++x)
            scalar(x);
    }
    if (scratch.empty())
        throw std::runtime_error("insufficient_paper_background");
    auto middle = scratch.begin() + scratch.size() / 2;
    std::nth_element(scratch.begin(), middle, scratch.end());
    float median = *middle;
    if (!(scratch.size() & 1))
        median = (median + *std::max_element(scratch.begin(), middle)) * .5f;
    return std::clamp(220.f / median, .9f, 1.1f);
}

inline void apply_neon(const uint8_t *gray, uint8_t *output, std::size_t n, const float *gain,
                       float scalar) {
    auto process = [&](uint32x4_t pixels, const float *g) {
        auto scale = vmaxq_f32(vdupq_n_f32(.8f),
                               vminq_f32(vdupq_n_f32(1.4f), vmulq_n_f32(vld1q_f32(g), scalar)));
        auto value = vminq_f32(vdupq_n_f32(250.f), vmulq_f32(vcvtq_f32_u32(pixels), scale));
        return vcvtq_u32_f32(vaddq_f32(value, vdupq_n_f32(.5f)));
    };
    std::size_t i = 0;
    for (; i + 16 <= n; i += 16) {
        auto pixels = vld1q_u8(gray + i);
        auto low = vmovl_u8(vget_low_u8(pixels)), high = vmovl_u8(vget_high_u8(pixels));
        auto a = process(vmovl_u16(vget_low_u16(low)), gain + i),
             b = process(vmovl_u16(vget_high_u16(low)), gain + i + 4);
        auto c = process(vmovl_u16(vget_low_u16(high)), gain + i + 8),
             d = process(vmovl_u16(vget_high_u16(high)), gain + i + 12);
        auto left = vcombine_u16(vmovn_u32(a), vmovn_u32(b)),
             right = vcombine_u16(vmovn_u32(c), vmovn_u32(d));
        vst1q_u8(output + i, vcombine_u8(vmovn_u16(left), vmovn_u16(right)));
    }
    if (i < n)
        apply(gray + i, output + i, n - i, gain + i, scalar);
}
#else
inline float brightness_neon(const uint8_t *g, int w, int h, const float *f,
                             std::vector<float> &s) {
    return brightness(g, w, h, f, s);
}
inline void apply_neon(const uint8_t *g, uint8_t *o, std::size_t n, const float *f, float s) {
    apply(g, o, n, f, s);
}
#endif
} // namespace pen_photo
