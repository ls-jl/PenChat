#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace edgesearch {

struct EngineInfo {
    std::string name;
    std::string provider;
};

struct Options {
    std::vector<std::string> engines = {"bing_cn", "so360", "sogou", "baidu"};
    std::size_t max_results = 10;
    std::size_t per_engine = 10;
    std::size_t concurrency = 2;
    std::size_t max_body_bytes = 2 * 1024 * 1024;
    long timeout_ms = 15000;  // One shared network deadline, including queued engines.
    long connect_timeout_ms = 8000;
    long engine_timeout_ms = 0;  // Opt-in tighter slice; default preserves the shared budget.
    std::string bing_format = "html";  // auto/rss are explicit experimental RSS opt-ins.
    std::string fallback_doh_url;  // Explicit opt-in; never chooses a resolver silently.
    std::vector<std::string> fallback_doh_bootstrap;
    bool fallback_system_dns = false;
    std::string region = "cn-zh";
    std::string proxy;  // Empty means direct; environment proxies are ignored.
    std::string ca_bundle;
    std::string browser;  // Empty uses the native impersonation build's default.
    std::size_t early_results = 3;  // Optional return after enough filtered results.
    bool full = false;  // All fetched candidates; disables count-based early return.
    long early_grace_ms = 750;
    std::string ip_family = "auto";  // auto, v4, v6; applies to local resolution.
    std::size_t connection_cache = 4;  // Retained idle connections in a Searcher.
    std::string snapshot_directory;  // Optional latest-per-engine diagnostic captures.
    std::size_t snapshot_body_bytes = 2 * 1024 * 1024;
    bool enable_cooldown = true;
    long rate_limit_cooldown_ms = 60000;
    long challenge_cooldown_ms = 15000;
    bool progressive_cooldown = true;
    long cooldown_max_ms = 300000;  // Client backoff ceiling; explicit base/server delay can exceed it.
    std::string doh_url;  // Optional HTTPS DNS resolver; empty uses system resolution.
    std::vector<std::string> doh_bootstrap;  // Up to four numeric IPs for this resolver only.
    long connection_idle_seconds = 30;  // Maximum idle age eligible for reuse, 1..300 s.
};

// Cumulative libcurl timestamps, in microseconds. Zero can mean unfinished or reused.
// Redirects and proxies affect these timestamps; they are not independent durations.
struct NetworkTiming {
    std::int64_t dns_done_us = 0;
    std::int64_t connect_done_us = 0;
    std::int64_t tls_done_us = 0;
    std::int64_t first_byte_us = 0;
    std::int64_t total_us = 0;
    std::int64_t redirect_us = 0;
    long new_connections = 0;
    long redirects = 0;
    std::size_t body_bytes = 0;
};

struct Result {
    std::string title;
    std::string url;
    std::string content;
    std::vector<std::string> engines;
    std::vector<std::string> providers;
    double score = 0.0;
    std::string relevance = "uncertain";
    std::string relevance_reason;
    double query_coverage = 0.0;
};

struct EngineAttempt {
    std::string variant, resolver, status, failure_stage;
    long http_status = 0;
    int transport_code = -1;
    std::int64_t elapsed_ms = 0;
    bool requested = false;
    std::size_t result_count = 0;
};
struct EngineReport {
    std::string name;
    std::string provider;
    std::string status;  // ok, empty, challenge, parse_error, rate_limited, timeout, ...
    long http_status = 0;
    std::int64_t elapsed_ms = 0;
    std::size_t result_count = 0;
    std::string message;
    std::size_t candidates_seen = 0;
    std::size_t ads_filtered = 0;
    NetworkTiming network{};
    std::string content_quality;
    std::string failure_stage;
    std::vector<EngineAttempt> attempts;
    std::size_t rejected_count = 0;
    bool requested = false;
    std::string snapshot_error{};
    std::int64_t retry_after_ms = -1;  // -1 means missing or invalid header.
    std::int64_t cooldown_remaining_ms = 0;
    std::int64_t cooldown_applied_ms = 0;  // New decision after this actual response.
    unsigned cooldown_level = 0;
    int transport_code = -1;  // libcurl completion CURLcode; -1 if no completion/offline parse.
};

struct Response {
    std::string query;
    std::string status;
    bool partial = false;
    std::int64_t elapsed_ms = 0;
    std::vector<Result> results;
    std::vector<EngineReport> engines;
    std::vector<Result> rejected_results;
    std::vector<std::string> suggested_queries;
    std::size_t attempt_count = 0;
    bool resolver_fallback_used = false;
    bool fetch_complete = false;
    bool return_complete = false;
    std::size_t total_results = 0;
    std::size_t usable_results = 0;  // Supported evidence across all retained candidates.
    std::vector<std::string> incomplete_reasons;
    bool completed_early = false;
    std::int64_t first_result_ms = -1;
};

struct ParsedPage {
    std::string status;
    std::string message;
    std::vector<Result> results;
    std::size_t candidates_seen = 0;
    std::size_t ads_filtered = 0;
};

std::vector<EngineInfo> available_engines();
std::vector<std::string> profile_engines(const std::string& profile);
Response search(const std::string& query, const Options& options = {});
// Owns bounded network/cookie state between queries. One caller at a time per object.
class Searcher {
public:
    explicit Searcher(Options options = {});
    ~Searcher();
    Searcher(Searcher&&) noexcept;
    Searcher& operator=(Searcher&&) noexcept;
    Searcher(const Searcher&) = delete;
    Searcher& operator=(const Searcher&) = delete;
    Response search(const std::string& query);
    Response search(const std::string& query, bool full, long timeout_ms, std::size_t per_engine);
    void clear_session();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Offline diagnostics and fixture tests; no network access from this function.
ParsedPage parse_html(const std::string& engine, const std::string& html,
                      const std::string& effective_url = "", std::size_t limit = 10);
std::string to_json(const Response& response);
std::string engines_json();
std::string transport_json();

}  // namespace edgesearch
