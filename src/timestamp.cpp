#include "timestamp.hpp"

#include <charconv>
#include <stdexcept>
#include <string>

namespace
{
constexpr bool isLeap(int y) noexcept
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

constexpr int daysInMonth(int y, int m) noexcept
{
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && isLeap(y)) ? 29 : days[m - 1];
}

// Days since 1970-01-01 for a calendar date (Howard Hinnant's "days_from_civil").
constexpr std::int64_t daysFromCivil(int y, unsigned m, unsigned d) noexcept
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// Reads exactly `len` digits starting at `pos`. False if any of them is not 0-9.
bool readDigits(std::string_view s, std::size_t pos, std::size_t len, int& out) noexcept
{
    int v = 0;
    for (std::size_t i = pos; i < pos + len; ++i)
    {
        const char c = s[i];
        if (c < '0' || c > '9')
            return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

[[noreturn]] void bad(std::string_view text)
{
    throw std::invalid_argument("Bad timestamp: " + std::string(text));
}
}

std::int64_t parseTimestamp(std::string_view text)
{
    // Drop one pair of surrounding quotes by narrowing the view (no copy).
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);

    // Not date-shaped (YYYY-MM-...): treat it as plain epoch seconds.
    if (text.size() < 10 || text[4] != '-' || text[7] != '-')
    {
        std::int64_t value{};
        const char* end = text.data() + text.size();
        const auto result = std::from_chars(text.data(), end, value);
        if (result.ec != std::errc{} || result.ptr != end)
            bad(text);
        return value;
    }

    int year, month, day;
    if (!readDigits(text, 0, 4, year) || !readDigits(text, 5, 2, month) || !readDigits(text, 8, 2, day))
        bad(text);
    if (month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month))
        bad(text);

    int hour = 0, minute = 0, second = 0;
    if (text.size() > 10)
    {
        if (text.size() != 19 || (text[10] != ' ' && text[10] != 'T') || text[13] != ':' || text[16] != ':')
            bad(text);
        if (!readDigits(text, 11, 2, hour) || !readDigits(text, 14, 2, minute) || !readDigits(text, 17, 2, second))
            bad(text);
        if (hour > 23 || minute > 59 || second > 59)
            bad(text);
    }

    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400
         + hour * 3600 + minute * 60 + second;
}

std::int64_t tradingDay(std::int64_t timestamp, std::int64_t sessionOffsetSeconds)
{
    const std::int64_t shifted = timestamp + sessionOffsetSeconds;
    std::int64_t day = shifted / 86400;
    if (shifted % 86400 < 0)      // C++ division rounds toward zero; we want to round down
        --day;
    return day;
}
