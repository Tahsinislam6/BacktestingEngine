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
