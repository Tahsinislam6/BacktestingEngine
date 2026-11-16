#pragma once

#include <cstddef>
#include <vector>

#include "bar.hpp"


struct Pivot
{
    std::size_t index{};   // position in the series
    double price{};        // the close at that position
    bool isHigh{};         // true = swing high, false = swing low
};


std::vector<Pivot> zigzagPivots(const std::vector<double>& closes,
                                const std::vector<double>& thresholds);

// Same, with the threshold set to atrMultiple * ATR(atrPeriod) at each bar
// (no pivots can be confirmed while the ATR is warming up).
// Throws std::invalid_argument if atrMultiple <= 0 or atrPeriod <= 0.
std::vector<Pivot> zigzag(const std::vector<Bar>& bars, double atrMultiple = 3.0, int atrPeriod = 14);
