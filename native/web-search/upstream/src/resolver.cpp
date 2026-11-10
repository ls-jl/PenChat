#include "internal.hpp"
#include "curl_backend.hpp"
#include <arpa/inet.h>
#include <array>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace edgesearch::detail {
namespace {
using Url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;
struct Endpoint { std::string host, port; };

Endpoint endpoint(const std::string& value) {
    initialize_runtime();
    if (value.size() > 2048 || value.find('\0') != std::string::npos) throw std::invalid_argument("invalid DoH URL");
    for (const unsigned char c : value) if (c <= 32 || c == 127) throw std::invalid_argument("invalid DoH URL whitespace");
    Url url(curl_url(), curl_url_cleanup);
    if (!url) throw std::bad_alloc();
    if (curl_url_set(url.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) throw std::invalid_argument("invalid DoH URL");
    auto part = [&](CURLUPart which, unsigned flags = 0) {
        char* raw = nullptr;
        const auto code = curl_url_get(url.get(), which, &raw, flags);
        std::unique_ptr<char, decltype(&curl_free)> owned(raw, curl_free);
        return code == CURLUE_OK ? std::string(raw) : std::string{};
    };
    char* user = nullptr;
    const bool has_user = curl_url_get(url.get(), CURLUPART_USER, &user, 0) == CURLUE_OK;
    curl_free(user);
    if (part(CURLUPART_SCHEME) != "https" || has_user ||
        !part(CURLUPART_PASSWORD).empty() || value.find('#') != std::string::npos) throw std::invalid_argument("DoH URL must use HTTPS without userinfo or fragment");
    Endpoint result{part(CURLUPART_HOST), part(CURLUPART_PORT, CURLU_DEFAULT_PORT)};
    if (result.host.empty() || result.port == "0") throw std::invalid_argument("invalid DoH host or port");
    return result;
}

[[maybe_unused]] bool numeric_address(const std::string& value) {
    std::array<unsigned char, 16> storage{};
    return value.size() <= 45 && value.find('\0') == std::string::npos &&
        (inet_pton(AF_INET, value.c_str(), storage.data()) == 1 || inet_pton(AF_INET6, value.c_str(), storage.data()) == 1);
}
}  // namespace

void validate_resolver_options(const Options& options) {
    if (!options.fallback_doh_url.empty() || options.fallback_system_dns || !options.fallback_doh_bootstrap.empty()) {
        if (options.fallback_system_dns && (!options.fallback_doh_url.empty() || options.doh_url.empty())) throw std::invalid_argument("fallback-system-dns needs a primary DoH and no fallback DoH");
        if (options.fallback_doh_url.empty() && !options.fallback_doh_bootstrap.empty()) throw std::invalid_argument("fallback bootstrap requires fallback DoH URL");
        if (!options.fallback_doh_url.empty() && options.fallback_doh_url == options.doh_url) throw std::invalid_argument("fallback resolver must differ from primary");
        auto alternate = options; alternate.doh_url = options.fallback_doh_url; alternate.doh_bootstrap = options.fallback_doh_bootstrap;
        alternate.fallback_doh_url.clear(); alternate.fallback_doh_bootstrap.clear(); alternate.fallback_system_dns = false;
        validate_resolver_options(alternate);
    }
    if (options.doh_url.empty()) {
        if (!options.doh_bootstrap.empty()) throw std::invalid_argument("DoH bootstrap requires a DoH URL");
        return;
    }
#if LIBCURL_VERSION_NUM < 0x074c00
    throw std::invalid_argument("DoH requires libcurl headers >= 7.76 for independent TLS verification");
#else
    const auto server = endpoint(options.doh_url);
    if (curl_version_info(CURLVERSION_NOW)->version_num < 0x074c00) throw std::invalid_argument("DoH requires libcurl runtime >= 7.76");
    if (!options.proxy.empty()) throw std::invalid_argument("DoH is supported in direct mode; explicit proxy mode may resolve names remotely");
    // libcurl's internal DoH transfers do not inherit the parent's proxy setting.
    // The library never changes a caller's process environment. The standalone
    // CLI clears its own proxy environment before creating a search session.
    for (const auto* key : {"http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"}) {
        const auto* value = std::getenv(key);
        if (value && *value) throw std::invalid_argument(std::string("DoH requires a proxy-free process environment: ") + key);
    }
    if (options.doh_bootstrap.size() > 4) throw std::invalid_argument("DoH bootstrap supports at most four numeric addresses");
    auto host = server.host;
    if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    if (!options.doh_bootstrap.empty() && numeric_address(host)) throw std::invalid_argument("DoH bootstrap is for a resolver hostname; the configured endpoint is already numeric");
    for (const auto& address : options.doh_bootstrap) if (!numeric_address(address)) throw std::invalid_argument("DoH bootstrap must contain numeric IPv4/IPv6 addresses");
#endif
}

std::string resolver_override(const Options& options) {
    if (options.doh_bootstrap.empty()) return {};
    const auto server = endpoint(options.doh_url);
    std::string result = "+" + server.host + ":" + server.port + ":";
    for (const auto& address : options.doh_bootstrap) {
        if (result.back() != ':') result += ',';
        result += address.find(':') == std::string::npos ? address : "[" + address + "]";
    }
    return result;
}
}  // namespace edgesearch::detail
