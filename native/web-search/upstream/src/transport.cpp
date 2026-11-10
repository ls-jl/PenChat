#include "internal.hpp"

#include "curl_backend.hpp"
#include <libxml/parser.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <sys/socket.h>
#include <netinet/in.h>

namespace edgesearch::detail {
namespace {
using Clock = std::chrono::steady_clock;
using Multi = std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)>;

struct PoolEntry {
    std::string key;
    CURL* easy = nullptr;
};

void bound_cookies(CURL* easy) noexcept {
    curl_slist* list = nullptr;
    if (curl_easy_getinfo(easy, CURLINFO_COOKIELIST, &list) != CURLE_OK) {
        curl_slist_free_all(list);
        curl_easy_setopt(easy, CURLOPT_COOKIELIST, "ALL");
        return;
    }
    std::size_t count = 0;
    std::size_t bytes = 0;
    for (auto* item = list; item; item = item->next) {
        ++count;
        if (item->data) bytes += std::strlen(item->data);
    }
    curl_slist_free_all(list);
    if (count > 64 || bytes > 32768) curl_easy_setopt(easy, CURLOPT_COOKIELIST, "ALL");
}

std::int64_t elapsed(Clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

struct Transfer {
    CURLM* multi = nullptr;
    CURL* easy = nullptr;
    curl_slist* headers = nullptr;
    curl_slist* resolve = nullptr;
    bool added = false;
    bool too_large = false;
    bool allocation_failed = false;
    bool blocked_address = false;
    PoolEntry* slot = nullptr;
    std::size_t index = 0;
    std::size_t limit = 0;
    std::size_t header_bytes = 0;
    ConnectionTrace* trace = nullptr;
    std::string body;
    std::string location;
    std::array<char, CURL_ERROR_SIZE> error{};
    std::array<std::array<char, 128>, 8> retry_fields{};
    std::size_t retry_count = 0;
    bool retry_overflow = false;
    std::optional<std::int64_t> server_date;
    Clock::time_point server_date_received{};
    Clock::time_point start = Clock::now();

    Transfer(CURLM* owner, std::size_t id, std::size_t cap, PoolEntry* entry)
        : multi(owner), slot(entry), index(id), limit(cap) {
        if (slot && slot->easy) { easy = slot->easy; slot->easy = nullptr; }
        else easy = curl_easy_init();
        if (!easy) throw std::bad_alloc();
    }
    ~Transfer() {
        if (added) curl_multi_remove_handle(multi, easy);
        if (slot && !slot->easy) {
            bound_cookies(easy);
            curl_easy_reset(easy);  // Drop pointers into Transfer; retain cookies and TLS state.
            slot->easy = easy;
        } else curl_easy_cleanup(easy);
        curl_slist_free_all(headers);
        curl_slist_free_all(resolve);
    }
    Transfer(const Transfer&) = delete;
    Transfer& operator=(const Transfer&) = delete;
};

bool public_address(const sockaddr* address) noexcept {
    auto ipv4 = [](const unsigned char* p) {
        return !(p[0] == 0 || p[0] == 10 || p[0] == 127 || p[0] >= 224 ||
            (p[0] == 100 && (p[1] & 192U) == 64U) ||
            (p[0] == 169 && p[1] == 254) ||
            (p[0] == 172 && p[1] >= 16 && p[1] <= 31) ||
            (p[0] == 192 && p[1] == 168) ||
            (p[0] == 192 && p[1] == 0 && (p[2] == 0 || p[2] == 2)) ||
            (p[0] == 198 && (p[1] == 18 || p[1] == 19)) ||
            (p[0] == 198 && p[1] == 51 && p[2] == 100) ||
            (p[0] == 203 && p[1] == 0 && p[2] == 113));
    };
    if (address->sa_family == AF_INET)
        return ipv4(reinterpret_cast<const unsigned char*>(&reinterpret_cast<const sockaddr_in*>(address)->sin_addr));
    if (address->sa_family != AF_INET6) return false;
    const auto* p = reinterpret_cast<const sockaddr_in6*>(address)->sin6_addr.s6_addr;
    bool mapped = p[10] == 255 && p[11] == 255;
    for (int i = 0; i < 10; ++i) mapped &= p[i] == 0;
    if (mapped) return ipv4(p + 12);
    // Conservative global unicast; exclude documentation and transition tunnels.
    return (p[0] & 224U) == 32U && !(p[0] == 32 && p[1] == 2) &&
        !(p[0] == 32 && p[1] == 1 && p[2] == 0 && p[3] == 0) &&
        !(p[0] == 32 && p[1] == 1 && p[2] == 13 && p[3] == 184);
}

curl_socket_t public_socket(void* opaque, curlsocktype purpose, curl_sockaddr* address) noexcept {
    auto& transfer = *static_cast<Transfer*>(opaque);
    if (purpose != CURLSOCKTYPE_IPCXN || !public_address(&address->addr)) {
        transfer.blocked_address = true;
        return CURL_SOCKET_BAD;
    }
    return ::socket(address->family, address->socktype, address->protocol);
}

int debug_trace(CURL* easy, curl_infotype kind, char* data, std::size_t bytes, void* opaque) noexcept {
    auto& transfer = *static_cast<Transfer*>(opaque);
    // Internal DoH handles may also call the callback; do not mix their clocks.
    if (easy != transfer.easy || !transfer.trace) return 0;
    TraceInput input;
    switch (kind) {
    case CURLINFO_TEXT: input = TraceInput::text; break;
    case CURLINFO_SSL_DATA_IN: input = TraceInput::tls_in; break;
    case CURLINFO_SSL_DATA_OUT: input = TraceInput::tls_out; break;
    case CURLINFO_HEADER_IN: input = TraceInput::headers_in; break;
    case CURLINFO_HEADER_OUT: input = TraceInput::headers_out; break;
    default: return 0;
    }
    const auto at = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - transfer.start).count();
    transfer.trace->observe(input, kind == CURLINFO_TEXT ? std::string_view(data, bytes) : std::string_view{}, at);
    return 0;
}

std::size_t receive(char* data, std::size_t size, std::size_t count, void* opaque) noexcept {
    auto& transfer = *static_cast<Transfer*>(opaque);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return 0;
    const auto bytes = size * count;
    if (bytes > transfer.limit - transfer.body.size()) { transfer.too_large = true; return 0; }
    try { transfer.body.append(data, bytes); }
    catch (...) { transfer.allocation_failed = true; return 0; }
    return bytes;
}

std::size_t header(char* data, std::size_t size, std::size_t count, void* opaque) noexcept {
    auto& transfer = *static_cast<Transfer*>(opaque);
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return 0;
    const auto bytes = size * count;
    if (bytes > 65536 - transfer.header_bytes) { transfer.too_large = true; return 0; }
    transfer.header_bytes += bytes;
    if (bytes >= 5 && std::equal(data, data + 5, "HTTP/")) {
        transfer.body.clear(); transfer.location.clear(); transfer.retry_count = 0; transfer.retry_overflow = false; transfer.server_date.reset();
    } else {
        std::string_view line(data, bytes);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            const auto key = line.substr(0, colon);
            auto equal = [&](std::string_view expected) {
                if (key.size() != expected.size()) return false;
                for (std::size_t i = 0; i < key.size(); ++i) {
                    const auto lower = key[i] >= 'A' && key[i] <= 'Z' ? static_cast<char>(key[i] + ('a' - 'A')) : key[i];
                    if (lower != expected[i]) return false;
                }
                return true;
            };
            auto value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
            if (equal("location") && value.size() <= 8192 && value.find('\0') == std::string_view::npos) {
                try { transfer.location.assign(value); }
                catch (...) { transfer.allocation_failed = true; return 0; }
            }
            if (value.size() < 128 && value.find('\0') == std::string_view::npos) {
                if (equal("retry-after")) {
                    if (transfer.retry_count < transfer.retry_fields.size()) {
                        auto& target = transfer.retry_fields[transfer.retry_count++]; target.fill(0);
                        std::copy(value.begin(), value.end(), target.begin());
                    } else transfer.retry_overflow = true;
                } else if (equal("date")) {
                    const auto parsed = parse_http_date_seconds(value, static_cast<std::int64_t>(std::time(nullptr)));
                    if (parsed) { transfer.server_date = *parsed; transfer.server_date_received = Clock::now(); }
                }
            }
        }
    }
    return bytes;
}

HttpResponse finish(Transfer& transfer, CURLcode code) {
    HttpResponse response;
    response.requested = true;
    response.transport_code = static_cast<int>(code);
    response.location = std::move(transfer.location);
    const auto reference = transfer.server_date
        ? *transfer.server_date + std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - transfer.server_date_received).count()
        : static_cast<std::int64_t>(std::time(nullptr));
    for (std::size_t i = 0; i < transfer.retry_count; ++i) {
        response.retry_after_ms = std::max(response.retry_after_ms, parse_retry_after_ms(transfer.retry_fields[i].data(), reference));
    }
    if (transfer.retry_overflow) response.retry_after_ms = 86400000;
    response.elapsed_ms = elapsed(transfer.start);
    response.body = std::move(transfer.body);
    long request_bytes = 0;
    curl_easy_getinfo(transfer.easy, CURLINFO_REQUEST_SIZE, &request_bytes);
    response.request_sent = request_bytes > 0;
    auto stamp = [&](CURLINFO key) {
        curl_off_t value = 0;
        if (curl_easy_getinfo(transfer.easy, key, &value) != CURLE_OK) return std::int64_t{0};
        return static_cast<std::int64_t>(std::max(value, curl_off_t{0}));
    };
    response.network.dns_done_us = stamp(CURLINFO_NAMELOOKUP_TIME_T);
    response.network.connect_done_us = stamp(CURLINFO_CONNECT_TIME_T);
    response.network.tls_done_us = stamp(CURLINFO_APPCONNECT_TIME_T);
    response.network.first_byte_us = stamp(CURLINFO_STARTTRANSFER_TIME_T);
    response.network.total_us = stamp(CURLINFO_TOTAL_TIME_T);
    response.network.redirect_us = stamp(CURLINFO_REDIRECT_TIME_T);
    curl_easy_getinfo(transfer.easy, CURLINFO_NUM_CONNECTS, &response.network.new_connections);
    curl_easy_getinfo(transfer.easy, CURLINFO_REDIRECT_COUNT, &response.network.redirects);
    response.network.body_bytes = response.body.size();
    curl_easy_getinfo(transfer.easy, CURLINFO_RESPONSE_CODE, &response.http_status);
    char* url = nullptr;
    char* type = nullptr;
    curl_easy_getinfo(transfer.easy, CURLINFO_EFFECTIVE_URL, &url);
    curl_easy_getinfo(transfer.easy, CURLINFO_CONTENT_TYPE, &type);
    if (url) response.effective_url = url;
    if (type) response.content_type = type;
    if (transfer.too_large || code == CURLE_FILESIZE_EXCEEDED) {
        response.status = "response_too_large";
        response.message = "response exceeded configured body or 64 KiB header limit";
    } else if (transfer.allocation_failed) {
        response.status = "memory_error";
        response.message = "unable to allocate response buffer";
    } else if (code == CURLE_OPERATION_TIMEDOUT) {
        response.status = "timeout";
        response.message = transfer.error[0] ? transfer.error.data() : "shared network deadline or connect timeout exceeded";
    } else if (code == CURLE_COULDNT_CONNECT && transfer.blocked_address) {
        response.status = "blocked_address";
        response.message = "destination resolved to a non-public address";
    } else if (code != CURLE_OK) {
        response.status = "network_error";
        response.message = transfer.error[0] ? transfer.error.data() : curl_easy_strerror(code);
    } else if (response.http_status == 429) {
        response.status = "rate_limited";
        response.message = "upstream HTTP 429; no automatic retry";
    } else if (response.http_status < 200 || response.http_status >= 300) {
        response.status = "http_error";
        response.message = "upstream HTTP " + std::to_string(response.http_status);
    } else response.status = "ok";
    return response;
}

void configure(Transfer& transfer, const Request& request, const Options& options, long remaining) {
    if (request.timeout_ms > 0) remaining = std::min(remaining, request.timeout_ms);
    auto set = [&](CURLoption key, auto value) {
        const auto result = curl_easy_setopt(transfer.easy, key, value);
        if (result != CURLE_OK) throw std::runtime_error(std::string("libcurl option: ") + curl_easy_strerror(result));
    };
    set(CURLOPT_URL, request.url.c_str());
    set(CURLOPT_TIMEOUT_MS, remaining);
    set(CURLOPT_CONNECTTIMEOUT_MS, std::min(options.connect_timeout_ms, remaining));
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_PROXY, options.proxy.c_str());
    set(CURLOPT_NOPROXY, "");
    set(CURLOPT_IPRESOLVE, static_cast<long>(options.ip_family == "v4" ? CURL_IPRESOLVE_V4 :
        (options.ip_family == "v6" ? CURL_IPRESOLVE_V6 : CURL_IPRESOLVE_WHATEVER)));
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    if (!options.ca_bundle.empty()) set(CURLOPT_CAINFO, options.ca_bundle.c_str());
#if EDGE_SEARCH_USE_IMPERSONATE
    const auto profile = options.browser.empty() ? "chrome145" : options.browser.c_str();
    if (curl_easy_impersonate(transfer.easy, profile, 1) != CURLE_OK) throw std::invalid_argument("unsupported browser profile: " + std::string(profile));
#else
    set(CURLOPT_USERAGENT, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36");
#endif
    // Header profiles alone do not activate libcurl's body decoder.
    set(CURLOPT_ACCEPT_ENCODING, "");
    set(CURLOPT_COOKIEFILE, "");  // Memory-only cookies for this engine and its redirects.
    set(CURLOPT_DNS_CACHE_TIMEOUT, 60L);
    set(CURLOPT_MAXAGE_CONN, options.connection_idle_seconds);
    // Reassert certificate verification after all browser profile settings.
    set(CURLOPT_SSL_VERIFYPEER, 1L);
    set(CURLOPT_SSL_VERIFYHOST, 2L);
    set(CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
    if (request.public_network_only) {
        set(CURLOPT_OPENSOCKETFUNCTION, &public_socket);
        set(CURLOPT_OPENSOCKETDATA, &transfer);
    }
#if LIBCURL_VERSION_NUM >= 0x074c00
    if (!options.doh_url.empty()) {
        set(CURLOPT_DOH_URL, options.doh_url.c_str());
        set(CURLOPT_DOH_SSL_VERIFYPEER, 1L);
        set(CURLOPT_DOH_SSL_VERIFYHOST, 2L);
        const auto entry = resolver_override(options);
        if (!entry.empty()) {
            transfer.resolve = curl_slist_append(nullptr, entry.c_str());
            if (!transfer.resolve) throw std::bad_alloc();
            set(CURLOPT_RESOLVE, transfer.resolve);
        }
    }
#endif
    set(CURLOPT_MAXREDIRS, 3L);
#if LIBCURL_VERSION_NUM >= 0x075500
    set(CURLOPT_PROTOCOLS_STR, "http,https");
    set(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    set(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
    set(CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    set(CURLOPT_WRITEFUNCTION, &receive);
    set(CURLOPT_WRITEDATA, &transfer);
    set(CURLOPT_HEADERFUNCTION, &header);
    set(CURLOPT_HEADERDATA, &transfer);
    set(CURLOPT_ERRORBUFFER, transfer.error.data());
    for (const auto& value : request.headers) {
        auto* updated = curl_slist_append(transfer.headers, value.c_str());
        if (!updated) throw std::bad_alloc();
        transfer.headers = updated;
    }
    if (transfer.headers) set(CURLOPT_HTTPHEADER, transfer.headers);
    if (!request.post_data.empty()) {
        set(CURLOPT_POST, 1L);
        set(CURLOPT_POSTFIELDS, request.post_data.c_str());
        set(CURLOPT_POSTFIELDSIZE, static_cast<long>(request.post_data.size()));
    }
    if (request.trace) {
        transfer.trace = request.trace;
        *transfer.trace = ConnectionTrace{};
        set(CURLOPT_DEBUGFUNCTION, &debug_trace);
        set(CURLOPT_DEBUGDATA, &transfer);
        set(CURLOPT_VERBOSE, 1L);
    }
}
}  // namespace

struct HttpSession::Impl {
    Multi multi{nullptr, curl_multi_cleanup};
    std::vector<PoolEntry> pool;
    std::string proxy, ca_bundle, browser, ip_family, doh_url;
    std::vector<std::string> doh_bootstrap;
    explicit Impl(const Options& options)
        : multi(curl_multi_init(), curl_multi_cleanup), proxy(options.proxy), ca_bundle(options.ca_bundle),
          browser(options.browser), ip_family(options.ip_family), doh_url(options.doh_url), doh_bootstrap(options.doh_bootstrap) {
        if (!multi) throw std::bad_alloc();
        pool.reserve(8);
    }
    ~Impl() { for (auto& item : pool) curl_easy_cleanup(item.easy); }
    PoolEntry* entry(const std::string& key) {
        if (key.empty()) return nullptr;
        if (key.size() > 64) throw std::invalid_argument("session key too long");
        for (auto& item : pool) if (item.key == key) return &item;
        if (pool.size() >= 8) return nullptr;
        pool.push_back({key, nullptr});
        return &pool.back();
    }
    bool matches(const Options& options) const {
        return proxy == options.proxy && ca_bundle == options.ca_bundle &&
               browser == options.browser && ip_family == options.ip_family &&
               doh_url == options.doh_url && doh_bootstrap == options.doh_bootstrap;
    }
};

HttpSession::HttpSession() { initialize_runtime(); }
HttpSession::~HttpSession() = default;
HttpSession::HttpSession(HttpSession&&) noexcept = default;
HttpSession& HttpSession::operator=(HttpSession&&) noexcept = default;
void HttpSession::reset() { impl_.reset(); }
void HttpSession::forget_session(const std::string& key) {
    if (!impl_) return;
    for (auto& entry : impl_->pool) if (entry.key == key) {
        if (entry.easy) curl_easy_cleanup(entry.easy);
        entry.easy = nullptr; return;
    }
}
std::size_t HttpSession::retained_handles() const noexcept {
    std::size_t count = 0;
    if (impl_) for (const auto& item : impl_->pool) if (item.easy) ++count;
    return count;
}

void initialize_runtime() {
    struct Runtime {
        Runtime() {
#if EDGE_SEARCH_USE_IMPERSONATE
            if (edgesearch_curl_namespace_abi() != 1) throw std::runtime_error("unsupported private curl namespace ABI");
#endif
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("libcurl initialization failed");
            xmlInitParser();
        }
        ~Runtime() { curl_global_cleanup(); }
    };
    static const Runtime runtime;
    (void)runtime;
}

void validate_options(const Options& options) {
    validate_resolver_options(options);
    if (options.engines.empty() || options.engines.size() > registry().size()) throw std::invalid_argument("select 1 to 7 unique engines");
    std::vector<std::string> seen;
    for (const auto& name : options.engines) {
        (void)find_engine(name);
        if (std::find(seen.begin(), seen.end(), name) != seen.end()) throw std::invalid_argument("duplicate engine: " + name);
        seen.push_back(name);
    }
    if (options.engine_timeout_ms != 0 && (options.engine_timeout_ms < 100 || options.engine_timeout_ms > 60000)) throw std::invalid_argument("engine-timeout-ms must be 0 or 100..60000");
    if (options.bing_format != "auto" && options.bing_format != "rss" && options.bing_format != "html") throw std::invalid_argument("bing-format must be auto, rss or html");
    if (options.concurrency < 1 || options.concurrency > 4) throw std::invalid_argument("concurrency must be 1..4");
    if (options.connection_cache < 1 || options.connection_cache > 16) throw std::invalid_argument("connection-cache must be 1..16");
    if (options.connection_idle_seconds < 1 || options.connection_idle_seconds > 300) throw std::invalid_argument("connection-idle-seconds must be 1..300");
    if (options.snapshot_directory.size() > 4096 || options.snapshot_directory.find('\0') != std::string::npos) throw std::invalid_argument("invalid snapshot directory");
    if (options.snapshot_body_bytes < 1024 || options.snapshot_body_bytes > 8 * 1024 * 1024) throw std::invalid_argument("snapshot-body limit must be 1 KiB..8 MiB");
    if (options.rate_limit_cooldown_ms < 0 || options.rate_limit_cooldown_ms > 86400000 ||
        options.challenge_cooldown_ms < 0 || options.challenge_cooldown_ms > 86400000 ||
        options.cooldown_max_ms < 0 || options.cooldown_max_ms > 86400000) throw std::invalid_argument("cooldowns must be 0..86400000 ms");
    if (options.max_results < 1 || options.max_results > 100 || options.per_engine < 1 || options.per_engine > 50) throw std::invalid_argument("max-results must be 1..100; per-engine must be 1..50");
    if (options.timeout_ms < 100 || options.timeout_ms > 60000 || options.connect_timeout_ms < 100 || options.connect_timeout_ms > 60000) throw std::invalid_argument("timeouts must be 100..60000 ms");
    if (options.max_body_bytes < 64 * 1024 || options.max_body_bytes > 8 * 1024 * 1024) throw std::invalid_argument("body limit must be 64 KiB..8 MiB");
    if (options.region != "cn-zh" && options.region != "us-en") throw std::invalid_argument("region must be cn-zh or us-en");
    if (options.ip_family != "auto" && options.ip_family != "v4" && options.ip_family != "v6") throw std::invalid_argument("ip-family must be auto, v4, or v6");
    if (options.early_grace_ms < 0 || options.early_grace_ms > 5000) throw std::invalid_argument("early grace must be 0..5000 ms");
    if (options.early_results > 100) throw std::invalid_argument("early-results must be 0..100");
    if (options.browser.size() > 64 || options.browser.find_first_of("\r\n\0", 0, 3) != std::string::npos) throw std::invalid_argument("invalid browser profile");
#if !EDGE_SEARCH_USE_IMPERSONATE
    if (!options.browser.empty()) throw std::invalid_argument("browser profiles require EDGE_SEARCH_USE_IMPERSONATE=ON");
#endif
    if (options.proxy.size() > 2048 || options.ca_bundle.size() > 4096 || options.proxy.find('\0') != std::string::npos || options.ca_bundle.find('\0') != std::string::npos) throw std::invalid_argument("invalid proxy or CA path");
}

void HttpSession::fetch(const std::vector<Request>& requests, const Options& options, const ResponseHandler& handler, const StopPredicate& stop) {
    initialize_runtime();
    validate_options(options);
    if (!(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_ASYNCHDNS)) throw std::runtime_error("libcurl must have an asynchronous DNS resolver for bounded network waits");
    if (!impl_ || !impl_->matches(options)) impl_ = std::make_unique<Impl>(options);
    auto& multi = impl_->multi;
    if (curl_multi_setopt(multi.get(), CURLMOPT_MAXCONNECTS, static_cast<long>(options.connection_cache)) != CURLM_OK) throw std::runtime_error("unable to bound connection cache");
    std::unordered_map<CURL*, std::unique_ptr<Transfer>> active;
    const auto start = Clock::now();
    const auto deadline = start + std::chrono::milliseconds(options.timeout_ms);
    std::size_t next = 0;
    auto remaining = [&]() -> long {
        return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
    };
    while (next < requests.size() || !active.empty()) {
        while (next < requests.size() && active.size() < options.concurrency && remaining() > 0) {
            auto transfer = std::make_unique<Transfer>(multi.get(), next, options.max_body_bytes, impl_->entry(requests[next].session_key));
            configure(*transfer, requests[next], options, std::max(remaining(), 1L));
            if (curl_multi_add_handle(multi.get(), transfer->easy) != CURLM_OK) throw std::runtime_error("unable to add libcurl transfer");
            transfer->added = true;
            CURL* key = transfer->easy;
            active.emplace(key, std::move(transfer));
            ++next;
        }
        int running = 0;
        const auto performed = curl_multi_perform(multi.get(), &running);
        if (performed != CURLM_OK) throw std::runtime_error(curl_multi_strerror(performed));
        int queued = 0;
        while (auto* message = curl_multi_info_read(multi.get(), &queued)) {
            if (message->msg != CURLMSG_DONE) continue;
            const auto found = active.find(message->easy_handle);
            if (found == active.end()) continue;
            const auto index = found->second->index;
            auto response = finish(*found->second, message->data.result);
            active.erase(found);  // Detach and clean up before parsing.
            handler(index, std::move(response));
        }
        const bool enough = stop && stop();
        if (enough || remaining() <= 0) {
            for (auto& entry : active) {
                auto response = finish(*entry.second, CURLE_OPERATION_TIMEDOUT);
                response.transport_code = -1;  // Application stopped it before CURLMSG_DONE.
                if (enough) { response.status = "cancelled"; response.message = "requested result budget reached"; }
                handler(entry.second->index, std::move(response));
            }
            active.clear();  // Cancel outstanding transfers; no thread-pool join.
            while (next < requests.size()) {
                HttpResponse response;
                response.status = enough ? "cancelled" : "timeout";
                response.message = enough ? "requested result budget reached before this engine was started" : "shared network deadline expired before this engine was started";
                handler(next++, std::move(response));
            }
            break;
        }
        if (active.empty() || (next < requests.size() && active.size() < options.concurrency)) continue;
        const auto polled = curl_multi_poll(multi.get(), nullptr, 0,
            static_cast<int>(std::min(remaining(), 100L)), nullptr);
        if (polled != CURLM_OK) throw std::runtime_error(curl_multi_strerror(polled));
    }
}

void fetch(const std::vector<Request>& requests, const Options& options, const ResponseHandler& handler, const StopPredicate& stop) {
    HttpSession session;
    session.fetch(requests, options, handler, stop);
}

}  // namespace edgesearch::detail

namespace edgesearch {
std::string transport_json() {
    detail::initialize_runtime();
    const auto* info = curl_version_info(CURLVERSION_NOW);
    std::string result = "{\"backend\":";
#if EDGE_SEARCH_USE_IMPERSONATE
    result += "\"curl-impersonate\",\"default_browser\":\"chrome145\",";
#else
    result += "\"system-libcurl\",\"default_browser\":null,";
#endif
    return result + "\"curl_version\":" + detail::json_quote(info->version) + ",\"tls\":" +
        detail::json_quote(info->ssl_version ? info->ssl_version : "") + ",\"async_dns\":" +
        ((info->features & CURL_VERSION_ASYNCHDNS) ? "true" : "false") + '}';
}
}  // namespace edgesearch
