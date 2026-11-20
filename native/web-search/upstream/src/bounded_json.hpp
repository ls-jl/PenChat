#pragma once
#include <libxml/xmlstring.h>
#include <map>
#include <string>
#include <stdexcept>
#include <vector>

namespace edgesearch::detail {
// Repository API data only. Depth, nodes and bytes are bounded before allocation.
struct Json {
    enum Kind { object, array, string, primitive } kind = primitive;
    std::string value;
    std::map<std::string, Json> members;
    std::vector<Json> items;
    const Json& at(const std::string& key) const {
        static const Json empty;
        const auto it = members.find(key); return it == members.end() ? empty : it->second;
    }
    std::string str(const std::string& key) const { const auto& v = at(key); return v.kind == string ? v.value : ""; }
    static Json parse(const std::string& input) {
        struct Parser {
            const std::string& s; std::size_t pos = 0, nodes = 0;
            [[noreturn]] void fail() { throw std::invalid_argument("invalid or over-budget repository JSON"); }
            void ws() { while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) ++pos; }
            bool take(char c) { ws(); if (pos < s.size() && s[pos] == c) { ++pos; return true; } return false; }
            unsigned hex() {
                unsigned n = 0;
                for (int i = 0; i < 4; ++i) {
                    if (pos == s.size()) fail();
                    const char c = s[pos++]; unsigned d = 16;
                    if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
                    if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
                    if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
                    if (d == 16) fail();
                    n = n * 16 + d;
                } return n;
            }
            void utf8(std::string& out, unsigned n) {
                if (n < 128) out += static_cast<char>(n);
                else if (n < 2048) { out += static_cast<char>(192U | (n >> 6U)); out += static_cast<char>(128U | (n & 63U)); }
                else if (n < 65536) { out += static_cast<char>(224U | (n >> 12U)); out += static_cast<char>(128U | ((n >> 6U) & 63U)); out += static_cast<char>(128U | (n & 63U)); }
                else { out += static_cast<char>(240U | (n >> 18U)); out += static_cast<char>(128U | ((n >> 12U) & 63U)); out += static_cast<char>(128U | ((n >> 6U) & 63U)); out += static_cast<char>(128U | (n & 63U)); }
            }
            std::string text() {
                if (!take('"')) fail();
                std::string out;
                while (pos < s.size()) {
                    const auto c = static_cast<unsigned char>(s[pos++]);
                    if (c == '"') return out;
                    if (c < 32) fail();
                    if (c != '\\') { out += static_cast<char>(c); continue; }
                    if (pos == s.size()) fail();
                    const char e = s[pos++];
                    if (e == '"' || e == '/' || e == '\\') out += e;
                    else if (e == 'n') out += '\n'; else if (e == 'r') out += '\r'; else if (e == 't') out += '\t';
                    else if (e == 'b') out += '\b'; else if (e == 'f') out += '\f';
                    else if (e == 'u') {
                        auto n = hex();
                        if (n >= 0xd800 && n <= 0xdbff) {
                            if (pos + 2 > s.size() || s[pos++] != '\\' || s[pos++] != 'u') fail();
                            auto low = hex(); if (low < 0xdc00 || low > 0xdfff) fail();
                            n = 0x10000U + ((n - 0xd800U) << 10U) + low - 0xdc00U;
                        } else if (n >= 0xdc00 && n <= 0xdfff) fail();
                        utf8(out, n);
                    } else fail();
                } fail();
            }
            Json node(unsigned depth = 0) {
                if (depth > 32 || ++nodes > 40000) fail();
                ws(); Json out;
                if (take('{')) {
                    out.kind = object;
                    if (!take('}')) { do { auto key = text(); if (!take(':')) fail(); if (!out.members.emplace(key, node(depth + 1)).second) fail(); } while (take(',')); if (!take('}')) fail(); }
                } else if (take('[')) {
                    out.kind = array;
                    if (!take(']')) { do { out.items.push_back(node(depth + 1)); } while (take(',')); if (!take(']')) fail(); }
                } else if (pos < s.size() && s[pos] == '"') { out.kind = string; out.value = text(); }
                else {
                    const auto begin = pos;
                    while (pos < s.size() && s[pos] != ',' && s[pos] != '}' && s[pos] != ']' && s[pos] != ' ' && s[pos] != '\n' && s[pos] != '\r' && s[pos] != '\t') ++pos;
                    out.value = s.substr(begin, pos - begin);
                    if (out.value != "true" && out.value != "false" && out.value != "null") {
                        std::size_t i = 0;
                        const auto& n = out.value;
                        if (i < n.size() && n[i] == '-') ++i;
                        if (i == n.size()) fail();
                        if (n[i] == '0') ++i;
                        else { if (n[i] < '1' || n[i] > '9') fail(); while (i < n.size() && n[i] >= '0' && n[i] <= '9') ++i; }
                        if (i < n.size() && n[i] == '.') { const auto b = ++i; while (i < n.size() && n[i] >= '0' && n[i] <= '9') ++i; if (i == b) fail(); }
                        if (i < n.size() && (n[i] == 'e' || n[i] == 'E')) { ++i; if (i < n.size() && (n[i] == '+' || n[i] == '-')) ++i; const auto b = i; while (i < n.size() && n[i] >= '0' && n[i] <= '9') ++i; if (i == b) fail(); }
                        if (i != n.size()) fail();
                    }
                } return out;
            }
        } parser{input};
        if (input.size() > 8 * 1024 * 1024 || input.find('\0') != std::string::npos || !xmlCheckUTF8(BAD_CAST input.c_str())) parser.fail();
        auto result = parser.node(); parser.ws(); if (parser.pos != input.size()) parser.fail(); return result;
    }
};
} // namespace edgesearch::detail
