#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "aligned.hpp"
#include "capture.hpp"
#include "costs.hpp"
#include "metrics.hpp"
#include "multi_backtester.hpp"
#include "param_space.hpp"

// Runs one strategy for every parameter set, scores each run, and ranks them.
//
//   expandGrid(ranges) -> runGrid(data, grid, factory) -> rankResults(results, score, minTrades)
//   optimise(...) does all three.
//
// The factory builds a FRESH strategy for a parameter set and the number of assets. It is called from
// several threads at once, so it must not change shared state.
using StrategyFactory = std::function<std::unique_ptr<MultiStrategy>(const Params&, std::size_t assets)>;

struct GridOptions
{
    double startingCash{100000.0};
    double periodsPerYear{260.0};
    double zigzagMultiple{3.0};     // ZigZag threshold = zigzagMultiple * ATR(atrPeriod), for capture efficiency
    int atrPeriod{14};
    int threads{0};                 // 0 = one per hardware core; 1 = run in the calling thread
    CostModel costs;                // default: no cost. Spreads are looked up by the data's symbols
};

// The scores of one run.
struct Evaluation
{
    Params params;
    Metrics metrics;
    CaptureResult capture;
};

// One run. `pivots` is zigzagAll(data, ...): it does not depend on the strategy, so compute it once.
// Throws std::invalid_argument if the factory returns no strategy.
Evaluation evaluate(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                    const std::vector<std::vector<Pivot>>& pivots, const GridOptions& options = GridOptions{});

// Calls body(0), body(1), ... body(count-1), spread over `threads` threads (0 = one per hardware core,
// 1 = all in the calling thread). Each index is used exactly once. The body must only touch its own slot
// of any shared output. If any call throws, the first exception is rethrown here once the threads have
// stopped, and indexes not yet started are skipped. Throws std::invalid_argument if threads < 0.
void parallelFor(std::size_t count, int threads, const std::function<void(std::size_t)>& body);

// Every parameter set, run in parallel. results[i] belongs to grid[i], whatever the number of threads,
// and the numbers do not depend on the thread count. If any run throws, the exception is rethrown here.
// Throws std::invalid_argument if options.threads < 0.
std::vector<Evaluation> runGrid(const AlignedData& data, const std::vector<Params>& grid,
                                const StrategyFactory& factory, const GridOptions& options = GridOptions{});

// ---------- ranking ----------

enum class Objective { Sharpe, Sortino, Calmar, TotalReturn, CaptureEfficiency };

// How good a run is: higher is better. NaN means "can't be scored".
using ScoreFn = std::function<double(const Evaluation&)>;

ScoreFn scoreBy(Objective objective);

struct Ranked
{
    Evaluation evaluation;
    double score{};
};

// Best first. Runs with fewer than minTrades closed trades are dropped (too few to mean anything),
// and so are runs whose score is NaN. Equal scores keep their grid order.
std::vector<Ranked> rankResults(const std::vector<Evaluation>& results, const ScoreFn& score, int minTrades);

struct OptimiseOptions
{
    GridOptions grid;
    Objective objective{Objective::Sharpe};
    int minTrades{30};
};

// Expand the ranges, run every combination, rank the results.
std::vector<Ranked> optimise(const AlignedData& data, const std::vector<ParamRange>& ranges,
                             const StrategyFactory& factory, const OptimiseOptions& options = OptimiseOptions{});
