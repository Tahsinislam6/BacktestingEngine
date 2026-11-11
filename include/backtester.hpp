#pragma once

#include <vector>

#include "bar.hpp"
#include "trade.hpp"
#include "strategy.hpp"

struct BacktestResult
{
    std::vector<double> equityCurve;
    std::vector<ClosedTrade> closedTrades;
    std::vector<Trade> openTrades;
    double finalEquity{};
};

BacktestResult runBacktest(const BarSeries& series, Strategy& strategy, double startingCash);