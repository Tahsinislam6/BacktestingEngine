#pragma once

#include <cmath>
#include <stdexcept>
#include <vector>

#include "order.hpp"

// The entry of one NNFX signal: two open orders on the same asset.
//   [0] the half that takes profit at takeProfitAtr * ATR
//   [1] the runner, with no take-profit
// Both share the stop loss, stopAtr * ATR from the close. Each half is sized so that hitting the stop
// loses riskPerHalf of equity, so the pair risks twice that (1% + 1% = the usual 2% per signal).
struct NnfxEntryRules
{
    double riskPerHalf{0.01};       // share of equity lost if the stop is hit, per half
    double stopAtr{1.5};
    double takeProfitAtr{1.0};
};

// Throws std::invalid_argument unless riskPerHalf is in (0, 0.5] and stopAtr and takeProfitAtr are positive.
inline void validateNnfxRules(const NnfxEntryRules& rules)
{
    auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    if (!positive(rules.riskPerHalf) || rules.riskPerHalf > 0.5)
        throw std::invalid_argument("riskPerHalf must be above 0 and at most 0.5");
    if (!positive(rules.stopAtr) || !positive(rules.takeProfitAtr))
        throw std::invalid_argument("stopAtr and takeProfitAtr must be positive");
}

// `close` is the signal bar's close, `atr` the ATR at that bar, `equity` the current account equity.
// Throws std::invalid_argument if any input is not a positive number, if riskPerHalf is above 0.5,
// or if a long's stop would be at or below zero.
// Spread is not part of the sizing: the stop distance is measured from the plain close.
inline std::vector<Order> nnfxEntryOrders(Direction direction, double close, double atr, double equity, int asset,
                                          const NnfxEntryRules& rules = NnfxEntryRules{})
{
    auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    if (!positive(close) || !positive(atr) || !positive(equity))
        throw std::invalid_argument("close, ATR and equity must be positive numbers");
    validateNnfxRules(rules);

    const double stopDistance = rules.stopAtr * atr;
    const double targetDistance = rules.takeProfitAtr * atr;
    const bool isLong = direction == Direction::Long;

    const double stop = isLong ? close - stopDistance : close + stopDistance;
    const double target = isLong ? close + targetDistance : close - targetDistance;
    if (stop <= 0.0)
        throw std::invalid_argument("the stop loss would be at or below zero");

    const double quantity = equity * rules.riskPerHalf / stopDistance;

    return {openOrder(direction, quantity, stop, target, asset),
            openOrder(direction, quantity, stop, std::nullopt, asset)};
}
