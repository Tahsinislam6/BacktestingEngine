#pragma once

#include <optional>

#include "trade.hpp"

enum class OrderType {Open, Close};

struct Order
{
    OrderType type{OrderType::Open};
    int asset{};
    Direction direction{Direction::Long};
    double quantity{};
    std::optional<double> stopLoss;
    std::optional<double> takeProfit;

    int tradeId{};
    ExitReason reason{ExitReason::Signal};   // for a Close order: what the closed trade is recorded as
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
