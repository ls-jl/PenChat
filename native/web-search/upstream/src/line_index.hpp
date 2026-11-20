#pragma once
#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>
namespace edgesearch::detail {
struct LineIndex {
    static constexpr std::size_t stride = 4096;
    std::vector<std::uint32_t> prefix{0}; // Newline count at each 4 KiB boundary.
    std::size_t total_lines = 0;
    explicit LineIndex(std::string_view text = {}) {
        prefix.reserve(text.size() / stride + 1);
        std::uint32_t count = 0;
        for (std::size_t at = 0; at < text.size();) {
            const auto end = at + std::min(stride, text.size() - at);
            count += static_cast<std::uint32_t>(std::count(text.begin() + static_cast<std::ptrdiff_t>(at), text.begin() + static_cast<std::ptrdiff_t>(end), '\n'));
            if (end % stride == 0) prefix.push_back(count);
            at = end;
        }
        total_lines = text.empty() ? 0 : count + (text.back() != '\n' ? 1U : 0U);
    }
    std::size_t bytes() const { return sizeof(LineIndex) + prefix.size() * sizeof(std::uint32_t); }
    std::size_t line_at(std::string_view text, std::size_t offset) const {
        const auto block = offset / stride, begin = block * stride;
        return 1 + prefix[block] + static_cast<std::size_t>(std::count(text.begin() + static_cast<std::ptrdiff_t>(begin), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
    }
    std::size_t offset(std::string_view text, std::size_t line) const {
        if (line <= 1) return 0;
        if (line > total_lines) return text.size();
        const auto needed = static_cast<std::uint32_t>(line - 1);
        const auto found = std::lower_bound(prefix.begin() + 1, prefix.end(), needed);
        const auto block = static_cast<std::size_t>(found - prefix.begin()) - 1;
        std::size_t at = block * stride; auto count = prefix[block];
        while (count < needed) {
            const auto end = text.find('\n', at);
            if (end == std::string_view::npos) return text.size();
            at = end + 1; ++count;
        }
        return at;
    }
};
}
