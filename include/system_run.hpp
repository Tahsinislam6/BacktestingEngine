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
#include "swap.hpp"

// Runs one strategy once over some data and scores it.
//
// The factory builds a FRESH strategy for a parameter set and the number of assets. It may be called from
// several threads at once, so it must not change shared state.
using StrategyFactory = std::function<std::unique_ptr<MultiStrategy>(const Params&, std::size_t assets)>;

struct RunOptions
{
    double startingCash{100000.0};
    double periodsPerYear{260.0};
    double zigzagMultiple{3.0};     // ZigZag threshold = zigzagMultiple * ATR(atrPeriod), for capture efficiency
    int atrPeriod{14};
    int threads{0};                 // 0 = one per hardware core; 1 = run in the calling thread
    CostModel costs;                // default: no cost. Spreads are looked up by the data's symbols
    SwapModel swap;                 // default: no swap (financing); see swap.hpp
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
                    const std::vector<std::vector<Pivot>>& pivots, const RunOptions& options = RunOptions{});

// Calls body(0), body(1), ... body(count-1), spread over `threads` threads (0 = one per hardware core,
// 1 = all in the calling thread). Each index is used exactly once. The body must only touch its own slot
// of any shared output. If any call throws, the first exception is rethrown here once the threads have
// stopped, and indexes not yet started are skipped. Throws std::invalid_argument if threads < 0.
void parallelFor(std::size_t count, int threads, const std::function<void(std::size_t)>& body);
