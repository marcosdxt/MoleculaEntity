#pragma once

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace MoleculaEntity {

/// Conversion between the text the database stores and `system_clock::time_point`.
///
/// **Everything here is UTC, and that isn't an aesthetic preference.** SQLite's
/// `CURRENT_TIMESTAMP` — the default for the `created_at`/`updated_at` columns and
/// what the `updated_at` trigger writes — produces UTC. Reading that text with
/// `mktime`, which interprets LOCAL time, shifts every timestamp by the machine's
/// offset: on a host at UTC−3 the record travels three hours into the past,
/// silently, and no test running in UTC ever notices.
///
/// The functions below never consult the system timezone.
namespace time_utils {

/// Days since 1970-01-01 for a civil calendar date, without relying on `timegm`
/// (which doesn't exist everywhere) or `mktime` (which is local).
/// Howard Hinnant's `days_from_civil`.
[[nodiscard]] inline int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) noexcept
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153U * (m + (m > 2 ? -3U : 9U)) + 2U) / 5U + d - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

/// The inverse: civil date from days since the epoch.
inline void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) noexcept
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    const int64_t yr = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const unsigned mp = (5U * doy + 2U) / 153U;
    d = doy - (153U * mp + 2U) / 5U + 1U;
    m = mp + (mp < 10U ? 3U : -9U);
    y = yr + (m <= 2U);
}

/// Accepts what SQLite writes and what people write by hand:
///
///     2026-09-16 23:14:11        (the CURRENT_TIMESTAMP format)
///     2026-09-16T23:14:11Z       (ISO 8601)
///     2026-09-16T23:14:11.250Z   (with a fraction — truncated to the second)
///     2026-09-16                 (midnight UTC)
///
/// Returns empty when the text is none of those: an invalid date has to show up
/// as absence, never as a plausible and wrong instant.
[[nodiscard]] inline std::optional<std::chrono::system_clock::time_point>
parseUtc(std::string_view text)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;

    const std::string buffer(text);
    int consumed = 0;
    const int fields = std::sscanf(buffer.c_str(), "%4d-%2d-%2d%n", &year, &month, &day, &consumed);
    if (fields != 3) {
        return std::nullopt;
    }

    if (static_cast<std::size_t>(consumed) < buffer.size()) {
        const char separator = buffer[static_cast<std::size_t>(consumed)];
        if (separator != ' ' && separator != 'T' && separator != 't') {
            return std::nullopt;
        }
        if (std::sscanf(buffer.c_str() + consumed + 1, "%2d:%2d:%2d", &hour, &minute, &second) != 3) {
            return std::nullopt;
        }
    }

    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return std::nullopt;
    }

    const int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;

    return std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
}

/// The format SQLite uses for `CURRENT_TIMESTAMP`, in UTC — for anyone writing a
/// timestamp by hand who wants it to compare against the automatic ones.
[[nodiscard]] inline std::string formatUtc(std::chrono::system_clock::time_point tp)
{
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();

    int64_t days = seconds / 86400;
    int64_t rest = seconds % 86400;
    if (rest < 0) {          // instants before 1970 round down
        rest += 86400;
        --days;
    }

    int64_t year = 0;
    unsigned month = 0, day = 0;
    civilFromDays(days, year, month, day);

    // 64 and not 32: the year is an int64_t and the compiler can't prove it fits.
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02u %02lld:%02lld:%02lld",
                  static_cast<long long>(year), month, day,
                  static_cast<long long>(rest / 3600),
                  static_cast<long long>((rest % 3600) / 60),
                  static_cast<long long>(rest % 60));
    return buffer;
}

}  // namespace time_utils
}  // namespace MoleculaEntity
