#include "internal.hpp"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

long number(const std::string& text) {
    long value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < 1) throw std::invalid_argument("expected a positive integer: " + text);
    return value;
}

std::vector<std::string> split(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto end = value.find(',', start);
        const auto name = value.substr(start, end == std::string::npos ? end : end - start);
        if (name.empty()) throw std::invalid_argument("empty engine name");
        result.push_back(name);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

std::string error_json(const std::string& message) {
    return "{\"schema_version\":1,\"status\":\"error\",\"message\":" + edgesearch::detail::json_quote(message) + '}';
}

void help() {
    std::cout << "edge-search " << EDGE_SEARCH_VERSION << R"( - native search for embedded chatbots
Usage: edge-search [options] "query"
       edge-search [options] --stdin
       edge-search --parse ENGINE FILE [options]

  --profile domestic|global|all   Domestic defaults to bing_cn,so360,sogou,baidu
  --engines NAME,NAME             Explicit backend set; at most 4 attempted per query
  --full                         Return all fetched candidates; disable early return
  --max-results N                 Merged result cap, 1..100 (10)
  --engine-timeout-ms N           Per-engine slice (0: shared total budget)
  --bing-format auto|rss|html      Default html; auto/rss opt in to RSS
  --fallback-doh-url HTTPS_URL     Explicit DNS/connect failure recovery route
  --fallback-doh-bootstrap IP      Bootstrap for fallback resolver, repeat up to 4
  --fallback-system-dns            Use system resolver if primary DoH fails
  --per-engine N                  Per-engine cap, 1..50 (10)
  --concurrency N                 Requested transfers, 1..4 (search capped at 2)
  --connection-cache N            Retained idle connections, 1..16 (4)
  --connection-idle-seconds N     Maximum idle age eligible for reuse, 1..300 (30)
  --timeout-ms N                  Shared network deadline, 100..60000 (15000)
  --connect-timeout-ms N          Connect timeout, 100..60000 (8000)
  --max-body-kib N                Decoded response cap, 64..8192 (2048)
  --region cn-zh|us-en            Search locale (cn-zh)
  --proxy URL                    Explicit HTTP/SOCKS proxy; default is direct
  --ca-bundle FILE                Custom CA bundle; TLS verification stays on
  --browser PROFILE              Native impersonation profile (patched build only)
  --ip-family auto|v4|v6          IP address family for local resolution (auto)
  --doh-url HTTPS_URL            Optional direct-mode HTTPS DNS resolver
  --doh-bootstrap IP             Numeric resolver bootstrap address; repeat up to 4
  --early-results N              Supported result threshold (3); 0 disables, grace 750 ms
  --snapshots DIR                Keep bounded latest-per-engine response captures
  --snapshot-body-kib N          Snapshot body cap, 1..8192 (2048)
  --rate-cooldown-ms N           Default engine cooldown after 429 (60000)
  --challenge-cooldown-ms N      Default cooldown after verification (15000)
  --no-cooldown                  Disable cooldown for controlled comparisons
  --fixed-cooldown               Disable progressive backoff after repeated blocks
  --cooldown-max-ms N            Progressive client-delay ceiling (300000)
  --transport-info               Print backend and TLS build information as JSON
  --stdin                        One UTF-8 query per line, one JSON response per line
  --parse ENGINE FILE            Offline HTML diagnostics; no HTTP request
  --list-engines                 Print engine registry as JSON
  --version                      Print version
  --help                         Show this message

Exit: 0 = results (including partial); 2 = usage/runtime error; 3 = no results.
Stdin mode keeps serving after query errors; no Python or browser is needed.
Ordinary libcurl does not implement browser TLS fingerprint impersonation.
)";
}

int run(int argc, char** argv) {
    edgesearch::Options options;
    std::string query;
    std::string parse_engine;
    std::string parse_file;
    bool stdin_mode = false;
    bool positional = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + arg);
            return argv[++i];
        };
        if (!positional && arg == "--") { positional = true; continue; }
        if (!positional && (arg == "--help" || arg == "-h")) { help(); return 0; }
        if (!positional && arg == "--version") { std::cout << "edge-search " << EDGE_SEARCH_VERSION << '\n'; return 0; }
        if (!positional && arg == "--transport-info") { std::cout << edgesearch::transport_json() << '\n'; return 0; }
        if (!positional && arg == "--list-engines") { std::cout << edgesearch::engines_json() << '\n'; return 0; }
        if (!positional && arg == "--profile") options.engines = edgesearch::profile_engines(value());
        else if (!positional && arg == "--engines") options.engines = split(value());
        else if (!positional && arg == "--max-results") options.max_results = static_cast<std::size_t>(number(value()));
        else if (!positional && arg == "--per-engine") options.per_engine = static_cast<std::size_t>(number(value()));
        else if (!positional && arg == "--concurrency") options.concurrency = static_cast<std::size_t>(number(value()));
        else if (!positional && arg == "--connection-cache") options.connection_cache = static_cast<std::size_t>(number(value()));
        else if (!positional && arg == "--connection-idle-seconds") options.connection_idle_seconds = number(value());
        else if (!positional && arg == "--timeout-ms") options.timeout_ms = number(value());
        else if (!positional && arg == "--engine-timeout-ms") { const auto v = value(); options.engine_timeout_ms = v == "0" ? 0 : number(v); }
        else if (!positional && arg == "--bing-format") options.bing_format = value();
        else if (!positional && arg == "--fallback-doh-url") options.fallback_doh_url = value();
        else if (!positional && arg == "--fallback-doh-bootstrap") options.fallback_doh_bootstrap.push_back(value());
        else if (!positional && arg == "--fallback-system-dns") options.fallback_system_dns = true;
        else if (!positional && arg == "--connect-timeout-ms") options.connect_timeout_ms = number(value());
        else if (!positional && arg == "--max-body-kib") {
            const auto kib = number(value());
            if (kib > 8192) throw std::invalid_argument("max-body-kib must be 64..8192");
            options.max_body_bytes = static_cast<std::size_t>(kib) * 1024;
        } else if (!positional && arg == "--region") options.region = value();
        else if (!positional && arg == "--proxy") options.proxy = value();
        else if (!positional && arg == "--ca-bundle") options.ca_bundle = value();
        else if (!positional && arg == "--browser") options.browser = value();
        else if (!positional && arg == "--ip-family") options.ip_family = value();
        else if (!positional && arg == "--doh-url") options.doh_url = value();
        else if (!positional && arg == "--doh-bootstrap") options.doh_bootstrap.push_back(value());
        else if (!positional && arg == "--early-results") { const auto v = value(); options.early_results = v == "0" ? 0 : static_cast<std::size_t>(number(v)); }
        else if (!positional && arg == "--snapshots") options.snapshot_directory = value();
        else if (!positional && arg == "--rate-cooldown-ms") options.rate_limit_cooldown_ms = number(value());
        else if (!positional && arg == "--challenge-cooldown-ms") options.challenge_cooldown_ms = number(value());
        else if (!positional && arg == "--no-cooldown") options.enable_cooldown = false;
        else if (!positional && arg == "--fixed-cooldown") options.progressive_cooldown = false;
        else if (!positional && arg == "--cooldown-max-ms") options.cooldown_max_ms = number(value());
        else if (!positional && arg == "--snapshot-body-kib") {
            const auto kib = number(value());
            if (kib > 8192) throw std::invalid_argument("snapshot-body-kib must be 1..8192");
            options.snapshot_body_bytes = static_cast<std::size_t>(kib) * 1024;
        }
        else if (!positional && arg == "--full") options.full = true;
        else if (!positional && arg == "--stdin") stdin_mode = true;
        else if (!positional && arg == "--parse") {
            parse_engine = value();
            parse_file = value();
        } else if (!positional && !arg.empty() && arg.front() == '-') throw std::invalid_argument("unknown option: " + arg);
        else { if (!query.empty()) query += ' '; query += arg; }
    }
    if (!options.doh_url.empty() || !options.fallback_doh_url.empty()) {
        for (const auto* key : {"http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "no_proxy", "NO_PROXY"}) {
            if (::unsetenv(key) != 0) throw std::runtime_error("unable to clear CLI proxy environment for DoH");
        }
    }
    edgesearch::detail::validate_options(options);
    if (stdin_mode && (!query.empty() || !parse_file.empty())) throw std::invalid_argument("--stdin cannot be combined with query arguments or --parse");
    if (!parse_file.empty()) {
        std::ifstream file(parse_file, std::ios::binary);
        if (!file) throw std::runtime_error("cannot open HTML fixture");
        std::string html;
        char buffer[8192];
        while (file) {
            file.read(buffer, sizeof(buffer));
            const auto bytes = static_cast<std::size_t>(file.gcount());
            if (bytes > options.max_body_bytes - html.size()) throw std::runtime_error("fixture exceeds max-body-kib");
            html.append(buffer, bytes);
        }
        const auto parsed = edgesearch::parse_html(parse_engine, html, "", options.per_engine);
        const auto& engine = edgesearch::detail::find_engine(parse_engine);
        edgesearch::Response response;
        response.query = query;
        response.status = parsed.status;
        response.results = edgesearch::detail::aggregate({parsed.results}, options.full ? 1000 : options.max_results);
        edgesearch::EngineReport report; report.name = parse_engine; report.provider = engine.provider;
        report.status = parsed.status; report.result_count = parsed.results.size(); report.message = parsed.message;
        response.engines.push_back(std::move(report));
        response.total_results = parsed.results.size(); response.fetch_complete = parsed.status == "ok" || parsed.status == "empty";
        response.return_complete = response.results.size() == response.total_results;
        response.engines.back().candidates_seen = parsed.candidates_seen;
        response.engines.back().ads_filtered = parsed.ads_filtered;
        std::cout << edgesearch::to_json(response) << '\n';
        return response.results.empty() ? 3 : 0;
    }
    if (stdin_mode) {
        edgesearch::Searcher searcher(options);
        while (true) {
            // One extra byte admits CRLF at the exact query limit. Preserve
            // embedded NULs for validation; never size the buffer from input.
            std::array<char, edgesearch::detail::query_byte_limit + 2> buffer{};
            std::cin.getline(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            if (std::cin.bad()) throw std::runtime_error("unable to read stdin");
            const auto consumed = std::cin.gcount();
            if (!consumed) break;
            const bool overflow = std::cin.fail() && !std::cin.eof();
            const bool delimiter = !std::cin.fail() && !std::cin.eof();
            const auto bytes = static_cast<std::size_t>(consumed - (delimiter ? 1 : 0));
            std::string line(buffer.data(), bytes);
            if (overflow) {
                std::cin.clear(std::cin.rdstate() & ~std::ios::failbit);
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                if (std::cin.bad()) throw std::runtime_error("unable to read stdin");
            }
            if (!line.empty() && line.back() == '\r') line.pop_back();
            try {
                if (overflow || line.size() > edgesearch::detail::query_byte_limit) throw std::invalid_argument("query exceeds 1024 bytes");
                std::cout << edgesearch::to_json(searcher.search(line)) << '\n' << std::flush;
            } catch (const std::exception& error) { std::cout << error_json(error.what()) << '\n' << std::flush; }
        }
        return 0;
    }
    const auto response = edgesearch::search(query, options);
    std::cout << edgesearch::to_json(response) << '\n';
    return response.results.empty() ? 3 : 0;
}
}  // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);  // Responses are explicitly flushed in stdin mode.
    try { return run(argc, argv); }
    catch (const std::exception& error) { std::cout << error_json(error.what()) << '\n'; return 2; }
}
