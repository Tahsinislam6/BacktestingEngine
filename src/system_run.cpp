#include "system_run.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>

Evaluation evaluate(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                    const std::vector<std::vector<Pivot>>& pivots, const RunOptions& options)
{
    std::unique_ptr<MultiStrategy> strategy = factory(params, data.symbols.size());
    if (!strategy)
        throw std::invalid_argument("the strategy factory returned no strategy");

    const BacktestResult run = runMultiBacktest(data, *strategy, options.startingCash, spreadsFor(options.costs, data.symbols),
                                                 makeSwapSchedule(options.swap, data.symbols, data));

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
