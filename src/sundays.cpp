#include "sundays.hpp"

#include <algorithm>

namespace
{
    // Whole days since 1970-01-01 (rounded down, so times before 1970 work too).
    std::int64_t dayNumber(std::int64_t timestamp)
    {
        std::int64_t day = timestamp / kTimestampUnitsPerDay;
        if (timestamp % kTimestampUnitsPerDay < 0)
            --day;
        return day;
    }
}

bool isSunday(std::int64_t timestamp)
{
    // 1970-01-01 was a Thursday, so day 0 is weekday 3 when Monday is 0 and Sunday is 6.
    return ((dayNumber(timestamp) + 3) % 7 + 7) % 7 == 6;
}

BarSeries mergeSundayBars(const BarSeries& series)
{
    BarSeries out;
    out.symbol = series.symbol;
    out.timeframe = series.timeframe;

    const std::vector<Bar>& bars = series.bars;
    for (std::size_t i = 0; i < bars.size(); ++i)
    {
        const Bar& bar = bars[i];
        if (!isSunday(bar.timestamp))
        {
            out.bars.push_back(bar);
            continue;
        }

        // A Sunday bar: fold it into the next bar if that one is on the next day.
        if (i + 1 < bars.size() && dayNumber(bars[i + 1].timestamp) == dayNumber(bar.timestamp) + 1)
        {
            Bar merged = bars[i + 1];                       // keeps the Monday timestamp and close
            merged.open = bar.open;
            merged.high = std::max(bar.high, merged.high);
            merged.low = std::min(bar.low, merged.low);
            merged.volume = bar.volume + merged.volume;
            out.bars.push_back(merged);
            ++i;                                            // the Monday bar is used up
        }
        // otherwise there is nothing to join it to, so it is dropped
    }
    return out;
}
