#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "optimiser.hpp"

// Guards against overfitting: a parameter set that wins on the data it was tuned on must still work on
// data it has never seen.
//
//   trainTestSplit   tune on the first part, score the winner once on the rest
//   walkForward      repeat that over rolling windows and chain the out-of-sample results
//   neighbourStability   is the winner a plateau or a lone spike?
//
// Windows are counted in snapshots (days), not in time.

// ---------- windows ----------

// The snapshots [from, to). Throws std::invalid_argument if from > to or to > snapshots.size().
AlignedData sliceSnapshots(const AlignedData& data, std::size_t from, std::size_t to);

// Score the run over snapshots [start, end). The strategy first sees up to `warmup` snapshots before
// `start` (fewer if there aren't that many), so its indicators are already warm when scoring begins.
//   - the equity curve covers only [start, end) and is rebased to start at startingCash
//   - closed trades are those that closed inside the window. A trade opened during warm-up and closed in
//     the window counts with its whole profit.
//   - open trades at the end are kept as they are
//   - capture efficiency uses only the legs from the window's start onward
struct WindowSpec
{
    std::size_t start{};
    std::size_t end{};
    std::size_t warmup{};
};

struct WindowRun
{
    BacktestResult result;
    CaptureResult capture;
};

// Throws std::invalid_argument if start > end or end > data.snapshots.size(), or the factory gives nothing.
WindowRun runWindow(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                    const WindowSpec& window, const GridOptions& options = GridOptions{});

Evaluation evaluateWindow(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                          const WindowSpec& window, const GridOptions& options = GridOptions{});

// Like runGrid, over one window. results[i] belongs to grid[i].
std::vector<Evaluation> runGridWindow(const AlignedData& data, const std::vector<Params>& grid,
                                      const StrategyFactory& factory, const WindowSpec& window,
                                      const GridOptions& options = GridOptions{});

// ---------- train / test ----------

struct ValidationOptions
{
    OptimiseOptions optimise;

    // How many snapshots of history a parameter set needs before a test window starts (e.g. its period).
    // Empty = none. Only used for test windows; training windows simply start cold.
    std::function<std::size_t(const Params&)> warmupBars;
};

struct TrainTestResult
{
    std::size_t splitIndex{};            // first snapshot of the test window
    std::vector<Ranked> ranking;         // every run, scored on the training window, best first
    std::optional<Evaluation> test;      // the winner scored on the test window (empty if nothing ranked)
    double testScore{};                  // the same objective on the test window (NaN if nothing ranked)
};

// Train on the first trainFraction of the snapshots, test on the rest.
// Throws std::invalid_argument unless 0 < trainFraction < 1.
TrainTestResult trainTestSplit(const AlignedData& data, const std::vector<ParamRange>& ranges,
                               const StrategyFactory& factory, double trainFraction,
                               const ValidationOptions& options = ValidationOptions{});

// ---------- walk-forward ----------

struct FoldWindows
{
    std::size_t trainStart{}, trainEnd{}, testStart{}, testEnd{};     // [start, end)
};

// Rolling windows: train, then the test right after it, then everything moves forward by stepBars
// (0 = by testBars, so test windows do not overlap). Only whole windows are returned.
// Throws std::invalid_argument if trainBars or testBars is 0.
std::vector<FoldWindows> walkForwardWindows(std::size_t snapshots, std::size_t trainBars,
                                            std::size_t testBars, std::size_t stepBars = 0);

struct Fold
{
    FoldWindows windows;
    std::optional<Ranked> best;          // winner on the training window (empty if nothing ranked)
    std::optional<Evaluation> test;      // winner on the test window
};

struct WalkForwardResult
{
    std::vector<Fold> folds;

    // The test windows chained into one out-of-sample run. Each window's profit is added on to where the
    // previous one ended. A fold with no winner stays flat in cash. Positions still open when a window
    // ends are not counted as trades (their profit is in the equity curve).
    BacktestResult stitched;
    Metrics metrics;                     // of the stitched run
    CaptureResult capture;               // captured and available summed over the test windows
};

WalkForwardResult walkForward(const AlignedData& data, const std::vector<ParamRange>& ranges,
                              const StrategyFactory& factory, std::size_t trainBars, std::size_t testBars,
                              const ValidationOptions& options = ValidationOptions{}, std::size_t stepBars = 0);

// ---------- stability ----------

// The neighbours of a parameter set are the grid points one step away in exactly one parameter
// (everything else equal): two per parameter, fewer at the edge of the grid.
struct StabilityResult
{
    double score{};                      // the chosen set's score
    std::size_t neighbours{};            // neighbours that could be scored
    double meanNeighbour{};              // NaN if there are none
    double worstNeighbour{};             // NaN if there are none
};

// `results` are runs over the grid of `ranges` (any order). Neighbours with fewer than minTrades closed
// trades, or a NaN score, are skipped. Throws std::invalid_argument if `chosen` is not in `results`.
StabilityResult neighbourStability(const std::vector<Evaluation>& results, const std::vector<ParamRange>& ranges,
                                   const Params& chosen, const ScoreFn& score, int minTrades = 0);
