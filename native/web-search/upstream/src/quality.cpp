#include "internal.hpp"
#include "query_constraints.hpp"
#include "curl_backend.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <memory>
#include <libxml/HTMLparser.h>

namespace edgesearch::detail {
namespace {
std::string ascii_lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return value;
}
bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
bool ascii_word(unsigned char c) { return c < 128 && (std::isalnum(c) || c == '_'); }
std::string model_text(const std::string& text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((text[i] == ' ' || text[i] == '\t') && !result.empty() && std::isdigit(static_cast<unsigned char>(result.back()))) {
            auto next = text.find_first_not_of(" \t", i);
            auto end = next;
            while (end != std::string::npos && end < text.size() && text[end] >= 'a' && text[end] <= 'z') ++end;
            static const std::set<std::string> suffixes = {"pro", "max", "plus", "ultra", "lite", "mini"};
            if (next != std::string::npos && suffixes.count(text.substr(next, end - next))) { i = next - 1; continue; }
        }
        result += text[i];
    }
    return result;
}
bool entity_contains(const std::string& text, const std::string& token) {
    for (auto at = text.find(token); at != std::string::npos; at = text.find(token, at + 1)) {
        const auto end = at + token.size();
        const bool numeric = !token.empty() && std::isdigit(static_cast<unsigned char>(token.front()));
        const bool before = at == 0 || (numeric ? !std::isdigit(static_cast<unsigned char>(text[at - 1])) : !ascii_word(static_cast<unsigned char>(text[at - 1])));
        if (before && (end == text.size() || !ascii_word(static_cast<unsigned char>(text[end])))) return true;
    }
    return false;
}
enum class Entry { none, translation, image };
Entry search_entry(const std::string& value) {
    // Classify only known tool/search endpoints, not pages whose title happens
    // to mention translation or images. Host parsing prevents suffix/userinfo tricks.
    if (value.size() > 8192 || value.find('\0') != std::string::npos) return Entry::none;
    initialize_runtime();
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
    if (!url || curl_url_set(url.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) return Entry::none;
    auto get = [&](CURLUPart part) {
        char* raw = nullptr; const auto code = curl_url_get(url.get(), part, &raw, 0);
        std::unique_ptr<char, decltype(&curl_free)> owned(raw, curl_free);
        return code == CURLUE_OK ? std::string(raw) : std::string{};
    };
    auto host = ascii_lower(get(CURLUPART_HOST));
    while (!host.empty() && host.back() == '.') host.pop_back();
    const auto path = get(CURLUPART_PATH);
    if ((host == "fanyi.so.com" || host == "fanyi.baidu.com" || host == "fanyi.sogou.com") &&
        (path == "/" || path == "/index" || path == "/translate")) return Entry::translation;
    if ((host == "image.so.com" && (path == "/" || path == "/i")) ||
        (host == "image.baidu.com" && path == "/search/index") ||
        (host == "pic.sogou.com" && path == "/pics")) return Entry::image;
    return Entry::none;
}
std::set<std::string> cjk_pairs(const std::string& value) {
    std::set<std::string> pairs;
    std::string previous;
    for (std::size_t i = 0; i < value.size();) {
        const auto c = static_cast<unsigned char>(value[i]);
        const auto n = c < 128 ? 1U : (c < 224 ? 2U : (c < 240 ? 3U : 4U));
        if (n == 3 && i + n <= value.size() && c >= 0xe3 && c <= 0xe9) {
            auto current = value.substr(i, n);
            if (!previous.empty()) pairs.insert(previous + current);
            previous = std::move(current);
        } else previous.clear();
        i += n;
    }
    return pairs;
}
}
bool is_challenge(const std::string& title, const std::string& visible, const std::string& source, xmlNode* root) {
    const auto t = collapse_space(ascii_lower(title.substr(0, 1024))), v = collapse_space(ascii_lower(visible.substr(0, 8192)));
    const bool documentation = contains(t, "guide") || contains(t, "documentation") || contains(t, "integration") ||
        contains(t, "troubleshoot") || contains(t, "教程") || contains(t, "介绍") || contains(t, "集成") || contains(t, "文档") || contains(t, "排查");
    const bool hints = contains(t, "captcha") || contains(t, "just a moment") || contains(t, "安全验证") || contains(t, "人机验证") ||
        contains(t, "access denied") || contains(t, "not a bot") || contains(t, "checking your browser") ||
        contains(v, "challenge") || contains(v, "anubis") || contains(v, "checking your browser") || contains(v, "验证码") || contains(v, "请完成安全验证") ||
        std::string_view(source).substr(0, 256 * 1024).find("anubis_challenge") != std::string_view::npos;
    if (!hints) return false;
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> owned(nullptr, xmlFreeDoc);
    if (!root && !source.empty()) {
        const auto bytes = std::min(source.size(), std::size_t{256 * 1024});
        owned.reset(htmlReadMemory(source.data(), static_cast<int>(bytes), nullptr, "UTF-8",
            HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_NONET | HTML_PARSE_COMPACT));
        if (owned) root = xmlDocGetRootElement(owned.get());
    }
    bool form = false, control = false, anubis = false, cloudflare = false, article = false;
    auto attr = [](xmlNode* node, const char* key) {
        xmlChar* raw = xmlGetProp(node, BAD_CAST key);
        std::unique_ptr<xmlChar, decltype(xmlFree)> value(raw, xmlFree);
        return raw ? ascii_lower(reinterpret_cast<const char*>(raw)) : std::string{};
    };
    xmlNode* node = root;
    for (unsigned visited = 0; node && visited < 20000; ++visited) {
        const auto name = node->name ? reinterpret_cast<const char*>(node->name) : "";
        const std::string_view tag(name);
        const bool example = tag == "pre" || tag == "code" || tag == "template" || tag == "noscript";
        if (node->type == XML_ELEMENT_NODE && !example) {
            const auto id = attr(node, "id"), cls = attr(node, "class"), key = attr(node, "name");
            form |= tag == "form";
            article |= tag == "article";
            control |= (tag == "input" || tag == "div" || tag == "form") &&
                (contains(id, "captcha") || contains(id, "seccode") || contains(cls, "captcha-container") || contains(key, "captcha"));
            if (tag == "script") {
                const auto src = attr(node, "src");
                anubis |= id == "anubis_challenge";
                cloudflare |= contains(src, "cf-chl") || contains(src, "/cdn-cgi/challenge-platform/");
                if (node->children && node->children->content) {
                    const auto content = ascii_lower(std::string(reinterpret_cast<const char*>(node->children->content),
                        std::min(std::size_t{32768}, static_cast<std::size_t>(xmlStrlen(node->children->content)))));
                    cloudflare |= contains(content, "cf-chl") || contains(content, "_cf_chl_opt");
                }
            }
        }
        if (!example && tag != "script" && node->children) { node = node->children; continue; }
        while (node != root && !node->next) node = node->parent;
        if (node == root) break;
        node = node->next;
    }
    std::string body = v;
    if (!t.empty() && body.compare(0, t.size(), t) == 0) body = collapse_space(body.substr(t.size()));
    auto starts_prompt = [](const std::string& value) {
        for (const auto* phrase : {"please complete the following challenge", "javascript is required to complete this challenge",
                                  "此验证码用于确认", "checking your browser", "请完成安全验证", "verify you are human", "verifying your request"})
            if (value.compare(0, std::char_traits<char>::length(phrase), phrase) == 0) return true;
        return false;
    };
    const bool prompt = starts_prompt(v) || starts_prompt(body);
    const bool verification_title = contains(t, "captcha") || contains(t, "安全验证") || contains(t, "人机验证") ||
        contains(t, "access denied") || contains(t, "checking your browser");
    return (t == "captcha" && (v == "verify" || v == "captcha verify")) || (contains(t, "just a moment") && cloudflare) ||
        ((contains(t, "making sure you're not a bot") || contains(t, "making sure you’re not a bot")) &&
         (anubis || contains(v, "protected by anubis") || contains(v, "proof of work"))) ||
        (anubis && contains(v, "javascript") && visible.size() < 8192) ||
        (prompt && visible.size() < 8192 && (anubis || cloudflare || (!documentation && !article && (form || control || verification_title)))) ||
        (!documentation && !article && verification_title && (control || prompt || body == "verify"));
}
void assess_relevance(const std::string& query, Result& result) {
    result.relevance = "uncertain"; result.relevance_reason = "insufficient entity evidence";
    result.query_coverage = 0.0;
    const auto constraints = query_constraints(query, result.url);
    if (constraints.rejected || constraints.uncertain) {
        result.relevance = constraints.rejected ? "rejected" : "uncertain"; result.relevance_reason = constraints.reason; return;
    }
    const auto& evidence = constraints.evidence;
    const auto q = ascii_lower(evidence);
    const auto entry = search_entry(result.url);
    if (entry != Entry::none) {
        const bool requested = (entry == Entry::translation ?
            (contains(q, "翻译") || entity_contains(q, "translate") || entity_contains(q, "translation") || entity_contains(q, "fanyi")) :
            (contains(q, "图片") || contains(q, "图像") || contains(q, "照片") || contains(q, "壁纸") ||
             entity_contains(q, "image") || entity_contains(q, "images") || entity_contains(q, "photo") || entity_contains(q, "pictures"))) ||
            (constraints.verified && collapse_space(evidence).empty());
        result.relevance = requested ? "uncertain" : "rejected";
        const auto kind = entry == Entry::translation ? "translation" : "image search";
        result.relevance_reason = requested ? std::string(kind) + " navigation entry; open it to inspect content, not verified source evidence" :
            std::string(kind) + " entry is not a source document for this query";
        return;
    }
    if (constraints.verified && collapse_space(evidence).empty()) {
        result.relevance = "supported"; result.relevance_reason = "query site expression verified"; result.query_coverage = 1.0; return;
    }
    const auto text = ascii_lower(result.title + " " + result.content + " " + result.url);
    const auto normalized_models = model_text(text), normalized_query = model_text(q);
    std::set<std::string> anchors;
    // A leading project/name or distinctive mixed-case token is an entity hint;
    // ordinary question/modifier words are deliberately not mandatory evidence.
    static const std::set<std::string> modifiers = {"how", "what", "when", "where", "which", "why", "does", "can", "could", "would", "should", "the", "a", "an", "best", "latest", "official", "public", "find", "search", "get", "download", "install", "manual", "documentation", "docs", "guide", "tutorial", "compare", "difference", "differences", "site", "http", "https"};
    const auto first_token = q.find_first_not_of(" \t\"'");
    for (std::size_t i = 0; i < q.size();) {
        if (!(std::isalnum(static_cast<unsigned char>(q[i])) && static_cast<unsigned char>(q[i]) < 128)) { ++i; continue; }
        auto end = i;
        bool digit = false, alpha = false, underscore = false;
        while (end < q.size() && static_cast<unsigned char>(q[end]) < 128 &&
               (std::isalnum(static_cast<unsigned char>(q[end])) || q[end] == '_')) {
            digit |= std::isdigit(static_cast<unsigned char>(q[end])) != 0;
            alpha |= std::isalpha(static_cast<unsigned char>(q[end])) != 0;
            underscore |= q[end] == '_'; ++end;
        }
        const auto token = q.substr(i, end - i);
        bool mixed_case = false;
        for (auto at = i + 1; at < end; ++at) mixed_case |= evidence[at] >= 'A' && evidence[at] <= 'Z';
        const bool name_hint = token.size() >= 4 && !modifiers.count(token) && ((i == first_token && evidence[i] >= 'A' && evidence[i] <= 'Z') || mixed_case);
        if (token.size() >= 3 && (underscore || (digit && alpha) || name_hint)) anchors.insert(token);
        i = end;
    }
    for (std::size_t i = 0; i < normalized_query.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(normalized_query[i]))) continue;
        auto end = normalized_query.find_first_not_of("0123456789", i);
        if (end == std::string::npos) break;
        auto suffix_end = end;
        while (suffix_end < normalized_query.size() && normalized_query[suffix_end] >= 'a' && normalized_query[suffix_end] <= 'z') ++suffix_end;
        if (suffix_end > end) anchors.insert(normalized_query.substr(i, suffix_end - i));
        i = suffix_end - 1;
    }
    if (anchors.empty()) {
        for (std::size_t at = 0; at < q.size(); ++at) {
            if (q[at] != '"') continue;
            const auto end = q.find('"', at + 1);
            if (end == std::string::npos) break;
            const auto quoted = q.substr(at + 1, end - at - 1);
            if (quoted.size() >= 3 && quoted.size() <= 128) anchors.insert(quoted);
            at = end;
        }
    }
    // A missing model is uncertain; a different explicit number+same model suffix is contradictory.
    for (const auto& token : anchors) {
        if (!constraints.complex_logic && token.size() > 1 && std::isdigit(static_cast<unsigned char>(token[0]))) {
            auto n = token.find_first_not_of("0123456789");
            if (n == std::string::npos) continue;
            const auto suffix = token.substr(n);
            for (std::size_t at = 0; at < normalized_models.size(); ++at) if (std::isdigit(static_cast<unsigned char>(normalized_models[at]))) {
                auto stop = normalized_models.find_first_not_of("0123456789", at);
                if (stop != std::string::npos && normalized_models.compare(stop, suffix.size(), suffix) == 0 &&
                    normalized_models.substr(at, stop - at) != token.substr(0, n) && !entity_contains(normalized_models, token) &&
                    !entity_contains(normalized_query, normalized_models.substr(at, stop - at) + suffix) &&
                    entity_contains(normalized_models, normalized_models.substr(at, stop - at) + suffix)) {
                    result.relevance = "rejected"; result.relevance_reason = "explicit product model conflict"; return;
                }
                at = stop == std::string::npos ? normalized_models.size() : stop;
            }
        }
    }
    std::size_t matched = 0, strong = 0;
    for (const auto& token : anchors) {
        if (!std::isdigit(static_cast<unsigned char>(token[0]))) ++strong;
        if (entity_contains(text, token) || entity_contains(normalized_models, token)) ++matched;
    }
    if (!constraints.complex_logic && !anchors.empty() && !matched && strong) {
        // A short entry with the first subject still present can lead to useful
        // documentation even if the requested API identifier is absent in its preview.
        std::string subject;
        for (std::size_t at = 0; at < q.size() && subject.empty();) {
            if (!ascii_word(static_cast<unsigned char>(q[at]))) { ++at; continue; }
            auto end = at + 1; while (end < q.size() && ascii_word(static_cast<unsigned char>(q[end]))) ++end;
            auto term = q.substr(at, end - at);
            if (term.size() >= 3 && !modifiers.count(term) && term != "use" && term != "using") subject = std::move(term);
            at = end;
        }
        if (result.title.size() + result.content.size() <= 384 && !subject.empty() && entity_contains(text, subject)) {
            result.relevance_reason = "related subject in a limited preview; inspect this entry for the requested identifier";
        } else {
            result.relevance = "rejected"; result.relevance_reason = "none of the explicit technical entities occur in title, snippet or URL"; return;
        }
    }
    const auto qp = cjk_pairs(evidence), rp = cjk_pairs(result.title + " " + result.content);
    std::size_t common = 0;
    for (const auto& pair : qp) common += rp.count(pair);
    const auto title = ascii_lower(result.title);
    const bool dictionary = title.find("拼音") != std::string::npos || title.find("笔顺") != std::string::npos || title.find("字的解释") != std::string::npos;
    if (!constraints.complex_logic && qp.size() >= 4 && !common && !matched && dictionary) {
        result.relevance = "rejected"; result.relevance_reason = "dictionary definition does not match the requested topic"; return;
    }
    std::set<std::string> terms;
    for (std::size_t i = 0; i < q.size();) {
        if (!ascii_word(static_cast<unsigned char>(q[i]))) { ++i; continue; }
        auto end = i + 1;
        while (end < q.size() && ascii_word(static_cast<unsigned char>(q[end]))) ++end;
        const auto term = q.substr(i, end - i);
        if (term.size() >= 3 && !modifiers.count(term) && term != "com" && term != "org" && term != "net") terms.insert(term);
        i = end;
    }
    std::size_t covered = 0;
    for (const auto& term : terms) covered += entity_contains(text, term) || entity_contains(normalized_models, term);
    const auto evidence_total = terms.size() + qp.size();
    result.query_coverage = evidence_total ? static_cast<double>(covered + common) / static_cast<double>(evidence_total) : 0.0;
    if ((!anchors.empty() && matched == anchors.size()) || (anchors.empty() && common >= 2) ||
        (anchors.empty() && !terms.empty() && covered >= std::min(std::size_t{2}, terms.size()) && result.query_coverage >= 0.5)) {
        result.relevance = "supported"; result.relevance_reason = "query entity or phrase evidence found";
    }
    const bool numeric_model = std::any_of(anchors.begin(), anchors.end(), [](const std::string& token) {
        return !token.empty() && std::isdigit(static_cast<unsigned char>(token.front()));
    });
    if (result.relevance == "supported" && numeric_model && qp.size() >= 4 && common < 2) {
        result.relevance = "uncertain"; result.relevance_reason = "model matches, but evidence for the Chinese product topic is insufficient";
    }

}
} // namespace edgesearch::detail

namespace edgesearch::detail {
std::string failure_stage(const HttpResponse& http) {
    if (!http.requested) return "queue";
    if (http.status == "cancelled") return "cancelled";
    if (http.transport_code == 5) return "proxy_dns";
    if (http.transport_code == 6) return "dns";
    if (http.transport_code == 60 || http.transport_code == 51 || http.transport_code == 58) return "tls_verify";
    if (http.transport_code == 35) return "tls";
    if (http.status == "blocked_address") return "address_policy";
    if (http.transport_code == 7) return "connect";
    if (http.status == "timeout") {
        if (http.request_sent || http.http_status || http.network.first_byte_us) return "body";
        if (http.message.find("Resolving") != std::string::npos || http.message.find("resolving") != std::string::npos) return "dns";
        if (!http.network.connect_done_us) return "connect";
        if (!http.http_status && !http.network.first_byte_us && http.effective_url.compare(0, 8, "https://") == 0 && !http.network.tls_done_us) return "tls";
        return "body";
    }
    if (http.http_status >= 400) return "http";
    if (http.status == "network_error") return "transport";
    if (http.status == "response_too_large") return "body_limit";
    return {};
}
std::vector<std::string> suggest_queries(const std::string& query) {
    // Suggestions are data for the caller; never execute a hidden rewritten query.
    // Quoted text, site restrictions, model identifiers and boolean syntax survive.
    static const std::set<std::string> filler = {"please", "official", "documentation", "explain"};
    // Do not simplify expressions where dropping an operand could change an operator.
    if (query.find_first_of("()|+-") != std::string::npos) return {};
    for (const auto* op : {"AND", "OR", "NOT"}) {
        if (entity_contains(query, op)) return {};
    }
    std::string simplified, word;
    bool quoted = false;
    auto add = [&] {
        if (word.empty()) return;
        if (word.front() == '"' || !filler.count(ascii_lower(word))) {
            if (!simplified.empty()) simplified += ' ';
            simplified += word;
        }
        word.clear();
    };
    for (char c : query) {
        if (c == '"') quoted = !quoted;
        if (!quoted && (c == ' ' || c == '\t' || c == '\n')) add();
        else word += c;
    }
    add();
    if (!quoted && !simplified.empty() && simplified != collapse_space(query)) return {simplified};
    return {};
}
} // namespace edgesearch::detail
