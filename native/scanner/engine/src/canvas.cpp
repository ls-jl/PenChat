#include "internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
namespace pen::stream {
namespace {
constexpr std::size_t hotBytes = 128 * 128 * 5;
std::uint32_t word(const std::uint8_t *p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
void unpack(const std::uint8_t *src, std::size_t length, std::uint8_t *dst) {
    std::size_t p = 0, i = 0;
    while (p < length) {
        unsigned n = unsigned(src[p]) | (unsigned(src[p + 1]) << 8);
        p += 2;
        for (unsigned k = 0; k < n; ++k, ++i) {
            std::memcpy(dst + i * 4, src + p, 4);
            dst[128 * 128 * 4 + i] = src[p + 4];
        }
        p += 5;
    }
}
} // namespace
bool MemoryCanvas::compress(Tile &t) {
    std::lock_guard<std::recursive_mutex> guard(budget_->admission);
    if (!t.hot.size())
        return false;
    const auto *p = t.hot.bytes();
    std::size_t out = 0;
    for (unsigned i = 0; i < N;) {
        unsigned j = i + 1;
        auto v = word(p + i * 4);
        auto mask = p[N * 4 + i];
        while (j < N && j - i < 65535 && word(p + j * 4) == v && p[N * 4 + j] == mask)
            ++j;
        scratch_[out++] = std::uint8_t(j - i);
        scratch_[out++] = std::uint8_t((j - i) >> 8);
        std::memcpy(scratch_.data() + out, p + i * 4, 4);
        out += 4;
        scratch_[out++] = mask;
        i = j;
    }
    const auto need = (out + 4095) & ~std::size_t(4095);
    if (need >= t.hot.size())
        return false;
    // Scratch owns the exact lossless replacement while pages are exchanged.
    t.hot.clear();
    t.cold.allocate(out, budget_, budget_->canvas, budget_->limits.canvasBytes);
    std::memcpy(t.cold.bytes(), scratch_.data(), out);
    t.coldSize = out;
    return true;
}
bool MemoryCanvas::room(std::size_t n) {
    if (budget_->canvas + n <= budget_->limits.canvasBytes)
        return true;
    // Tile count is bounded by the canvas budget, no unbounded LRU history.
    std::vector<Tile *> candidates;
    candidates.reserve(tiles_.size());
    for (auto &e : tiles_)
        if (e.second->hot.size() && !e.second->pinned)
            candidates.push_back(e.second.get());
    std::sort(candidates.begin(), candidates.end(),
              [](auto *a, auto *b) { return a->age < b->age; });
    for (auto *t : candidates) {
        compress(*t);
        if (budget_->canvas + n <= budget_->limits.canvasBytes)
            return true;
    }
    return false;
}
bool MemoryCanvas::heat(Tile &t) {
    std::lock_guard<std::recursive_mutex> guard(budget_->admission);
    if (t.hot.size()) {
        t.age = ++clock_;
        return true;
    }
    std::size_t old = t.cold.size();
    if (!room(hotBytes - old))
        return false;
    if (t.cold.size()) {
        unpack(t.cold.bytes(), t.coldSize, scratch_.data());
        t.cold.clear();
    } else {
        std::fill(scratch_.begin(), scratch_.begin() + hotBytes, 0);
    }
    t.hot.allocate(hotBytes, budget_, budget_->canvas, budget_->limits.canvasBytes);
    std::memcpy(t.hot.bytes(), scratch_.data(), hotBytes);
    t.age = ++clock_;
    return true;
}
bool MemoryCanvas::draw(const cv::Mat &gray, const cv::Matx33d &h, const cv::Matx33d &toSmall,
                        bool first) {
    const double determinant = cv::determinant(h);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
        throw std::runtime_error("singular_geometry");
    auto map = [](const cv::Matx33d &a, double x, double y) {
        auto v = a * cv::Vec3d(x, y, 1);
        if (std::abs(v[2]) < 1e-9)
            throw std::runtime_error("projective_horizon");
        return cv::Point2d(v[0] / v[2], v[1] / v[2]);
    };
    double minx = 1e30, miny = 1e30, maxx = -1e30, maxy = -1e30, sign = 0;
    for (auto p : {cv::Point2d(-.5, -.5), cv::Point2d(gray.cols - .5, -.5),
                   cv::Point2d(gray.cols - .5, gray.rows - .5), cv::Point2d(-.5, gray.rows - .5)}) {
        double z = h(2, 0) * p.x + h(2, 1) * p.y + h(2, 2);
        if (!std::isfinite(z) || std::abs(z) < 1e-8 || (sign && sign * z < 0))
            throw std::runtime_error("projective_horizon");
        sign = z;
        auto q = map(h, p.x, p.y);
        minx = std::min(minx, q.x);
        miny = std::min(miny, q.y);
        maxx = std::max(maxx, q.x);
        maxy = std::max(maxy, q.y);
    }
    if (!std::isfinite(minx) || !std::isfinite(maxx) || !std::isfinite(miny) ||
        !std::isfinite(maxy) ||
        std::max({std::abs(minx), std::abs(maxx), std::abs(miny), std::abs(maxy)}) > 1000000)
        throw std::runtime_error("geometry_out_of_range");
    int x0 = int(std::ceil(minx - 1e-7)), x1 = int(std::floor(maxx + 1e-7)),
        y0 = int(std::ceil(miny - 1e-7)), y1 = int(std::floor(maxy + 1e-7));
    std::size_t count = std::size_t(divide(x1) - divide(x0) + 1) * (divide(y1) - divide(y0) + 1);
    if (count > budget_->limits.canvasBytes / 4096)
        return false;
    // Reserve all new tiles before touching pixels; a capacity failure never
    // leaves a half-painted accepted frame. Cold tile decoding uses scratch.
    std::vector<std::pair<int, int>> added;
    struct Pins {
        std::map<std::pair<int, int>, std::unique_ptr<Tile>> &t;
        ~Pins() {
            for (auto &e : t)
                e.second->pinned = false;
        }
    } pins{tiles_};
    for (auto &e : tiles_)
        if (e.first.first >= divide(x0) && e.first.first <= divide(x1) &&
            e.first.second >= divide(y0) && e.first.second <= divide(y1))
            e.second->pinned = true;
    for (int ty = divide(y0); ty <= divide(y1); ++ty)
        for (int tx = divide(x0); tx <= divide(x1); ++tx) {
            auto key = std::make_pair(tx, ty);
            if (!tiles_.count(key)) {
                if (!room(hotBytes + 256)) {
                    for (auto k : added)
                        tiles_.erase(k);
                    return false;
                }
                auto t = std::make_unique<Tile>(budget_);
                t->pinned = true;
                try {
                    t->hot.allocate(hotBytes, budget_, budget_->canvas,
                                    budget_->limits.canvasBytes);
                } catch (const std::bad_alloc &) {
                    for (auto k : added)
                        tiles_.erase(k);
                    return false;
                }
                tiles_.emplace(key, std::move(t));
                added.push_back(key);
            }
        }
    cv::Matx33d inv = h.inv();
    for (int ty = divide(y0); ty <= divide(y1); ++ty)
        for (int tx = divide(x0); tx <= divide(x1); ++tx) {
            auto &t = *tiles_.at({tx, ty});
            bool cold = !t.hot.size();
            // Reading a cold tile into a fixed scratch block avoids decompression
            // allocations at the resource boundary. Newly modified cold tiles may
            // grow, so preflight currently heats all affected tiles before painting.
            (void)cold;
            if (!heat(t)) {
                for (auto k : added)
                    tiles_.erase(k);
                return false;
            }
        }
    for (int ty = divide(y0); ty <= divide(y1); ++ty)
        for (int tx = divide(x0); tx <= divide(x1); ++tx) {
            auto &t = *tiles_.at({tx, ty});
            // No allocations or compression after the first pixel is written.
            if (!t.hot.size())
                throw std::runtime_error("canvas_hot_reservation_lost");
            auto *g = reinterpret_cast<float *>(t.hot.bytes());
            auto *mask = t.hot.bytes() + N * 4;
            for (int y = std::max(y0, ty * T); y <= std::min(y1, (ty + 1) * T - 1); ++y) {
                int begin = std::max(x0, tx * T), end = std::min(x1, (tx + 1) * T - 1),
                    validBegin = end + 1, validEnd = begin - 1;
                double nx = inv(0, 0) * begin + inv(0, 1) * y + inv(0, 2),
                       ny = inv(1, 0) * begin + inv(1, 1) * y + inv(1, 2),
                       den = inv(2, 0) * begin + inv(2, 1) * y + inv(2, 2);
                for (int x = begin; x <= end;
                     ++x, nx += inv(0, 0), ny += inv(1, 0), den += inv(2, 0)) {
                    if (!std::isfinite(den) || std::abs(den) < 1e-12)
                        continue;
                    double reciprocal = 1. / den, qx = nx * reciprocal, qy = ny * reciprocal;
                    if (qx < -.5 || qy < -.5 || qx >= gray.cols - .5 || qy >= gray.rows - .5)
                        continue;
                    double sx = std::clamp(qx, 0., double(gray.cols - 1)),
                           sy = std::clamp(qy, 0., double(gray.rows - 1));
                    int ix = int(sx), iy = int(sy), jx = std::min(ix + 1, gray.cols - 1),
                        jy = std::min(iy + 1, gray.rows - 1);
                    float ax = float(sx - ix), ay = float(sy - iy);
                    const auto *a = gray.ptr<std::uint8_t>(iy);
                    const auto *b = gray.ptr<std::uint8_t>(jy);
                    float v = (a[ix] * (1 - ax) + a[jx] * ax) * (1 - ay) +
                              (b[ix] * (1 - ax) + b[jx] * ax) * ay;
                    // rawToWork is affine. Avoid a second projective division
                    // and general matrix product at every output pixel.
                    double smallX = toSmall(0, 0) * qx + toSmall(0, 1) * qy + toSmall(0, 2),
                           smallY = toSmall(1, 0) * qx + toSmall(1, 1) * qy + toSmall(1, 2);
                    float alpha = first ? 1.f : std::clamp(float((smallX - 32.) / 7.), 0.f, 1.f);
                    if (smallX < 0 || smallX >= 159 || smallY < 0 || smallY >= 287)
                        alpha = 0;
                    int k = (y - ty * T) * T + (x - tx * T);
                    if (!mask[k]) {
                        g[k] = v;
                        mask[k] = 255;
                    } else
                        g[k] = g[k] * (1 - alpha) + v * alpha;
                    validBegin = std::min(validBegin, x);
                    validEnd = x;
                }
                if (validBegin <= validEnd) {
                    if (!bounds_.width)
                        bounds_ = {validBegin, y, validEnd - validBegin + 1, 1};
                    else {
                        int bx = std::min(bounds_.x, validBegin), by = std::min(bounds_.y, y),
                            ex = std::max(bounds_.x + bounds_.width, validEnd + 1),
                            ey = std::max(bounds_.y + bounds_.height, y + 1);
                        bounds_ = {bx, by, ex - bx, ey - by};
                    }
                }
            }
        }
    return true;
}
Code MemoryCanvas::row(int row, std::uint8_t *gray, std::uint8_t *mask, std::size_t capacity) {
    if (row < 0 || row >= bounds_.height || !gray || !mask || capacity < std::size_t(bounds_.width))
        return Code::InvalidArgument;
    std::memset(gray, 255, bounds_.width);
    std::memset(mask, 0, bounds_.width);
    int y = row + bounds_.y, ty = divide(y);
    for (int tx = divide(bounds_.x); tx <= divide(bounds_.x + bounds_.width - 1); ++tx) {
        auto it = tiles_.find({tx, ty});
        if (it == tiles_.end())
            continue;
        auto &t = *it->second;
        const std::uint8_t *p = t.hot.bytes();
        if (!p) {
            unpack(t.cold.bytes(), t.coldSize, scratch_.data());
            p = scratch_.data();
        }
        auto *values = reinterpret_cast<const float *>(p);
        auto *valid = p + N * 4;
        for (int x = std::max(bounds_.x, tx * T);
             x < std::min(bounds_.x + bounds_.width, (tx + 1) * T); ++x) {
            int k = (y - ty * T) * T + x - tx * T, j = x - bounds_.x;
            if (valid[k]) {
                mask[j] = 255;
                gray[j] = std::uint8_t(std::clamp(values[k] + .5f, 0.f, 255.f));
            }
        }
    }
    return Code::Ok;
}
Code MemoryCanvas::preview(int w, int h, std::uint8_t *gray, std::uint8_t *mask,
                           std::size_t capacity) {
    if (w < 1 || h < 1 || w > 1024 || h > 256 || !gray || !mask || capacity < std::size_t(w) * h)
        return Code::InvalidArgument;
    if (!bounds_.width)
        return Code::NotReady;
    for (int y = 0; y < h; ++y) {
        int wy =
            bounds_.y + std::min(bounds_.height - 1, int((double(y) + .5) * bounds_.height / h));
        std::pair<int, int> last{std::numeric_limits<int>::min(), 0};
        const std::uint8_t *p = nullptr;
        for (int x = 0; x < w; ++x) {
            int wx =
                bounds_.x + std::min(bounds_.width - 1, int((double(x) + .5) * bounds_.width / w));
            auto key = std::make_pair(divide(wx), divide(wy));
            if (key != last) {
                last = key;
                auto it = tiles_.find(key);
                p = nullptr;
                if (it != tiles_.end()) {
                    auto &t = *it->second;
                    p = t.hot.bytes();
                    if (!p) {
                        unpack(t.cold.bytes(), t.coldSize, scratch_.data());
                        p = scratch_.data();
                    }
                }
            }
            unsigned k = (wy - key.second * T) * T + wx - key.first * T;
            std::size_t out = std::size_t(y) * w + x;
            mask[out] = p ? p[N * 4 + k] : 0;
            if (mask[out]) {
                float v;
                std::memcpy(&v, p + k * 4, 4);
                gray[out] = std::uint8_t(std::clamp(v + .5f, 0.f, 255.f));
            } else
                gray[out] = 255;
        }
    }
    return Code::Ok;
}
} // namespace pen::stream
