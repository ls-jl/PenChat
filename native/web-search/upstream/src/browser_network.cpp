#include "browser_network.hpp"
#include "curl_backend.hpp"
#include <algorithm>
#include <memory>
namespace edgesearch::detail {
RoutedResponse fetch_routed(const Request& request, const Options& options, HttpSession& primary,
                           HttpSession& alternate, SearchRouteState& state, const SingleFetcher& fetcher,
                           const CooldownClock& now, std::size_t attempt_limit) {
    RoutedResponse result;
    const auto start = std::chrono::steady_clock::now();
    const bool configured = !options.fallback_doh_url.empty() || options.fallback_system_dns;
    const unsigned preferred = configured && now() < state.alternate_until ? 1U : 0U;
    for (unsigned n = 0; n < std::min(attempt_limit, configured ? std::size_t{2} : std::size_t{1}); ++n) {
        const auto left = options.timeout_ms - static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
        if (left < 1) break;
        const unsigned route = n ? 1U - preferred : preferred;
        auto settings = options; settings.timeout_ms = left;
        if (!n && configured) settings.connect_timeout_ms = std::min(settings.connect_timeout_ms, std::max(100L, options.timeout_ms * 2 / 5));
        if (route) { settings.doh_url = settings.fallback_doh_url; settings.doh_bootstrap = settings.fallback_doh_bootstrap; }
        settings.fallback_doh_url.clear(); settings.fallback_doh_bootstrap.clear(); settings.fallback_system_dns = false;
        auto attempt_request = request;
        attempt_request.timeout_ms = request.timeout_ms > 0 ? std::min(request.timeout_ms, left) : left;
        if (fetcher) result.response = fetcher(attempt_request, settings, route != 0);
        else (route ? alternate : primary).fetch({attempt_request}, settings, [&](std::size_t, HttpResponse&& response) { result.response = std::move(response); });
        const auto& response = result.response;
        EngineAttempt attempt; attempt.variant = route ? "alternate" : "primary";
        attempt.resolver = settings.doh_url.empty() ? "system" : "doh";
        attempt.status = response.status; attempt.failure_stage = failure_stage(response);
        attempt.http_status = response.http_status; attempt.transport_code = response.transport_code;
        attempt.elapsed_ms = response.elapsed_ms; attempt.requested = response.requested;
        result.attempts.push_back(std::move(attempt));
        result.requested_count += response.requested ? 1U : 0U;
        result.alternate_used |= response.requested && route != 0;
        if (response.transport_code == 0 && response.http_status >= 200 && response.http_status < 300) {
            state.alternate_until = route ? now() + std::chrono::seconds(60) : CooldownTable::Clock::time_point{};
        }
        const auto stage = failure_stage(response);
        if (!request.post_data.empty() || !response.requested || (response.status != "timeout" && response.status != "network_error") ||
            (stage != "dns" && stage != "connect")) break;
    }
    result.response.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    return result;
}
std::string image_referrer(const std::string& source, const std::string& target) {
    initialize_runtime();
    using Url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
    Url a(curl_url(), curl_url_cleanup), b(curl_url(), curl_url_cleanup);
    if (!a || !b || normalize_url(source).empty() || normalize_url(target).empty() ||
        curl_url_set(a.get(), CURLUPART_URL, source.c_str(), 0) != CURLUE_OK ||
        curl_url_set(b.get(), CURLUPART_URL, target.c_str(), 0) != CURLUE_OK) return {};
    auto part = [](CURLU* u, CURLUPart key) {
        char* raw = nullptr; const auto code = curl_url_get(u, key, &raw, 0);
        std::unique_ptr<char, decltype(&curl_free)> value(raw, curl_free);
        return code == CURLUE_OK ? std::string(raw) : std::string{};
    };
    if (part(a.get(), CURLUPART_SCHEME) == "https" && part(b.get(), CURLUPART_SCHEME) == "http") return {};
    // Send only the source origin, including on same-origin fetches. Never leak
    // a page's path/query or fragment to a CDN, redirect or another site.
    curl_url_set(a.get(), CURLUPART_PATH, "/", 0); curl_url_set(a.get(), CURLUPART_QUERY, nullptr, 0); curl_url_set(a.get(), CURLUPART_FRAGMENT, nullptr, 0);
    return part(a.get(), CURLUPART_URL);
}
}
