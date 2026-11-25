#include "sweep_job.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <exception>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

#include "density.hpp"
#include "job.hpp"
#include "null_series.hpp"
#include "parameter_grid.hpp"

namespace
{
    constexpr std::size_t kMaxAxisValues = 100000;
    constexpr std::size_t kMaxCells = 50000;
    constexpr std::size_t kMaxBacktests = 2000000;

    bool blank(const std::string& text)
    {
        return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    }

    const ParamSpec* findParam(const StrategyInfo& info, const std::string& name)
    {
        for (const ParamSpec& p : info.params)
            if (p.name == name)
                return &p;
        return nullptr;
    }

    const StrategyInfo& findStrategy(const StrategyRegistry& registry, const std::string& name)
    {
        for (const StrategyInfo& info : registry.list())
            if (info.name == name)
                return info;
        throw std::invalid_argument("unknown strategy: " + name);
    }

    std::size_t cellsOf(const SweepSpec& spec)
    {
        std::size_t cells = 1;
        for (const AxisSpec& axis : spec.axes)
            cells *= expandAxis(axis).size();
        return cells;
    }
}

std::vector<double> expandAxis(const AxisSpec& axis)
{
    if (axis.param.empty())
        throw std::invalid_argument("an axis needs a parameter name");
    if (!std::isfinite(axis.from) || !std::isfinite(axis.to) || !std::isfinite(axis.step))
        throw std::invalid_argument("axis " + axis.param + ": from, to and step must be numbers");
    if (!(axis.step > 0.0))
        throw std::invalid_argument("axis " + axis.param + ": the step must be above 0");
    if (axis.to < axis.from)
        throw std::invalid_argument("axis " + axis.param + ": 'to' is below 'from'");

    // Multiply (from + i * step) instead of adding step over and over, so rounding errors do not pile up. The small extra
    // lets 0 to 1 step 0.1 reach 1.
    const double count = std::floor((axis.to - axis.from) / axis.step + 1e-9) + 1.0;
    if (count > static_cast<double>(kMaxAxisValues))
        throw std::invalid_argument("axis " + axis.param + " has too many values (the limit is 100000)");

    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(count));
    for (std::size_t i = 0; i < static_cast<std::size_t>(count); ++i)
        values.push_back(axis.from + static_cast<double>(i) * axis.step);
    return values;
}

std::function<double(const Metrics&)> metricByName(const std::string& name)
{
    if (name == "mar") return [](const Metrics& m) { return m.mar; };
    if (name == "cagr") return [](const Metrics& m) { return m.cagr; };
    if (name == "sharpe") return [](const Metrics& m) { return m.sharpe; };
    if (name == "sortino") return [](const Metrics& m) { return m.sortino; };
    if (name == "total-return") return [](const Metrics& m) { return m.totalReturn; };
    if (name == "profit-factor") return [](const Metrics& m) { return m.profitFactor; };
    if (name == "expectancy") return [](const Metrics& m) { return m.expectancy; };
    throw std::invalid_argument("unknown metric: " + name);
}

void validateSweepSpec(const StrategyRegistry& registry, const std::string& dataDir, const SweepSpec& spec)
{
    if (blank(spec.note))
        throw std::invalid_argument("the note is required: write what you expect and what counts as a pass before running");
    const StrategyInfo& info = findStrategy(registry, spec.strategy);
    metricByName(spec.metric);
    resolveDataset(dataDir, spec.dataset);

    if (!std::isfinite(spec.threshold))
        throw std::invalid_argument("threshold must be a number");
    if (spec.radius < 0 || spec.radius > 20)
        throw std::invalid_argument("radius must be from 0 to 20");
    if (spec.minTrades < 0)
        throw std::invalid_argument("minTrades must be 0 or more");
    if (spec.nullRuns < 1 || spec.nullRuns > 1000)
        throw std::invalid_argument("nullRuns must be from 1 to 1000");
    if (spec.block < 1 || spec.block > 1000)
        throw std::invalid_argument("block must be from 1 to 1000");
    if (!std::isfinite(spec.cash) || spec.cash <= 0.0)
        throw std::invalid_argument("cash must be above 0");
    if (!std::isfinite(spec.periodsPerYear) || spec.periodsPerYear <= 0.0)
        throw std::invalid_argument("periodsPerYear must be above 0");

    if (spec.axes.size() != 2)
        throw std::invalid_argument("a sweep needs exactly two axes");
    std::set<std::string> swept;
    for (const AxisSpec& axis : spec.axes)
    {
        if (!swept.insert(axis.param).second)
            throw std::invalid_argument("the parameter " + axis.param + " is on both axes");
        if (!findParam(info, axis.param))
            throw std::invalid_argument("strategy " + spec.strategy + " has no parameter " + axis.param);
        if (spec.params.count(axis.param))
            throw std::invalid_argument("parameter " + axis.param + " is swept, so it cannot also have a fixed value");
    }
    registry.resolveParams(spec.strategy, spec.params);          // the fixed values: known names, inside the limits

    std::size_t cells = 1;
    for (const AxisSpec& axis : spec.axes)
    {
        const std::vector<double> values = expandAxis(axis);
        for (double v : values)
        {
            NamedParams one = spec.params;
            one[axis.param] = v;
            registry.resolveParams(spec.strategy, one);         // inside the limits, whole where it must be
        }
        cells *= values.size();
        if (cells > kMaxCells)
            throw std::invalid_argument("the grid has too many cells (the limit is 50000)");
    }
    if (cells * (1 + static_cast<std::size_t>(spec.nullRuns)) > kMaxBacktests)
        throw std::invalid_argument("too many backtests: cells x (1 + nullRuns) is over 2000000");
}

SweepSpec resolveSweepSpec(const StrategyRegistry& registry, const SweepSpec& spec)
{
    SweepSpec out = spec;
    out.params = registry.resolveParams(spec.strategy, spec.params);
    for (const AxisSpec& axis : spec.axes)
        out.params.erase(axis.param);                         // swept parameters have no single value
    return out;
}

std::size_t sweepIdeas(const StrategyRegistry&, const SweepSpec& spec)
{
    return cellsOf(spec) * (1 + static_cast<std::size_t>(spec.nullRuns));
}

SweepOutput runSweep(const StrategyRegistry& registry, const std::string& dataDir, const SweepSpec& request,
                     const ProgressFn& progress, unsigned threads)
{
    validateSweepSpec(registry, dataDir, request);

    SweepOutput out;
    out.spec = resolveSweepSpec(registry, request);
    const SweepSpec& spec = out.spec;

    const BarSeries series = loadDataset(dataDir, spec.dataset);
    if (series.bars.empty())
        throw std::runtime_error("dataset has no bars: " + spec.dataset);
    out.bars = series.bars.size();

    std::vector<ParameterAxis> axes;
    for (const AxisSpec& axis : spec.axes)
    {
        axes.push_back({axis.param, expandAxis(axis)});
        out.axisValues.push_back(axes.back().values);
    }
    out.cells = cellCount(gridShape(axes));
    out.ideasTried = out.cells * (1 + static_cast<std::size_t>(spec.nullRuns));

    // A fresh strategy for every cell (it keeps state while it runs): the fixed values plus this cell's two values.
    const StrategyFactory factory = [&registry, &spec](const Params& p) -> std::unique_ptr<Strategy> {
        NamedParams named = spec.params;
        named[spec.axes[0].param] = p[0];
        named[spec.axes[1].param] = p[1];
        return registry.create(spec.strategy, named);
    };
    const auto metric = metricByName(spec.metric);

    const std::size_t total = 1 + static_cast<std::size_t>(spec.nullRuns);
    std::mutex progressMutex;
    std::size_t done = 0;
    auto step = [&]() {
        std::lock_guard<std::mutex> lock(progressMutex);
        ++done;
        if (progress)
            progress(done, total);
    };

    // The real grid.
    const GridResult real = sweep(series, axes, factory, spec.cash, spec.periodsPerYear);
    const DensityGrid grid = toDensityGrid(real, metric, spec.minTrades);
    for (std::size_t i = 0; i < out.cells; ++i)
    {
        out.trades.push_back(real.metrics[i].trades);
        out.values.push_back(grid.values[i]);
        out.valid.push_back(grid.valid[i]);
        out.validCells += grid.valid[i] ? 1 : 0;
    }
    out.passDensity = passDensity(grid, spec.threshold);
    out.plateauDensity = plateauDensity(grid, spec.threshold, spec.radius);
    out.peakOverNeighbours = peakOverNeighbours(grid, spec.radius);
    step();

    // The same grid on shuffled copies (seed k for copy k), a few at a time. Each copy writes only its own slot, so the
    // answer is the same however many threads there are.
    const std::size_t copies = static_cast<std::size_t>(spec.nullRuns);
    out.nullPass.assign(copies, 0.0);
    out.nullPlateau.assign(copies, 0.0);

    unsigned workers = threads != 0 ? threads : std::max(1u, std::thread::hardware_concurrency());
    workers = static_cast<unsigned>(std::min<std::size_t>(workers, copies));

    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr firstError;
    std::mutex errorMutex;

    auto work = [&]() {
        for (;;)
        {
            const std::size_t k = next.fetch_add(1);
            if (k >= copies || failed.load())
                return;
            try
            {
                const BarSeries copy = shuffleReturns(series, static_cast<std::uint64_t>(k + 1), spec.block);
                const DensityGrid g = toDensityGrid(sweep(copy, axes, factory, spec.cash, spec.periodsPerYear), metric, spec.minTrades);
                out.nullPass[k] = passDensity(g, spec.threshold);
                out.nullPlateau[k] = plateauDensity(g, spec.threshold, spec.radius);
                step();
            }
            catch (...)
            {
                std::lock_guard<std::mutex> lock(errorMutex);
                if (!firstError)
                    firstError = std::current_exception();
                failed = true;
                return;
            }
        }
    };

    std::vector<std::thread> pool;
    for (unsigned i = 1; i < workers; ++i)
        pool.emplace_back(work);
    work();                                                   // this thread works too
    for (std::thread& t : pool)
        t.join();
    if (firstError)
        std::rethrow_exception(firstError);

    out.nullPassMedian = percentile(out.nullPass, 0.5);
    out.nullPass95 = percentile(out.nullPass, 0.95);
    out.nullPlateauMedian = percentile(out.nullPlateau, 0.5);
    out.nullPlateau95 = percentile(out.nullPlateau, 0.95);
    for (double d : out.nullPass)
        out.nullPassAtLeast += d >= out.passDensity ? 1 : 0;
    for (double d : out.nullPlateau)
        out.nullPlateauAtLeast += d >= out.plateauDensity ? 1 : 0;
    return out;
}
