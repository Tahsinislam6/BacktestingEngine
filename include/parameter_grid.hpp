#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "backtester.hpp"
#include "density.hpp"
#include "metrics.hpp"
#include "strategy.hpp"

// One parameter and the values to try for it, e.g. {"lookback", {10, 20, 30}}.
struct ParameterAxis
{
    std::string name;
    std::vector<double> values;
};

// One value per axis, in axis order: the settings for one run.
using Params = std::vector<double>;

// Builds a FRESH strategy for one set of parameters (a strategy keeps state while it runs, so a run needs its own).
// Example: [](const Params& p) { return std::make_unique<MyStrategy>(static_cast<int>(p[0]), p[1]); }
using StrategyFactory = std::function<std::unique_ptr<Strategy>(const Params&)>;

struct GridResult
{
    std::vector<ParameterAxis> axes;
    std::vector<Metrics> metrics;   // one per cell, row-major (the LAST axis changes fastest), see density.hpp
};

// The number of values on each axis (the `shape` that density.hpp works with). Throws if there is no axis or one is empty.
std::vector<std::size_t> gridShape(const std::vector<ParameterAxis>& axes);

// `periodsPerYear` (bars per year) is passed to computeMetrics for every run: it changes CAGR, Sharpe, Sortino and MAR.
// Runs the strategy once for every combination of axis values on `series` and keeps each run's Metrics.
// Count the cells: every one is an idea tried on this data. Throws std::invalid_argument for no axes, an empty axis, an
// empty factory or a factory that returns no strategy; anything the factory throws (e.g. an invalid parameter) passes through.
GridResult sweep(const BarSeries& series, const std::vector<ParameterAxis>& axes, const StrategyFactory& factory,
                 double startingCash, double periodsPerYear = 260.0);

// Turns a sweep into a DensityGrid: `metric` picks one number from each run's Metrics (e.g. expectancy). A cell is valid
// only if its run closed at least `minTrades` trades and the number is a real number.
DensityGrid toDensityGrid(const GridResult& result, const std::function<double(const Metrics&)>& metric, int minTrades);
