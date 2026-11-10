#include "edgesearch/browser.hpp"
#include "internal.hpp"
#include "repository.hpp"
#include "text_semantics.hpp"
#include "curl_backend.hpp"
#include "image.hpp"
#include "browser_network.hpp"
#include "page_view.hpp"
#include "line_index.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include <algorithm>
#include <cctype>
#include <deque>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <sstream>

#include <stdexcept>
#include <unordered_set>

namespace edgesearch {
namespace {
using Document = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;
using Url = std::unique_ptr<CURLU, decltype(&curl_url_cleanup)>;

std::string lower(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return text;
}
std::string url_part(CURLU* url, CURLUPart part) {
    char* out = nullptr;
    if (curl_url_get(url, part, &out, 0) != CURLUE_OK) return {};
    std::string result(out);
    curl_free(out);
    return result;
}
std::string resolve_url(const std::string& value, const std::string& base = {}) {
    if (value.empty() || value.size() > 8192 ||
        value.find_first_of("\r\n\t\0", 0, 4) != std::string::npos) return {};
    Url handle(curl_url(), curl_url_cleanup);
    if (!handle || (!base.empty() && curl_url_set(handle.get(), CURLUPART_URL, base.c_str(), 0) != CURLUE_OK) ||
        curl_url_set(handle.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) return {};
    const auto normalized = detail::normalize_url(url_part(handle.get(), CURLUPART_URL));
    if (normalized.empty() || !url_part(handle.get(), CURLUPART_ZONEID).empty()) return {};
    const auto fragment = url_part(handle.get(), CURLUPART_FRAGMENT);
    return normalized + (fragment.empty() ? "" : "#" + fragment);
}
std::string origin(const std::string& value) {
    Url handle(curl_url(), curl_url_cleanup);
    if (!handle || curl_url_set(handle.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) return {};
    return lower(url_part(handle.get(), CURLUPART_SCHEME)) + "://" +
        lower(url_part(handle.get(), CURLUPART_HOST)) + ":" + url_part(handle.get(), CURLUPART_PORT);
}
std::string property(xmlNode* node, const char* name) {
    auto* value = xmlGetProp(node, BAD_CAST name);
    if (!value) return {};
    std::string result(reinterpret_cast<const char*>(value));
    xmlFree(value);
    return result;
}
bool named(xmlNode* node, const char* name) {
    return node && node->type == XML_ELEMENT_NODE && xmlStrEqual(node->name, BAD_CAST name);
}
bool skip(xmlNode* node) {
    if (!node || node->type != XML_ELEMENT_NODE) return false;
    for (const auto* name : {"script", "style", "noscript", "template", "svg", "canvas"})
        if (named(node, name)) return true;
    if (xmlHasProp(node, BAD_CAST "hidden") || lower(property(node, "aria-hidden")) == "true") return true;
    auto style = lower(property(node, "style"));
    style.erase(std::remove_if(style.begin(), style.end(), [](unsigned char c) { return std::isspace(c); }), style.end());
    return style.find("display:none") != std::string::npos || style.find("visibility:hidden") != std::string::npos;
}
bool block(xmlNode* node) {
    for (const auto* name : {"p", "div", "section", "article", "main", "header", "footer", "nav",
                            "h1", "h2", "h3", "h4", "h5", "h6", "li", "ul", "ol",
                            "pre", "blockquote", "tr", "table", "br", "hr", "dl", "dt", "dd"})
        if (named(node, name)) return true;
    return false;
}
bool deleted_node(xmlNode* node) { return named(node, "s") || named(node, "del") || named(node, "strike"); }
struct Text {
    std::string value;
    std::size_t cap = 0;
    bool truncated = false;
    std::vector<DeletedRange> deleted_ranges;
    std::size_t deleted_depth = 0, deleted_begin = 0;
    std::size_t reserved() const { return deleted_depth ? 10 : 0; }
    void begin_deleted() {
        if (deleted_depth) { ++deleted_depth; return; }
        if (cap - std::min(cap, value.size()) < 19) { truncated = true; return; }
        deleted_begin = value.size(); deleted_depth = 1; append("[deleted]", true);
    }
    void end_deleted() {
        if (!deleted_depth || --deleted_depth) return;
        const auto content = value.begin() + static_cast<std::ptrdiff_t>(deleted_begin + 9);
        if (std::all_of(content, value.end(), [](unsigned char c) { return c < 128 && std::isspace(c); })) { value.resize(deleted_begin); return; }
        append("[/deleted]", true); deleted_ranges.push_back({deleted_begin, value.size()});
    }
    void newline() {
        while (!value.empty() && value.back() == ' ') value.pop_back();
        if (!value.empty() && value.back() != '\n') append("\n", true);
    }
    void append(const char* data, bool preserve) {
        if (!data) return;
        for (const auto* p = reinterpret_cast<const unsigned char*>(data); *p;) {
            const auto c = *p;
            if (c == '\r' || (c < 32 && c != '\t' && c != '\n')) { ++p; continue; }
            std::size_t width = c >= 0xC2 && c <= 0xDF ? 2U : (c >= 0xE0 && c <= 0xEF ? 3U : (c >= 0xF0 && c <= 0xF4 ? 4U : 1U));
            for (std::size_t i = 1; i < width; ++i) if (!p[i] || (p[i] & 0xC0U) != 0x80U) { width = 1; break; }
            if (!preserve && c < 128 && std::isspace(c)) {
                if (value.empty() || value.back() == ' ' || value.back() == '\n') { ++p; continue; }
            }
            if (value.size() > cap - reserved() || width > cap - reserved() - value.size()) { truncated = true; return; }
            if (!preserve && c < 128 && std::isspace(c)) value += ' ';
            else value.append(reinterpret_cast<const char*>(p), width);
            p += width;
        }
    }
    void finish() {
        while (deleted_depth) end_deleted();
        value = detail::truncate_utf8(value, cap);
        while (!value.empty() && (value.back() == '\n' || value.back() == ' ')) value.pop_back();
    }
};

// Walk siblings even when an element is hidden or its subtree is excluded.
template<class Visitor>
bool elements(xmlNode* root, Visitor visitor, std::size_t limit = 200000, bool include_hidden = false) {
    xmlNode* node = root;
    std::size_t visited = 0;
    while (node) {
        if (++visited > limit) return false;
        const bool visible = include_hidden || !skip(node);
        if (visible) visitor(node);
        if (visible && node->children) { node = node->children; continue; }
        while (node != root && !node->next) node = node->parent;
        if (node == root) break;
        node = node->next;
    }
    return true;
}

bool navigation_node(xmlNode* node) {
    for (const auto* tag : {"a", "nav", "header", "footer", "button", "select", "input"}) if (named(node, tag)) return true;
    auto markers = lower(property(node, "class") + " " + property(node, "id"));
    for (auto& c : markers) if (!std::isalnum(static_cast<unsigned char>(c))) c = ' ';
    std::istringstream words(markers); std::string word;
    while (words >> word) if (word == "nav" || word == "header" || word == "footer" || word == "menu" || word == "sidebar") return true;
    return false;
}
Text visible_text(xmlNode* root, std::size_t cap, bool following_siblings = false, bool prose_only = false) {
    Text out; out.cap = cap;
    // Explicit entry/exit traversal preserves paragraph boundaries and code.
    xmlNode* node = root;
    xmlNode* boundary = root ? root->parent : nullptr;
    if (following_siblings && root) {
        // A heading is often inside a title wrapper whose siblings contain
        // the actual section. Continue in document order to the main/body
        // boundary, rather than stopping at that wrapper.
        for (auto* parent = root->parent; parent; parent = parent->parent) {
            if (named(parent, "main") || named(parent, "body") || property(parent, "role") == "main") {
                boundary = parent; break;
            }
        }
    }
    for (auto* parent = root ? root->parent : nullptr; parent; parent = parent->parent)
        if (deleted_node(parent)) out.begin_deleted();
    std::size_t visited = 0, pre = 0;
    while (node) {
        if (++visited > 200000) { out.truncated = true; break; }
        const bool visible = !skip(node) && !(prose_only && navigation_node(node));
        if (visible) {
            if (deleted_node(node)) out.begin_deleted();
            if (named(node, "pre")) ++pre;
            if (block(node)) out.newline();
            if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE)
                out.append(reinterpret_cast<const char*>(node->content), pre > 0);
            if (named(node, "td") || named(node, "th")) out.append(" ", true);
            if (out.truncated) break;
            if (node->children) { node = node->children; continue; }
        }
        if (visible && deleted_node(node)) out.end_deleted();
        if (visible && block(node)) out.newline();
        if (visible && named(node, "pre") && pre) --pre;
        while (true) {
            if (node == root && !following_siblings) { node = nullptr; break; }
            if (node->next) { node = node->next; break; }
            node = node->parent;
            if (!node || (following_siblings && node == boundary)) { node = nullptr; break; }
            if (deleted_node(node)) out.end_deleted();
            if (block(node)) out.newline();
            if (named(node, "pre") && pre) --pre;
        }
    }
    out.finish();
    return out;
}

void validate(const BrowserOptions& options) {
    detail::validate_options(options.network);
    if (options.max_text_bytes < 1024 || options.max_text_bytes > 8 * 1024 * 1024)
        throw std::invalid_argument("browser text limit must be 1 KiB..8 MiB");
    if (options.max_links < 1 || options.max_links > 16384 || options.max_pages < 1 || options.max_pages > 16)
        throw std::invalid_argument("browser links must be 1..16384 and pages 1..16");
    if (options.cache_bytes < options.max_text_bytes + options.max_link_bytes + 16384 || options.cache_bytes > 64 * 1024 * 1024 ||
        options.max_link_bytes < 1024 || options.max_link_bytes > 8 * 1024 * 1024 ||
        options.max_response_bytes < 4096 || options.max_response_bytes > 32 * 1024 * 1024 ||
        options.request_per_engine_limit < 1 || options.request_per_engine_limit > 50)
        throw std::invalid_argument("invalid browser cache, link, response or per-engine budget");
    if (!options.allow_private_network && !options.network.proxy.empty())
        throw std::invalid_argument("a browser proxy requires explicit allow-private-network; remote addresses cannot be checked");
}

void add_link(Page& page, std::unordered_set<std::string>& seen, const BrowserOptions& options,
              const std::string& value, const std::string& base, std::string label,
              const std::string& kind = "link", const std::string& rel = {}) {
    const auto resolved = resolve_url(value, base);
    const auto actual_kind = kind == "link" && !detail::image_url_mime(resolved).empty() ? "image" : kind;
    const auto key = actual_kind == "image" ? "image:" + resolved : resolved;
    if (resolved.empty() || seen.count(key)) return;
    if (page.links.size() >= options.max_links || resolved.size() + label.size() + rel.size() + sizeof(PageLink) > options.max_link_bytes - std::min(page.link_bytes, options.max_link_bytes)) { page.links_truncated = true; return; }
    seen.insert(key);
    label = detail::truncate_utf8(detail::collapse_space(label), 512);
    if (label.empty()) label = resolved;
    page.link_bytes += resolved.size() + label.size() + rel.size() + sizeof(PageLink);
    page.links.push_back({page.links.size() + 1, label, resolved, actual_kind,
        detail::truncate_utf8(rel, 256), origin(resolved) != origin(page.url)});
}

void image_links(Page& page, std::unordered_set<std::string>& seen, const BrowserOptions& options,
                 xmlNode* node, const std::string& base) {
    const bool img = named(node, "img");
    if (!img && !(named(node, "source") && named(node->parent, "picture"))) return;
    auto label = property(node, "alt");
    if (label.empty()) label = property(node, "title");
    const auto rel = lower(property(node, "referrerpolicy")) == "no-referrer" ? "noreferrer" : "";
    if (img) {
        for (const auto* key : {"src", "data-src", "data-original"}) {
            const auto value = property(node, key);
            if (!resolve_url(value, base).empty()) {
                add_link(page, seen, options, value, base, label, "image", rel);
            }
        }
    }
    for (const auto* key : {"srcset", "data-srcset"}) {
        const auto set = property(node, key); std::size_t at = 0;
        // Only HTTP URL candidates; descriptors are hints, no viewport selection.
        for (unsigned n = 0; at < set.size() && n < 128; ++n) {
            at = set.find_first_not_of(" ,\t\r\n", at);
            if (at == std::string::npos) { at = set.size(); break; }
            const auto end = set.find_first_of(" \t\r\n", at);
            auto item = set.substr(at, end == std::string::npos ? end : end - at);
            const bool separator = !item.empty() && item.back() == ',';
            while (!item.empty() && item.back() == ',') item.pop_back();
            add_link(page, seen, options, item, base, label, "image", rel);
            if (end == std::string::npos) { at = set.size(); break; }
            const auto comma = separator ? end : set.find(',', end);
            at = comma == std::string::npos ? set.size() : comma + (separator ? 0 : 1);
        }
        if (at < set.size()) page.links_truncated = true;
    }
}

void markdown_links(Page& page, const std::string& source, const BrowserOptions& options) {
    std::unordered_set<std::string> seen;
    std::map<std::string, std::string> references;
    // Common Markdown destinations, not a full Markdown/HTML renderer.
    auto destination = [](std::string value) {
        value = detail::collapse_space(value);
        if (!value.empty() && value.front() == '<') {
            const auto end = value.find('>');
            return end == std::string::npos ? std::string{} : value.substr(1, end - 1);
        }
        return value.substr(0, value.find_first_of(" \t"));
    };
    const auto scan_limit = std::min(source.size(), options.network.max_body_bytes);
    page.links_truncated |= scan_limit < source.size();
    auto bounded_find = [&](char c, std::size_t start, std::size_t maximum) {
        if (start >= scan_limit) return std::string::npos;
        const auto end = std::min(scan_limit, start + maximum);
        const auto found = std::find(source.begin() + static_cast<std::ptrdiff_t>(start),
                                     source.begin() + static_cast<std::ptrdiff_t>(end), c);
        return found == source.begin() + static_cast<std::ptrdiff_t>(end) ? std::string::npos :
            static_cast<std::size_t>(found - source.begin());
    };
    for (std::size_t at = 0; at < scan_limit; ) {
        const auto end_line = source.find('\n', at);
        const auto end_at = std::min(scan_limit, end_line == std::string::npos ? source.size() : end_line);
        const auto line = std::string_view(source).substr(at, end_at - at);
        at = end_at + 1;
        if (references.size() >= 512) break;
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || first > 3 || line[first] != '[') continue;
        const auto end = line.find("]:", first + 1);
        if (end == std::string_view::npos || end - first > 256) continue;
        const auto key = lower(detail::collapse_space(std::string(line.substr(first + 1, end - first - 1))));
        if (line.size() - end - 2 > 8192) continue;
        const auto value = destination(std::string(line.substr(end + 2)));
        if (!key.empty() && value.size() <= 8192) references.emplace(key, value);
    }
    for (unsigned pass = 0; pass < 2; ++pass) {
    for (std::size_t at = 0; at < scan_limit; ++at) {
        if (pass == 0 && source[at] == '<' && (source.compare(at + 1, 7, "http://") == 0 || source.compare(at + 1, 8, "https://") == 0)) {
            const auto end = bounded_find('>', at + 1, 8192);
            if (end != std::string::npos && end - at <= 8193)
                add_link(page, seen, options, source.substr(at + 1, end - at - 1), page.url, {});
        }
        if (source[at] != '[' || (at && source[at - 1] == '\\')) continue;
        const bool image = at && source[at - 1] == '!' && !(at > 1 && source[at - 2] == '\\');
        if (image != (pass == 1)) continue;
        auto end = at + 1;
        unsigned brackets = 1;
        for (; end < scan_limit && end - at <= 1024; ++end) {
            if (source[end] == '\\' && end + 1 < scan_limit) { ++end; continue; }
            if (source[end] == '[') ++brackets;
            if (source[end] == ']' && --brackets == 0) break;
        }
        if (brackets != 0) end = std::string::npos;
        if (end == std::string::npos) { at += std::min(std::size_t{1024}, scan_limit - at - 1); page.links_truncated = true; continue; }
        auto label = source.substr(at + 1, end - at - 1);
        if (label.compare(0, 2, "![") == 0) {
            const auto alt = label.find(']', 2);
            if (alt != std::string::npos) label = label.substr(2, alt - 2);
        }
        if (end + 1 < source.size() && source[end + 1] == '(') {
            auto close = end + 2; unsigned depth = 1;
            for (; close < scan_limit && close - end <= 8194; ++close) {
                if (source[close] == '\\' && close + 1 < scan_limit) { ++close; continue; }
                if (source[close] == '(') ++depth;
                if (source[close] == ')' && --depth == 0) break;
            }
            if (depth == 0) {
                add_link(page, seen, options, destination(source.substr(end + 2, close - end - 2)), page.url, label, image ? "image" : "link");
                at = close;
            } else { at = close - 1; page.links_truncated = true; }
        } else {
            auto key = lower(detail::collapse_space(label));
            if (end + 1 < source.size() && source[end + 1] == '[') {
                const auto close = bounded_find(']', end + 2, 256);
                if (close != std::string::npos && close - end <= 256) {
                    if (close > end + 2) key = lower(detail::collapse_space(source.substr(end + 2, close - end - 2)));
                }
            }
            const auto found = references.find(key);
            if (found != references.end()) add_link(page, seen, options, found->second, page.url, label, image ? "image" : "link");
        }
    }
    }
    const auto html_hint = lower(source.substr(0, scan_limit));
    if (html_hint.find("<a") != std::string::npos || html_hint.find("<img") != std::string::npos || html_hint.find("<picture") != std::string::npos) {
        Document doc(htmlReadMemory(source.data(), static_cast<int>(scan_limit), page.url.c_str(), "UTF-8",
            HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET | HTML_PARSE_COMPACT), xmlFreeDoc);
        if (doc) {
            const bool complete = elements(xmlDocGetRootElement(doc.get()), [&](xmlNode* node) {
                if (named(node, "a")) add_link(page, seen, options, property(node, "href"), page.url,
                    visible_text(node, 512).value, "link", property(node, "rel"));
                image_links(page, seen, options, node, page.url);
            });
            page.links_truncated |= !complete;
        }
    }
}
}  // namespace

Page extract_page(const std::string& source, const std::string& url,
                  const std::string& content_type, const BrowserOptions& options) {
    detail::initialize_runtime();
    validate(options);
    Page page;
    page.requested_url = page.url = resolve_url(url);
    page.content_type = content_type;
    if (page.url.empty()) throw std::invalid_argument("expected an HTTP/HTTPS URL without credentials");
    const auto fragment_at = page.url.find('#');
    if (fragment_at != std::string::npos) page.fragment = detail::decode(page.url.substr(fragment_at + 1));
    if (source.size() > options.network.max_body_bytes) {
        page.status = "response_too_large"; page.message = "page exceeds body limit"; return page;
    }
    const auto semicolon = content_type.find(';');
    auto mime = lower(content_type.substr(0, semicolon));
    mime = detail::collapse_space(mime);
    const auto image_mime = detail::image_mime(source, mime);
    if (!image_mime.empty()) {
        page.resource_type = "image"; page.image_mime_type = image_mime;
        page.image_data = source; page.total_image_bytes = source.size();
        page.status = source.empty() ? "empty_image" : "ok";
        page.message = source.empty() ? "empty image response" : "image bytes; no decoding or OCR performed";
        page.retained_complete = page.fetch_complete = page.return_complete = !source.empty();
        if (source.empty()) page.incomplete_reasons.push_back("empty_image");
        return page;
    }
    const bool html = mime == "text/html" || mime == "application/xhtml+xml" ||
        (mime.empty() && source.find('<') != std::string::npos);
    if (!html) {
        if (mime != "text/plain" && mime != "text/markdown" && mime != "text/x-markdown" &&
            mime != "application/json" && mime != "application/xml" && mime != "text/xml") {
            page.status = "unsupported_type"; page.message = "supported: HTML, plain text, Markdown, JSON and XML"; return page;
        }
        page.text = detail::truncate_utf8(source, options.max_text_bytes);
        page.text_truncated = page.text.size() < source.size();
        if (mime == "text/plain" || mime == "text/markdown" || mime == "text/x-markdown")
            markdown_links(page, source, options);
    } else {
        std::string charset;
        const auto charset_at = lower(content_type).find("charset=");
        if (charset_at != std::string::npos) {
            charset = content_type.substr(charset_at + 8);
            charset = charset.substr(0, charset.find_first_of("; \t\r\n"));
            if (charset.size() >= 2 && (charset.front() == '"' || charset.front() == '\''))
                charset = charset.substr(1, charset.size() - 2);
        }
        if (charset.empty() && lower(source.substr(0, 4096)).find("charset") == std::string::npos) charset = "UTF-8";
        Document doc(htmlReadMemory(source.data(), static_cast<int>(source.size()), page.url.c_str(),
            charset.empty() ? nullptr : charset.c_str(),
            HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET | HTML_PARSE_COMPACT), xmlFreeDoc);
        if (!doc) { page.status = "parse_error"; page.message = "unable to parse HTML"; return page; }
        auto* root = xmlDocGetRootElement(doc.get());
        xmlNode *body = nullptr, *main = nullptr, *article = nullptr, *focus = nullptr;
        std::string base = page.url;
        bool base_found = false;
        elements(root, [&](xmlNode* node) {
            if (!body && named(node, "body")) body = node;
            if (!main && (named(node, "main") || property(node, "role") == "main")) main = node;
            if (!article && named(node, "article")) article = node;
            if (!focus && !page.fragment.empty() && (property(node, "id") == page.fragment ||
                (named(node, "a") && property(node, "name") == page.fragment))) focus = node;
            if (page.title.empty() && named(node, "title"))
                page.title = detail::collapse_space(visible_text(node, 512).value);
            if (!base_found && named(node, "base")) {
                const auto resolved = resolve_url(property(node, "href"), page.url);
                if (!resolved.empty()) { base = resolved; base_found = true; }
            }
        });
        if (focus) {
            for (auto* parent = focus->parent; parent && parent != body; parent = parent->parent) {
                if (named(parent, "h1") || named(parent, "h2") || named(parent, "h3") ||
                    named(parent, "h4") || named(parent, "h5") || named(parent, "h6")) { focus = parent; break; }
            }
            page.fragment_found = true;
        }
        auto text = visible_text(focus ? focus : (main ? main : (article ? article : (body ? body : root))),
                                 options.max_text_bytes, focus != nullptr);
        page.text = std::move(text.value); page.text_truncated = text.truncated;
        page.deleted_ranges = std::move(text.deleted_ranges);
        std::unordered_set<std::string> seen;
        auto collect = [&](xmlNode* node) {
            image_links(page, seen, options, node, base);
            if (!named(node, "a") && !named(node, "area") && !named(node, "iframe")) return;
            auto label = detail::collapse_space(visible_text(node, 512).value);
            if (label.empty()) label = detail::truncate_utf8(property(node, "aria-label"), 512);
            if (label.empty()) label = detail::truncate_utf8(property(node, "title"), 512);
            add_link(page, seen, options, property(node, named(node, "iframe") ? "src" : "href"),
                base, label, named(node, "iframe") ? "frame" : "link", property(node, "rel"));
        };
        bool complete = true;
        if (main || article) complete &= elements(main ? main : article, collect);
        complete &= elements(body ? body : root, collect);
        page.links_truncated |= !complete;
        if (detail::is_challenge(page.title, page.text, source, root)) {
            page.status = "challenge"; page.message = "page requires browser or human verification"; return page;
        }
        static constexpr char script_tag[] = "<script";
        const bool scripted_shell = page.links.size() >= 32 && page.text.size() < 8192 &&
            std::search(source.begin(), source.end(), script_tag, script_tag + 7,
                [](unsigned char a, unsigned char b) { return (a >= 'A' && a <= 'Z' ? a + 32 : a) == b; }) != source.end();
        if (scripted_shell) {
            bool bindings = false;
            const auto prose = visible_text(body ? body : root, 1024, false, true).value;
            if (prose.find("{{") != std::string::npos && prose.find("}}") != std::string::npos) {
                elements(body ? body : root, [&](xmlNode* node) {
                    if (node->type == XML_ELEMENT_NODE && (xmlHasProp(node, BAD_CAST "v-for") || xmlHasProp(node, BAD_CAST "v-if") ||
                        xmlHasProp(node, BAD_CAST "v-cloak") || xmlHasProp(node, BAD_CAST "ng-repeat"))) bindings = true;
                });
            }
            if ((!main && !article && (prose.size() < 128 || (bindings && prose.size() < 1024))) ||
                ((main || article) && page.text.empty())) {
                page.status = "navigation_only";
                page.message = "only navigation/controls were readable; main content may require scripts";
                page.total_text_bytes = page.text.size(); page.total_links = page.links.size();
                page.retained_complete = !page.text_truncated && !page.links_truncated;
                return page;
            }
        }
        // Follow only unambiguous, immediate, literal transitions on empty pages.
        if (page.text.size() < 128 && page.links.empty()) {
            std::unordered_set<std::string> destinations;
            bool ambiguous = false, invalid = false;
            auto trim = [](std::string value) {
                const auto begin = value.find_first_not_of(" \t\r\n");
                return begin == std::string::npos ? std::string{} : value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
            };
            auto quoted = [&](std::string value) {
                value = trim(value);
                if (value.size() < 2 || (value.front() != '\'' && value.front() != '"') || value.back() != value.front()) return std::string{};
                value = value.substr(1, value.size() - 2);
                if (value.find_first_of("\\\"'") != std::string::npos) return std::string{};
                return value;
            };
            elements(root, [&](xmlNode* node) {
                std::string target;
                if (named(node, "meta") && lower(property(node, "http-equiv")) == "refresh") {
                    const auto content = property(node, "content"); const auto semi = content.find(';');
                    if (semi != std::string::npos) {
                        const auto delay = trim(content.substr(0, semi)); auto rest = trim(content.substr(semi + 1));
                        const bool zero = !delay.empty() && delay[0] == '0' && std::all_of(delay.begin(), delay.end(), [](char c) { return c == '0' || c == '.'; }) && std::count(delay.begin(), delay.end(), '.') <= 1;
                        if (zero && lower(rest).compare(0, 3, "url") == 0) {
                            rest = trim(rest.substr(3));
                            if (!rest.empty() && rest.front() == '=') {
                                target = trim(rest.substr(1));
                                if (!target.empty() && (target.front() == '\'' || target.front() == '"')) target = quoted(target);
                            }
                        }
                    }
                } else if (named(node, "script")) {
                    if (!property(node, "src").empty()) { ambiguous = true; return; }
                    if (!node->children || !node->children->content) return;
                    const auto* raw = reinterpret_cast<const char*>(node->children->content);
                    if (xmlStrlen(node->children->content) > 2048) { ambiguous = true; return; }
                    auto code = trim(raw);
                    if (code.empty()) return;
                    if (code.back() == ';') code = trim(code.substr(0, code.size() - 1));
                    if (code.compare(0, 7, "window.") == 0) code.erase(0, 7);
                    if (code.compare(0, 8, "location") == 0) {
                        auto rest = trim(code.substr(8));
                        if (rest.compare(0, 5, ".href") == 0) rest = trim(rest.substr(5));
                        if (!rest.empty() && rest[0] == '=') target = quoted(rest.substr(1));
                        else {
                            for (const auto* method : {".replace", ".assign"}) {
                                const std::string prefix(method);
                                if (rest.compare(0, prefix.size(), prefix) != 0) continue;
                                auto argument = trim(rest.substr(prefix.size()));
                                if (argument.size() > 2 && argument.front() == '(' && argument.back() == ')') target = quoted(argument.substr(1, argument.size() - 2));
                            }
                        }
                    }
                    if (target.empty()) ambiguous = true;
                }
                if (!target.empty()) {
                    const auto resolved = resolve_url(target, page.url);
                    if (!resolved.empty()) destinations.insert(resolved); else { ambiguous = true; invalid = true; }
                }
            }, 200000, true);
            if (invalid && destinations.empty()) {
                page.status = "invalid_redirect"; page.message = "unsupported static redirect URL"; return page;
            }
            if (!ambiguous && destinations.size() == 1) page.static_redirect = *destinations.begin();
        }
    }
    page.status = page.text.empty() && page.links.empty() ? "empty" : "ok";
    if (page.status == "empty") page.message = "no readable text or HTTP links; JavaScript is not executed";
    if (html && page.static_redirect.empty() && page.links.empty() && page.text.size() < 512 &&
        lower(source).find("<script") != std::string::npos) {
        page.status = "script_required";
        page.message = "script-dependent page without sufficient readable content";
    }
    page.total_text_bytes = page.text.size(); page.total_links = page.links.size();
    page.retained_complete = !page.text_truncated && !page.links_truncated;
    page.fetch_complete = true; page.return_complete = true;
    if (page.text_truncated) page.incomplete_reasons.push_back("retained_text_limit");
    if (page.links_truncated) page.incomplete_reasons.push_back("retained_link_limit");
    return page;
}

struct Browser::Impl {
    BrowserOptions options;
    Searcher searcher;
    detail::HttpSession session;
    struct CachedPage { Page page; detail::LineIndex index; };
    std::deque<CachedPage> pages;
    std::deque<std::uint64_t> history;
    std::map<std::string, std::string> sessions;
    std::deque<std::string> session_order;
    detail::HttpSession alternate;
    std::map<std::string, detail::SearchRouteState> routes;
    std::deque<std::string> route_order;
    std::uint64_t next_id = 1;
    std::size_t cached_bytes = 0;
    static std::size_t weight(const Page& page) {
        std::size_t bytes = sizeof(Page) + page.text.size() + page.image_data.size() + page.image_mime_type.size() + page.resource_type.size() + page.link_bytes + page.title.size() + page.url.size() + page.requested_url.size() + page.message.size() + page.fragment.size() + page.content_type.size();
        bytes += page.deleted_ranges.size() * sizeof(DeletedRange) + page.evicted_page_ids.size() * sizeof(std::uint64_t);
        bytes += page.network_attempts.size() * sizeof(EngineAttempt) + page.failure_stage.size();
        for (const auto& a : page.network_attempts) bytes += a.variant.size() + a.resolver.size() + a.status.size() + a.failure_stage.size();
        for (const auto& redirect : page.redirects) bytes += sizeof(std::string) + redirect.size();
        return bytes;
    }
    const detail::LineIndex& index(std::uint64_t id) const {
        for (const auto& entry : pages) if (entry.page.id == id) return entry.index;
        throw std::invalid_argument("page index is not retained");
    }
    static BrowserOptions checked(BrowserOptions value) { validate(value); return value; }
    explicit Impl(BrowserOptions value) : options(checked(std::move(value))), searcher(options.network) {}
};

Browser::Browser(BrowserOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
Browser::~Browser() = default;
Browser::Browser(Browser&&) noexcept = default;
Browser& Browser::operator=(Browser&&) noexcept = default;
Response Browser::search(const std::string& query) {
    if (!impl_) throw std::logic_error("browser has been moved");
    return impl_->searcher.search(query);
}
Response Browser::search(const std::string& query, bool full, long timeout_ms, std::size_t per_engine) {
    if (!impl_) throw std::logic_error("browser has been moved");
    if (per_engine > impl_->options.request_per_engine_limit) throw std::invalid_argument("per_engine exceeds configured limit");
    return impl_->searcher.search(query, full, timeout_ms, per_engine);
}
const Page& Browser::page(std::uint64_t id) const {
    if (!impl_) throw std::logic_error("browser has been moved");
    if (!id && !impl_->history.empty()) id = impl_->history.back();
    for (const auto& entry : impl_->pages) if (entry.page.id == id) return entry.page;
    throw std::invalid_argument("page ID " + std::to_string(id) + " does not exist, was not cached or was evicted");
}
Page Browser::open(const std::string& value, long timeout_ms) {
    return open_impl(value, timeout_ms, {});
}
Page Browser::open_impl(const std::string& value, long timeout_ms, const std::string& image_source, bool explicit_image) {
    if (!impl_) throw std::logic_error("browser has been moved");
    if (timeout_ms && (timeout_ms < 100 || timeout_ms > impl_->options.network.timeout_ms)) throw std::invalid_argument("timeout_ms exceeds configured browser budget");
    const auto budget = timeout_ms ? timeout_ms : impl_->options.network.timeout_ms;
    const auto start = std::chrono::steady_clock::now();
    auto url = resolve_url(value);
    if (url.empty()) throw std::invalid_argument("expected an HTTP/HTTPS URL without credentials, max 8192 bytes");
    Page result;
    result.requested_url = value; result.url = url;
    std::unordered_set<std::string> visited;
    std::size_t api_calls = 0;
    auto routed_fetch = [&](detail::Request request, const Options& settings) {
        if (result.network_attempt_count >= 64) {
            detail::HttpResponse limited; limited.status = "request_limit"; limited.message = "64 browser network attempts exhausted";
            return limited;
        }
        const auto route_key = origin(request.url);
        if (!impl_->routes.count(route_key) && impl_->routes.size() >= 16) {
            impl_->routes.erase(impl_->route_order.front()); impl_->route_order.pop_front();
        }
        impl_->routes.try_emplace(route_key);
        impl_->route_order.erase(std::remove(impl_->route_order.begin(), impl_->route_order.end(), route_key), impl_->route_order.end());
        impl_->route_order.push_back(route_key);
        auto& state = impl_->routes.at(route_key);
        if (request.session_key.empty()) {
            if (!impl_->sessions.count(route_key)) {
                auto key = "browser-" + std::to_string(impl_->sessions.size() + 1);
                if (impl_->sessions.size() >= 8) {
                    const auto old = impl_->session_order.front(); impl_->session_order.pop_front();
                    key = impl_->sessions.at(old); impl_->sessions.erase(old);
                    impl_->session.forget_session(key); impl_->alternate.forget_session(key);
                }
                impl_->sessions.emplace(route_key, std::move(key));
            }
            request.session_key = impl_->sessions.at(route_key);
        }
        impl_->session_order.erase(std::remove(impl_->session_order.begin(), impl_->session_order.end(), route_key), impl_->session_order.end());
        impl_->session_order.push_back(route_key);
        auto response = detail::fetch_routed(request, settings, impl_->session, impl_->alternate, state, {},
            detail::CooldownTable::Clock::now, 64 - result.network_attempt_count);
        result.network_attempt_count += response.requested_count;
        result.resolver_fallback_used |= response.alternate_used;
        result.network_attempts = std::move(response.attempts);
        result.failure_stage = detail::failure_stage(response.response);
        return std::move(response.response);
    };
    const auto repo_fetch = [&](const std::string& repo_url) { return detail::repository_page(repo_url, impl_->options, [&](const std::string& target) {
        detail::HttpResponse http;
        auto api_url = target;
        for (std::size_t hop = 0; hop <= 3; ++hop) {
            const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (spent >= budget || ++api_calls > 64) {
                http.status = "timeout"; http.message = "shared repository request budget exhausted"; return http;
            }
            auto options = impl_->options.network; options.timeout_ms = budget - static_cast<long>(spent);
            detail::Request request; request.url = api_url; request.follow_redirects = false;
            request.public_network_only = !impl_->options.allow_private_network;
            request.headers = {"Accept: application/json"};
            request.variant = "repository";
            http = routed_fetch(std::move(request), options);
            if (http.transport_code == 0 && (http.http_status == 301 || http.http_status == 302 || http.http_status == 307 || http.http_status == 308)) {
                const auto next = resolve_url(http.location, api_url);
                if (next.empty() || hop == 3) { http.status = "invalid_redirect"; http.message = "invalid repository redirect"; return http; }
                api_url = next; continue;
            }
            return http;
        }
        return http;
    }); };
    auto repository = repo_fetch(url);
    if (repository) {
        auto attempts = std::move(result.network_attempts); auto stage = std::move(result.failure_stage);
        const auto count = result.network_attempt_count; const auto fallback = result.resolver_fallback_used;
        result = std::move(*repository); result.network_attempts = std::move(attempts); result.failure_stage = std::move(stage);
        result.network_attempt_count = count; result.resolver_fallback_used = fallback;
    }
    for (std::size_t hop = 0; !repository; ++hop) {
        if (hop > 0) {
            if (auto adapted = repo_fetch(url)) {
                auto redirects = std::move(result.redirects); auto attempts = std::move(result.network_attempts); auto stage = std::move(result.failure_stage);
                const auto count = result.network_attempt_count; const auto fallback = result.resolver_fallback_used;
                result = std::move(*adapted); result.redirects = std::move(redirects); result.network_attempts = std::move(attempts);
                result.failure_stage = std::move(stage); result.network_attempt_count = count; result.resolver_fallback_used = fallback; break;
            }
        }
        if (!visited.insert(url).second) { result.status = "redirect_loop"; result.message = "redirect loop"; break; }
        auto options = impl_->options.network; options.timeout_ms = budget;
        const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (spent >= options.timeout_ms) { result.status = "timeout"; result.message = "page network deadline exceeded"; break; }
        detail::Request request;
        request.url = url; request.follow_redirects = false;
        request.timeout_ms = options.timeout_ms - static_cast<long>(spent);
        request.public_network_only = !impl_->options.allow_private_network;
        request.variant = image_source.empty() ? "page" : "image";
        const bool image_request = explicit_image || !image_source.empty() || !detail::image_url_mime(url).empty();
        request.headers = {image_request ? "Accept: image/avif,image/webp,image/png,image/jpeg,image/*;q=0.9,*/*;q=0.5" : "Accept: text/html,text/plain,text/markdown,application/json;q=0.9,*/*;q=0.5"};
        if (!image_source.empty()) {
            const auto referrer = detail::image_referrer(image_source, url);
            if (!referrer.empty()) request.headers.push_back("Referer: " + referrer);
        }
        detail::HttpResponse http;
        http = routed_fetch(std::move(request), options);
        result.url = url; result.http_status = http.http_status; result.transport_code = http.transport_code;
        result.network = http.network; result.content_type = http.content_type;
        const bool redirect = http.http_status == 301 || http.http_status == 302 || http.http_status == 303 ||
            http.http_status == 307 || http.http_status == 308;
        if (redirect && http.transport_code == 0) {
            if (hop >= 3) { result.status = "redirect_limit"; result.message = "more than 3 redirects"; break; }
            auto next = resolve_url(http.location, url);
            if (next.empty()) { result.status = "invalid_redirect"; result.message = "missing or unsupported redirect URL"; break; }
            const auto fragment = url.find('#');
            if (http.location.find('#') == std::string::npos && next.find('#') == std::string::npos && fragment != std::string::npos)
                next += url.substr(fragment);
            result.redirects.push_back(next); url = next; continue;
        }
        result.status = http.status; result.message = http.message;
        if (http.status == "ok" || http.status == "http_error") {
            auto parsed = extract_page(http.body, url, http.content_type, impl_->options);
            if (!parsed.static_redirect.empty() && http.status == "ok") {
                if (hop >= 3) { result.status = "redirect_limit"; result.message = "more than 3 redirects"; break; }
                result.redirects.push_back(parsed.static_redirect); url = parsed.static_redirect; continue;
            }
            result.retained_complete = parsed.retained_complete;
            result.incomplete_reasons = std::move(parsed.incomplete_reasons);
            result.link_bytes = parsed.link_bytes;
            result.total_text_bytes = parsed.text.size(); result.total_links = parsed.links.size();
            result.title = std::move(parsed.title); result.text = std::move(parsed.text);
            result.resource_type = std::move(parsed.resource_type); result.image_mime_type = std::move(parsed.image_mime_type);
            if (http.status == "ok") {
                result.image_data = std::move(parsed.image_data); result.total_image_bytes = parsed.total_image_bytes;
            }
            result.deleted_ranges = std::move(parsed.deleted_ranges);
            result.links = std::move(parsed.links);
            result.fragment = std::move(parsed.fragment); result.fragment_found = parsed.fragment_found;
            result.text_truncated = parsed.text_truncated; result.links_truncated = parsed.links_truncated;
            if (http.status == "ok" || parsed.status == "challenge") {
                result.status = parsed.status; result.message = std::move(parsed.message);
            }
        }
        break;
    }
    result.requested_url = value;
    if (result.failure_stage.empty()) {
        if (result.status == "challenge") result.failure_stage = "challenge";
        else if (result.status == "parse_error" || result.status == "repository_unavailable") result.failure_stage = "parse";
    }
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (impl_->next_id == std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("page ID space exhausted");
    result.id = impl_->next_id++;
    result.fetch_complete = result.transport_code == 0 && result.http_status >= 200 && result.http_status < 300 &&
        (result.status == "ok" || result.status == "empty");
    if (!result.fetch_complete) result.incomplete_reasons.push_back(result.status);
    // Keep current failure diagnostics when there is room, but do not discard useful
    // research just to retain an error. Previous failures are always reclaimed first.
    auto useful = [](const Page& page) { return page.status == "ok" || page.status == "empty" || page.status == "navigation_only"; };
    const auto headroom = impl_->options.max_pages * sizeof(std::uint64_t);
    detail::LineIndex index(result.text);
    const auto estimate = Impl::weight(result) + index.bytes() + headroom;
    if (estimate > impl_->options.cache_bytes) return result;
    while (impl_->pages.size() >= impl_->options.max_pages || impl_->cached_bytes > impl_->options.cache_bytes - estimate) {
        auto victim = std::find_if(impl_->pages.begin(), impl_->pages.end(), [&](const Impl::CachedPage& entry) { return !useful(entry.page); });
        if (victim == impl_->pages.end()) {
            if (!useful(result)) return result;
            victim = impl_->pages.begin();
        }
        if (victim == impl_->pages.end()) break;
        const auto evicted = victim->page.id; impl_->cached_bytes -= Impl::weight(victim->page) + victim->index.bytes(); impl_->pages.erase(victim);
        impl_->history.erase(std::remove(impl_->history.begin(), impl_->history.end(), evicted), impl_->history.end());
        result.evicted_page_ids.push_back(evicted);
    }
    result.cache_retained = true;
    impl_->cached_bytes += Impl::weight(result) + index.bytes(); impl_->pages.push_back({result, std::move(index)}); impl_->history.push_back(result.id);
    return result;
}
Page Browser::click(std::uint64_t page_id, std::size_t link_id, long timeout_ms) {
    const auto& current = page(page_id);
    for (const auto& link : current.links) if (link.id == link_id) {
        const auto url = link.url;
        const auto rel = " " + lower(detail::collapse_space(link.rel)) + " ";
        const auto source = link.kind == "image" && rel.find(" noreferrer ") == std::string::npos ? current.url : std::string{};
        return open_impl(url, timeout_ms, source, link.kind == "image");
    }
    throw std::invalid_argument("link ID does not exist on this page");
}
std::size_t Browser::line_offset(std::uint64_t id, std::size_t line) const {
    if (!line) throw std::invalid_argument("line must be at least one");
    const auto& current = page(id);
    if (current.resource_type == "image") throw std::invalid_argument("image has no text; inspect image_data");
    return impl_->index(current.id).offset(current.text, line);
}
PageText Browser::read(std::uint64_t id, std::size_t start, std::size_t count) const {
    if (start < 1 || count < 1 || count > 500) throw std::invalid_argument("read start_line >=1 and lines 1..500");
    const auto& current = page(id);
    if (current.resource_type == "image") throw std::invalid_argument("image has no text; inspect image_data");
    const auto& index = impl_->index(current.id);
    PageText result; result.page_id = current.id; result.start_line = start;
    result.total_lines = index.total_lines; result.source_truncated = current.text_truncated;
    const auto offset = index.offset(current.text, start);
    const auto end_line = start > index.total_lines ? index.total_lines : start - 1 + std::min(count, index.total_lines - start + 1);
    auto end = index.offset(current.text, end_line + 1);
    if (end > offset && current.text[end - 1] == '\n') --end;
    result.text = current.text.substr(offset, end - offset);
    result.end_line = start <= index.total_lines ? end_line : 0; result.more = end_line < index.total_lines;
    result.offset_bytes = offset; result.next_offset_bytes = end;
    result.starts_deleted = detail::inside_deleted(current, offset); result.ends_deleted = detail::inside_deleted(current, end);
    result.deletion_markers = !current.deleted_ranges.empty(); return result;
}
std::vector<PageMatch> Browser::find(std::uint64_t id, const std::string& needle, std::size_t maximum, std::size_t start_line, bool full_lines) const {
    if (needle.empty() || needle.size() > 1024 || maximum < 1 || maximum > 2097152 || start_line < 1)
        throw std::invalid_argument("find text must be 1..1024 bytes; max_matches 1..2097152");
    std::vector<PageMatch> result;
    const auto& current = page(id);
    if (current.resource_type == "image") throw std::invalid_argument("image has no text; inspect image_data");
    const auto& source = current.text;
    const auto& index = impl_->index(current.id);
    std::size_t offset = index.offset(source, start_line), line = start_line;
    while (offset < source.size() && result.size() < maximum) {
        const auto next = line == index.total_lines ? (source.back() == '\n' ? source.size() - 1 : std::string::npos) : source.find('\n', offset);
        const auto end = next == std::string::npos ? source.size() : next;
        const auto part = std::string_view(source).substr(offset, end - offset);
        const auto found = part.find(needle);
        if (found != std::string_view::npos) {
            auto begin = !full_lines && found > 256 ? found - 256 : 0;
            while (begin < part.size() && (static_cast<unsigned char>(part[begin]) & 192U) == 128U) ++begin;
            const auto snippet = full_lines ? part : detail::utf8_prefix(part.substr(begin), 1536);
            const auto at = offset + begin;
            result.push_back({line, std::string(snippet), at, !full_lines && (begin > 0 || snippet.size() < part.size()),
                              detail::inside_deleted(current, at), detail::inside_deleted(current, at + snippet.size())});
        }
        if (next == std::string::npos) break;
        offset = next + 1; ++line;
    }
    return result;
}
PageText Browser::read_bytes(std::uint64_t id, std::size_t offset, std::size_t maximum) const {
    const auto& current = page(id);
    if (current.resource_type == "image") throw std::invalid_argument("image has no text; inspect image_data");
    if (!maximum || maximum > 8 * 1024 * 1024 || offset > current.text.size()) throw std::invalid_argument("invalid byte range");
    if (offset < current.text.size() && (static_cast<unsigned char>(current.text[offset]) & 192U) == 128U)
        throw std::invalid_argument("offset_bytes must be a UTF-8 boundary");
    const auto part = detail::utf8_prefix(std::string_view(current.text).substr(offset), maximum);
    if (part.empty() && offset < current.text.size()) throw std::invalid_argument("max_bytes cannot hold next UTF-8 character");
    const auto end = offset + part.size();
    PageText result; result.page_id = current.id; result.offset_bytes = offset; result.next_offset_bytes = end;
    result.text = std::string(part); result.more = end < current.text.size();
    result.source_truncated = current.text_truncated;
    result.starts_deleted = detail::inside_deleted(current, offset); result.ends_deleted = detail::inside_deleted(current, end);
    result.deletion_markers = !current.deleted_ranges.empty();
    const auto& index = impl_->index(current.id);
    result.start_line = index.line_at(current.text, offset);
    result.end_line = result.start_line + static_cast<std::size_t>(std::count(part.begin(), part.end(), '\n'));
    result.total_lines = index.total_lines; return result;
}
Page Browser::back() {
    if (!impl_) throw std::logic_error("browser has been moved");
    if (impl_->history.size() < 2) throw std::invalid_argument("no earlier page in retained history");
    impl_->history.pop_back();
    return page();
}
void Browser::clear_session() {
    if (!impl_) throw std::logic_error("browser has been moved");
    impl_->searcher.clear_session(); impl_->session.reset(); impl_->alternate.reset();
    impl_->pages.clear(); impl_->history.clear(); impl_->sessions.clear(); impl_->session_order.clear();
    impl_->routes.clear(); impl_->route_order.clear(); impl_->cached_bytes = 0;
    // IDs are never reused, including across explicit clear_session calls.
}

std::string to_json(const Page& page) { return detail::page_view_json(detail::PageView(page)); }
std::string detail::page_view_json(const detail::PageView& view) {
    const auto& page = view.page;
    using detail::json_quote;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"schema_version\":1,\"page_id\":" << page.id
        << ",\"status\":" << json_quote(page.status) << ",\"message\":" << json_quote(page.message)
        << ",\"requested_url\":" << json_quote(page.requested_url) << ",\"url\":" << json_quote(page.url)
        << ",\"title\":" << json_quote(page.title) << ",\"content_type\":" << json_quote(page.content_type)
        << ",\"resource_type\":" << json_quote(page.resource_type)
        << ",\"http_status\":" << page.http_status << ",\"transport_code\":" << page.transport_code
        << ",\"elapsed_ms\":" << page.elapsed_ms << ",\"text\":" << json_quote(view.text)
        << ",\"network_attempt_count\":" << page.network_attempt_count
        << ",\"resolver_fallback_used\":" << (page.resolver_fallback_used ? "true" : "false")
        << ",\"failure_stage\":" << json_quote(page.failure_stage) << ",\"network_attempts\":[";
    for (std::size_t n = 0; n < page.network_attempts.size(); ++n) {
        const auto& a = page.network_attempts[n];
        if (n) out << ',';
        out << "{\"resolver\":" << json_quote(a.resolver) << ",\"route\":" << json_quote(a.variant) << ",\"status\":" << json_quote(a.status)
            << ",\"failure_stage\":" << json_quote(a.failure_stage) << ",\"http_status\":" << a.http_status
            << ",\"transport_code\":" << a.transport_code << ",\"requested\":" << (a.requested ? "true" : "false")
            << ",\"elapsed_ms\":" << a.elapsed_ms << '}';
    }
    out << ']'
        << ",\"fetch_complete\":" << (page.fetch_complete ? "true" : "false")
        << ",\"retained_complete\":" << (page.retained_complete ? "true" : "false")
        << ",\"return_complete\":" << (view.return_complete ? "true" : "false")
        << ",\"incomplete_reasons\":" << detail::strings(view.reasons)
        << ",\"total_text_bytes\":" << page.total_text_bytes << ",\"total_links\":" << page.total_links
        << ",\"next_offset_bytes\":" << view.next_offset_bytes << ",\"next_link_offset\":" << view.next_link_offset
        << ",\"text_format\":" << json_quote(page.deleted_ranges.empty() ? "plain" : "plain_with_deletion_markers")
        << ",\"starts_deleted\":" << (view.starts_deleted ? "true" : "false") << ",\"ends_deleted\":" << (view.ends_deleted ? "true" : "false")
        << ",\"cache_retained\":" << (page.cache_retained ? "true" : "false")
        << ",\"evicted_page_ids\":[";
    for (std::size_t i = 0; i < page.evicted_page_ids.size(); ++i) { if (i) out << ','; out << page.evicted_page_ids[i]; }
    out << "],\"repository_commit\":" << json_quote(page.repository_commit)
        << ",\"fragment\":" << json_quote(page.fragment) << ",\"fragment_found\":" << (page.fragment_found ? "true" : "false")
        << ",\"text_truncated\":" << (view.text_truncated ? "true" : "false")
        << ",\"links_truncated\":" << (view.links_truncated ? "true" : "false") << ",\"links\":[";
    for (std::size_t i = 0; i < view.link_count; ++i) {
        if (i) out << ',';
        const auto& link = page.links[view.link_begin + i];
        out << "{\"id\":" << link.id << ",\"text\":" << json_quote(link.text) << ",\"url\":" << json_quote(link.url)
            << ",\"kind\":" << json_quote(link.kind) << ",\"rel\":" << json_quote(link.rel)
            << ",\"external\":" << (link.external ? "true" : "false") << '}';
    }
    out << "],\"redirects\":[";
    for (std::size_t i = 0; i < page.redirects.size(); ++i) { if (i) out << ','; out << json_quote(page.redirects[i]); }
    out << "],\"image\":";
    if (page.resource_type != "image") out << "null";
    else {
        const auto begin = page.image_offset_bytes + view.image_begin;
        const auto next = begin + view.image_length;
        out << "{\"type\":\"image\",\"mime_type\":" << json_quote(page.image_mime_type)
            << ",\"encoding\":\"base64\",\"data\":\"";
        detail::write_base64(out, std::string_view(page.image_data).substr(view.image_begin, view.image_length));
        out << "\",\"total_bytes\":" << page.total_image_bytes << ",\"offset_bytes\":" << begin
            << ",\"next_offset_bytes\":" << next << ",\"more\":" << (next < page.total_image_bytes ? "true" : "false")
            << ",\"complete\":" << (page.status == "ok" && page.fetch_complete && page.retained_complete &&
                begin == 0 && view.image_length && next == page.total_image_bytes ? "true" : "false")
            << ",\"data_included\":" << (view.image_length ? "true" : "false") << '}';
    }
    out << ",\"javascript_executed\":false,\"content_trust\":\"untrusted_web_content\"}";
    return out.str();
}
std::string to_json(const PageText& text) {
    return "{\"schema_version\":1,\"page_id\":" + std::to_string(text.page_id) +
        ",\"start_line\":" + std::to_string(text.start_line) + ",\"end_line\":" + std::to_string(text.end_line) +
        ",\"total_lines\":" + std::to_string(text.total_lines) + ",\"text\":" + detail::json_quote(text.text) +
        ",\"text_format\":" + detail::json_quote(text.deletion_markers ? "plain_with_deletion_markers" : "plain") +
        ",\"starts_deleted\":" + (text.starts_deleted ? "true" : "false") + ",\"ends_deleted\":" + (text.ends_deleted ? "true" : "false") +
        ",\"more\":" + (text.more ? "true" : "false") +
        ",\"return_complete\":" + (text.more ? "false" : "true") + ",\"retained_complete\":" + (text.source_truncated ? "false" : "true") +
        ",\"offset_bytes\":" + std::to_string(text.offset_bytes) + ",\"next_offset_bytes\":" + std::to_string(text.next_offset_bytes) +
        ",\"source_truncated\":" + (text.source_truncated ? "true" : "false") + '}';
}
}  // namespace edgesearch
