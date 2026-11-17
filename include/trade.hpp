#pragma once

#include <cstdint>
#include <optional>

enum class Direction {Long, Short};

struct Trade
{
    int id{};
    int asset{};
    Direction direction{Direction::Long};
    double quantity{};
    double entryPrice{};
    std::int64_t entryTime{};
    std::optional<double> stopLoss;
    std::optional<double> takeProfit;
};

// Signal = closed by the strategy (e.g. the baseline crossed back); Breakeven = the strategy closed a runner
// that came back to its entry after the target half was taken; Trailing = the strategy closed a runner whose
// close fell back from its best by a multiple of the ATR.
enum class ExitReason {Signal, StopLoss, TakeProfit, Breakeven, Trailing};

struct ClosedTrade
{
    Trade trade;
    double exitPrice{};
    std::int64_t exitTime{};
    ExitReason reason{ExitReason::Signal};
    double pnl{};
};

inline double sign(Direction d)
{
    return d == Direction::Long ? 1.0 : -1.0;
}

inline double pnlOf(const Trade& t, double exitPrice)
{
    return (exitPrice - t.entryPrice) * t.quantity * sign(t.direction);
}

