#pragma once

#include <vector>

#include "aligned.hpp"
#include "backtester.hpp"
#include "order.hpp"
#include "portfolio.hpp"

class MultiStrategy
{
    public:
        virtual ~MultiStrategy() = default;

        virtual std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio) = 0;
};

BacktestResult runMultiBacktest(const AlignedData& data, MultiStrategy& strategy, double startingCash);