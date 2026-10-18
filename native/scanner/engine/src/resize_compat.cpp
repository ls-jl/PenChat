#include "internal.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace pen::stream {
// Re-expressed scalar arithmetic of OpenCV 3.4.3 Carotene's 8UC1 bilinear
// path: vertical first, seven-bit weights truncated, round at BOTH stages.
// Source: opencv/opencv 3.4.3 3rdparty/carotene/src/resize.cpp. See NOTICE.
// Explicit arithmetic also makes host tests independent of the host's SIMD.
void resizeCamera(const cv::Mat &src, cv::Mat &dst, int width, int height) {
    if (src.type() != CV_8UC1 || width < 1 || height < 1 || width > 1024 || height > 1024)
        throw std::invalid_argument("resize_contract");
    dst.create(height, width, CV_8UC1);
    const float sx = float(src.cols) / width, sy = float(src.rows) / height, ox = .5f * sx - .5f,
                oy = .5f * sy - .5f;
    std::array<int, 1024> left, right, wx;
    for (int x = 0; x < width; ++x) {
        float q = float(x) * sx + ox;
        int hi = int(std::ceil(q));
        wx[x] = int((float(hi) - q) * 128);
        left[x] = std::clamp(hi - 1, 0, src.cols - 1);
        right[x] = std::clamp(hi, 0, src.cols - 1);
    }
    for (int y = 0; y < height; ++y) {
        float q = float(y) * sy + oy;
        int lo = int(std::floor(q)), wy = int((float(lo + 1) - q) * 128);
        auto *a = src.ptr<std::uint8_t>(std::clamp(lo, 0, src.rows - 1));
        auto *b = src.ptr<std::uint8_t>(std::clamp(lo + 1, 0, src.rows - 1));
        auto *out = dst.ptr<std::uint8_t>(y);
        for (int x = 0; x < width; ++x) {
            int u = (a[left[x]] * wy + b[left[x]] * (128 - wy) + 64) >> 7,
                v = (a[right[x]] * wy + b[right[x]] * (128 - wy) + 64) >> 7;
            out[x] = std::uint8_t((u * wx[x] + v * (128 - wx[x]) + 64) >> 7);
        }
    }
}
} // namespace pen::stream
