#pragma once

#include <cstdint>
#include <optional>

enum class Direction {Long, Short};

// Why a trade ended. StopLoss / TakeProfit = a stop or target order was hit; Signal = closed by the strategy (e.g. the
// baseline crossed back); Breakeven / Trailing = a moved stop was hit (or the strategy closed the runner at the close
// because the stop could not be placed on the right side of the price); Indicator = an exit indicator turned against it.
enum class ExitReason {Signal, StopLoss, TakeProfit, Breakeven, Trailing, Indicator};

struct Trade
{
    int id{};
    int asset{};
    Direction direction{Direction::Long};
    double quantity{};
    double entryPrice{};
    std::int64_t entryTime{};
    std::optional<double> stopLoss;             // the stop now; a strategy can move it (OrderType::MoveStop)
    std::optional<double> takeProfit;
    std::optional<double> initialStop;          // the stop the trade was opened with; never changes (R is measured from it)
    ExitReason stopReason{ExitReason::StopLoss};   // what a hit of `stopLoss` is recorded as
    double swap{0.0};                           // financing accrued so far (positive = earned); part of the result
};

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

