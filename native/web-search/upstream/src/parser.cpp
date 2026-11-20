#include "internal.hpp"

#include <libxml/HTMLparser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

namespace edgesearch {
namespace {
using Document = std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)>;
using Context = std::unique_ptr<xmlXPathContext, decltype(&xmlXPathFreeContext)>;
using XPath = std::unique_ptr<xmlXPathObject, decltype(&xmlXPathFreeObject)>;

XPath evaluate(xmlXPathContext* context, xmlNode* node, const char* expression) {
    context->node = node;
    XPath result(xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(expression), context), xmlXPathFreeObject);
    if (!result) throw std::runtime_error("invalid engine XPath");
    return result;
}

std::string text(xmlNode* node, std::size_t limit) {
    std::string result;
    auto separate = [&](xmlNode* element) {
        if (!element || element->type != XML_ELEMENT_NODE || !element->name) return;
        const std::string name = reinterpret_cast<const char*>(element->name);
        if (name == "div" || name == "p" || name == "li" || name == "br" || name == "td" || name == "tr" ||
            name == "h1" || name == "h2" || name == "h3") {
            if (!result.empty() && result.size() < limit && result.back() != ' ') result += ' ';
        }
    };
    // Iterative traversal avoids recursive stack growth on remote HTML.
    xmlNode* current = node;
    while (current && result.size() < limit) {
        const std::string name = current->name ? reinterpret_cast<const char*>(current->name) : "";
        const bool skip = current->type == XML_ELEMENT_NODE && (name == "script" || name == "style" || name == "noscript");
        separate(current);
        if ((current->type == XML_TEXT_NODE || current->type == XML_CDATA_SECTION_NODE) && current->content) {
            const auto* content = reinterpret_cast<const char*>(current->content);
            result.append(content, std::min(limit - result.size(), static_cast<std::size_t>(xmlStrlen(current->content))));
        }
        if (!skip && current->children) { current = current->children; continue; }
        while (current != node && !current->next) { separate(current); current = current->parent; }
        separate(current);
        if (current == node) break;
        current = current->next;
    }
    return detail::truncate_utf8(detail::collapse_space(result), limit);
}

std::string xpath_text(xmlXPathContext* context, xmlNode* node, const char* expression, std::size_t limit) {
    const auto result = evaluate(context, node, expression);
    if (!result->nodesetval) return {};
    std::string value;
    std::unordered_set<xmlNode*> selected;
    for (int i = 0; i < result->nodesetval->nodeNr && value.size() < limit; ++i) {
        auto* candidate = result->nodesetval->nodeTab[i];
        bool nested = false;
        for (auto* parent = candidate->parent; parent; parent = parent->parent) {
            if (selected.count(parent)) { nested = true; break; }
        }
        if (nested) continue;
        selected.insert(candidate);
        if (!value.empty()) value += ' ';
        value += text(candidate, limit - value.size());
    }
    return detail::truncate_utf8(detail::collapse_space(value), limit);
}

std::string attribute(xmlNode* node, const char* name) {
    xmlChar* raw = xmlGetProp(node, reinterpret_cast<const xmlChar*>(name));
    if (!raw) return {};
    std::string result(reinterpret_cast<const char*>(raw));
    xmlFree(raw);
    return result;
}

bool has(const std::string& value, const std::string& needle) { return value.find(needle) != std::string::npos; }

bool token(xmlNode* node, const std::string& value) {
    const auto classes = " " + detail::collapse_space(attribute(node, "class")) + " ";
    return has(classes, " " + value + " ");
}

bool direct_marker(xmlNode* node) {
    for (const auto* cls : {"EC_result", "b_ad", "ad-result", "sponsored-result", "res-list-ad", "ad-container"}) {
        if (token(node, cls)) return true;
    }
    for (const auto* key : {"data-ad", "data-sponsored"}) {
        const auto flag = attribute(node, key);
        if (flag == "true" || flag == "1" || flag == "sponsored") return true;
    }
    if (!attribute(node, "data-cmatchid").empty()) return true;
    return false;
}

// Cache direct flags only: descendant disclosures remain scoped to each item.
// Entries are owned by this parse and never outlive the immutable DOM.
class AncestorSignals {
    std::unordered_map<xmlNode*, bool> long_classes_;
public:
    bool inherited(xmlNode* item) {
        for (auto* node = item; node && node->type == XML_ELEMENT_NODE; node = node->parent) {
            if (!long_classes_.empty()) {
                const auto found = long_classes_.find(node);
                if (found != long_classes_.end()) {
                    if (found->second) return true;
                    continue;
                }
            }
            const auto* cls = xmlHasProp(node, reinterpret_cast<const xmlChar*>("class"));
            // Preserve the existing predicate for small or compound attributes.
            const bool costly = cls && cls->type == XML_ATTRIBUTE_NODE && cls->children &&
                cls->children->type == XML_TEXT_NODE && cls->children->content && !cls->children->next &&
                xmlStrlen(cls->children->content) > 256;
            const bool paid = direct_marker(node);
            // A full memo table falls back to the same computation, never a skip.
            if (costly && long_classes_.size() < 4096) long_classes_.emplace(node, paid);
            if (paid) return true;
        }
        return false;
    }
};

bool advertising(xmlXPathContext* context, xmlNode* item, AncestorSignals& signals) {
    if (signals.inherited(item)) return true;
    const auto labels = evaluate(context, item,
        ".//*[@data-tuiguang or @data-ad-label or "
        "contains(concat(' ',normalize-space(@class),' '),' ec-tuiguang ') or "
        "contains(concat(' ',normalize-space(@class),' '),' ad-label ') or "
        "contains(concat(' ',normalize-space(@class),' '),' sponsored-label ')]");
    if (!labels->nodesetval) return false;
    for (int i = 0; i < labels->nodesetval->nodeNr; ++i) {
        auto* label = labels->nodesetval->nodeTab[i];
        if (!attribute(label, "data-tuiguang").empty()) return true;
        const auto value = text(label, 64);
        if (value == "广告" || value == "推广" || value == "商业推广" || value == "Ad" || value == "Ads" ||
            value == "Sponsored" || value == "Sponsored results") return true;
    }
    return false;
}
}  // namespace

ParsedPage parse_html(const std::string& name, const std::string& source, const std::string& effective_url, std::size_t limit) {
    detail::initialize_runtime();
    const auto& engine = detail::find_engine(name);
    if (source.empty()) return {"parse_error", "empty HTTP response", {}};
    if (source.size() > 8 * 1024 * 1024 || limit == 0 || limit > 100) throw std::invalid_argument("HTML or result limit out of range");
    const std::string base = effective_url.empty() ? engine.endpoint : effective_url;
    // Recover HTML but never enable entity expansion, DTD loading, or HUGE mode.
    Document document(htmlReadMemory(source.data(), static_cast<int>(source.size()), base.c_str(), "UTF-8",
        HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET | HTML_PARSE_COMPACT), xmlFreeDoc);
    if (!document) return {"parse_error", "HTML document could not be parsed", {}};
    Context context(xmlXPathNewContext(document.get()), xmlXPathFreeContext);
    if (!context) throw std::bad_alloc();
    xmlNode* root = xmlDocGetRootElement(document.get());
    if (!root) return {"parse_error", "HTML has no root element", {}};
    AncestorSignals signals;
    ParsedPage page;
    std::unordered_set<std::string> seen;
    const auto items = evaluate(context.get(), root, engine.items_xpath);
    const int count = items->nodesetval ? std::min(items->nodesetval->nodeNr, 512) : 0;
    for (int i = 0; i < count && page.results.size() < limit; ++i) {
        xmlNode* item = items->nodesetval->nodeTab[i];
        const auto links = evaluate(context.get(), item, engine.link_xpath);
        if (!links->nodesetval || links->nodesetval->nodeNr == 0) continue;
        ++page.candidates_seen;
        const auto raw_url = attribute(links->nodesetval->nodeTab[0], "href");
        if (advertising(context.get(), item, signals)) { ++page.ads_filtered; continue; }
        auto url = detail::clean_url(name, raw_url, base);
        if (url.empty()) {
            // Count explicit tracking endpoints separately from malformed URLs.
            if ((name == "baidu" && has(raw_url, "baidu.com/baidu.php?")) ||
                (name == "bing_cn" && has(raw_url, "bing.com/aclick?"))) ++page.ads_filtered;
            continue;
        }
        const auto heading = xpath_text(context.get(), item, engine.title_xpath, 512);
        if (url.empty() || heading.empty()) continue;
        if (name == "baidu") {
            const auto canonical = detail::normalize_url(attribute(item, "mu"));
            if (!canonical.empty() && canonical.find("nourl.ubs.baidu.com") == std::string::npos) url = canonical;
        } else if (name == "sogou") {
            const auto canonical = detail::normalize_url(attribute(links->nodesetval->nodeTab[0], "linkurl"));
            if (!canonical.empty()) url = canonical;
        }
        if (!seen.insert(url).second) continue;
        const auto snippet = xpath_text(context.get(), item, engine.snippet_xpath, 2048);
        page.results.push_back({heading, url, snippet, {name}, {engine.provider}, 0.0, "uncertain", ""});
    }
    if (!page.results.empty()) {
        page.status = "ok";
        return page;
    }
    // These diagnostics cannot override usable results. Avoid scanning the whole
    // visible page and building up to 256 KiB of text on the ordinary success path.
    const auto title = xpath_text(context.get(), root, "//title", 1024);
    const auto visible = text(root, 256 * 1024);
    const bool challenge = detail::is_challenge(title, visible, source, root) || has(base, "/captcha/") || has(base, "/antispider/") || has(base, "/sorry/");
    if (challenge) {
        page.status = "challenge";
        page.message = "search engine requires browser or human verification";
    }
    else if (has(visible, "No results found") || has(visible, "No results.") || has(visible, "没有找到") || has(visible, "未找到相关结果")) {
        page.status = "empty";
        page.message = "engine explicitly reported no matching results";
    } else if (page.candidates_seen && page.ads_filtered == page.candidates_seen) {
        page.status = "empty";
        page.message = "all recognizable candidates were advertising";
    } else {
        page.status = "parse_error";
        page.message = "no recognizable result blocks; response may be blocked or layout may have changed";
    }
    return page;
}

}  // namespace edgesearch

namespace edgesearch::detail {
ParsedPage parse_response(const std::string& engine, const HttpResponse& response, std::size_t limit) {
    const bool html = response.content_type.empty() || response.content_type.find("html") != std::string::npos;
    if (response.status != "ok") {
        if (response.status == "http_error" && html && !response.body.empty() &&
            (response.http_status == 403 || response.http_status == 503)) {
            auto parsed = parse_html(engine, response.body, response.effective_url, limit);
            if (parsed.status == "challenge") return parsed;
        }
        return {response.status, response.message, {}};
    }
    if (engine == "bing_cn" && (response.body.find("<rss") < 1024 || response.content_type.find("rss") != std::string::npos))
        return parse_rss(response.body, limit);
    if (!html) return {"parse_error", "expected an HTML search response", {}};
    return parse_html(engine, response.body, response.effective_url, limit);
}
}  // namespace edgesearch::detail
