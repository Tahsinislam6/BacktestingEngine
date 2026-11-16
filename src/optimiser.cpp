#include "optimiser.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>

Evaluation evaluate(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                    const std::vector<std::vector<Pivot>>& pivots, const GridOptions& options)
{
    std::unique_ptr<MultiStrategy> strategy = factory(params, data.symbols.size());
    if (!strategy)
        throw std::invalid_argument("the strategy factory returned no strategy");

    const BacktestResult run = runMultiBacktest(data, *strategy, options.startingCash);

    Evaluation e;
    e.params = params;
    e.metrics = computeMetrics(run, options.startingCash, options.periodsPerYear);
    e.capture = captureEfficiency(data, pivots, run);
    return e;
}

void parallelFor(std::size_t count, int threads, const std::function<void(std::size_t)>& body)
{
    if (threads < 0)
        throw std::invalid_argument("threads must not be negative");
    if (count == 0)
        return;

    std::size_t n = threads > 0 ? static_cast<std::size_t>(threads)
                                : std::max(1u, std::thread::hardware_concurrency());
    n = std::min(n, count);

    // Workers take the next unclaimed index.
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex errorMutex;

    auto worker = [&]
    {
        while (!failed)
        {
            const std::size_t i = next.fetch_add(1);
            if (i >= count)
                return;
            try
            {
                body(i);
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(errorMutex);
                if (!error)
                    error = std::current_exception();
                failed = true;
            }
        }
    };

    if (n == 1)
    {
        worker();
    }
    else
    {
        std::vector<std::thread> pool;
        for (std::size_t t = 0; t < n; ++t)
            pool.emplace_back(worker);
        for (std::thread& t : pool)
            t.join();
    }

    if (error)
        std::rethrow_exception(error);
}

std::vector<Evaluation> runGrid(const AlignedData& data, const std::vector<Params>& grid,
                                const StrategyFactory& factory, const GridOptions& options)
{
    if (options.threads < 0)
        throw std::invalid_argument("threads must not be negative");

    std::vector<Evaluation> results(grid.size());
    if (grid.empty())
        return results;

    // Same for every run, so compute it once.
    const std::vector<std::vector<Pivot>> pivots = zigzagAll(data, options.zigzagMultiple, options.atrPeriod);

    parallelFor(grid.size(), options.threads,
                [&](std::size_t i) { results[i] = evaluate(data, grid[i], factory, pivots, options); });
    return results;
}

ScoreFn scoreBy(Objective objective)
{
    switch (objective)
    {
        case Objective::Sharpe:            return [](const Evaluation& e) { return e.metrics.sharpe; };
        case Objective::Sortino:           return [](const Evaluation& e) { return e.metrics.sortino; };
        case Objective::Calmar:            return [](const Evaluation& e) { return e.metrics.calmar; };
        case Objective::TotalReturn:       return [](const Evaluation& e) { return e.metrics.totalReturn; };
        case Objective::CaptureEfficiency: return [](const Evaluation& e) { return e.capture.efficiency; };
    }
    throw std::invalid_argument("unknown objective");
}

std::vector<Ranked> rankResults(const std::vector<Evaluation>& results, const ScoreFn& score, int minTrades)
{
    std::vector<Ranked> ranked;
    for (const Evaluation& e : results)
    {
        if (e.metrics.trades < minTrades)
            continue;
        const double s = score(e);
        if (std::isnan(s))
            continue;
        ranked.push_back({e, s});
    }

    std::stable_sort(ranked.begin(), ranked.end(),
        [](const Ranked& a, const Ranked& b) { return a.score > b.score; });
    return ranked;
}

std::vector<Ranked> optimise(const AlignedData& data, const std::vector<ParamRange>& ranges,
                             const StrategyFactory& factory, const OptimiseOptions& options)
{
    const std::vector<Evaluation> results = runGrid(data, expandGrid(ranges), factory, options.grid);
    return rankResults(results, scoreBy(options.objective), options.minTrades);
}
