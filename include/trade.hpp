#pragma once

#include <cstdint>
#include <optional>

enum class Direction {Long, Short};

struct Trade
{
    int id{};
    Direction direction{Direction::Long};
    double quantity{};
    double entryPrice{};
    std::int64_t entryTime{};
    std::optional<double> stopLoss;
    std::optional<double> takeProfit;
};

enum class ExitReason {Signal, StopLoss, TakeProfit};

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

