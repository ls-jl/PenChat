#include "geometry_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pen::y07 {

float centeredExpectation(const float *logits, std::size_t bins) {
    if (!logits || bins == 0 || bins > 512)
        throw std::invalid_argument("DFL requires 1..512 logits");
    float maximum = logits[0];
    for (std::size_t i = 0; i < bins; ++i) {
        if (!std::isfinite(logits[i]))
            throw std::invalid_argument("non-finite DFL logit");
        maximum = std::max(maximum, logits[i]);
    }
    std::array<float, 512> probabilities;
    float denominator = 0.0f;
    for (std::size_t i = 0; i < bins; ++i) {
        probabilities[i] = std::exp(logits[i] - maximum);
        denominator += probabilities[i];
    }
    float expectation = 0.0f;
    for (std::size_t i = 0; i < bins; ++i) {
        probabilities[i] /= denominator;
        expectation += probabilities[i] * static_cast<float>(i);
    }
    return expectation - static_cast<float>(bins / 2);
}

std::vector<DecodedFrame> decodeBatch(const std::array<FloatHead, 9> &heads, std::size_t frames) {
    if (frames != 16 && frames != 32 && frames != 48 && frames != 64)
        throw std::invalid_argument("unsupported Y07 model batch shape");
    for (std::size_t h = 0; h < heads.size(); ++h) {
        if (!heads[h].data || heads[h].elements != frames * kHeadBins[h])
            throw std::invalid_argument("Y07 output head size mismatch");
    }

    std::vector<DecodedFrame> output;
    output.reserve(frames);
    float previous_scale = 0.0f;
    for (std::size_t t = 0; t < frames; ++t) {
        std::array<float, 8> decoded;
        for (std::size_t h = 0; h < decoded.size(); ++h)
            decoded[h] = centeredExpectation(heads[h].data + t * kHeadBins[h], kHeadBins[h]);
        const float bug = heads[8].data[t];
        if (!std::isfinite(bug))
            throw std::invalid_argument("non-finite bug_frame score");
        const float scale = (t == 0 ? 0.0f : decoded[3]) / 320.0f;
        output.push_back({t == 0 ? 0.0f : decoded[0], t == 0 ? 0.0f : decoded[1], decoded[2] / 2.5f,
                          t == 0 ? 0.0f : scale - previous_scale, decoded[4], decoded[5],
                          decoded[6], decoded[7], bug});
        previous_scale = scale;
    }
    return output;
}

} // namespace pen::y07
