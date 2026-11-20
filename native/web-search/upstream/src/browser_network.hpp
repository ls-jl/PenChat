#pragma once
#include "internal.hpp"
namespace edgesearch::detail {
struct RoutedResponse {
    HttpResponse response;
    std::vector<EngineAttempt> attempts; // One hop, at most two network attempts.
    bool alternate_used = false;
    std::size_t requested_count = 0;
};
using SingleFetcher = std::function<HttpResponse(const Request&, const Options&, bool)>;
RoutedResponse fetch_routed(const Request& request, const Options& options, HttpSession& primary,
                           HttpSession& alternate, SearchRouteState& state,
                           const SingleFetcher& fetcher = {}, const CooldownClock& now = CooldownTable::Clock::now,
                           std::size_t attempt_limit = 2);
std::string image_referrer(const std::string& source, const std::string& target);
}
