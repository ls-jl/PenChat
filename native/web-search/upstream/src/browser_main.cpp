#include "edgesearch/browser.hpp"
#include "internal.hpp"
#include "text_semantics.hpp"
#include "page_view.hpp"

#include <array>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
using edgesearch::detail::json_quote;
struct Value { bool numeric = false; bool boolean = false; std::string text; };
using Fields = std::map<std::string, Value>;

// Deliberately small protocol: a flat JSON object with strings and unsigned
// integers. Reject duplicate keys, trailing input and unsupported JSON types.
class CommandParser {
    const std::string& input;
    std::size_t pos = 0;
    [[noreturn]] void fail() const { throw std::invalid_argument("expected a flat JSON command with strings, booleans and unsigned integers"); }
    void space() { while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\r' || input[pos] == '\n')) ++pos; }
    bool take(char c) { space(); if (pos < input.size() && input[pos] == c) { ++pos; return true; } return false; }
    unsigned hex() {
        unsigned result = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos == input.size()) fail();
            const auto c = input[pos++]; unsigned digit;
            if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
            else fail();
            result = result * 16 + digit;
        }
        return result;
    }
    static void utf8(std::string& out, unsigned value) {
        if (value < 128) out += static_cast<char>(value);
        else if (value < 2048) {
            out += static_cast<char>(192U | (value >> 6U)); out += static_cast<char>(128U | (value & 63U));
        } else if (value < 65536) {
            out += static_cast<char>(224U | (value >> 12U));
            out += static_cast<char>(128U | ((value >> 6U) & 63U)); out += static_cast<char>(128U | (value & 63U));
        } else {
            out += static_cast<char>(240U | (value >> 18U));
            out += static_cast<char>(128U | ((value >> 12U) & 63U));
            out += static_cast<char>(128U | ((value >> 6U) & 63U)); out += static_cast<char>(128U | (value & 63U));
        }
    }
    static bool valid_utf8(const std::string& text) {
        std::size_t at = 0;
        while (at < text.size()) {
            const auto c = static_cast<unsigned char>(text[at++]);
            if (c < 128) continue;
            unsigned value, count, minimum;
            if (c >= 194 && c <= 223) { value = c & 31U; count = 1; minimum = 128; }
            else if (c >= 224 && c <= 239) { value = c & 15U; count = 2; minimum = 2048; }
            else if (c >= 240 && c <= 244) { value = c & 7U; count = 3; minimum = 65536; }
            else return false;
            if (count > text.size() - at) return false;
            while (count--) {
                const auto d = static_cast<unsigned char>(text[at++]);
                if ((d & 192U) != 128U) return false;
                value = (value << 6U) | (d & 63U);
            }
            if (value < minimum || value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU)) return false;
        }
        return true;
    }
    std::string string() {
        if (!take('"')) fail();
        std::string out;
        while (pos < input.size()) {
            const auto c = static_cast<unsigned char>(input[pos++]);
            if (c == '"') { if (!valid_utf8(out)) fail(); return out; }
            if (c < 32) fail();
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (pos == input.size()) fail();
            const auto escaped = input[pos++];
            if (escaped == '"' || escaped == '\\' || escaped == '/') out += escaped;
            else if (escaped == 'n') out += '\n';
            else if (escaped == 'r') out += '\r';
            else if (escaped == 't') out += '\t';
            else if (escaped == 'b') out += '\b';
            else if (escaped == 'f') out += '\f';
            else if (escaped == 'u') {
                auto code = hex();
                if (code >= 0xd800U && code <= 0xdbffU) {
                    if (pos + 2 > input.size() || input[pos++] != '\\' || input[pos++] != 'u') fail();
                    const auto low = hex();
                    if (low < 0xdc00U || low > 0xdfffU) fail();
                    code = 0x10000U + ((code - 0xd800U) << 10U) + low - 0xdc00U;
                } else if (code >= 0xdc00U && code <= 0xdfffU) fail();
                utf8(out, code);
            } else fail();
        }
        fail();
    }
public:
    explicit CommandParser(const std::string& text) : input(text) {}
    Fields parse() {
        Fields fields;
        if (!take('{')) fail();
        if (!take('}')) {
            do {
                const auto key = string();
                if (!take(':')) fail();
                space(); Value value;
                if (pos < input.size() && input[pos] == '"') value.text = string();
                else if (input.compare(pos, 4, "true") == 0 || input.compare(pos, 5, "false") == 0) {
                    value.boolean = true;
                    const auto length = input[pos] == 't' ? 4U : 5U;
                    value.text = input.substr(pos, length); pos += length;
                } else {
                    value.numeric = true;
                    const auto begin = pos;
                    while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
                    if (begin == pos || (input[begin] == '0' && pos - begin > 1)) fail();
                    value.text = input.substr(begin, pos - begin);
                }
                if (!fields.emplace(key, std::move(value)).second) fail();
            } while (take(','));
            if (!take('}')) fail();
        }
        space(); if (pos != input.size()) fail();
        return fields;
    }
};

std::uint64_t integer(const std::string& value) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("integer is out of range");
    return result;
}
std::string field(const Fields& fields, const std::string& key) {
    const auto item = fields.find(key);
    if (item == fields.end() || (item->second.numeric || item->second.boolean)) throw std::invalid_argument("missing string field: " + key);
    return item->second.text;
}
std::uint64_t number(const Fields& fields, const std::string& key, std::uint64_t fallback = 0) {
    const auto item = fields.find(key);
    if (item == fields.end()) return fallback;
    if (!(item->second.numeric || item->second.boolean)) throw std::invalid_argument("expected integer field: " + key);
    return integer(item->second.text);
}
std::size_t count(const Fields& fields, const std::string& key, std::size_t fallback) {
    const auto result = number(fields, key, fallback);
    if (result > std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("integer exceeds platform size");
    return static_cast<std::size_t>(result);
}
bool flag(const Fields& fields, const std::string& key) {
    const auto item = fields.find(key);
    if (item == fields.end()) return false;
    if (!item->second.boolean) throw std::invalid_argument("expected boolean field: " + key);
    return item->second.text == "true";
}
void keys(const Fields& fields, std::initializer_list<const char*> allowed) {
    for (const auto& item : fields) {
        if (item.first == "action" || item.first == "full" || item.first == "max_response_bytes") continue;
        bool found = false;
        for (const auto* key : allowed) found |= item.first == key;
        if (!found) throw std::invalid_argument("unexpected field: " + item.first);
    }
}
std::string page_result(const edgesearch::Page& page, const Fields& fields, std::size_t budget, bool only_links = false) {
    using edgesearch::detail::utf8_prefix;
    edgesearch::detail::PageView view(page);
    const bool full = flag(fields, "full"), image = page.resource_type == "image" && !only_links;
    const auto offset = count(fields, "offset_bytes", 0);
    view.link_begin = count(fields, "offset", 0);
    if (view.link_begin > page.links.size()) throw std::invalid_argument("offset exceeds retained links");
    view.link_count = page.links.size() - view.link_begin;
    if (!full) view.link_count = std::min(view.link_count, count(fields, "limit", 128));
    bool bounded = false;
    if (image) {
        if (offset > page.image_data.size()) throw std::invalid_argument("offset exceeds retained image");
        const auto maximum = count(fields, "max_bytes", page.image_data.size());
        if (fields.count("max_bytes") && (!maximum || maximum > 8 * 1024 * 1024)) throw std::invalid_argument("max_bytes must be 1..8388608");
        view.image_begin = offset; view.image_length = std::min(maximum, page.image_data.size() - offset);
        const auto wire_bound = (budget / 4) * 3;
        bounded = view.image_length > wire_bound;
        if (bounded) { view.image_length = wire_bound; view.reasons.push_back("response_byte_limit"); }
    } else {
        view.image_length = 0;
        if (offset > page.text.size()) throw std::invalid_argument("offset exceeds retained content");
        if (offset < page.text.size() && (static_cast<unsigned char>(page.text[offset]) & 192U) == 128U)
            throw std::invalid_argument("offset_bytes must be a UTF-8 boundary");
        view.text = only_links ? std::string_view{} : utf8_prefix(std::string_view(page.text).substr(offset), full ? page.text.size() : 65536);
    }
    auto finish = [&] {
        view.next_offset_bytes = only_links || image ? 0 : offset + view.text.size();
        view.next_link_offset = view.link_begin + view.link_count;
        view.starts_deleted = !only_links && !image && edgesearch::detail::inside_deleted(page, offset);
        view.ends_deleted = !only_links && !image && edgesearch::detail::inside_deleted(page, view.next_offset_bytes);
        view.return_complete = (image ? offset + view.image_length == page.total_image_bytes :
            (only_links || view.next_offset_bytes == page.text.size())) && view.next_link_offset == page.links.size();
        view.text_truncated = page.text_truncated || (!only_links && !image && view.next_offset_bytes < page.text.size());
        view.links_truncated = page.links_truncated || view.next_link_offset < page.links.size();
        return edgesearch::detail::page_view_json(view);
    };
    // Account for metadata/reasons before enforcing the final wire budget.
    if (!bounded && ((image && offset + view.image_length < page.total_image_bytes) ||
        (!image && !only_links && offset + view.text.size() < page.text.size()) ||
        view.link_begin + view.link_count < page.links.size()))
        view.reasons.push_back(image ? "image_chunk_limit" : "preview_limit");
    auto result = finish();
    if (result.size() > budget) {
        if (!bounded) view.reasons.push_back("response_byte_limit");
        do {
            if (image && view.image_length) view.image_length /= 2;
            else if (!view.text.empty()) view.text = utf8_prefix(view.text, view.text.size() / 2);
            else if (view.link_count) view.link_count /= 2;
            else throw std::invalid_argument("response budget cannot hold page metadata");
            result = finish();
        } while (result.size() > budget);
    }
    if (image && offset < page.image_data.size() && !view.image_length)
        throw std::invalid_argument("response budget cannot hold image bytes");
    return result;
}
std::string execute(edgesearch::Browser& browser, const Fields& fields, const edgesearch::BrowserOptions& options) {
    const auto action = field(fields, "action");
    const auto maximum = count(fields, "max_response_bytes", options.max_response_bytes);
    if (maximum < 4096 || maximum > options.max_response_bytes) throw std::invalid_argument("max_response_bytes must be 4096..configured limit");
    const auto budget = maximum - 128; // Includes the protocol envelope.
    const bool full = flag(fields, "full");
    const auto timeout = number(fields, "timeout_ms", 0);
    if (fields.count("timeout_ms") && (timeout < 100 || timeout > static_cast<std::uint64_t>(options.network.timeout_ms))) throw std::invalid_argument("timeout_ms exceeds configured budget");
    std::string result;
    if (action == "search") {
        keys(fields, {"query", "timeout_ms", "per_engine"});
        if (fields.count("per_engine") && number(fields, "per_engine") == 0) throw std::invalid_argument("per_engine must be positive");
        auto response = browser.search(field(fields, "query"), full, static_cast<long>(timeout), count(fields, "per_engine", 0));
        result = edgesearch::to_json(response);
        if (result.size() > budget) {
            response.return_complete = false; response.incomplete_reasons.push_back("response_byte_limit");
            do {
                if (!response.rejected_results.empty()) response.rejected_results.pop_back();
                else if (!response.results.empty()) response.results.pop_back();
                else throw std::invalid_argument("response budget cannot hold engine diagnostics");
                result = edgesearch::to_json(response);
            } while (result.size() > budget);
        }
    } else if (action == "open") {
        keys(fields, {"url", "timeout_ms"}); result = page_result(browser.open(field(fields, "url"), static_cast<long>(timeout)), fields, budget);
    } else if (action == "click") {
        keys(fields, {"page_id", "link_id", "timeout_ms"});
        result = page_result(browser.click(number(fields, "page_id"), count(fields, "link_id", 0), static_cast<long>(timeout)), fields, budget);
    } else if (action == "read_image") {
        keys(fields, {"page_id", "offset_bytes", "max_bytes"});
        const auto& source = browser.page(number(fields, "page_id"));
        if (source.resource_type != "image") throw std::invalid_argument("page is not an image");
        Fields view = fields;
        if (!full && !view.count("max_bytes")) view.emplace("max_bytes", Value{true, false, "16384"});
        result = page_result(source, view, budget);
    } else if (action == "read") {
        keys(fields, {"page_id", "start_line", "lines", "offset_bytes", "max_bytes"});
        const auto id = number(fields, "page_id");
        std::size_t offset = count(fields, "offset_bytes", 0);
        const auto& page = browser.page(id);
        if (page.resource_type == "image") throw std::invalid_argument("image has no text; use read_image");
        if (fields.count("max_bytes") && (number(fields, "max_bytes") == 0 || number(fields, "max_bytes") > 8 * 1024 * 1024)) throw std::invalid_argument("max_bytes must be 1..8388608");
        std::size_t length = full ? page.text.size() : count(fields, "max_bytes", 16384);
        if ((!full && !fields.count("offset_bytes") && !fields.count("max_bytes")) || fields.count("start_line") || fields.count("lines")) {
            if (fields.count("offset_bytes")) throw std::invalid_argument("choose line range or byte offset");
            const auto start = count(fields, "start_line", 1), n = count(fields, "lines", 80);
            if (!start || !n || n > 500) throw std::invalid_argument("read start_line >=1 and lines 1..500");
            offset = browser.line_offset(id, start);
            const auto next_line = start > std::numeric_limits<std::size_t>::max() - n ? std::numeric_limits<std::size_t>::max() : start + n;
            const auto end = browser.line_offset(id, next_line);
            if (!full) length = std::min(length, end - offset - ((end > offset && page.text[end - 1] == '\n') ? 1 : 0));
        }
        length = std::max(std::size_t{1}, std::min(length, std::size_t{8 * 1024 * 1024}));
        auto text = browser.read_bytes(id, offset, length);
        result = edgesearch::to_json(text);
        while (result.size() > budget) {
            length /= 2;
            if (length < 1) throw std::invalid_argument("response budget cannot hold read metadata");
            text = browser.read_bytes(id, offset, length); result = edgesearch::to_json(text);
        }
    } else if (action == "find") {
        keys(fields, {"page_id", "text", "max_matches", "start_line"});
        const auto id = number(fields, "page_id");
        const auto limit = full ? std::size_t{2097151} : count(fields, "max_matches", 20);
        if (!limit || limit > 2097151) throw std::invalid_argument("invalid max_matches");
        const auto start = count(fields, "start_line", 1);
        const auto matches = browser.find(id, field(fields, "text"), std::min(limit + 1, budget / 64 + 1), start, full);
        const auto& source_page = browser.page(id);
        result = "{\"page_id\":" + std::to_string(source_page.id) + ",\"text_format\":" + json_quote(source_page.deleted_ranges.empty() ? "plain" : "plain_with_deletion_markers") + ",\"matches\":[";
        std::size_t included = 0;
        bool text_incomplete = false;
        for (const auto& match : matches) {
            auto text = match.text;
            bool truncated = match.text_truncated;
            auto item = std::string(included ? "," : "") + "{\"line\":" + std::to_string(match.line) + ",\"offset_bytes\":" + std::to_string(match.offset_bytes) + ",\"text\":" + json_quote(text);
            if (!included && item.size() + 512 > budget) {
                text = edgesearch::detail::truncate_utf8(text, (budget - 512) / 6); truncated = true;
                item = "{\"line\":" + std::to_string(match.line) + ",\"offset_bytes\":" + std::to_string(match.offset_bytes) + ",\"text\":" + json_quote(text);
            }
            item += ",\"starts_deleted\":" + std::string(edgesearch::detail::inside_deleted(source_page, match.offset_bytes) ? "true" : "false") +
                ",\"ends_deleted\":" + std::string(edgesearch::detail::inside_deleted(source_page, match.offset_bytes + text.size()) ? "true" : "false") +
                ",\"text_truncated\":" + std::string(truncated ? "true" : "false") + ",\"next_offset_bytes\":" + std::to_string(match.offset_bytes + text.size()) + '}';
            if (included >= limit || result.size() + item.size() + 256 > budget) break;
            result += item; ++included; text_incomplete |= truncated;
        }
        const bool more = included < matches.size();
        result += "],\"return_complete\":" + std::string(more || text_incomplete ? "false" : "true") +
            ",\"source_truncated\":" + (browser.page(id).text_truncated ? "true" : "false") +
            ",\"more\":" + (more ? "true" : "false") + ",\"next_start_line\":" +
            std::to_string(more && included ? matches[included - 1].line + 1 : 0) + '}';
    } else if (action == "current" || action == "links") {
        keys(fields, {"page_id", "offset", "limit", "offset_bytes"});
        result = page_result(browser.page(number(fields, "page_id")), fields, budget, action == "links");
    } else if (action == "back") { keys(fields, {}); result = page_result(browser.back(), fields, budget); }
    else if (action == "clear") { keys(fields, {}); browser.clear_session(); result = "{\"status\":\"ok\"}"; }
    else throw std::invalid_argument("unknown action: " + action);
    return "{\"schema_version\":1,\"action\":" + json_quote(action) + ",\"result\":" + result + '}';
}
std::string error(const std::string& message) {
    return "{\"schema_version\":1,\"status\":\"error\",\"message\":" + json_quote(message) + '}';
}
void help() {
    std::cout << "edge-browser " << EDGE_SEARCH_VERSION << R"( - bounded text browser for AI tools
Usage: edge-browser --open URL [options]
       edge-browser --stdin [options]

JSON-line actions (add "full":true for complete retained results):
  {"action":"search","query":"search text"}
  {"action":"open","url":"https://example.org/"}
  {"action":"click","page_id":1,"link_id":2}
  {"action":"read_image","page_id":2,"offset_bytes":0,"max_bytes":16384}
  {"action":"read","page_id":1,"start_line":1,"lines":80}
  {"action":"find","page_id":1,"text":"README","max_matches":20}
  {"action":"links","page_id":1}
  {"action":"current"}
  {"action":"back"}
  {"action":"clear"}
page_id may be omitted or zero for the current page.

Options:
  --profile NAME          domestic (default), global or all; at most 4 per query
  --engines NAME,NAME       Search adapters (bing_cn,so360,sogou,baidu)
  --max-results N          Search result limit (10)
  --per-engine N           Per-engine search limit (10)
  --timeout-ms N           Network deadline (15000), shared across redirects
  --engine-timeout-ms N    Search per-engine slice (0: shared total budget)
  --bing-format auto|rss|html  Default html; auto/rss opt in to RSS
  --fallback-doh-url HTTPS_URL  Explicit alternate DNS route after DNS/connect failure
  --fallback-doh-bootstrap IP  Repeat up to four for alternate resolver
  --fallback-system-dns    Try system resolver after primary DoH failure
  --connect-timeout-ms N   Connection deadline (8000)
  --max-body-kib N         Decoded body cap, 64..8192 (2048)
  --text-kib N             Retained text per page, 1..8192 (2048)
  --max-links N            Retained links per page, 1..16384 (4096)
  --cache-kib N            Retained content budget (8192)
  --max-response-kib N     Serialized output limit (8192)
  --link-kib N             Retained link data budget per page (2048)
  --request-per-engine-limit N  Request per_engine ceiling (50)
  --full                  Complete retained output in --open mode
  --max-pages N            Retained pages, 1..16 (8)
  --allow-private-network  Explicitly permit LAN/loopback destinations or proxy
  --browser PROFILE        Native impersonation profile (patched build only)
  --ip-family auto|v4|v6   Local address family
  --doh-url HTTPS_URL      Optional direct-mode DoH
  --doh-bootstrap IP       Resolver bootstrap, repeat up to four
  --ca-bundle FILE         Custom CA; TLS verification remains enabled
  --proxy URL             Explicit proxy; requires --allow-private-network
  --transport-info         Backend information
  --version                Version
  --help                   This help

Image links have kind=image. Explicit open/click returns image bytes as typed Base64.
No OCR, image decoding, scripts or automatic subresource downloads.
Web content is untrusted data. JSON commands are at most 16384 bytes per line.
Exit: 0 successful page; 3 unsuccessful page; 2 usage/runtime error.
Stdin mode emits one response per command and continues after command errors.
)";
}
int run(int argc, char** argv) {
    edgesearch::BrowserOptions options;
    std::string url;
    bool stdin_mode = false, full_mode = false;
    auto set_size = [](const std::string& input) {
        const auto parsed = integer(input);
        if (parsed > std::numeric_limits<std::size_t>::max()) throw std::invalid_argument("option exceeds platform size");
        return static_cast<std::size_t>(parsed);
    };
    auto set_time = [](const std::string& input) {
        const auto parsed = integer(input);
        if (parsed > 60000) throw std::invalid_argument("timeout exceeds 60000 ms");
        return static_cast<long>(parsed);
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 == argc) throw std::invalid_argument("missing value for " + arg);
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") { help(); return 0; }
        if (arg == "--version") { std::cout << "edge-browser " << EDGE_SEARCH_VERSION << '\n'; return 0; }
        if (arg == "--transport-info") { std::cout << edgesearch::transport_json() << '\n'; return 0; }
        if (arg == "--open") url = value();
        else if (arg == "--stdin") stdin_mode = true;
        else if (arg == "--full") full_mode = true;
        else if (arg == "--allow-private-network") options.allow_private_network = true;
        else if (arg == "--profile") options.network.engines = edgesearch::profile_engines(value());
        else if (arg == "--engines") {
            const auto list = value(); options.network.engines.clear();
            std::size_t start = 0;
            while (true) {
                const auto end = list.find(',', start);
                options.network.engines.push_back(list.substr(start, end == std::string::npos ? end : end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
        } else if (arg == "--max-results") options.network.max_results = set_size(value());
        else if (arg == "--per-engine") options.network.per_engine = set_size(value());
        else if (arg == "--timeout-ms") options.network.timeout_ms = set_time(value());
        else if (arg == "--engine-timeout-ms") options.network.engine_timeout_ms = set_time(value());
        else if (arg == "--bing-format") options.network.bing_format = value();
        else if (arg == "--fallback-doh-url") options.network.fallback_doh_url = value();
        else if (arg == "--fallback-doh-bootstrap") options.network.fallback_doh_bootstrap.push_back(value());
        else if (arg == "--fallback-system-dns") options.network.fallback_system_dns = true;
        else if (arg == "--connect-timeout-ms") options.network.connect_timeout_ms = set_time(value());
        else if (arg == "--max-body-kib" || arg == "--text-kib") {
            const auto kib = set_size(value());
            if (kib > 8192) throw std::invalid_argument("KiB option too large");
            if (arg == "--text-kib") options.max_text_bytes = kib * 1024;
            else options.network.max_body_bytes = kib * 1024;
        } else if (arg == "--cache-kib" || arg == "--max-response-kib" || arg == "--link-kib") {
            const auto kib = set_size(value());
            if (kib > 65536) throw std::invalid_argument("KiB option too large");
            if (arg == "--cache-kib") options.cache_bytes = kib * 1024;
            else if (arg == "--link-kib") options.max_link_bytes = kib * 1024;
            else options.max_response_bytes = kib * 1024;
        } else if (arg == "--request-per-engine-limit") options.request_per_engine_limit = set_size(value());
        else if (arg == "--max-links") options.max_links = set_size(value());
        else if (arg == "--max-pages") options.max_pages = set_size(value());
        else if (arg == "--browser") options.network.browser = value();
        else if (arg == "--ip-family") options.network.ip_family = value();
        else if (arg == "--doh-url") options.network.doh_url = value();
        else if (arg == "--doh-bootstrap") options.network.doh_bootstrap.push_back(value());
        else if (arg == "--ca-bundle") options.network.ca_bundle = value();
        else if (arg == "--proxy") options.network.proxy = value();
        else throw std::invalid_argument("unknown option: " + arg);
    }
    if (stdin_mode == !url.empty()) throw std::invalid_argument("choose either --open URL or --stdin");
    if (!options.network.doh_url.empty() || !options.network.fallback_doh_url.empty())
        for (const auto* key : {"http_proxy","https_proxy","all_proxy","HTTP_PROXY","HTTPS_PROXY","ALL_PROXY","no_proxy","NO_PROXY"})
            if (unsetenv(key) != 0) throw std::runtime_error("unable to clear CLI proxy environment");
    edgesearch::Browser browser(options);
    if (!stdin_mode) {
        const auto page = browser.open(url);
        std::cout << "{\"schema_version\":1,\"action\":\"open\",\"result\":" << page_result(page, {{"full", {false, true, full_mode ? "true" : "false"}}}, options.max_response_bytes - 128) << "}\n";
        return page.status == "ok" || page.status == "empty" ? 0 : 3;
    }
    while (true) {
        std::array<char, 16386> buffer{};
        std::cin.getline(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (std::cin.bad()) throw std::runtime_error("unable to read stdin");
        const auto consumed = std::cin.gcount();
        if (!consumed) break;
        const bool overflow = std::cin.fail() && !std::cin.eof();
        const bool delimiter = !std::cin.fail() && !std::cin.eof();
        std::string command(buffer.data(), static_cast<std::size_t>(consumed - (delimiter ? 1 : 0)));
        if (overflow) {
            std::cin.clear(std::cin.rdstate() & ~std::ios::failbit);
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            if (std::cin.bad()) throw std::runtime_error("unable to read stdin");
        }
        if (!command.empty() && command.back() == '\r') command.pop_back();
        try {
            if (overflow || command.size() > 16384) throw std::invalid_argument("command exceeds 16384 bytes");
            std::cout << execute(browser, CommandParser(command).parse(), options) << '\n' << std::flush;
        } catch (const std::exception& ex) { std::cout << error(ex.what()) << '\n' << std::flush; }
    }
    return 0;
}
}  // namespace
int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false); std::cin.tie(nullptr);
    try { return run(argc, argv); }
    catch (const std::exception& ex) { std::cout << error(ex.what()) << '\n'; return 2; }
}
