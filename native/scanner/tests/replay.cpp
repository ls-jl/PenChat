// Test harness only. Core has no output-file or PNG dependency. Input is a
// binary stream on stdin; results on stdout, bounded per-frame diagnostics on
// stderr. Cached-head mode is HOST ORACLE ONLY, never a production backend.
#include "geometry_decoder.hpp"
#include "pen_stream/stream.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace pen::stream;
class Cached final : public Inference {
    std::string root;
    unsigned call = 0;

  public:
    explicit Cached(std::string p) : root(std::move(p)) {}
    void open() override {}
    void close() noexcept override {}
    std::size_t managedBytes() const noexcept override {
        return 0;
    }
    void infer(const NetworkBatch &b, Heads &heads) override {
        char stem[64];
        std::snprintf(stem, sizeof(stem), "/call_%03u", call++);
        std::string prefix = root + stem;
        std::ifstream input(prefix + "_input.bin", std::ios::binary);
        std::vector<unsigned char> oracle(32 * 144 * 80 * 3);
        input.read(reinterpret_cast<char *>(oracle.data()), oracle.size());
        if (!input)
            throw std::runtime_error("missing_oracle_input");
        const unsigned char padding[3] = {124, 116, 104};
        std::size_t different = 0;
        for (unsigned t = 0; t < 32; ++t)
            for (unsigned i = 0; i < 144 * 80; ++i)
                for (unsigned c = 0; c < 3; ++c) {
                    auto v = t < b.validCount ? b.gray[t][i] : padding[c];
                    different += v != oracle[(t * 144 * 80 + i) * 3 + c];
                }
        std::fprintf(stderr, "ORACLE_INPUT call=%u valid=%u different=%zu\n", call - 1,
                     b.validCount, different);
        if (different)
            throw std::runtime_error("oracle_input_mismatch");
        for (unsigned h = 0; h < 9; ++h) {
            heads[h].resize(32 * pen::y07::kHeadBins[h]);
            std::ifstream f(prefix + "_head_" + std::to_string(h) + ".bin", std::ios::binary);
            f.read(reinterpret_cast<char *>(heads[h].data()), heads[h].size() * 4);
            if (!f)
                throw std::runtime_error("missing_oracle_head");
        }
    }
};
class Metered final : public Inference {
    std::unique_ptr<Inference> backend;

  public:
    explicit Metered(std::unique_ptr<Inference> b) : backend(std::move(b)) {}
    void open() override {
        auto t = std::chrono::steady_clock::now();
        backend->open();
        std::fprintf(stderr, "MODEL_OPEN_MS %.3f\n",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t)
                         .count());
    }
    void close() noexcept override {
        backend->close();
    }
    std::size_t managedBytes() const noexcept override {
        return backend->managedBytes();
    }
    void infer(const NetworkBatch &b, Heads &h) override {
        auto t = std::chrono::steady_clock::now();
        backend->infer(b, h);
        std::fprintf(stderr, "INFER_MS %.3f\n",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t)
                         .count());
    }
};
static void read(void *p, std::size_t n) {
    if (std::fread(p, 1, n, stdin) != n)
        throw std::runtime_error("truncated_input");
}
static void write(const void *p, std::size_t n) {
    if (std::fwrite(p, 1, n, stdout) != n)
        throw std::runtime_error("output_failure");
}
int main(int argc, char **argv) {
    try {
        if (argc >= 6 && std::string(argv[argc - 2]) == "--tcp") {
            unsigned port = 0;
            for (const char *p = argv[argc - 1]; *p; ++p) {
                if (*p < '0' || *p > '9')
                    throw std::runtime_error("bad_port");
                port = port * 10 + (*p - '0');
            }
            if (port < 1024 || port > 65535)
                throw std::runtime_error("bad_port");
            int listener = socket(AF_INET, SOCK_STREAM, 0), one = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = htons(port);
            if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ||
                listen(listener, 1))
                throw std::runtime_error("socket_bind");
            std::fprintf(stderr, "STREAM32_LISTEN %u\n", port);
            int client = accept(listener, nullptr, nullptr);
            close(listener);
            if (client < 0)
                throw std::runtime_error("socket_accept");
            dup2(client, STDIN_FILENO);
            dup2(client, STDOUT_FILENO);
            close(client);
            argc -= 2;
        }
        std::setvbuf(stdout, nullptr, _IONBF, 0);

        if (argc < 3)
            throw std::runtime_error(
                "Usage: stream32_replay rknn MODEL [RUNTIME] | cached NN_ORACLE_DIRECTORY");
        std::unique_ptr<Inference> backend;
        if (std::string(argv[1]) == "cached")
            backend = std::make_unique<Cached>(argv[2]);
        else
            backend = makeRknn32(argv[2], argc > 3 ? argv[3] : "librknnrt.so");
        auto started = std::chrono::steady_clock::now();
        std::array<std::atomic<bool>, 4> firstPreview{};
        std::mutex diagnosticMutex;
        Engine engine(std::make_unique<Metered>(std::move(backend)));
        // Header: magic PS32, version=1, session count. Each scan: w,h,rotation,
        // frame count, replay interval usec, flags (1=accept all diagnostic).
        char magic[4];
        read(magic, 4);
        std::uint32_t version, sessions;
        read(&version, 4);
        read(&sessions, 4);
        if (std::memcmp(magic, "PS32", 4) || version != 1 || sessions > 4)
            throw std::runtime_error("bad_protocol");
        std::vector<ScanId> ids;
        started = std::chrono::steady_clock::now();
        bool idleTest = false;
        unsigned peakFrames = 0;
        std::size_t peakManaged = 0, peakCanvas = 0;
        for (unsigned j = 0; j < sessions; ++j) {
            std::uint32_t h[6];
            read(h, sizeof(h));
            Config c;
            c.width = h[0];
            c.height = h[1];
            c.rotation = Rotation(h[2]);
            c.diagnosticAcceptAll = h[5] & 1;
            idleTest = idleTest || (h[5] & 2);
            auto calibration = std::make_shared<Calibration>();
            calibration->version = "frozen-test-field-v1";
            calibration->sensorWidth = c.width;
            calibration->sensorHeight = c.height;
            calibration->rotation = c.rotation;
            read(calibration->gain.data(), calibration->gain.size() * 4);
            c.calibration = calibration;
            auto s = engine.beginScan(
                c, {[&](const Event &e) {
                    std::lock_guard<std::mutex> guard(diagnosticMutex);
                    if (e.kind == Event::Kind::Preview &&
                        !firstPreview[e.scan - 1].exchange(true)) {
                        std::fprintf(
                            stderr, "FIRST_PREVIEW_US %llu %lld\n", (unsigned long long)e.scan,
                            (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count());
                        auto view = engine.preview(e.scan);
                        std::array<std::uint8_t, 320 * 96> g{}, m{};
                        if (view && view->bounds().width &&
                            view->readPreview(320, 96, g.data(), m.data(), g.size()) != Code::Ok)
                            std::fprintf(stderr, "PREVIEW_ERROR\n");
                    } else if (e.kind == Event::Kind::Submitted) {
                        char line[512];
                        int n = std::snprintf(
                            line, sizeof(line), "POSE %llu %llu %llu", (unsigned long long)e.scan,
                            (unsigned long long)e.sequence, (unsigned long long)e.selectedIndex);
                        for (auto x : e.rawToCanvas)
                            n += std::snprintf(line + n, sizeof(line) - n, " %.12g", x);
                        std::fprintf(stderr, "%s\n", line);
                    } else if (e.kind == Event::Kind::Selected || e.kind == Event::Kind::Filtered)
                        std::fprintf(stderr, "SELECT %llu %llu %d\n", (unsigned long long)e.scan,
                                     (unsigned long long)e.sequence,
                                     e.kind == Event::Kind::Selected);
                    else if (e.kind == Event::Kind::Finished ||
                             e.kind == Event::Kind::StopRequested)
                        std::fprintf(stderr, "EVENT %llu %d %s\n", (unsigned long long)e.scan,
                                     int(e.kind), e.reason);
                }});
            if (s.code != Code::Ok)
                throw std::runtime_error("begin_scan_rejected_" + std::to_string(int(s.code)));
            ids.push_back(s.scan);
            std::vector<std::uint8_t> raw(std::size_t(c.width) * c.height);
            auto captureStart = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < h[3]; ++i) {
                read(raw.data(), raw.size());
                if (h[4])
                    std::this_thread::sleep_until(
                        captureStart + std::chrono::microseconds(std::uint64_t(i) * h[4]));
                auto p = engine.pushFrame(s.scan, raw.data(), c.width, c.height, c.width, i,
                                          std::int64_t(i) * h[4]);
                auto snapshot = engine.metrics();
                peakFrames = std::max(peakFrames, snapshot.frames);
                peakManaged = std::max(peakManaged, snapshot.managedBytes);
                peakCanvas = std::max(peakCanvas, snapshot.canvasBytes);
                if (p.code != Code::Ok)
                    std::fprintf(stderr, "PUSH_STOP scan=%llu input=%u accepted=%d\n",
                                 (unsigned long long)s.scan, i, p.accepted);
            }
            engine.endScan(s.scan);
            std::fprintf(stderr, "END_SCAN_US %llu %lld\n", (unsigned long long)s.scan,
                         (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count());
        }
        for (auto id : ids)
            engine.wait(id);
        if (idleTest) {
            auto idleStart = std::chrono::steady_clock::now();
            while (engine.metrics().modelLoaded &&
                   std::chrono::steady_clock::now() - idleStart < std::chrono::seconds(10))
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            auto idle = engine.metrics();
            std::fprintf(stderr, "IDLE_CHECK loaded=%d frames=%u sessions=%u elapsed_ms=%.3f\n",
                         idle.modelLoaded, idle.frames, idle.sessions,
                         std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - idleStart)
                             .count());
        }
        for (auto id : ids) {
            auto result = engine.wait(id);
            if (!result)
                throw std::runtime_error("missing_result");
            auto snapshot = engine.metrics();
            peakFrames = std::max(peakFrames, snapshot.frames);
            peakManaged = std::max(peakManaged, snapshot.managedBytes);
            peakCanvas = std::max(peakCanvas, snapshot.canvasBytes);
            std::fprintf(stderr, "READY_US %llu %lld\n", (unsigned long long)id,
                         (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count());
            for (std::size_t i = 0; i < result->unlocatedCount(); ++i) {
                auto view = result->unlocatedFrame(i);
                std::uint64_t hash = 1469598103934665603ULL;
                for (int y = 0; y < view.height; ++y)
                    for (int x = 0; x < view.width; ++x) {
                        hash ^= view.y[std::size_t(y) * view.stride + x];
                        hash *= 1099511628211ULL;
                    }
                std::fprintf(stderr, "UNLOCATED scan=%llu source=%llu hash=%016llx\n",
                             (unsigned long long)id, (unsigned long long)view.sequence,
                             (unsigned long long)hash);
            }
            auto b = result->bounds();
            auto c = result->counts();
            const char marker[] = "PS32_RESULT\n";
            write(marker, sizeof(marker) - 1);
            std::uint64_t numbers[] = {id,
                                       std::uint64_t(result->status()),
                                       std::uint64_t(std::int64_t(b.x)),
                                       std::uint64_t(std::int64_t(b.y)),
                                       std::uint64_t(b.width),
                                       std::uint64_t(b.height),
                                       c.received,
                                       c.selected,
                                       c.filtered,
                                       c.committed,
                                       c.unlocated,
                                       c.inferenceBatches};
            write(numbers, sizeof(numbers));
            std::vector<std::uint8_t> gray(b.width), mask(b.width);
            for (int y = 0; y < b.height; ++y) {
                if (result->readRow(y, gray.data(), mask.data(), gray.size()) != Code::Ok)
                    throw std::runtime_error("row_failure");
                write(gray.data(), gray.size());
                write(mask.data(), mask.size());
            }
            std::fprintf(stderr,
                         "RESULT scan=%llu status=%d received=%llu selected=%llu committed=%llu "
                         "unlocated=%llu batches=%llu width=%d height=%d reason=%s\n",
                         (unsigned long long)id, int(result->status()),
                         (unsigned long long)c.received, (unsigned long long)c.selected,
                         (unsigned long long)c.committed, (unsigned long long)c.unlocated,
                         (unsigned long long)c.inferenceBatches, b.width, b.height,
                         result->reason().c_str());
            auto tick = std::chrono::steady_clock::now();
            engine.releaseResult(id);
            result.reset();
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                          std::chrono::steady_clock::now() - tick)
                          .count();
            std::fprintf(stderr, "RELEASE_US %lld\n", (long long)us);
        }
        std::fprintf(stderr, "PEAK frames=%u managed=%zu canvas=%zu\n", peakFrames, peakManaged,
                     peakCanvas);
        engine.shutdown();
        auto m = engine.metrics();
        std::fprintf(
            stderr,
            "STREAM32_REPLAY_OK elapsed_ms=%.3f rss=%zu hwm=%zu managed=%zu frames=%u canvas=%zu\n",
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count(),
            m.residentBytes, m.peakResidentBytes, m.managedBytes, m.frames, m.canvasBytes);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "STREAM32_REPLAY_ERROR %s\n", e.what());
        return 1;
    }
}
