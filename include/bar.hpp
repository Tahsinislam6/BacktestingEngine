#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Bar timestamps are kept exactly as they appear in the data files: milliseconds since 1970-01-01 UTC.
// This is the only place that says so, for the few calculations that need a real duration.
inline constexpr std::int64_t kTimestampUnitsPerDay = 86'400'000;

struct Bar
{
    std::int64_t timestamp{};
    double open{};
    double high{};
    double low{};
    double close{};
    double volume{};       // as in the data file's "volume" column (0 when the file has none); an FX feed's tick/size volume,
                           // only meaningful relative to its own recent average
};

struct BarSeries
{
    std::string symbol;
    std::string timeframe;
    std::vector<Bar> bars;
};

