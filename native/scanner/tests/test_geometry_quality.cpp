#include "internal.hpp"
#include <cmath>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace pen::stream;
static void need(bool b, const char *message) { if (!b) throw std::runtime_error(message); }
static cv::Matx33d turn(double degrees) {
    const double a = degrees * 3.141592653589793 / 180, c = std::cos(a), s = std::sin(a);
    return {c, -s, 80 - c * 80 + s * 144, s, c, 144 - s * 80 - c * 144, 0, 0, 1};
}
static std::array<std::uint8_t, 80 * 144> texture(const cv::Matx33d &h, double gain = 1, double offset = 0) {
    std::array<std::uint8_t, 80 * 144> pixels;
    for (int y = 0; y < 144; ++y) for (int x = 0; x < 80; ++x) {
        const auto p = h * cv::Vec3d(2 * x + .5, 2 * y + .5, 1);
        const double u = p[0] / p[2], v = p[1] / p[2];
        const double value = 135 + 42 * std::sin(.19 * u) + 40 * std::cos(.16 * v) + 20 * std::sin(.008 * u * v);
        pixels[y * 80 + x] = std::uint8_t(std::clamp(value * gain + offset, 0., 255.));
    }
    return pixels;
}
static void quality() {
    auto budget = std::make_shared<Budget>(Limits{});
    GeometryQuality guard;
    const auto identity = cv::Matx33d::eye();
    auto pixels = texture(identity);
    need(!guard.accept(identity, pixels.data(), budget), "first frame accepted");
    need(budget->other <= 96 * 1024, "thumbnail history bounded, no full-frame copies");
    need(guard.accept(turn(35), pixels.data(), budget), "incorrect tilted tail rejected before painting");
    need(!guard.accept(identity, pixels.data(), budget), "rejected pose does not poison history");
    guard.clear(); need(!budget->other, "quiescent clear releases history pages");
    // Stable, genuinely rotated images must not be rejected by an absolute
    // angle cutoff. Cross +/-180 as well as a large, gradual hand turn.
    for (int degree = 0; degree <= 220; degree += 2) {
        auto h = turn(degree); auto p = texture(h, .85, 15);
        need(!guard.accept(h, p.data(), budget), "image-supported rotation retained");
    }
    guard.clear();
    need(!guard.accept(identity, pixels.data(), budget), "fresh reference");
    for (int degree = 1; degree <= 6; ++degree)
        need(!guard.accept(turn(degree), pixels.data(), budget), "small changes within tolerance");
    need(guard.accept(turn(12), pixels.data(), budget), "gradual wrong turn checked against older reference");
    guard.clear();
    std::array<std::uint8_t, 80 * 144> blank; blank.fill(220);
    need(!guard.accept(identity, blank.data(), budget), "blank reference");
    need(!guard.accept(turn(40), blank.data(), budget), "low texture is unknown, not a mismatch");
    for (auto h : {cv::Matx33d(-1, 0, 160, 0, 1, 0, 0, 0, 1),
                   cv::Matx33d(1, 0, 0, 0, .01, 0, 0, 0, 1),
                   cv::Matx33d(1, 0, 0, 0, 1, 0, -.006, 0, 1),
                   cv::Matx33d(1, 0, 0, 0, 1, 0, -.01, 0, 1)})
        need(guard.accept(h, blank.data(), budget), "fold/horizon/severe stretch rejected even without texture");
    guard.clear();
    for (unsigned i = 0; i < 40; ++i) {
        auto h = identity; h(0, 2) = i * 1000;
        need(!guard.accept(h, pixels.data(), budget), "no overlap is unknown, not a mismatch");
    }
    guard.clear(); need(!budget->other, "all pages released");
}
class Tail final : public Inference {
    unsigned call_ = 0, bad_;
  public:
    explicit Tail(unsigned bad) : bad_(bad) {}
    void open() override {}
    void close() noexcept override {}
    std::size_t managedBytes() const noexcept override { return 0; }
    void infer(const NetworkBatch &, Heads &heads) override {
        for (unsigned k = 0; k < 9; ++k) {
            const auto bins = pen::y07::kHeadBins[k]; heads[k].assign(32 * bins, -80);
            for (unsigned t = 0; t < 32; ++t) {
                const unsigned at = unsigned(bins / 2) + (k == 2 && call_ * 24 + t >= bad_ ? 88 : 0);
                heads[k][t * bins + at] = 0;
            }
        }
        ++call_;
    }
};
static void pipeline() {
    for (unsigned total : {31u, 32u, 33u, 56u, 57u, 80u}) {
        const unsigned bad = total - 2;
        Engine engine(std::make_unique<Tail>(bad));
        Config c; c.width = 160; c.height = 288; c.rotation = Rotation::None; c.diagnosticAcceptAll = true;
        auto calibration = std::make_shared<Calibration>(); calibration->version = "synthetic-tail";
        calibration->sensorWidth = c.width; calibration->sensorHeight = c.height;
        calibration->rotation = c.rotation; calibration->gain.fill(1); c.calibration = calibration;
        auto model = texture(cv::Matx33d::eye()); cv::Mat source;
        resizeCamera(cv::Mat(144, 80, CV_8UC1, model.data()), source, c.width, c.height);
        unsigned stops = 0;
        auto scan = engine.beginScan(c, {[&](const Event &e) {
            if (e.kind == Event::Kind::StopRequested) {
                need(std::string(e.reason) == "geometry_unstable", "specific stop reason"); ++stops;
            }
        }});
        unsigned accepted = 0;
        for (unsigned i = 0; i < total; ++i) {
            if (!engine.pushFrame(scan.scan, source.data, c.width, c.height, source.step, i, i).accepted) break;
            ++accepted;
        }
        engine.endScan(scan.scan); auto result = engine.wait(scan.scan); const auto counts = result->counts();
        need(result->status() == Status::Segmented && result->reason() == "geometry_unstable", "partial is explicit");
        need(counts.committed == bad && counts.unlocated + bad == accepted && !counts.pending && stops == 1,
             "accepted originals retained, no silent drop or double commit");
        const auto late = engine.pushFrame(scan.scan, source.data, c.width, c.height, source.step, total, total);
        need(!late.accepted && late.code == Code::StopCapture && std::string(late.reason) == "geometry_unstable",
             "capture racing the stop event sees the real cause, not a false memory failure");
        for (unsigned i = 0; i < result->unlocatedCount(); ++i)
            need(result->unlocatedFrame(i).sequence == bad + i, "remaining raw identity preserved");
        const auto bounds = result->bounds();
        need(bounds.width <= c.width + 1 && bounds.height <= c.height + 1, "bad tail cannot enlarge or overwrite canvas");
        std::vector<std::uint8_t> row(bounds.width), mask(bounds.width);
        need(result->readRow(bounds.height / 2, row.data(), mask.data(), row.size()) == Code::Ok, "earlier image remains readable");
        engine.releaseResult(scan.scan); result.reset(); engine.shutdown();
        need(!engine.metrics().frames && !engine.metrics().managedBytes, "tail failure releases all references");
    }
}
static void benchmark() {
    GeometryQuality guard; auto budget = std::make_shared<Budget>(Limits{});
    std::vector<cv::Matx33d> poses;
    std::vector<std::array<std::uint8_t, 80 * 144>> images;
    for (unsigned i = 0; i < 24; ++i) { poses.push_back(turn(i * 2)); images.push_back(texture(poses.back())); }
    std::vector<long long> times;
    for (unsigned i = 0; i < 240; ++i) {
        const auto n = i % poses.size();
        const auto start = std::chrono::steady_clock::now();
        need(!guard.accept(poses[n], images[n].data(), budget), "benchmark valid image");
        times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());
    }
    std::sort(times.begin(), times.end());
    std::cout << "GEOMETRY_VALIDATION_US p50=" << times[times.size()/2]
              << " p95=" << times[times.size()*95/100] << " max=" << times.back()
              << " history_bytes=" << budget->other << '\n';
}
int main() {
    try { quality(); pipeline(); benchmark(); std::cout << "GEOMETRY_QUALITY_AND_TAIL_PRESERVATION_OK\n"; return 0; }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
