#pragma once

#include <optional>

#include "trade.hpp"

enum class OrderType {Open, Close};

struct Order
{
    OrderType type{OrderType::Open};

    Direction direction{Direction::Long};
    double quantity{};
    std::optional<double> stopLoss;
    std::optional<double> takeProfit;

    int tradeId{};
};

inline Order openOrder(Direction d, double qty,
                        std::optional<double> sl = std::nullopt,
                        std::optional<double> tp = std::nullopt)
{
    Order o;
    o.type = OrderType::Open;
    o.direction = d;
    o.quantity = qty;
    o.stopLoss = sl;
    o.takeProfit = tp;
    return o;
}            

inline Order closeOrder(int tradeId)
{
    Order o;
    o.type = OrderType::Close;
    o.tradeId = tradeId;
    return o;
}
