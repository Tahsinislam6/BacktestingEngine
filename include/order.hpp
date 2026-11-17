#pragma once

#include <optional>

#include "trade.hpp"

// Open = a new trade; Close = close a trade at the close; MoveStop = give an open trade a new stop (from `stopLoss`),
// to be recorded as `reason` when it is hit. A MoveStop is filled after the stops were checked, so the new stop first
// applies on the next bar.
enum class OrderType {Open, Close, MoveStop};

struct Order
{
    OrderType type{OrderType::Open};
    int asset{};
    Direction direction{Direction::Long};
    double quantity{};
    std::optional<double> stopLoss;
    std::optional<double> takeProfit;

    int tradeId{};
    ExitReason reason{ExitReason::Signal};   // Close: what the closed trade is recorded as; MoveStop: what a hit of the new stop is
};

inline Order openOrder(Direction d, double qty,
                        std::optional<double> sl = std::nullopt,
                        std::optional<double> tp = std::nullopt,
                        int asset = 0)
{
    Order o;
    o.type = OrderType::Open;
    o.asset = asset;
    o.direction = d;
    o.quantity = qty;
    o.stopLoss = sl;
    o.takeProfit = tp;
    return o;
}            

inline Order closeOrder(int tradeId, ExitReason reason = ExitReason::Signal)
{
    Order o;
    o.type = OrderType::Close;
    o.tradeId = tradeId;
    o.reason = reason;
    return o;
}

// Move the stop of trade `tradeId` to `price`; a hit of it is recorded as `reason`.
inline Order moveStopOrder(int tradeId, double price, ExitReason reason = ExitReason::StopLoss)
{
    Order o;
    o.type = OrderType::MoveStop;
    o.tradeId = tradeId;
    o.stopLoss = price;
    o.reason = reason;
    return o;
}
