#include "validation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

AlignedData sliceSnapshots(const AlignedData& data, std::size_t from, std::size_t to)
{
    if (from > to || to > data.snapshots.size())
        throw std::invalid_argument("bad snapshot range");

    AlignedData out;
    out.symbols = data.symbols;
    out.snapshots.assign(data.snapshots.begin() + static_cast<std::ptrdiff_t>(from),
                         data.snapshots.begin() + static_cast<std::ptrdiff_t>(to));
    return out;
}

namespace
{
    // Everything about a window that does not depend on the parameters, so a grid can share it.
    struct Prepared
    {
        AlignedData slice;                              // warm-up + window
        std::size_t trimIndex{};                        // snapshots of warm-up at the front of the slice
        std::int64_t firstTime{};                       // timestamp of the window's first snapshot
        std::vector<std::vector<Pivot>> pivots;         // only legs from the window's start onward
    };

    Prepared prepare(const AlignedData& data, const WindowSpec& w, const GridOptions& options)
    {
        if (w.start > w.end || w.end > data.snapshots.size())
            throw std::invalid_argument("bad window");

        const std::size_t sliceStart = w.start >= w.warmup ? w.start - w.warmup : 0;

        Prepared p;
        p.slice = sliceSnapshots(data, sliceStart, w.end);
        p.trimIndex = w.start - sliceStart;
        p.firstTime = p.trimIndex < p.slice.snapshots.size() ? p.slice.snapshots[p.trimIndex].timestamp
                                                              : std::numeric_limits<std::int64_t>::max();

        p.pivots = zigzagAll(p.slice, options.zigzagMultiple, options.atrPeriod);
        for (std::size_t a = 0; a < p.pivots.size(); ++a)
        {
            const std::vector<Bar> bars = barsOf(p.slice, static_cast<int>(a));
            std::vector<Pivot>& pv = p.pivots[a];
            pv.erase(std::remove_if(pv.begin(), pv.end(),
                         [&](const Pivot& x) { return bars[x.index].timestamp < p.firstTime; }),
                     pv.end());
        }
        return p;
    }

    WindowRun runPrepared(const Prepared& p, const Params& params, const StrategyFactory& factory,
                          const GridOptions& options)
    {
        std::unique_ptr<MultiStrategy> strategy = factory(params, p.slice.symbols.size());
        if (!strategy)
            throw std::invalid_argument("the strategy factory returned no strategy");

        const BacktestResult full = runMultiBacktest(p.slice, *strategy, options.startingCash,
                                                      spreadsFor(options.costs, p.slice.symbols));
        const double cash = options.startingCash;

        WindowRun run;

        // Equity over the window only, starting from `cash`.
        const double base = p.trimIndex > 0 ? full.equityCurve[p.trimIndex - 1] : cash;
        for (std::size_t i = p.trimIndex; i < full.equityCurve.size(); ++i)
            run.result.equityCurve.push_back(cash + (full.equityCurve[i] - base));

        for (const ClosedTrade& c : full.closedTrades)
            if (c.exitTime >= p.firstTime)
                run.result.closedTrades.push_back(c);
        run.result.openTrades = full.openTrades;
        run.result.finalEquity = run.result.equityCurve.empty() ? cash : run.result.equityCurve.back();

        // Capture uses ALL trades: one opened during warm-up still earns inside the window.
        run.capture = captureEfficiency(p.slice, p.pivots, full);
        return run;
    }

    Evaluation toEvaluation(const WindowRun& run, const Params& params, const GridOptions& options)
    {
        Evaluation e;
        e.params = params;
        e.metrics = computeMetrics(run.result, options.startingCash, options.periodsPerYear);
        e.capture = run.capture;
        return e;
    }

    std::size_t warmupFor(const ValidationOptions& options, const Params& params)
    {
        return options.warmupBars ? options.warmupBars(params) : 0;
    }

    void addCapture(CaptureResult& total, const CaptureResult& r)
    {
        total.captured += r.captured;
        total.available += r.available;
        total.legs += r.legs;
        total.legsRight += r.legsRight;
    }

    void finishCapture(CaptureResult& total)
    {
        if (total.available > 0.0)
            total.efficiency = total.captured / total.available;
        if (total.legs > 0)
            total.rightShare = static_cast<double>(total.legsRight) / total.legs;
    }
}

WindowRun runWindow(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                    const WindowSpec& window, const GridOptions& options)
{
    return runPrepared(prepare(data, window, options), params, factory, options);
}

Evaluation evaluateWindow(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                          const WindowSpec& window, const GridOptions& options)
{
    return toEvaluation(runWindow(data, params, factory, window, options), params, options);
}

std::vector<Evaluation> runGridWindow(const AlignedData& data, const std::vector<Params>& grid,
                                      const StrategyFactory& factory, const WindowSpec& window,
                                      const GridOptions& options)
{
    if (options.threads < 0)
        throw std::invalid_argument("threads must not be negative");

    const Prepared prepared = prepare(data, window, options);     // once for the whole grid

    std::vector<Evaluation> results(grid.size());
    parallelFor(grid.size(), options.threads, [&](std::size_t i)
    {
        results[i] = toEvaluation(runPrepared(prepared, grid[i], factory, options), grid[i], options);
    });
    return results;
}

TrainTestResult trainTestSplit(const AlignedData& data, const std::vector<ParamRange>& ranges,
                               const StrategyFactory& factory, double trainFraction,
                               const ValidationOptions& options)
{
    if (!(trainFraction > 0.0 && trainFraction < 1.0))
        throw std::invalid_argument("trainFraction must be between 0 and 1");

    const std::size_t n = data.snapshots.size();
    TrainTestResult out;
    out.splitIndex = static_cast<std::size_t>(std::floor(static_cast<double>(n) * trainFraction));
    out.testScore = std::numeric_limits<double>::quiet_NaN();

    const ScoreFn score = scoreBy(options.optimise.objective);

    const std::vector<Evaluation> trained = runGridWindow(data, expandGrid(ranges), factory,
                                                          {0, out.splitIndex, 0}, options.optimise.grid);
    out.ranking = rankResults(trained, score, options.optimise.minTrades);
    if (out.ranking.empty())
        return out;

    const Params& winner = out.ranking[0].evaluation.params;
    out.test = evaluateWindow(data, winner, factory, {out.splitIndex, n, warmupFor(options, winner)},
                              options.optimise.grid);
    out.testScore = score(*out.test);
    return out;
}

std::vector<FoldWindows> walkForwardWindows(std::size_t snapshots, std::size_t trainBars,
                                            std::size_t testBars, std::size_t stepBars)
{
    if (trainBars == 0 || testBars == 0)
        throw std::invalid_argument("trainBars and testBars must be positive");
    if (stepBars == 0)
        stepBars = testBars;

    std::vector<FoldWindows> windows;
    for (std::size_t start = 0; start + trainBars + testBars <= snapshots; start += stepBars)
        windows.push_back({start, start + trainBars, start + trainBars, start + trainBars + testBars});
    return windows;
}

WalkForwardResult walkForward(const AlignedData& data, const std::vector<ParamRange>& ranges,
                              const StrategyFactory& factory, std::size_t trainBars, std::size_t testBars,
                              const ValidationOptions& options, std::size_t stepBars)
{
    const GridOptions& grid = options.optimise.grid;
    const ScoreFn score = scoreBy(options.optimise.objective);
    const std::vector<Params> paramGrid = expandGrid(ranges);

    WalkForwardResult out;
    double level = grid.startingCash;       // where the stitched equity currently stands

    for (const FoldWindows& w : walkForwardWindows(data.snapshots.size(), trainBars, testBars, stepBars))
    {
        Fold fold;
        fold.windows = w;

        const std::vector<Evaluation> trained =
            runGridWindow(data, paramGrid, factory, {w.trainStart, w.trainEnd, 0}, grid);
        const std::vector<Ranked> ranking = rankResults(trained, score, options.optimise.minTrades);

        if (ranking.empty())
        {
            // nothing worth trading: stay in cash for this window
            for (std::size_t i = w.testStart; i < w.testEnd; ++i)
                out.stitched.equityCurve.push_back(level);
        }
        else
        {
            fold.best = ranking[0];
            const Params& winner = ranking[0].evaluation.params;
            const WindowRun run = runWindow(data, winner, factory,
                                            {w.testStart, w.testEnd, warmupFor(options, winner)}, grid);
            fold.test = toEvaluation(run, winner, grid);

            for (double e : run.result.equityCurve)
                out.stitched.equityCurve.push_back(level + (e - grid.startingCash));
            if (!run.result.equityCurve.empty())
                level = out.stitched.equityCurve.back();

            out.stitched.closedTrades.insert(out.stitched.closedTrades.end(),
                                             run.result.closedTrades.begin(), run.result.closedTrades.end());
            addCapture(out.capture, run.capture);
        }
        out.folds.push_back(fold);
    }

    out.stitched.finalEquity = out.stitched.equityCurve.empty() ? grid.startingCash
                                                               : out.stitched.equityCurve.back();
    out.metrics = computeMetrics(out.stitched, grid.startingCash, grid.periodsPerYear);
    finishCapture(out.capture);
    return out;
}

namespace
{
    bool sameValue(double a, double b)
    {
        return std::abs(a - b) <= 1e-9 * std::max(1.0, std::max(std::abs(a), std::abs(b)));
    }

    bool sameParams(const Params& a, const Params& b)
    {
        if (a.values().size() != b.values().size())
            return false;
        for (const auto& [name, value] : a.values())
            if (!b.has(name) || !sameValue(value, b.get(name)))
                return false;
        return true;
    }

    const Evaluation* find(const std::vector<Evaluation>& results, const Params& p)
    {
        for (const Evaluation& e : results)
            if (sameParams(e.params, p))
                return &e;
        return nullptr;
    }
}

StabilityResult neighbourStability(const std::vector<Evaluation>& results, const std::vector<ParamRange>& ranges,
                                   const Params& chosen, const ScoreFn& score, int minTrades)
{
    const Evaluation* self = find(results, chosen);
    if (!self)
        throw std::invalid_argument("the chosen parameter set is not among the results");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    StabilityResult out;
    out.score = score(*self);
    out.meanNeighbour = nan;
    out.worstNeighbour = nan;

    double sum = 0.0;
    double worst = std::numeric_limits<double>::infinity();

    for (const ParamRange& r : ranges)
    {
        if (!chosen.has(r.name))
            continue;
        for (double direction : {-1.0, 1.0})
        {
            Params target = chosen;
            target.set(r.name, chosen.get(r.name) + direction * r.step);

            const Evaluation* neighbour = find(results, target);
            if (!neighbour || neighbour->metrics.trades < minTrades)
                continue;
            const double s = score(*neighbour);
            if (std::isnan(s))
                continue;

            sum += s;
            worst = std::min(worst, s);
            ++out.neighbours;
        }
    }

    if (out.neighbours > 0)
    {
        out.meanNeighbour = sum / static_cast<double>(out.neighbours);
        out.worstNeighbour = worst;
    }
    return out;
}
