#pragma once

#include "edgesearch/search.hpp"

#include <functional>
#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
struct _xmlNode;

namespace edgesearch::detail {

inline constexpr std::size_t query_byte_limit = 1024;

// Internal diagnostic hook. Stores event names/times only; never debug text,
// addresses, URLs, headers, cookies or TLS bytes. Caller owns it through fetch().
enum class TraceInput { text, tls_in, tls_out, headers_in, headers_out };
struct ConnectionTrace {
    struct Event { const char* name = nullptr; std::int64_t at_us = 0; };
    std::array<Event, 128> events{};
    std::size_t size = 0;
    std::size_t dropped = 0;
    unsigned seen = 0;
    void observe(TraceInput input, std::string_view text, std::int64_t at_us) noexcept;
};

struct Request {
    std::string url;
    std::string post_data;
    std::vector<std::string> headers;
    std::string session_key;  // Engine identity; empty disables easy-handle pooling.
    ConnectionTrace* trace = nullptr;  // Disabled for ordinary searches.
    bool follow_redirects = true;
    bool public_network_only = false;
    long timeout_ms = 0;  // Optional tighter per-request budget, including redirect hops.
    std::string variant;
};

struct Engine {
    const char* name;
    const char* provider;
    const char* endpoint;
    const char* items_xpath;
    const char* link_xpath;
    const char* title_xpath;
    const char* snippet_xpath;
};

struct HttpResponse {
    std::string body;
    std::string effective_url;
    std::string content_type;
    std::string status;
    std::string message;
    long http_status = 0;
    std::int64_t elapsed_ms = 0;
    NetworkTiming network{};
    bool requested = false;
    std::int64_t retry_after_ms = -1;
    int transport_code = -1;
    std::string location;
    bool request_sent = false; // Distinguishes reused-connection body timeouts from connect failures.
};

class SnapshotStore {
public:
    explicit SnapshotStore(const Options& options);
    ~SnapshotStore();
    SnapshotStore(SnapshotStore&&) noexcept;
    SnapshotStore& operator=(SnapshotStore&&) noexcept;
    std::string save(const std::string& engine, const std::string& query, const HttpResponse& response) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::int64_t parse_retry_after_ms(std::string_view value, std::int64_t reference_unix_seconds) noexcept;
std::optional<std::int64_t> parse_http_date_seconds(std::string_view value, std::int64_t reference_unix_seconds) noexcept;
class CooldownTable {
public:
    using Clock = std::chrono::steady_clock;
    std::int64_t remaining(const std::string& engine, Clock::time_point now);
    std::string reason(const std::string& engine) const;
    unsigned level(const std::string& engine) const;
    void observe(EngineReport& report, const Options& options, Clock::time_point now);
    void clear();
    unsigned penalty(const std::string& engine) const;
    void note_request(const std::string& engine);
    std::uint64_t last_request(const std::string& engine) const;
private:
    struct State {
        Clock::time_point until;
        std::string reason;
        Clock::time_point last_block;
        std::int64_t client_delay_ms;
        unsigned level;
    };
    std::map<std::string, State> entries_;
    std::map<std::string, unsigned> failures_;
    std::map<std::string, std::uint64_t> recent_;
    std::uint64_t sequence_ = 0;
};
using RequestFactory = std::function<Request(const Engine&, const std::string&, const Options&)>;
using CooldownClock = std::function<CooldownTable::Clock::time_point()>;

const std::vector<Engine>& registry();
const Engine& find_engine(const std::string& name);
Request make_request(const Engine& engine, const std::string& query, const Options& options);
void initialize_runtime();
using ResponseHandler = std::function<void(std::size_t, HttpResponse&&)>;
using StopPredicate = std::function<bool()>;
class HttpSession {
public:
    HttpSession();
    ~HttpSession();
    HttpSession(HttpSession&&) noexcept;
    HttpSession& operator=(HttpSession&&) noexcept;
    void fetch(const std::vector<Request>& requests, const Options& options, const ResponseHandler& handler,
               const StopPredicate& stop = {});
    void reset();
    void forget_session(const std::string& key); // Only between fetch() calls; preserves bounded pool slots.
    std::size_t retained_handles() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct SearchRouteState { CooldownTable::Clock::time_point alternate_until{}; };
using SearchFetcher = std::function<void(const std::vector<Request>&, const Options&, const ResponseHandler&, const StopPredicate&, bool)>;
Response run_search(const std::string& query, const Options& options, HttpSession& session,
                    SnapshotStore& snapshots, CooldownTable& cooldown,
                    const RequestFactory& factory = make_request,
                    const CooldownClock& cooldown_now = CooldownTable::Clock::now,
                    HttpSession* alternate_session = nullptr, SearchRouteState* route_state = nullptr,
                    const SearchFetcher& fetcher = {});
void fetch(const std::vector<Request>& requests, const Options& options, const ResponseHandler& handler,
           const StopPredicate& stop = {});
void validate_options(const Options& options);
void validate_resolver_options(const Options& options);
std::string resolver_override(const Options& options);
ParsedPage parse_response(const std::string& engine, const HttpResponse& response, std::size_t limit);
bool is_challenge(const std::string& title, const std::string& visible, const std::string& source, _xmlNode* root = nullptr);
ParsedPage parse_rss(const std::string& source, std::size_t limit);
std::string failure_stage(const HttpResponse& response);
std::vector<std::string> suggest_queries(const std::string& query);
void assess_relevance(const std::string& query, Result& result);
std::string encode(const std::string& value);
std::string decode(const std::string& value);
std::string normalize_url(const std::string& url);
std::string clean_url(const std::string& engine, const std::string& url, const std::string& base);
std::string collapse_space(const std::string& value);
std::string truncate_utf8(const std::string& value, std::size_t bytes);
std::string strings(const std::vector<std::string>& values);
std::string json_quote(std::string_view value);
std::vector<Result> aggregate(const std::vector<std::vector<Result>>& groups, std::size_t max_results);

}  // namespace edgesearch::detail
