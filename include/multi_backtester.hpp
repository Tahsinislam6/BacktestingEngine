#pragma once

#include <string>
#include <vector>

#include "aligned.hpp"
#include "backtester.hpp"
#include "order.hpp"
#include "portfolio.hpp"

class MultiStrategy
{
    public:
        virtual ~MultiStrategy() = default;

        // Called once before the first snapshot with the asset names (index = asset). Strategies that need to
        // know which pair is which (e.g. currency exposure) override it; the default ignores it.
        virtual void setSymbols(const std::vector<std::string>& symbols) { (void)symbols; }

        virtual std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio) = 0;
};

// spreads[a] = asset a's full spread in price units (missing entries and an empty list mean no cost).
// Every fill is at the close, worse by half the spread: buying (opening a long, closing a short) pays more,
// selling (opening a short, closing a long) receives less. Stop and target exits are worse by half too.
// Open trades are valued at the plain close.
BacktestResult runMultiBacktest(const AlignedData& data, MultiStrategy& strategy, double startingCash,
                                const std::vector<double>& spreads = {});