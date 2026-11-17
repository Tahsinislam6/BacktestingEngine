#pragma once

#include <string>
#include <vector>

#include "aligned.hpp"
#include "backtester.hpp"
#include "order.hpp"
#include "portfolio.hpp"
#include "swap.hpp"

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
// swap: for every snapshot after the first, each trade open at the end of the previous one accrues the nights in between
// (the gap between the two snapshots in whole calendar days, so a weekend is 3): swapAmount of its quantity, the PREVIOUS
// close and the rate difference in force at the previous snapshot. It is part of the trade's result (pnl, equity, R).
// An inactive schedule (the default) changes nothing.
BacktestResult runMultiBacktest(const AlignedData& data, MultiStrategy& strategy, double startingCash,
                                const std::vector<double>& spreads = {}, const SwapSchedule& swap = {});