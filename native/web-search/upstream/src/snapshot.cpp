#include "internal.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

namespace edgesearch::detail {
namespace {

struct File {
    int fd = -1;
    explicit File(int value) : fd(value) {}
    ~File() { if (fd >= 0) ::close(fd); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

[[noreturn]] void fail(const char* action) {
    throw std::system_error(errno, std::generic_category(), action);
}

struct Temporary {
    int directory;
    std::string name;
    bool owned = false;
    Temporary(int folder, std::string filename, const char* data, std::size_t size)
        : directory(folder), name(std::move(filename)) {
        File file(::openat(directory, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (file.fd < 0) fail("create snapshot temporary file");
        owned = true;
        try {
            std::size_t offset = 0;
            while (offset < size) {
                const auto written = ::write(file.fd, data + offset, size - offset);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) fail("write snapshot");
                offset += static_cast<std::size_t>(written);
            }
        } catch (...) {
            ::unlinkat(directory, name.c_str(), 0);
            owned = false;
            throw;
        }
    }
    ~Temporary() { if (owned) ::unlinkat(directory, name.c_str(), 0); }
    void commit(const std::string& destination) {
        if (::renameat(directory, name.c_str(), directory, destination.c_str()) != 0) fail("commit snapshot");
        owned = false;
    }
    Temporary(const Temporary&) = delete;
    Temporary& operator=(const Temporary&) = delete;
};

std::string digest(const char* data, std::size_t bytes) {
    std::uint64_t value = UINT64_C(14695981039346656037);
    for (std::size_t i = 0; i < bytes; ++i) {
        value ^= static_cast<unsigned char>(data[i]);
        value *= UINT64_C(1099511628211);
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}
}  // namespace

struct SnapshotStore::Impl {
    File directory;
    std::size_t limit;
    explicit Impl(const Options& options)
        : directory(-1), limit(options.snapshot_body_bytes) {
        if (::mkdir(options.snapshot_directory.c_str(), 0700) != 0 && errno != EEXIST) fail("create snapshot directory");
        directory.fd = ::open(options.snapshot_directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (directory.fd < 0) fail("open snapshot directory");
    }
};

SnapshotStore::SnapshotStore(const Options& options) {
    if (!options.snapshot_directory.empty()) impl_ = std::make_unique<Impl>(options);
}
SnapshotStore::~SnapshotStore() = default;
SnapshotStore::SnapshotStore(SnapshotStore&&) noexcept = default;
SnapshotStore& SnapshotStore::operator=(SnapshotStore&&) noexcept = default;

std::string SnapshotStore::save(const std::string& engine, const std::string& query, const HttpResponse& response) const {
    if (!impl_ || !response.requested) return {};
    try {
        (void)find_engine(engine);  // Filenames come only from the fixed registry.
        const auto bytes = std::min(response.body.size(), impl_->limit);
        const bool complete = response.status == "ok" || response.status == "http_error" || response.status == "rate_limited";
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const std::string metadata = "{\"snapshot_format\":1,\"engine\":" + json_quote(engine) +
            ",\"query\":" + json_quote(truncate_utf8(query, 1024)) +
            ",\"captured_at_unix_ms\":" + std::to_string(stamp) +
            ",\"transport_status\":" + json_quote(response.status) +
            ",\"http_status\":" + std::to_string(response.http_status) +
            ",\"retry_after_ms\":" + std::to_string(response.retry_after_ms) +
            ",\"content_type\":" + json_quote(truncate_utf8(response.content_type, 256)) +
            ",\"effective_url\":" + json_quote(truncate_utf8(response.effective_url, 8192)) +
            ",\"effective_url_truncated\":" + (response.effective_url.size() > 8192 ? "true" : "false") +
            ",\"received_body_bytes\":" + std::to_string(response.body.size()) +
            ",\"stored_bytes\":" + std::to_string(bytes) +
            ",\"complete_response\":" + (complete ? "true" : "false") +
            ",\"truncated\":" + (bytes < response.body.size() ? "true" : "false") +
            ",\"body_fnv1a64\":" + json_quote(digest(response.body.data(), bytes)) + "}\n";
        Temporary body(impl_->directory.fd, "." + engine + ".html.tmp", response.body.data(), bytes);
        Temporary info(impl_->directory.fd, "." + engine + ".json.tmp", metadata.data(), metadata.size());
        body.commit(engine + ".html");
        info.commit(engine + ".json");
        // Readers verify the digest because a crash can occur between the two renames.
        return {};
    } catch (const std::exception& error) { return error.what(); }
}

}  // namespace edgesearch::detail
