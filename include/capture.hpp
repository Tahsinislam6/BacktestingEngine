#pragma once

#include <vector>

#include "aligned.hpp"
#include "backtester.hpp"
#include "trade.hpp"
#include "zigzag.hpp"

struct CaptureResult
{
    double captured{};      
    double available{};     
    double efficiency{};    
    int legs{};
    int legsRight{};        
    double rightShare{};    
};

// One asset. `bars` and `pivots` must belong together (pivot indexes refer to `bars`).
// Only trades whose `asset` equals the given one are used. A trade whose entry or exit time is
// not the timestamp of one of the bars is ignored.
CaptureResult captureEfficiency(const std::vector<Bar>& bars,
                                const std::vector<Pivot>& pivots,
                                const std::vector<ClosedTrade>& closed,
                                const std::vector<Trade>& open,
                                int asset = 0);

// The bars that one asset has in the aligned data, in order (days without a bar are skipped).
std::vector<Bar> barsOf(const AlignedData& data, int asset);

// ZigZag pivots for every asset. Compute this once per dataset: it does not depend on the strategy.
std::vector<std::vector<Pivot>> zigzagAll(const AlignedData& data, double atrMultiple = 3.0, int atrPeriod = 14);

// All assets together: captured and available are summed, then divided.
// Throws std::invalid_argument if pivots.size() != data.symbols.size().
CaptureResult captureEfficiency(const AlignedData& data,
                                const std::vector<std::vector<Pivot>>& pivots,
                                const BacktestResult& result);
