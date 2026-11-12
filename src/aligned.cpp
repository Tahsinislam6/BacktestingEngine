#include "aligned.hpp"

#include <map>
#include <stdexcept>

AlignedData align(const std::vector<BarSeries>& series)
{
    AlignedData out;
    const std::size_t n = series.size(); 

    std::map<std::int64_t, std::vector<std::optional<Bar>>> rows;

    for (std::size_t i = 0; i < n; ++i)
    {
        out.symbols.push_back(series[i].symbol);

        for (std::size_t b = 0; b < series[i].bars.size(); ++b)
        {
            const Bar& bar = series[i].bars[b];

            if (b > 0 && bar.timestamp <= series[i].bars[b - 1].timestamp)
                throw std::invalid_argument("Series not strictly increasing: " + series[i].symbol);

            // Get the row for this timestamp (the map creates it if it is new).
            std::vector<std::optional<Bar>>& row = rows[bar.timestamp];

            // A brand new row is empty: give it n empty slots, one per series.
            if (row.empty())
                row.resize(n);

            // Put this bar in its series' slot. Series with no bar here stay empty.
            row[i] = bar;
        }
    }

    // Turn the sorted rows into snapshots (move = hand over the vector, no copy).
    for (auto& [timestamp, row] : rows)
        out.snapshots.push_back(Snapshot{timestamp, std::move(row)});

    return out;
}
