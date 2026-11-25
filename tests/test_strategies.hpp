#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "registry.hpp"
#include "strategy.hpp"

// Shared by the sweep, manager and API tests: a strategy with two whole-number parameters whose result can be worked out
// by hand, and a six-bar data file to run it on.
//
// "scaled_hold": waits `delay` bars (0..3, default 0), then buys `size` units (1..4, default 1) at that bar's close and
// holds to the end. On closes 100, 110, 120, 130, 140, 150 with cash 1000 its final profit is size * (150 - close[delay]),
// i.e. size * (50 - 10 * delay), and it closes no trade (so the number of closed trades is 0).
class ScaledHold : public Strategy
{
public:
    ScaledHold(int size, int delay) : size_(size), delay_(delay) {}

    std::vector<Order> onBar(const Bar&, const Portfolio&) override
    {
        const int bar = seen_++;
        if (bar == delay_)
            return { openOrder(Direction::Long, static_cast<double>(size_)) };
        return {};
    }

private:
    int size_;
    int delay_;
    int seen_{0};
};

inline void registerScaledHold(StrategyRegistry& registry)
{
    StrategyInfo info;
    info.name = "scaled_hold";
    info.description = "test strategy";
    info.params = {{"size", 1, 1, 4, true, "units bought"}, {"delay", 0, 0, 3, true, "bars to wait"}};
    registry.add(info, [](const NamedParams& p)
    {
        return std::make_unique<ScaledHold>(static_cast<int>(p.at("size")), static_cast<int>(p.at("delay")));
    });
}

// Six daily bars, closes 100, 110, 120, 130, 140, 150 (Stooq layout).
inline void writeSixBars(const std::filesystem::path& file)
{
    std::ofstream out(file);
    out << "<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>\n";
    for (int i = 0; i < 6; ++i)
    {
        const int close = 100 + 10 * i;
        out << "X,D,2020010" << (i + 1) << ",000000," << close << "," << close << "," << close << "," << close << ",0,0\n";
    }
}
