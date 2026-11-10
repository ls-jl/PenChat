#include "internal.hpp"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <memory>
#include <stdexcept>
#include <unordered_set>

namespace edgesearch::detail {
ParsedPage parse_rss(const std::string& source, std::size_t limit) {
    ParsedPage page;
    page.status = "parse_error";
    if (source.size() > 8 * 1024 * 1024 || !limit || limit > 100) throw std::invalid_argument("RSS body or result limit out of range");
    if (source.find("<!DOCTYPE") != std::string::npos || source.find("<!ENTITY") != std::string::npos) {
        page.message = "RSS DTD/entities are not supported"; return page;
    }
    using Doc = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;
    Doc doc(xmlReadMemory(source.data(), static_cast<int>(source.size()), nullptr, "UTF-8",
        XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_COMPACT), xmlFreeDoc);
    if (!doc || doc->intSubset || doc->extSubset) { page.message = "invalid RSS response"; return page; }
    auto named = [](xmlNode* n, const char* name) { return n && n->type == XML_ELEMENT_NODE && xmlStrEqual(n->name, BAD_CAST name); };
    auto* root = xmlDocGetRootElement(doc.get());
    if (!named(root, "rss")) { page.message = "expected RSS root"; return page; }
    xmlNode* channel = nullptr;
    for (auto* n = root->children; n; n = n->next) if (named(n, "channel")) { channel = n; break; }
    if (!channel) { page.message = "missing RSS channel"; return page; }
    auto child_text = [&](xmlNode* parent, const char* key, std::size_t cap) {
        for (auto* n = parent->children; n; n = n->next) if (named(n, key)) {
            std::unique_ptr<xmlChar, decltype(xmlFree)> text(xmlNodeGetContent(n), xmlFree);
            return text ? truncate_utf8(collapse_space(reinterpret_cast<const char*>(text.get())), cap) : std::string{};
        }
        return std::string{};
    };
    std::unordered_set<std::string> seen;
    for (auto* n = channel->children; n && page.results.size() < limit; n = n->next) {
        if (!named(n, "item")) continue;
        ++page.candidates_seen;
        const auto raw = child_text(n, "link", 8192);
        bool paid = false;
        for (auto* tag = n->children; tag; tag = tag->next) if (named(tag, "category")) {
            std::unique_ptr<xmlChar, decltype(xmlFree)> value(xmlNodeGetContent(tag), xmlFree);
            auto category = value ? collapse_space(reinterpret_cast<const char*>(value.get())) : std::string{};
            for (auto& c : category) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
            paid |= category == "sponsored" || category == "advertisement";
        }
        if (paid || raw.find("bing.com/aclick?") != std::string::npos) { ++page.ads_filtered; continue; }
        const auto url = clean_url("bing_cn", raw, "https://cn.bing.com/search");
        const auto title = child_text(n, "title", 512);
        if (url.empty() || title.empty() || !seen.insert(url).second) continue;
        Result result; result.title = title; result.url = url;
        result.content = child_text(n, "description", 2048); result.engines = {"bing_cn"}; result.providers = {"bing"};
        page.results.push_back(std::move(result));
    }
    page.status = page.results.empty() ? "empty" : "ok";
    return page;
}
} // namespace edgesearch::detail
