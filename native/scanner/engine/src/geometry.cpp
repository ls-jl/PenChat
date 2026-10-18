#include "internal.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
namespace pen::stream {
std::pair<std::size_t, std::size_t> rss() {
    // Read-only procfs, no logging or file creation. Fixed-size buffer.
    int fd = ::open("/proc/self/status", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return {0, 0};
    char b[8192];
    ssize_t n = read(fd, b, sizeof(b) - 1);
    ::close(fd);
    if (n <= 0)
        return {0, 0};
    b[n] = 0;
    auto value = [&](const char *key) {
        auto p = std::strstr(b, key);
        std::size_t v = 0;
        if (p) {
            p += std::strlen(key);
            while (*p == ' ' || *p == '\t')
                ++p;
            while (*p >= '0' && *p <= '9')
                v = v * 10 + (*p++ - '0');
        }
        return v * 1024;
    };
    return {value("VmRSS:"), value("VmHWM:")};
}
cv::Mat rotateY(const cv::Mat &in, Rotation r) {
    cv::Mat out;
    switch (r) {
    case Rotation::None:
        return in;
    case Rotation::Clockwise90:
        cv::rotate(in, out, cv::ROTATE_90_CLOCKWISE);
        break;
    case Rotation::HalfTurn:
        cv::rotate(in, out, cv::ROTATE_180);
        break;
    case Rotation::CounterClockwise90:
        cv::rotate(in, out, cv::ROTATE_90_COUNTERCLOCKWISE);
        break;
    }
    return out;
}
cv::Matx33d rawToWork(int w, int h, Rotation r, int tw, int th) {
    cv::Matx33d R = cv::Matx33d::eye();
    int ow = w, oh = h;
    if (r == Rotation::CounterClockwise90) {
        R = {0, 1, 0, -1, 0, double(w - 1), 0, 0, 1};
        ow = h;
        oh = w;
    }
    if (r == Rotation::Clockwise90) {
        R = {0, -1, double(h - 1), 1, 0, 0, 0, 0, 1};
        ow = h;
        oh = w;
    }
    if (r == Rotation::HalfTurn)
        R = {-1, 0, double(w - 1), 0, -1, double(h - 1), 0, 0, 1};
    double sx = double(tw) / ow, sy = double(th) / oh;
    return cv::Matx33d(sx, 0, (sx - 1) * .5, 0, sy, (sy - 1) * .5, 0, 0, 1) * R;
}
double Selection::texture(const cv::Mat &s) {
    // Squared difference between the original two vertically separated patches.
    double sum = 0;
    int y0 = s.rows / 5, dy = s.rows / 2 - y0, half = s.cols / 2;
    for (int y = 0; y < dy; ++y)
        for (int x = 0; x < half; ++x) {
            int v = int(s.at<std::uint8_t>(y + y0, x)) - s.at<std::uint8_t>(y + s.rows / 2, x);
            sum += v * v;
        }
    return sum / (dy * half);
}
bool Selection::accept(const cv::Mat &upright) {
    cv::Mat small;
    resizeCamera(upright, small, 96, 144);
    bool accepted = false;
    if (texture(small) >= 100 && !previous.empty()) {
        cv::Mat correlation;
        cv::matchTemplate(previous, small(cv::Rect(4, 14, 32, 116)), correlation,
                          cv::TM_CCOEFF_NORMED);
        double best;
        cv::Point position;
        cv::minMaxLoc(correlation, nullptr, &best, nullptr, &position);
        accepted = best <= .99 || position.x > 4 || position.y > 14;
    }
    previous = std::move(small);
    return accepted;
}
cv::Matx33d WindowGeometry::construct(const y07::DecodedFrame &f, float x, float y, float scale) {
    float a = float(double(f.angle_degrees) * 3.141592653589793 / 180.0), c = std::cos(a),
          s = std::sin(a);
    std::array<cv::Point2f, 4> src{{{0, 288}, {0, 0}, {160, 0}, {160, 288}}};
    std::array<cv::Point2f, 4> dst{
        {{0, 144}, {0, -144}, {160 + f.x2, -144 + f.y2}, {160 + f.x3, 144 + f.y3}}};
    for (auto &p : dst) {
        float px = p.x * c - p.y * s, py = p.x * s + p.y * c;
        p = {px * scale + x * 2, py * scale + y * 2 + 144};
    }
    auto H = cv::getPerspectiveTransform(src.data(), dst.data());
    cv::Matx33d h;
    std::memcpy(h.val, H.ptr<double>(), sizeof(h.val));
    for (double v : h.val)
        if (!std::isfinite(v))
            throw std::runtime_error("non_finite_geometry");
    return h;
}
std::vector<Pose> WindowGeometry::consume(std::uint64_t start,
                                          const std::vector<y07::DecodedFrame> &now, unsigned valid,
                                          bool last) {
    if (valid == 0 || valid > 32 || now.size() != 32 || start != next_ ||
        (hasPrevious_ && start != previousStart_ + 24))
        throw std::runtime_error("window_range_mismatch");
    const unsigned count = last ? valid : 24;
    if (!last && valid != 32)
        throw std::runtime_error("short_non_final_window");
    std::vector<Pose> out;
    out.reserve(count);
    float x = 0, y = 0, scale = 0;
    for (unsigned i = 0; i < count; ++i) {
        // The observed streaming overload gives the earlier window priority
        // on its eight shared frames. Do not substitute the offline averager.
        const auto &f = (hasPrevious_ && i < 8) ? previous_[24 + i] : now[i];
        x += f.dx;
        y += f.dy;
        scale += f.scale_delta;
        out.push_back({start + i, construct(f, x + float(carryX_), y + float(carryY_),
                                            scale + float(carryScale_) + 1.f)});
    }
    // Original streaming state truncates accumulated coordinates at each commit.
    carryX_ = int(x + float(carryX_));
    carryY_ = int(y + float(carryY_));
    carryScale_ = int(scale + float(carryScale_));
    std::copy(now.begin(), now.end(), previous_.begin());
    previousStart_ = start;
    hasPrevious_ = !last;
    next_ = start + count;
    return out;
}
std::vector<Pose> WindowGeometry::flush() {
    if (!hasPrevious_)
        return {};
    std::vector<Pose> out;
    out.reserve(8);
    float x = 0, y = 0, scale = 0;
    for (unsigned i = 24; i < 32; ++i) {
        auto &f = previous_[i];
        x += f.dx;
        y += f.dy;
        scale += f.scale_delta;
        out.push_back({previousStart_ + i, construct(f, x + float(carryX_), y + float(carryY_),
                                                     scale + float(carryScale_) + 1.f)});
    }
    next_ = previousStart_ + 32;
    hasPrevious_ = false;
    return out;
}
} // namespace pen::stream
