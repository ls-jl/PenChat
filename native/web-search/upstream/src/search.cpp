#include "internal.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace edgesearch::detail {

std::vector<Result> aggregate(const std::vector<std::vector<Result>>& groups, std::size_t max_results) {
    struct Entry { Result result; std::map<std::string, std::size_t> ranks; };
    std::map<std::string, Entry> cache;
    for (const auto& group : groups) {
        for (std::size_t rank = 0; rank < group.size(); ++rank) {
            const auto& source = group[rank];
            const auto key = normalize_url(source.url);
            if (key.empty()) continue;
            auto& entry = cache[key];
            if (entry.result.url.empty()) { entry.result = source; entry.result.url = key; entry.result.score = 0.0; }
            if (source.relevance == "supported") { entry.result.relevance = source.relevance; entry.result.relevance_reason = source.relevance_reason; }
            entry.result.query_coverage = std::max(entry.result.query_coverage, source.query_coverage);
            if (source.content.size() > entry.result.content.size()) entry.result.content = source.content;
            for (const auto& engine : source.engines) {
                if (std::find(entry.result.engines.begin(), entry.result.engines.end(), engine) == entry.result.engines.end()) entry.result.engines.push_back(engine);
            }
            for (const auto& provider : source.providers) {
                if (std::find(entry.result.providers.begin(), entry.result.providers.end(), provider) == entry.result.providers.end()) entry.result.providers.push_back(provider);
                const auto found = entry.ranks.find(provider);
                if (found == entry.ranks.end() || rank < found->second) entry.ranks[provider] = rank;
            }
        }
    }
    std::vector<Result> results;
    for (auto& item : cache) {
        for (const auto& rank : item.second.ranks) item.second.result.score += 1.0 / (61.0 + static_cast<double>(rank.second));
        results.push_back(std::move(item.second.result));
    }
    std::sort(results.begin(), results.end(), [](const Result& left, const Result& right) {
        if ((left.relevance == "supported") != (right.relevance == "supported")) return left.relevance == "supported";
        const auto left_rank = left.score * (0.5 + left.query_coverage * 0.5);
        const auto right_rank = right.score * (0.5 + right.query_coverage * 0.5);
        return left_rank != right_rank ? left_rank > right_rank : left.url < right.url;
    });
    if (results.size() > max_results) results.resize(max_results);
    return results;
}

}  // namespace edgesearch::detail

namespace edgesearch::detail {

Response run_search(const std::string& query, const Options& options, HttpSession& session, SnapshotStore& snapshots, CooldownTable& cooldown,
                    const RequestFactory& factory, const CooldownClock& cooldown_now,
                    HttpSession* alternate_session, SearchRouteState* route_state, const SearchFetcher& fetcher) {
    validate_options(options);
    if (query.empty() || collapse_space(query).empty() || query.size() > query_byte_limit || query.find('\0') != std::string::npos)
        throw std::invalid_argument("query must contain 1..1024 bytes of text");
    const auto start = std::chrono::steady_clock::now();
    auto remaining = [&] { return options.timeout_ms - static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()); };
    HttpSession temporary_alternate;
    SearchRouteState temporary_routes;
    auto& alternate = alternate_session ? *alternate_session : temporary_alternate;
    auto& routes = route_state ? *route_state : temporary_routes;
    const bool has_alternate = !options.fallback_doh_url.empty() || options.fallback_system_dns;
    Options resolver_options[2] = {options, options};
    resolver_options[1].doh_url = options.fallback_doh_url;
    resolver_options[1].doh_bootstrap = options.fallback_doh_bootstrap;
    for (auto& value : resolver_options) { value.fallback_doh_url.clear(); value.fallback_doh_bootstrap.clear(); value.fallback_system_dns = false; }
    const unsigned preferred = has_alternate && cooldown_now() < routes.alternate_until ? 1U : 0U;
    std::vector<std::vector<Result>> groups(options.engines.size());
    std::optional<std::chrono::steady_clock::time_point> enough_at;
    Response response; response.query = query;
    std::vector<std::size_t> selected;
    for (const auto& name : options.engines) {
        const auto& engine = find_engine(name);
        EngineReport report; report.name = name; report.provider = engine.provider; report.status = "pending";
        const auto wait = options.enable_cooldown ? cooldown.remaining(name, cooldown_now()) : 0;
        if (wait > 0) {
            report.status = "cooldown"; report.failure_stage = "cooldown"; report.cooldown_remaining_ms = wait;
            report.cooldown_level = cooldown.level(name); report.message = "engine cooling down after " + cooldown.reason(name);
        } else selected.push_back(response.engines.size());
        response.engines.push_back(std::move(report));
    }
    if (options.enable_cooldown) std::stable_sort(selected.begin(), selected.end(), [&](auto left, auto right) {
        const auto& a = response.engines[left].name; const auto& b = response.engines[right].name;
        if (cooldown.penalty(a) != cooldown.penalty(b)) return cooldown.penalty(a) < cooldown.penalty(b);
        return options.engines.size() > 4 && cooldown.last_request(a) < cooldown.last_request(b);
    });
    if (selected.size() > 4) {
        for (std::size_t i = 4; i < selected.size(); ++i) {
            auto& report = response.engines[selected[i]]; report.status = "not_attempted"; report.failure_stage = "queue";
            report.message = "four-engine limit; select a smaller engine set to query this source";
        }
        selected.resize(4); response.incomplete_reasons.push_back("engine_attempt_limit");
    }
    auto usable_count = [&] {
        const auto candidates = aggregate(groups, 1000);
        return static_cast<std::size_t>(std::count_if(candidates.begin(), candidates.end(), [](const Result& r) { return r.relevance == "supported"; }));
    };
    auto stop = [&] {
        if (options.full || !options.early_results) return false;
        if (usable_count() < options.early_results) { enough_at.reset(); return false; }
        const auto now = std::chrono::steady_clock::now();
        if (!enough_at) enough_at = now;
        return now - *enough_at >= std::chrono::milliseconds(options.early_grace_ms);
    };
    auto network_retry = [](const EngineReport& report) {
        return report.requested && (report.failure_stage == "dns" || report.failure_stage == "connect") &&
            (report.status == "timeout" || report.status == "network_error");
    };
    auto phase = [&](const std::vector<std::size_t>& indices, unsigned resolver, long budget, const std::string& bing_format) {
        if (indices.empty() || budget < 100 || response.attempt_count >= 6) return false;
        auto phase_options = resolver_options[resolver];
        phase_options.timeout_ms = std::min(remaining(), budget); if (phase_options.timeout_ms < 100) return false;
        phase_options.concurrency = std::min(options.concurrency, std::size_t{2});
        phase_options.bing_format = bing_format;
        std::vector<Request> requests; std::vector<std::size_t> dispatch;
        for (auto i : indices) {
            if (requests.size() >= 6 - response.attempt_count) break;
            const auto& engine = find_engine(response.engines[i].name);
            auto request = factory(engine, query, phase_options);
            if (options.engine_timeout_ms) request.timeout_ms = request.timeout_ms > 0 ? std::min(request.timeout_ms, options.engine_timeout_ms) : options.engine_timeout_ms;
            requests.push_back(std::move(request)); dispatch.push_back(i);
        }
        bool route_recovered = false;
        const auto resolver_name = phase_options.doh_url.empty() ? "system" : "doh";
        ResponseHandler handler = [&](std::size_t index, HttpResponse&& http) {
            auto& report = response.engines[dispatch[index]];
            route_recovered |= http.requested && http.transport_code == 0 && http.http_status >= 200 && http.http_status < 300;
            const auto prior_requested = report.requested;
            report.status = http.status; report.message = http.message; report.http_status = http.http_status;
            report.elapsed_ms += http.elapsed_ms; report.network = http.network; report.transport_code = http.transport_code;
            report.requested |= http.requested; report.retry_after_ms = http.retry_after_ms;
            report.failure_stage = failure_stage(http);
            report.snapshot_error = snapshots.save(report.name, query, http);
            if (http.requested) { ++response.attempt_count; response.resolver_fallback_used |= resolver == 1; cooldown.note_request(report.name); }
            std::size_t attempt_results = 0;
            try {
                auto parsed = parse_response(report.name, http, options.per_engine);
                report.status = parsed.status; report.message = parsed.message; report.content_quality = parsed.status;
                std::vector<Result> accepted;
                for (auto& candidate : parsed.results) {
                    assess_relevance(query, candidate);
                    if (candidate.relevance == "rejected") { ++report.rejected_count; response.rejected_results.push_back(std::move(candidate)); }
                    else accepted.push_back(std::move(candidate));
                }
                if (parsed.status == "ok" && accepted.empty() && !parsed.results.empty()) {
                    report.status = "low_relevance"; report.content_quality = "low_relevance";
                    report.message = "all parsed candidates conflict with query evidence";
                }
                if (report.status == "ok" && !accepted.empty() &&
                    std::none_of(accepted.begin(), accepted.end(), [](const Result& row) { return row.relevance == "supported"; }))
                    report.content_quality = "uncertain";
                if (report.status == "low_relevance") report.failure_stage = "relevance";
                else if (report.status == "parse_error") report.failure_stage = "parse";
                else if (report.status == "challenge") report.failure_stage = "challenge";
                else if (report.status == "ok" || report.status == "empty") report.failure_stage.clear();
                if (std::any_of(accepted.begin(), accepted.end(), [](const Result& row) { return row.relevance == "supported"; }) && response.first_result_ms < 0)
                    response.first_result_ms = options.timeout_ms - remaining();
                attempt_results = accepted.size();
                auto& retained = groups[dispatch[index]];
                retained.insert(retained.end(), std::make_move_iterator(accepted.begin()), std::make_move_iterator(accepted.end()));
                report.result_count = aggregate({retained}, 1000).size();
                report.candidates_seen += parsed.candidates_seen; report.ads_filtered += parsed.ads_filtered;
            } catch (const std::exception& error) {
                report.status = "parse_error"; report.failure_stage = "parse"; report.message = error.what();
            }
            EngineAttempt attempt; attempt.variant = requests[index].variant.empty() ? "html" : requests[index].variant;
            attempt.resolver = resolver_name; attempt.status = report.status; attempt.failure_stage = report.failure_stage;
            attempt.http_status = http.http_status; attempt.transport_code = http.transport_code;
            attempt.elapsed_ms = http.elapsed_ms; attempt.requested = http.requested; attempt.result_count = attempt_results;
            report.attempts.push_back(std::move(attempt));
            // DNS failures describe the route, not engine content health. Queued work is never observed as a failure.
            if (http.requested && report.failure_stage != "dns") cooldown.observe(report, options, cooldown_now());
            if (!http.requested && prior_requested) report.requested = true;
        };
        if (fetcher) fetcher(requests, phase_options, handler, stop, resolver == 1);
        else (resolver == 1 ? alternate : session).fetch(requests, phase_options, handler, stop);
        return route_recovered;
    };
    const auto primary_budget = has_alternate ? std::max(100L, options.timeout_ms * 2 / 5) : options.timeout_ms;
    phase(selected, preferred, primary_budget, options.bing_format);
    const bool route_failed = std::any_of(response.engines.begin(), response.engines.end(), network_retry);
    const unsigned recovery_resolver = has_alternate && route_failed ? 1U - preferred : preferred;
    if (has_alternate && !stop() && remaining() >= 100) {
        std::vector<std::size_t> retry;
        for (auto i : selected) {
            const auto& report = response.engines[i];
            if ((!report.requested || (route_failed && network_retry(report))) &&
                (!options.enable_cooldown || cooldown.remaining(report.name, cooldown_now()) == 0)) retry.push_back(i);
        }
        const bool recovered = phase(retry, recovery_resolver, remaining(), options.bing_format);
        if (recovery_resolver != preferred && recovered) {
            if (recovery_resolver == 1) routes.alternate_until = cooldown_now() + std::chrono::seconds(60);
            else routes.alternate_until = {};
        }
    }
    if (options.bing_format == "auto" && usable_count() == 0 && !stop() && remaining() >= 100) {
        for (auto i : selected) {
            const auto& report = response.engines[i];
            if (report.name == "bing_cn" && (report.status == "low_relevance" || report.status == "parse_error" || report.status == "empty" || report.content_quality == "uncertain")) {
                phase({i}, recovery_resolver, remaining(), "html"); break;
            }
        }
    }
    const auto all = aggregate(groups, 1000); response.total_results = all.size(); response.usable_results = usable_count();
    response.results = aggregate(groups, options.full ? 1000 : options.max_results);
    bool failure = false;
    for (const auto& report : response.engines) {
        response.completed_early |= report.status == "cancelled";
        failure |= report.status != "ok" && report.status != "empty" && report.status != "cancelled";
    }
    response.fetch_complete = !failure && !response.completed_early;
    response.return_complete = response.results.size() == response.total_results;
    if (!response.fetch_complete) response.incomplete_reasons.push_back(response.completed_early ? "early_return" : "engine_incomplete");
    if (!response.return_complete) response.incomplete_reasons.push_back("result_preview_limit");
    response.partial = failure && !response.results.empty();
    response.status = response.results.empty() ? (failure ? "failed" : "empty") : (failure ? "partial" : "ok");
    if (response.results.empty() && !response.rejected_results.empty()) response.status = "low_relevance";
    if (response.results.empty() || usable_count() == 0) response.suggested_queries = suggest_queries(query);
    response.elapsed_ms = options.timeout_ms - remaining();
    return response;
}
}  // namespace edgesearch::detail

namespace edgesearch {

struct Searcher::Impl {
    Options options;
    detail::HttpSession session;
    detail::HttpSession alternate;
    detail::SearchRouteState routes;
    detail::SnapshotStore snapshots;
    detail::CooldownTable cooldown;
    static Options checked(Options value) { detail::validate_options(value); return value; }
    explicit Impl(Options value) : options(checked(std::move(value))), snapshots(options) {}
};
Searcher::Searcher(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
Searcher::~Searcher() = default;
Searcher::Searcher(Searcher&&) noexcept = default;
Searcher& Searcher::operator=(Searcher&&) noexcept = default;
Response Searcher::search(const std::string& query) {
    if (!impl_) throw std::logic_error("searcher has been moved");
    return detail::run_search(query, impl_->options, impl_->session, impl_->snapshots, impl_->cooldown, detail::make_request, detail::CooldownTable::Clock::now, &impl_->alternate, &impl_->routes);
}
Response Searcher::search(const std::string& query, bool full, long timeout_ms, std::size_t per_engine) {
    if (!impl_) throw std::logic_error("searcher has been moved");
    auto options = impl_->options;
    options.full = full;
    if (timeout_ms) {
        if (timeout_ms < 100 || timeout_ms > impl_->options.timeout_ms) throw std::invalid_argument("timeout_ms exceeds configured search budget");
        options.timeout_ms = timeout_ms;
    }
    if (per_engine) options.per_engine = per_engine;
    return detail::run_search(query, options, impl_->session, impl_->snapshots, impl_->cooldown, detail::make_request, detail::CooldownTable::Clock::now, &impl_->alternate, &impl_->routes);
}
void Searcher::clear_session() {
    if (!impl_) throw std::logic_error("searcher has been moved");
    impl_->session.reset(); impl_->alternate.reset(); impl_->routes = {};
    impl_->cooldown.clear();
}
Response search(const std::string& query, const Options& options) {
    Searcher searcher(options);
    return searcher.search(query);
}

}  // namespace edgesearch
