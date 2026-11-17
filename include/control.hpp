#pragma once

#include <functional>
#include <string>
#include <vector>

#include "nnfx_strategy.hpp"
#include "optimiser.hpp"

// The direction control: does the baseline's direction add anything over luck?
//
// Both arms trade the same signals (the baseline crosses) with the same stops, targets, sizing and costs:
//   baseline arm   enters in the direction the baseline gives
//   random arm     enters in a coin-flip direction; run once per seed
// The baseline's result is then ranked among the random runs. Neither arm uses the cross-back exit (a random
// trade may start against the line), so the options must have exitOnCross == false.

// How one run did. Closed trades only; NaN where there is nothing to average.
struct ArmResult
{
    double sharpe{};
    double totalReturn{};
    double expectancyR{};      // mean R per trade
    double totalR{};
    int trades{};              // closed trades with a stop
};

// Share of the finite `samples` that are below `value`; samples equal to it count half. NaN if `value` is NaN
// or there is no finite sample.
double percentileRank(double value, const std::vector<double>& samples);

struct ControlResult
{
    ArmResult baseline;
    std::vector<ArmResult> random;      // seed 1, 2, ... in order
    double sharpeRank{};                // share of random runs the baseline beats
    double expectancyRank{};
    double totalRRank{};
};

using NnfxFactoryMaker = std::function<StrategyFactory(const NnfxOptions&)>;

// Runs the baseline arm once and the random arm for seeds 1..seeds, over `data`, with `grid` giving the cash,
// costs and thread count. The numbers do not depend on the thread count.
// Throws std::invalid_argument if seeds < 1, options.exitOnCross is true, or options.randomSeed is set.
ControlResult runDirectionControl(const AlignedData& data, const Params& params, const NnfxFactoryMaker& make,
                                  const NnfxOptions& options, int seeds, const GridOptions& grid = GridOptions{});

// A short table of one control result. `label` names the data (e.g. "out-of-sample").
std::string formatControl(const std::string& label, const ControlResult& result);
