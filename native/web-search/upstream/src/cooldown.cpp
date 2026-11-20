#include "internal.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <limits>

namespace edgesearch::detail {
namespace {
constexpr std::int64_t maximum_ms = 86400000;
constexpr auto history_window = std::chrono::minutes(30);

bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }

// Gregorian civil date to days since 1970, independent of libc/libcurl time_t ABI.
std::int64_t days_from_civil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const auto era = year / 400;
    const auto yoe = year - era * 400;
    const auto shifted_month = month + (month > 2 ? -3 : 9);
    const auto doy = (153 * shifted_month + 2) / 5 + day - 1;
    return static_cast<std::int64_t>(era) * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}

int digits(std::string_view value) {
    int result = 0;
    for (const auto c : value) {
        if (c < '0' || c > '9') return -1;
        result = result * 10 + (c - '0');
    }
    return result;
}

int month_number(std::string_view value) {
    constexpr std::array<std::string_view, 12> names = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    for (std::size_t i = 0; i < names.size(); ++i) if (value == names[i]) return static_cast<int>(i) + 1;
    return -1;
}

bool weekday(std::string_view value, bool full = false) {
    constexpr std::array<std::string_view, 7> names = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    return std::any_of(names.begin(), names.end(), [&](auto name) { return value == (full ? name : name.substr(0, 3)); });
}

int reference_year(std::int64_t reference) {
    int result = 1601, low = 1601, high = 9999;
    while (low <= high) {
        const auto middle = low + (high - low) / 2;
        if (days_from_civil(middle, 1, 1) * 86400 <= reference) { result = middle; low = middle + 1; }
        else high = middle - 1;
    }
    return result;
}
}

std::optional<std::int64_t> parse_http_date_seconds(std::string_view value, std::int64_t reference) noexcept {
    int day = -1, month = -1, year = -1;
    bool obsolete_year = false;
    std::string_view time;
    if (value.size() == 29 && weekday(value.substr(0, 3)) && value.substr(3, 2) == ", " &&
        value[7] == ' ' && value[11] == ' ' && value[16] == ' ' && value.substr(25) == " GMT") {
        day = digits(value.substr(5, 2)); month = month_number(value.substr(8, 3));
        year = digits(value.substr(12, 4)); time = value.substr(17, 8);
    } else if (value.size() == 24 && weekday(value.substr(0, 3)) && value[3] == ' ' &&
               value[7] == ' ' && value[10] == ' ' && value[19] == ' ') {
        month = month_number(value.substr(4, 3));
        day = digits(value[8] == ' ' ? value.substr(9, 1) : value.substr(8, 2));
        year = digits(value.substr(20, 4)); time = value.substr(11, 8);
    } else {
        const auto comma = value.find(',');
        if (comma == std::string_view::npos || !weekday(value.substr(0, comma), true) || value.substr(comma, 2) != ", ") return {};
        value.remove_prefix(comma + 2);
        if (value.size() != 22 || value[2] != '-' || value[6] != '-' || value[9] != ' ' || value.substr(18) != " GMT") return {};
        day = digits(value.substr(0, 2)); month = month_number(value.substr(3, 3));
        const auto short_year = digits(value.substr(7, 2));
        if (short_year < 0) return {};
        const auto current_year = reference_year(reference);
        year = current_year / 100 * 100 + short_year;
        if (year > current_year + 50) year -= 100;
        obsolete_year = true;
        time = value.substr(10, 8);
    }
    if (time.size() != 8 || time[2] != ':' || time[5] != ':') return {};
    const auto hour = digits(time.substr(0, 2)), minute = digits(time.substr(3, 2)), second = digits(time.substr(6, 2));
    constexpr std::array<int, 12> lengths = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 1601 || year > 9999 || month < 1 || month > 12 || day < 1 ||
        day > lengths[static_cast<std::size_t>(month - 1)] + (month == 2 && leap(year) ? 1 : 0) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return {};
    auto result = days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
    if (obsolete_year) {
        const auto bounded_reference = std::clamp(reference, days_from_civil(1601, 1, 1) * 86400,
                                                days_from_civil(10000, 1, 1) * 86400 - 1);
        const auto ref_year = reference_year(bounded_reference);
        int ref_month = 1;
        while (ref_month < 12 && days_from_civil(ref_year, ref_month + 1, 1) * 86400 <= bounded_reference) ++ref_month;
        const auto month_start = days_from_civil(ref_year, ref_month, 1) * 86400;
        const auto ref_day = static_cast<int>((bounded_reference - month_start) / 86400) + 1;
        const auto cutoff_day = std::min(ref_day, lengths[static_cast<std::size_t>(ref_month - 1)] + (ref_month == 2 && leap(ref_year + 50) ? 1 : 0));
        const auto cutoff = days_from_civil(ref_year + 50, ref_month, cutoff_day) * 86400 + (bounded_reference - month_start) % 86400;
        if (result > cutoff) {
            year -= 100;
            if (year < 1601 || (month == 2 && day == 29 && !leap(year))) return {};
            result = days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
        }
    }
    return result;
}

std::int64_t parse_retry_after_ms(std::string_view value, std::int64_t reference) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n')) value.remove_suffix(1);
    if (value.empty() || value.size() >= 128 || value.find('\0') != std::string_view::npos) return -1;
    if (value.front() >= '0' && value.front() <= '9') {
        if (!std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) return -1;
        std::int64_t seconds = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
        if (parsed.ec == std::errc::result_out_of_range) return maximum_ms;
        if (parsed.ec != std::errc{}) return -1;
        return std::min(seconds, maximum_ms / 1000) * 1000;
    }
    const auto date = parse_http_date_seconds(value, reference);
    if (!date) return -1;
    // Compare first, then bound before subtraction to avoid overflow on hostile references.
    if (*date <= reference) return 0;
    if (reference < *date - maximum_ms / 1000) return maximum_ms;
    const auto seconds = *date - reference;
    return std::min(seconds, maximum_ms / 1000) * 1000;
}

std::int64_t CooldownTable::remaining(const std::string& engine, Clock::time_point now) {
    const auto found = entries_.find(engine);
    if (found == entries_.end()) return 0;
    if (now >= found->second.until) {
        if (now - found->second.last_block >= history_window) entries_.erase(found);
        return 0;
    }
    const auto left = found->second.until - now;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(left);
    return ms.count() + (left > ms ? 1 : 0);
}

std::string CooldownTable::reason(const std::string& engine) const {
    const auto found = entries_.find(engine);
    return found == entries_.end() ? "" : found->second.reason;
}

unsigned CooldownTable::level(const std::string& engine) const {
    const auto found = entries_.find(engine);
    return found == entries_.end() ? 0 : found->second.level;
}

void CooldownTable::observe(EngineReport& report, const Options& options, Clock::time_point now) {
    report.cooldown_applied_ms = 0;
    report.cooldown_level = 0;
    (void)find_engine(report.name);  // State is bounded by the fixed registry.
    if (!options.enable_cooldown || !report.requested) return;
    const bool uncertain = report.status == "ok" && report.content_quality == "uncertain";
    if ((report.status == "ok" && !uncertain) || report.status == "empty") { entries_.erase(report.name); failures_.erase(report.name); return; }
    std::int64_t delay = 0;
    if (report.status == "rate_limited") delay = options.rate_limit_cooldown_ms;
    else if (report.status == "challenge") delay = options.challenge_cooldown_ms;
    else if (report.status == "timeout" || report.status == "low_relevance" || report.status == "parse_error" || uncertain) {
        auto& count = failures_[report.name];
        if (++count < 3) return;
        delay = 60000; count = 0;
    } else if (!(report.http_status == 503 && report.retry_after_ms >= 0)) return;
    const auto previous = entries_.find(report.name);
    unsigned current_level = 1;
    if (delay > 0 && options.progressive_cooldown && previous != entries_.end() &&
        now - previous->second.last_block < history_window && previous->second.client_delay_ms > 0) {
        const auto ceiling = std::max(delay, static_cast<std::int64_t>(options.cooldown_max_ms));
        delay = std::min(ceiling, std::max(delay, previous->second.client_delay_ms * 2));
        current_level = std::min(32U, previous->second.level + 1);
    }
    const auto applied = std::clamp(std::max(delay, report.retry_after_ms), std::int64_t{0}, maximum_ms);
    if (!applied) return;
    entries_[report.name] = {now + std::chrono::milliseconds(applied), uncertain ? "uncertain" : report.status, now, delay, current_level};
    report.cooldown_applied_ms = applied;
    report.cooldown_level = current_level;
}

unsigned CooldownTable::penalty(const std::string& engine) const {
    const auto failures = failures_.find(engine);
    const auto blocked = entries_.find(engine);
    return (failures == failures_.end() ? 0 : failures->second) + (blocked == entries_.end() ? 0 : blocked->second.level * 3);
}
void CooldownTable::note_request(const std::string& engine) {
    (void)find_engine(engine); // Fixed registry bounds the scheduling history.
    if (sequence_ == std::numeric_limits<std::uint64_t>::max()) { recent_.clear(); sequence_ = 0; }
    recent_[engine] = ++sequence_;
}
std::uint64_t CooldownTable::last_request(const std::string& engine) const {
    const auto found = recent_.find(engine); return found == recent_.end() ? 0 : found->second;
}
void CooldownTable::clear() { entries_.clear(); failures_.clear(); recent_.clear(); sequence_ = 0; }
}  // namespace edgesearch::detail
