#include "query_constraints.hpp"
#include "internal.hpp"
#include "curl_backend.hpp"
#include <algorithm>
#include <cctype>
#include <memory>
#include <string_view>
#include <vector>

namespace edgesearch::detail {
namespace {
enum class Truth { no, yes, unknown };
Truth neg(Truth a) { return a == Truth::unknown ? a : (a == Truth::yes ? Truth::no : Truth::yes); }
Truth both(Truth a, Truth b) { return a == Truth::no || b == Truth::no ? Truth::no : (a == Truth::yes && b == Truth::yes ? Truth::yes : Truth::unknown); }
Truth either(Truth a, Truth b) { return a == Truth::yes || b == Truth::yes ? Truth::yes : (a == Truth::no && b == Truth::no ? Truth::no : Truth::unknown); }
std::string lower(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32); return s; }
struct Address { std::string host, path, scheme, port; bool valid = false; };
Address address(const std::string& value) {
    initialize_runtime();
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> u(curl_url(), curl_url_cleanup);
    if (!u || value.size() > 8192 || value.find('\0') != std::string::npos || curl_url_set(u.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK) return {};
    auto get = [&](CURLUPart part) { char* p = nullptr; const auto code = curl_url_get(u.get(), part, &p, 0); std::unique_ptr<char, decltype(&curl_free)> owned(p, curl_free); return code == CURLUE_OK ? std::string(p) : std::string{}; };
    if (!get(CURLUPART_USER).empty() || !get(CURLUPART_PASSWORD).empty() || value.find('#') != std::string::npos || !get(CURLUPART_QUERY).empty()) return {};
    Address a; a.host = lower(get(CURLUPART_HOST)); a.path = get(CURLUPART_PATH); a.scheme = get(CURLUPART_SCHEME); a.port = get(CURLUPART_PORT);
    while (!a.host.empty() && a.host.back() == '.') a.host.pop_back();
    a.valid = !a.host.empty() && (a.scheme == "http" || a.scheme == "https"); return a;
}
std::string path_key(std::string s) {
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); };
    const char hex[] = "0123456789ABCDEF"; std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && digit(s[i + 1]) >= 0 && digit(s[i + 2]) >= 0) {
            const auto c = static_cast<unsigned char>(digit(s[i + 1]) * 16 + digit(s[i + 2]));
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') out += static_cast<char>(c);
            else { out += '%'; out += hex[c >> 4U]; out += hex[c & 15U]; } i += 2;
        } else out += s[i];
    }
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}
enum class Kind { atom, and_, or_, not_, left, right, end };
struct Token { Kind kind; std::size_t begin, end; };
struct Value { Truth truth = Truth::unknown; bool text = false; };
class Parser {
    const std::string& input; Address target; QueryConstraints result; std::vector<Token> tokens;
    std::size_t at = 0; bool valid = true, has_site = false, unsupported = false, opaque = false, negative_text = false;
    void blank(const Token& t) { std::fill(result.evidence.begin() + static_cast<std::ptrdiff_t>(t.begin), result.evidence.begin() + static_cast<std::ptrdiff_t>(t.end), ' '); }
    Kind peek() const { return at < tokens.size() ? tokens[at].kind : Kind::end; }
    void lex() {
        for (std::size_t i = 0; i < input.size();) {
            if (std::isspace(static_cast<unsigned char>(input[i]))) { ++i; continue; }
            if (tokens.size() >= 128) { valid = false; break; }
            const auto begin = i; Kind kind = Kind::atom;
            switch (input[i]) {
            case '(': kind = Kind::left; ++i; break;
            case ')': kind = Kind::right; ++i; break;
            case '|': kind = Kind::or_; ++i; break;
            case '-': kind = Kind::not_; ++i; break;
            case '+': ++i; continue; // Explicit positive operand; no exclusion semantics.
            default: {
                bool quoted = false;
                unsigned path_parentheses = 0;
                const bool site = lower(input.substr(begin, 5)) == "site:";
                bool path = false;
                while (i < input.size()) {
                    if (input[i] == '\\' && i + 1 < input.size()) { i += 2; continue; }
                    if (input[i] == '"') quoted = !quoted;
                    if (!quoted) {
                        // Balanced parentheses in an unquoted site URL path are
                        // literal. An extra ')' still closes an outer group.
                        if (site && input[i] == '/') path = true;
                        if (site && path && input[i] == '(') ++path_parentheses;
                        else if (site && path_parentheses && input[i] == ')') --path_parentheses;
                        else if (input[i] == '(' || input[i] == ')') {
                            if (site && input[i] == '(') unsupported = true;
                            break;
                        }
                        if (std::isspace(static_cast<unsigned char>(input[i])) || input[i] == '|') break;
                    }
                    ++i;
                }
                if (quoted || path_parentheses) valid = false;
                const auto word = input.substr(begin, i - begin);
                if (word == "AND") kind = Kind::and_;
                else if (word == "OR") kind = Kind::or_;
                else if (word == "NOT") kind = Kind::not_;
                break;
            }}
            tokens.push_back({kind, begin, i});
            if (kind != Kind::atom) blank(tokens.back());
            else if (lower(input.substr(begin, i - begin)).compare(0, 5, "site:") == 0) has_site = true;
        }
    }
    Truth match(std::string value) {
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        const bool scheme = value.find("://") != std::string::npos;
        if (value.empty() || value.find_first_of("*?@\\\" \t") != std::string::npos) { unsupported = true; return Truth::unknown; }
        auto pattern = address(scheme ? value : "https://" + value);
        if (!pattern.valid || std::any_of(pattern.host.begin(), pattern.host.end(), [](unsigned char c) { return c >= 128; })) { unsupported = true; return Truth::unknown; }
        if (!target.valid || opaque) return Truth::unknown;
        const auto& h = target.host; const auto& p = pattern.host;
        if (h != p && !(h.size() > p.size() && h.compare(h.size() - p.size(), p.size(), p) == 0 && h[h.size() - p.size() - 1] == '.')) return Truth::no;
        if (scheme && pattern.scheme != target.scheme) return Truth::no;
        if (!pattern.port.empty() && pattern.port != (target.port.empty() ? (target.scheme == "https" ? "443" : "80") : target.port)) return Truth::no;
        const auto prefix = path_key(pattern.path), path = path_key(target.path);
        if (prefix != "/" && path != prefix && !(path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0 && path[prefix.size()] == '/')) return Truth::no;
        return Truth::yes;
    }
    Value primary(unsigned depth, bool inverted) {
        if (depth > 16) { valid = false; return {}; }
        if (peek() == Kind::left) {
            ++at; auto value = expression(depth + 1, inverted);
            if (peek() != Kind::right) valid = false; else ++at;
            return value;
        }
        if (peek() != Kind::atom) { valid = false; return {}; }
        const auto token = tokens[at++]; const auto word = input.substr(token.begin, token.end - token.begin);
        if (lower(word).compare(0, 5, "site:") == 0) { has_site = true; blank(token); return {match(word.substr(5)), false}; }
        if (word.find("&&") != std::string::npos || word == "!" || word == "XOR" || word == "NEAR" || word == "AROUND") unsupported = true;
        if (inverted) { blank(token); negative_text = true; }
        return {Truth::unknown, true}; // Text is evaluated separately, never assumed true under NOT.
    }
    Value unary(unsigned depth, bool inverted) {
        if (depth > 16) { valid = false; return {}; }
        if (peek() != Kind::not_) return primary(depth, inverted);
        ++at; auto value = unary(depth + 1, !inverted); value.truth = neg(value.truth);
        result.complex_logic |= value.text; return value;
    }
    Value conjunction(unsigned depth, bool inverted) {
        auto value = unary(depth, inverted);
        while (valid && peek() != Kind::end && peek() != Kind::right && peek() != Kind::or_) {
            if (peek() == Kind::and_) ++at;
            auto next = unary(depth, inverted); value.truth = both(value.truth, next.truth); value.text |= next.text;
        }
        return value;
    }
    Value expression(unsigned depth, bool inverted) {
        auto value = conjunction(depth, inverted);
        while (valid && peek() == Kind::or_) {
            ++at; result.complex_logic = true; auto next = conjunction(depth, inverted);
            value.truth = either(value.truth, next.truth); value.text |= next.text;
        }
        return value;
    }
public:
    Parser(const std::string& query, const std::string& url) : input(query) {
        result.evidence = query;
        // A candidate URL may contain query/fragment; they are not part of site matching.
        target = address(url.substr(0, url.find_first_of("?#")));
        opaque = target.host == "www.so.com" || target.host == "so.com" || target.host == "www.baidu.com" || target.host == "baidu.com" || target.host == "www.sogou.com" || target.host == "sogou.com";
    }
    QueryConstraints run() {
        if (input.size() > query_byte_limit) { result.uncertain = true; result.reason = "query constraint parsing limit"; return result; }
        lex(); const auto value = expression(0, false);
        valid &= peek() == Kind::end;
        if (!has_site && !negative_text && !result.complex_logic) result.evidence = input;
        if (!valid || unsupported) { result.uncertain = true; result.reason = "query constraints are unsupported or ambiguous; retained conservatively"; }
        else if (has_site && value.truth == Truth::no) { result.rejected = true; result.reason = "result URL conflicts with query site expression"; }
        else if (has_site && (!target.valid || opaque)) { result.uncertain = true; result.reason = "query site expression cannot be verified before resolving the redirect target"; }
        result.verified = has_site && valid && !unsupported && value.truth == Truth::yes && !result.uncertain;
        return result;
    }
};
}
QueryConstraints query_constraints(const std::string& query, const std::string& url) { return Parser(query, url).run(); }
}
