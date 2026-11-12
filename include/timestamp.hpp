#pragma once

#include <cstdint>
#include <string_view>

// Converts text to seconds since 1970-01-01 00:00:00 UTC.
// Accepts (optionally wrapped in double quotes):
//   1735689600                 epoch seconds
//   2025-01-02                 date, read as midnight UTC
//   2025-01-01 22:04:00        date and time, read as UTC ('T' also allowed instead of the space)
// Throws std::invalid_argument for anything else, including impossible dates.
std::int64_t parseTimestamp(std::string_view text);

// ---- Preparation for aligning several series (multi-currency) ----
//
// Two series can describe the same trading day with different timestamps, for
// example a New York 5pm close that is 21:04 UTC in summer and 22:04 UTC in
// winter. Matching on exact timestamps would silently miss those. Instead,
// align on tradingDay(): a whole-number day index that does not move with the
// close time.
//
// sessionOffsetSeconds shifts the timestamp forward before the day is taken.
// Pick it so every timestamp of one trading day lands on the same calendar day.

// Bars stamped at the FX daily close (21:00-22:00 UTC plus a few minutes):
// +3 hours moves them just past midnight, onto the next calendar day, which is
// the trading date the vendor reports. Also lines up with sources that stamp
// the trading date at 00:00 UTC.
inline constexpr std::int64_t kFxDailyCloseOffsetSeconds = 3 * 3600;

// Days since 1970-01-01 of (timestamp + sessionOffsetSeconds), rounded down.
std::int64_t tradingDay(std::int64_t timestamp, std::int64_t sessionOffsetSeconds);
