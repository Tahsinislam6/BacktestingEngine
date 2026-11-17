#pragma once

#include <cstdint>

#include "bar.hpp"

// FX data is often cut at midnight UTC, but the FX week opens at 22:00 UTC on Sunday. That leaves a tiny
// two-hour "Sunday" bar before every Monday, which would count as a trading day of its own.

// True if the timestamp (milliseconds since 1970 UTC) falls on a Sunday in UTC.
bool isSunday(std::int64_t timestamp);

// Folds each Sunday bar into the bar right after it, when that bar is on the next calendar day (Monday):
//   open  = the Sunday bar's open
//   high  = the higher of the two highs         low = the lower of the two lows
//   close = the Monday bar's close              timestamp = the Monday bar's timestamp
// A Sunday bar with no bar on the next day (the end of the data, or a gap) is dropped.
// All other bars are unchanged. Symbol and timeframe are kept. The bars must be sorted by time.
BarSeries mergeSundayBars(const BarSeries& series);
