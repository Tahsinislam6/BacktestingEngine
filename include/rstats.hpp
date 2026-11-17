#pragma once

#include <vector>

#include "trade.hpp"

// R-multiples: a trade's profit divided by what it risked, quantity * |entry price - stop loss|.
// +1R = won what it risked; -1R = lost it (a stop hit); -0.5R = lost half of it. Spread and any other
// cost are in the profit, so they show up as a loss of R.
// Only closed trades with a stop loss have an R; open trades are not counted.

struct ExitStats
{
    int count{};
    double totalR{};
    double avgR() const { return count > 0 ? totalR / count : 0.0; }
};

struct RStats
{
    int trades{};              // trades that have an R
    int wins{};                // R > 0 (a trade with R == 0 is neither a win nor a loss)
    double winRate{};          // wins / trades; NaN with no trades
    double avgWinR{};          // NaN with no wins
    double avgLossR{};         // negative; NaN with no losses
    double expectancyR{};      // mean R over all trades; NaN with no trades
    double totalR{};
    double profitFactor{};     // R won / R lost: infinity with no losses, NaN with neither wins nor losses

    // the trades split by how they ended
    ExitStats stop;            // StopLoss
    ExitStats target;          // TakeProfit
    ExitStats breakeven;       // Breakeven
    ExitStats trailing;        // Trailing (the runner fell back from its best close)
    ExitStats signal;          // Signal (the baseline crossed back)
};

struct RReport
{
    RStats all;
    RStats targetHalf;         // trades that had a take-profit
    RStats runner;             // trades without one
    int openAtEnd{};           // trades still open when the data ended: not valued here
    int withoutStop{};         // closed trades left out because they have no stop (or a stop at the entry)
};

// `openAtEnd` is only recorded in the report.
RReport computeRReport(const std::vector<ClosedTrade>& closed, int openAtEnd = 0);
