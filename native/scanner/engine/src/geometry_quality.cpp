#include "internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace pen::stream {
namespace {
// Work coordinates are twice the 80x144 model pixel centres. Sensor size and
// rotation have already been handled by preprocessing, not by this validator.
const cv::Matx33d up(2, 0, .5, 0, 2, .5, 0, 0, 1);
const cv::Matx33d down(.5, 0, -.25, 0, .5, -.25, 0, 0, 1);
bool jacobian(const cv::Matx33d &h, double x, double y, cv::Matx22d &j) {
    auto p = h * cv::Vec3d(x, y, 1);
    if (!std::isfinite(p[2]) || std::abs(p[2]) < 1e-10) return false;
    const double u = p[0] / p[2], v = p[1] / p[2];
    j = {(h(0, 0) - u * h(2, 0)) / p[2], (h(0, 1) - u * h(2, 1)) / p[2],
         (h(1, 0) - v * h(2, 0)) / p[2], (h(1, 1) - v * h(2, 1)) / p[2]};
    return std::all_of(std::begin(j.val), std::end(j.val), [](double n) { return std::isfinite(n); });
}
bool stretches(const cv::Matx22d &j, double &lo, double &hi) {
    const double det = cv::determinant(j);
    if (!std::isfinite(det) || det <= 1e-10) return false; // No reflected paper.
    double sum = 0;
    for (double n : j.val) sum += n * n;
    hi = std::sqrt((sum + std::sqrt(std::max(0., sum * sum - 4 * det * det))) * .5);
    lo = det / hi;
    return std::isfinite(lo) && std::isfinite(hi);
}
bool plausible(const cv::Matx33d &h) {
    for (double n : h.val) if (!std::isfinite(n)) return false;
    double smallest = 1e30, largest = 0, sign = 0;
    for (double y : {0., 144., 288.}) for (double x : {0., 80., 160.}) {
        const double z = h(2, 0) * x + h(2, 1) * y + h(2, 2);
        if (!std::isfinite(z) || std::abs(z) < 1e-10 || (sign && sign * z <= 0)) return false;
        sign = z; smallest = std::min(smallest, std::abs(z)); largest = std::max(largest, std::abs(z));
        cv::Matx22d j; double lo, hi;
        if (!jacobian(h, x, y, j) || !stretches(j, lo, hi) || lo < .125 || hi > 8 || hi / lo > 12)
            return false;
    }
    return largest <= smallest * 8;
}
bool changedShape(const cv::Matx33d &now, const cv::Matx33d &before) {
    cv::Matx22d a, b;
    if (!jacobian(now, 80, 144, a) || !jacobian(before, 80, 144, b)) return true;
    const auto relative = b.inv() * a;
    double lo, hi;
    if (!stretches(relative, lo, hi)) return true;
    const double angle = std::atan2(relative(1, 0) - relative(0, 1), relative(0, 0) + relative(1, 1));
    return std::abs(angle) > 8 * 3.141592653589793 / 180 || lo < .8 || hi > 1.25;
}
// No evidence is different from a mismatch: little overlap/texture must never
// be interpreted as a bad frame. Search only +/-2 thumbnail pixels to tolerate
// quantization/translation error, not a second unbounded registration engine.
double correlation(const std::uint8_t *now, const std::uint8_t *before, const cv::Matx33d &map) {
    double best = -2;
    const int offsets[][2] = {{0, 0}, {-2, 0}, {2, 0}, {0, -2}, {0, 2},
                              {-2, -2}, {2, -2}, {-2, 2}, {2, 2}};
    for (const auto &offset : offsets) {
        const int ox = offset[0], oy = offset[1];
        double sa = 0, sb = 0, aa = 0, bb = 0, ab = 0; unsigned n = 0;
        for (int y = 5; y < 139; y += 4) for (int x = 5; x < 75; x += 3) {
            const auto v = map * cv::Vec3d(x, y, 1);
            if (!std::isfinite(v[2]) || std::abs(v[2]) < 1e-10) continue;
            const double qx = v[0] / v[2] + ox, qy = v[1] / v[2] + oy;
            if (!std::isfinite(qx) || !std::isfinite(qy) || qx < 2 || qx >= 77 || qy < 2 || qy >= 141) continue;
            const int ix = int(qx), iy = int(qy);
            const double ax = qx - ix, ay = qy - iy, a = now[y * 80 + x];
            const double b = (before[iy * 80 + ix] * (1 - ax) + before[iy * 80 + ix + 1] * ax) * (1 - ay) +
                             (before[(iy + 1) * 80 + ix] * (1 - ax) + before[(iy + 1) * 80 + ix + 1] * ax) * ay;
            sa += a; sb += b; aa += a * a; bb += b * b; ab += a * b; ++n;
        }
        if (n < 160) continue;
        const double va = aa - sa * sa / n, vb = bb - sb * sb / n;
        if (va < 50 * n || vb < 50 * n) continue;
        best = std::max(best, (ab - sa * sb / n) / std::sqrt(va * vb));
        // Only the <.2 rejection is consumed. A valid match needs no further
        // offset search, particularly in ordinary small-angle streaming.
        if (best >= .2) return best;
    }
    return best;
}
} // namespace
const char *GeometryQuality::accept(const cv::Matx33d &pose, const std::uint8_t *gray,
                                    const std::shared_ptr<Budget> &budget) {
    if (!plausible(pose)) return "geometry_unstable";
    // At most eight references, no source images. Check the oldest overlapping
    // informative reference first, so a gradual tail turn cannot evade the
    // check merely because each individual angular increment is small.
    if (gray) for (unsigned i = 0; i < used_; ++i) {
        const unsigned slot = (next_ + History - used_ + i) % History;
        if (!changedShape(pose, poses_[slot])) continue;
        const double score = correlation(gray, images_.bytes() + slot * Pixels,
                                          down * poses_[slot].inv() * pose * up);
        if (score == -2) continue;
        if (score < .2) return "geometry_unstable";
        break;
    }
    if (gray) {
        if (!images_.size()) images_.allocate(History * Pixels, budget, budget->other);
        std::memcpy(images_.bytes() + next_ * Pixels, gray, Pixels);
        poses_[next_] = pose;
        next_ = (next_ + 1) % History; used_ = std::min(used_ + 1, History);
    }
    return nullptr;
}
} // namespace pen::stream
