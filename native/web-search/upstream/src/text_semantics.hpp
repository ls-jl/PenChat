#pragma once
#include "edgesearch/browser.hpp"
#include <algorithm>
#include <iterator>
namespace edgesearch::detail {
inline bool inside_deleted(const Page& page, std::size_t offset) {
    const auto it = std::upper_bound(page.deleted_ranges.begin(), page.deleted_ranges.end(), offset,
        [](std::size_t value, const DeletedRange& range) { return value < range.begin; });
    if (it == page.deleted_ranges.begin()) return false;
    const auto& range = *std::prev(it);
    return offset > range.begin && offset < range.end;
}
}
