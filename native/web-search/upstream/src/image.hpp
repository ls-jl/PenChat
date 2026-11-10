#pragma once
#include <string>
#include <string_view>
#include <iosfwd>
namespace edgesearch::detail {
std::string image_mime(std::string_view bytes, const std::string& declared = {});
std::string image_url_mime(const std::string& url);
std::string encode_base64(std::string_view bytes);
void write_base64(std::ostream& out, std::string_view bytes);
}
