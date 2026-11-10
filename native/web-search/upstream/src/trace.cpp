#include "internal.hpp"

namespace edgesearch::detail {
void ConnectionTrace::observe(TraceInput input, std::string_view text, std::int64_t at_us) noexcept {
    const char* name = nullptr;
    unsigned bit = 0;
    switch (input) {
    case TraceInput::tls_in: name = "first_tls_record_in"; bit = 1; break;
    case TraceInput::tls_out: name = "first_tls_record_out"; bit = 2; break;
    case TraceInput::headers_in: name = "first_headers_in"; bit = 4; break;
    case TraceInput::headers_out: name = "first_headers_out"; bit = 8; break;
    case TraceInput::text:
        // libcurl informational wording is not a stable protocol interface.
        // Unrecognized text is discarded; timing fields remain authoritative.
        if (text.find("Trying ") != std::string_view::npos) name = "connect_attempt";
        else if (text.find("Connected to ") != std::string_view::npos) name = "connection_reported";
        else if (text.find("SSL connection using ") != std::string_view::npos) name = "tls_protocol_reported";
        else if (text.find("Re-using existing") != std::string_view::npos ||
                 text.find("Reusing existing") != std::string_view::npos ||
                 text.find("Reusing connection") != std::string_view::npos) name = "connection_reuse";
        else if (text.find("was resolved") != std::string_view::npos) name = "dns_resolved";
        else if (text.find("was found in DNS cache") != std::string_view::npos) name = "dns_cache_hit";
        break;
    }
    if (!name || (bit && (seen & bit))) return;
    seen |= bit;
    if (size == events.size()) { ++dropped; return; }
    events[size++] = {name, at_us};
}
}  // namespace edgesearch::detail
