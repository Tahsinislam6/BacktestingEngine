#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct Bar
{
    std::int64_t timestamp{};
    double open{};
    double high{};
    double low{};
    double close{};
};

struct BarSeries
{
    std::string symbol;
    std::string timeframe;
    std::vector<Bar> bars;
};

