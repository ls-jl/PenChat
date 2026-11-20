#include "internal.hpp"

#include <iomanip>
#include <locale>
#include <sstream>

namespace edgesearch::detail {

std::string json_quote(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
        else if (c < 32) { result += "\\u00"; result += hex[c >> 4U]; result += hex[c & 15U]; }
        else if (c < 128) result += static_cast<char>(c);
        else {
            const std::size_t length = c >= 0xC2 && c <= 0xDF ? 2 : (c >= 0xE0 && c <= 0xEF ? 3 : (c >= 0xF0 && c <= 0xF4 ? 4 : 0));
            bool valid = length && i + length <= value.size();
            for (std::size_t j = 1; valid && j < length; ++j) valid = (static_cast<unsigned char>(value[i + j]) & 0xC0U) == 0x80U;
            if (valid && length >= 3) {
                const auto next = static_cast<unsigned char>(value[i + 1]);
                valid = !((c == 0xE0 && next < 0xA0) || (c == 0xED && next >= 0xA0) || (c == 0xF0 && next < 0x90) || (c == 0xF4 && next >= 0x90));
            }
            if (!valid) result += "\\ufffd";
            else { result.append(value, i, length); i += length - 1; }
        }
    }
    result += '"';
    return result;
}

std::string strings(const std::vector<std::string>& values) {
    std::string result = "[";
    for (std::size_t i = 0; i < values.size(); ++i) { if (i) result += ','; result += json_quote(values[i]); }
    return result + ']';
}

}  // namespace edgesearch::detail

namespace edgesearch {

std::string to_json(const Response& response) {
    using detail::json_quote;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema_version\":1,\"query\":" << json_quote(response.query)
        << ",\"status\":" << json_quote(response.status)
        << ",\"partial\":" << (response.partial ? "true" : "false")
        << ",\"completed_early\":" << (response.completed_early ? "true" : "false")
        << ",\"first_result_ms\":" << response.first_result_ms
        << ",\"attempt_count\":" << response.attempt_count
        << ",\"resolver_fallback_used\":" << (response.resolver_fallback_used ? "true" : "false")
        << ",\"suggested_queries\":" << detail::strings(response.suggested_queries)
        << ",\"elapsed_ms\":" << response.elapsed_ms
        << ",\"fetch_complete\":" << (response.fetch_complete ? "true" : "false")
        << ",\"return_complete\":" << (response.return_complete ? "true" : "false")
        << ",\"total_results\":" << response.total_results
        << ",\"usable_results\":" << response.usable_results
        << ",\"completeness_scope\":\"selected_engine_pages_in_this_fetch\""
        << ",\"incomplete_reasons\":" << detail::strings(response.incomplete_reasons)
        << ",\"number_of_results\":" << response.results.size() << ",\"results\":[";
    for (std::size_t i = 0; i < response.results.size(); ++i) {
        const auto& row = response.results[i];
        if (i) out << ',';
        out << "{\"title\":" << json_quote(row.title) << ",\"url\":" << json_quote(row.url)
            << ",\"content\":" << json_quote(row.content)
            << ",\"engine\":" << json_quote(row.engines.empty() ? "" : row.engines.front())
            << ",\"engines\":" << detail::strings(row.engines)
            << ",\"providers\":" << detail::strings(row.providers)
            << ",\"relevance\":" << json_quote(row.relevance) << ",\"relevance_reason\":" << json_quote(row.relevance_reason) << ",\"query_coverage\":" << row.query_coverage
            << ",\"score\":" << std::setprecision(12) << row.score << '}';
    }
    out << "],\"rejected_results\":[";
    for (std::size_t i = 0; i < response.rejected_results.size(); ++i) {
        const auto& row = response.rejected_results[i];
        if (i) out << ',';
        out << "{\"title\":" << json_quote(row.title) << ",\"url\":" << json_quote(row.url)
            << ",\"content\":" << json_quote(row.content) << ",\"relevance\":\"rejected\",\"relevance_reason\":"
            << json_quote(row.relevance_reason) << ",\"engines\":" << detail::strings(row.engines) << '}';
    }
    out << "],\"engines\":[";
    for (std::size_t i = 0; i < response.engines.size(); ++i) {
        const auto& row = response.engines[i];
        if (i) out << ',';
        out << "{\"name\":" << json_quote(row.name) << ",\"provider\":" << json_quote(row.provider)
            << ",\"status\":" << json_quote(row.status) << ",\"http_status\":" << row.http_status
            << ",\"elapsed_ms\":" << row.elapsed_ms << ",\"result_count\":" << row.result_count
            << ",\"requested\":" << (row.requested ? "true" : "false")
            << ",\"transport_code\":" << row.transport_code
            << ",\"retry_after_ms\":" << row.retry_after_ms
            << ",\"cooldown_remaining_ms\":" << row.cooldown_remaining_ms
            << ",\"cooldown_applied_ms\":" << row.cooldown_applied_ms
            << ",\"cooldown_level\":" << row.cooldown_level
            << ",\"content_quality\":" << json_quote(row.content_quality)
            << ",\"rejected_count\":" << row.rejected_count
            << ",\"candidates_seen\":" << row.candidates_seen << ",\"ads_filtered\":" << row.ads_filtered
            << ",\"network\":{\"dns_done_us\":" << row.network.dns_done_us
            << ",\"connect_done_us\":" << row.network.connect_done_us
            << ",\"tls_done_us\":" << row.network.tls_done_us
            << ",\"first_byte_us\":" << row.network.first_byte_us
            << ",\"total_us\":" << row.network.total_us
            << ",\"redirect_us\":" << row.network.redirect_us
            << ",\"new_connections\":" << row.network.new_connections
            << ",\"redirects\":" << row.network.redirects
            << ",\"body_bytes\":" << row.network.body_bytes << '}'
            << ",\"message\":" << json_quote(row.message);
        out << ",\"failure_stage\":" << json_quote(row.failure_stage) << ",\"attempts\":[";
        for (std::size_t j = 0; j < row.attempts.size(); ++j) {
            if (j) out << ',';
            const auto& attempt = row.attempts[j];
            out << "{\"variant\":" << json_quote(attempt.variant) << ",\"resolver\":" << json_quote(attempt.resolver)
                << ",\"status\":" << json_quote(attempt.status) << ",\"failure_stage\":" << json_quote(attempt.failure_stage)
                << ",\"http_status\":" << attempt.http_status << ",\"transport_code\":" << attempt.transport_code
                << ",\"elapsed_ms\":" << attempt.elapsed_ms << ",\"requested\":" << (attempt.requested ? "true" : "false")
                << ",\"result_count\":" << attempt.result_count << '}';
        }
        out << ']';
        if (!row.snapshot_error.empty()) out << ",\"snapshot_error\":" << json_quote(row.snapshot_error);
        out << '}';
    }
    return out.str() + "]}";
}

std::string engines_json() {
    std::string result = "{\"engines\":[";
    bool first = true;
    for (const auto& engine : available_engines()) {
        if (!first) result += ',';
        first = false;
        result += "{\"name\":" + detail::json_quote(engine.name) + ",\"provider\":" + detail::json_quote(engine.provider) + '}';
    }
    return result + "]}";
}

}  // namespace edgesearch
