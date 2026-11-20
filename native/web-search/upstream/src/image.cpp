#include "image.hpp"
#include <stdexcept>
#include <array>
#include <ostream>
namespace edgesearch::detail {
namespace {
std::string lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return value;
}
}
std::string image_mime(std::string_view bytes, const std::string& declared) {
    auto mime = lower(declared.substr(0, declared.find(';')));
    const auto first = mime.find_first_not_of(" \t\r\n");
    mime = first == std::string::npos ? "" : mime.substr(first, mime.find_last_not_of(" \t\r\n") - first + 1);
    // Sniff only images or generic binary responses. Never reinterpret a text
    // document by its URL suffix. This is identification, not image decoding.
    if (!mime.empty() && mime != "application/octet-stream" && mime.compare(0, 6, "image/") != 0) return {};
    if (bytes.size() >= 8 && bytes.substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8)) return "image/png";
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xd8 && static_cast<unsigned char>(bytes[2]) == 0xff) return "image/jpeg";
    if (bytes.substr(0, 6) == "GIF87a" || bytes.substr(0, 6) == "GIF89a") return "image/gif";
    if (bytes.size() >= 12 && bytes.substr(0, 4) == "RIFF" && bytes.substr(8, 4) == "WEBP") return "image/webp";
    return mime.compare(0, 6, "image/") == 0 ? mime : "";
}
std::string image_url_mime(const std::string& url) {
    const auto path = lower(url.substr(0, url.find_first_of("?#")));
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) return {};
    const auto extension = path.substr(dot);
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".webp") return "image/webp";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".avif") return "image/avif";
    if (extension == ".bmp") return "image/bmp";
    if (extension == ".ico") return "image/x-icon";
    return {};
}
std::string encode_base64(std::string_view bytes) {
    if (bytes.size() > 8 * 1024 * 1024) throw std::invalid_argument("image exceeds serialization byte limit");
    static constexpr char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0U;
        const auto c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0U;
        out += chars[a >> 2U]; out += chars[((a & 3U) << 4U) | (b >> 4U)];
        out += i + 1 < bytes.size() ? chars[((b & 15U) << 2U) | (c >> 6U)] : '=';
        out += i + 2 < bytes.size() ? chars[c & 63U] : '=';
    }
    return out;
}
void write_base64(std::ostream& out, std::string_view bytes) {
    if (bytes.size() > 8 * 1024 * 1024) throw std::invalid_argument("image exceeds serialization byte limit");
    static constexpr char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<char, 4096> buffer{}; std::size_t used = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0U;
        const auto c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0U;
        buffer[used++] = chars[a >> 2U]; buffer[used++] = chars[((a & 3U) << 4U) | (b >> 4U)];
        buffer[used++] = i + 1 < bytes.size() ? chars[((b & 15U) << 2U) | (c >> 6U)] : '=';
        buffer[used++] = i + 2 < bytes.size() ? chars[c & 63U] : '=';
        if (used == buffer.size()) { out.write(buffer.data(), static_cast<std::streamsize>(used)); used = 0; }
    }
    if (used) out.write(buffer.data(), static_cast<std::streamsize>(used));
}
}
