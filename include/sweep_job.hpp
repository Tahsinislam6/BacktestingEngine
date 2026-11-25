#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include "metrics.hpp"
#include "registry.hpp"

// A density sweep job: run a strategy over a grid of two parameters on real data and on shuffled copies of it, and report
// how much of the grid gives good results (see density.hpp and null_series.hpp). It is the job version of the research
// program `density_sweep`, with the same rules:
//
//  - Nothing is picked for the user. The metric, threshold, radius, minimum trades, number of shuffled copies and block
//    length are all part of the request, and a note (what is expected, what counts as a pass) is required.
//  - Exactly two axes. Every axis value is checked against its parameter's limits before anything runs.
//  - The shuffled copies use seeds 1..nullRuns, so a run can be repeated exactly. The result does not depend on how many
//    threads ran the copies.
//  - The result is numbers only. Whether it counts as a pass is for the user to judge against their own note.
//  - "Ideas tried" is cells x (1 + nullRuns): every cell on the real data and on each copy.

// One axis of the grid: the values from, from + step, ... up to and including `to` when the step lands on it.
struct AxisSpec
{
    std::string param;      // a parameter of the strategy
    double from{};
    double to{};
    double step{};
};

struct SweepSpec
{
    std::string strategy;
    NamedParams params;             // fixed values for the parameters that are not swept (the rest take their defaults)
    std::string dataset;            // a file name in the data folder
    std::vector<AxisSpec> axes;     // exactly two; the first is the grid's rows, the second its columns
    std::string metric;             // mar | cagr | sharpe | sortino | total-return | profit-factor | expectancy
    double threshold{};             // a cell passes when its metric is ABOVE this
    int radius{};                   // plateau neighbourhood: 1 = the cell and its 8 neighbours
    int minTrades{};                // a cell with fewer closed trades is invalid (never passes, still counted)
    int nullRuns{};                 // shuffled copies of the data (seeds 1..nullRuns)
    int block{};                    // block length in bars for the shuffle; 1 destroys every dependency
    double cash{10000.0};
    double periodsPerYear{260.0};   // bars per year, for CAGR / Sharpe / Sortino / MAR
    std::string note;               // what is expected and what counts as a pass; must not be blank
    std::string layout{"auto"};     // how the dataset file is read: a name from listLayouts (layouts.hpp)
};

struct SweepOutput
{
    SweepSpec spec;                                 // as run: params holds every parameter that was not swept, defaults included
    std::size_t bars{};
    std::vector<std::vector<double>> axisValues;    // the values of each axis
    std::size_t cells{};

    // one entry per cell, row-major (the LAST axis changes fastest), real data
    std::vector<int> trades;
    std::vector<double> values;                     // the metric (may be not-a-number)
    std::vector<bool> valid;
    std::size_t validCells{};
    double passDensity{};
    double plateauDensity{};
    double peakOverNeighbours{};

    // the same grid on each shuffled copy (index k - 1 is seed k)
    std::vector<double> nullPass;
    std::vector<double> nullPlateau;
    double nullPassMedian{}, nullPass95{}, nullPlateauMedian{}, nullPlateau95{};
    std::size_t nullPassAtLeast{};                  // copies whose pass density is at or above the real one
    std::size_t nullPlateauAtLeast{};

    std::size_t ideasTried{};                       // cells x (1 + nullRuns)
};

// Called with (steps done, total steps) as the sweep goes: one step for the real grid and one for each shuffled copy.
// Calls never overlap, so the function needs no locking of its own; it may be called from any thread.
using ProgressFn = std::function<void(std::size_t, std::size_t)>;

// The values of one axis. Throws std::invalid_argument for an empty name, a number that is not finite, a step that is not
// above 0, `to` below `from`, or more than 100,000 values.
std::vector<double> expandAxis(const AxisSpec& axis);

// "mar", "cagr", "sharpe", "sortino", "total-return", "profit-factor" or "expectancy" -> the function that picks that
// number from a run's Metrics. Throws std::invalid_argument for any other name.
std::function<double(const Metrics&)> metricByName(const std::string& name);

// Everything that can be checked without reading the data: a known strategy, a non-blank note, a known metric, a dataset
// name that is a file in the folder, a known layout, exactly two different axes that are parameters of the strategy with every value inside
// the limits, no fixed value for a swept parameter, radius 0..20, minTrades >= 0, nullRuns 1..1000, block 1..1000, cash and
// periodsPerYear above 0, a finite threshold, at most 50,000 cells and at most 2,000,000 backtests (cells x (1 + nullRuns)).
// Throws std::invalid_argument. runSweep calls it first; the run manager calls it at submit time.
void validateSweepSpec(const StrategyRegistry& registry, const std::string& dataDir, const SweepSpec& spec);

// The spec with `params` completed: every parameter that is not swept, with its default if the request left it out.
SweepSpec resolveSweepSpec(const StrategyRegistry& registry, const SweepSpec& spec);

// cells x (1 + nullRuns), for a spec that passed validateSweepSpec.
std::size_t sweepIdeas(const StrategyRegistry& registry, const SweepSpec& spec);

// Thrown by a job that noticed it was cancelled and stopped before finishing.
struct JobCancelled : std::runtime_error
{
    JobCancelled() : std::runtime_error("cancelled") {}
};

// Asked often by a long job; true means "stop now". It may be called from any thread.
using CancelFn = std::function<bool()>;

// Runs the sweep. `threads` copies run at once (0 = one per CPU core). Throws what validateSweepSpec throws, and
// std::runtime_error if the dataset cannot be read or has no bars. If `cancelled` returns true the sweep stops at its next
// step (before the real grid, or before the next shuffled copy) and throws JobCancelled; a sweep nobody cancels gives the
// same result with or without the check.
SweepOutput runSweep(const StrategyRegistry& registry, const std::string& dataDir, const SweepSpec& spec,
                     const ProgressFn& progress = {}, unsigned threads = 0, const CancelFn& cancelled = {});
