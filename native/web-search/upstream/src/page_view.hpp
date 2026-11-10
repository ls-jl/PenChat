#pragma once
#include "edgesearch/browser.hpp"
#include <algorithm>
#include <string_view>
namespace edgesearch::detail {
inline std::string_view utf8_prefix(std::string_view text, std::size_t count) {
    auto end = std::min(count, text.size());
    while (end && end < text.size() && (static_cast<unsigned char>(text[end]) & 192U) == 128U) --end;
    return text.substr(0, end);
}
// A serialization view owns no body, image or link copies. The snapshot must
// remain alive and immutable until serialization has completed.
struct PageView {
    const Page& page;
    std::string_view text;
    std::size_t link_begin = 0, link_count = 0, image_begin = 0, image_length = 0;
    std::size_t next_offset_bytes = 0, next_link_offset = 0;
    bool return_complete = true, text_truncated = false, links_truncated = false;
    bool starts_deleted = false, ends_deleted = false;
    std::vector<std::string> reasons;
    explicit PageView(const Page& source) : page(source), text(source.text), link_count(source.links.size()),
        image_length(source.image_data.size()), next_offset_bytes(source.next_offset_bytes), next_link_offset(source.next_link_offset),
        return_complete(source.return_complete), text_truncated(source.text_truncated), links_truncated(source.links_truncated),
        starts_deleted(source.starts_deleted), ends_deleted(source.ends_deleted), reasons(source.incomplete_reasons) {}
};
std::string page_view_json(const PageView& view);
}
