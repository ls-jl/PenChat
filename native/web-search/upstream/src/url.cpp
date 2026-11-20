#include "internal.hpp"

#include "curl_backend.hpp"
#include <algorithm>
#include <cctype>
#include <memory>

namespace edgesearch::detail {
namespace {
using Url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;

std::string part(CURLU* url, CURLUPart which) {
    char* value = nullptr;
    if (curl_url_get(url, which, &value, 0) != CURLUE_OK) return {};
    std::string result(value);
    curl_free(value);
    return result;
}

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string query_value(const std::string& url, const std::string& key) {
    const auto question = url.find('?');
    if (question == std::string::npos) return {};
    auto start = question + 1;
    while (start < url.size()) {
        const auto end = url.find_first_of("&#", start);
        const auto pair = url.substr(start, end == std::string::npos ? end : end - start);
        if (pair.compare(0, key.size() + 1, key + "=") == 0) return decode(pair.substr(key.size() + 1));
        if (end == std::string::npos || url[end] == '#') break;
        start = end + 1;
    }
    return {};
}

std::string base64(const std::string& value) {
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int buffer = 0;
    unsigned int bits = 0;
    std::string output;
    for (char c : value) {
        if (c == '=') break;
        if (c == '-') c = '+';
        if (c == '_') c = '/';
        const auto pos = alphabet.find(c);
        if (pos == std::string::npos) return {};
        buffer = (buffer << 6U) | static_cast<unsigned int>(pos);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((buffer >> bits) & 255U));
        }
    }
    return output;
}
}  // namespace

std::string encode(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') result += static_cast<char>(c);
        else { result += '%'; result += hex[c >> 4U]; result += hex[c & 15U]; }
    }
    return result;
}

std::string decode(const std::string& value) {
    auto digit = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = digit(static_cast<unsigned char>(value[i + 1]));
            const int lo = digit(static_cast<unsigned char>(value[i + 2]));
            if (hi >= 0 && lo >= 0) {
                result += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        result += value[i];
    }
    return result;
}

std::string normalize_url(const std::string& value) {
    initialize_runtime();
    if (value.size() > 8192 || value.find('\0') != std::string::npos) return {};
    Url url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) return {};
    const auto scheme = lower(part(url.get(), CURLUPART_SCHEME));
    const auto host = lower(part(url.get(), CURLUPART_HOST));
    if ((scheme != "https" && scheme != "http") || host.empty() ||
        !part(url.get(), CURLUPART_USER).empty() || !part(url.get(), CURLUPART_PASSWORD).empty()) return {};
    curl_url_set(url.get(), CURLUPART_SCHEME, scheme.c_str(), 0);
    curl_url_set(url.get(), CURLUPART_HOST, host.c_str(), 0);
    const auto port = part(url.get(), CURLUPART_PORT);
    if ((scheme == "https" && port == "443") || (scheme == "http" && port == "80")) curl_url_set(url.get(), CURLUPART_PORT, nullptr, 0);
    curl_url_set(url.get(), CURLUPART_FRAGMENT, nullptr, 0);
    return part(url.get(), CURLUPART_URL);
}

std::string clean_url(const std::string& engine, const std::string& value, const std::string& base) {
    initialize_runtime();
    if (value.empty() || value.size() > 8192 || value.find('\0') != std::string::npos) return {};
    Url handle(curl_url(), curl_url_cleanup);
    if (!handle) return {};
    if (!base.empty() && curl_url_set(handle.get(), CURLUPART_URL, base.c_str(), 0) != CURLUE_OK) return {};
    if (curl_url_set(handle.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) return {};
    auto url = part(handle.get(), CURLUPART_URL);
    const auto host = lower(part(handle.get(), CURLUPART_HOST));
    const auto path = part(handle.get(), CURLUPART_PATH);
    if (engine == "duckduckgo" && (host == "duckduckgo.com" || host == "html.duckduckgo.com")) {
        const auto embedded = query_value(url, "uddg");
        if (!embedded.empty()) url = embedded;
        else return {};
    } else if (engine == "yahoo" && host == "r.search.yahoo.com") {
        const auto start = url.find("/RU=");
        if (start == std::string::npos) return {};
        const auto end = url.find("/RK=", start);
        const auto stop = end == std::string::npos ? url.find("/RS=", start) : end;
        url = decode(url.substr(start + 4, stop == std::string::npos ? stop : stop - start - 4));
    } else if (engine == "bing_cn" && (host == "www.bing.com" || host == "cn.bing.com")) {
        if (path == "/aclick") return {};
        if (path == "/ck/a") {
            const auto embedded = query_value(url, "u");
            if (embedded.compare(0, 2, "a1") == 0) url = base64(embedded.substr(2));
        }
    } else if (engine == "baidu" && (host == "www.baidu.com" || host == "baidu.com") && path == "/baidu.php") return {};
    return normalize_url(url);
}

std::string collapse_space(const std::string& value) {
    std::string output;
    bool space = false;
    for (const unsigned char c : value) {
        if (c < 128 && std::isspace(c)) { space = !output.empty(); continue; }
        if (c < 32 || c == 127) continue;
        if (space) { output += ' '; space = false; }
        output += static_cast<char>(c);
    }
    return output;
}

std::string truncate_utf8(const std::string& value, std::size_t bytes) {
    if (value.size() <= bytes) return value;
    while (bytes > 0 && (static_cast<unsigned char>(value[bytes]) & 0xC0U) == 0x80U) --bytes;
    return value.substr(0, bytes);
}

}  // namespace edgesearch::detail
