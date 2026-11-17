#include "control.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "metrics.hpp"
#include "rstats.hpp"

double percentileRank(double value, const std::vector<double>& samples)
{
    if (std::isnan(value))
        return std::numeric_limits<double>::quiet_NaN();

    double below = 0;
    int counted = 0;
    for (double s : samples)
    {
        if (std::isnan(s))
            continue;
        ++counted;
        if (s < value)
            below += 1.0;
        else if (s == value)
            below += 0.5;
    }
    return counted > 0 ? below / counted : std::numeric_limits<double>::quiet_NaN();
}

ArmResult runArm(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                 const GridOptions& grid)
{
    std::unique_ptr<MultiStrategy> strategy = factory(params, data.symbols.size());
    if (!strategy)
        throw std::invalid_argument("the strategy factory returned no strategy");

    const BacktestResult run = runMultiBacktest(data, *strategy, grid.startingCash,
                                                spreadsFor(grid.costs, data.symbols));
    const Metrics m = computeMetrics(run, grid.startingCash, grid.periodsPerYear);
    const RReport r = computeRReport(run.closedTrades);

    ArmResult a;
    a.sharpe = m.sharpe;
    a.totalReturn = m.totalReturn;
    a.expectancyR = r.all.expectancyR;
    a.totalR = r.all.totalR;
    a.trades = r.all.trades;
    return a;
}

namespace
{
    template <typename Member>
    std::vector<double> column(const std::vector<ArmResult>& arms, Member member)
    {
        std::vector<double> out;
        for (const ArmResult& a : arms)
            out.push_back(a.*member);
        return out;
    }

    struct Spread
    {
        double mean{}, sd{}, low{}, high{};
    };

    Spread spreadOf(const std::vector<double>& v)
    {
        Spread s;
        int n = 0;
        s.low = std::numeric_limits<double>::infinity();
        s.high = -std::numeric_limits<double>::infinity();
        for (double x : v)
        {
            if (std::isnan(x))
                continue;
            ++n;
            s.mean += x;
            s.low = std::min(s.low, x);
            s.high = std::max(s.high, x);
        }
        if (n == 0)
            return {std::nan(""), std::nan(""), std::nan(""), std::nan("")};
        s.mean /= n;
        double sq = 0;
        for (double x : v)
            if (!std::isnan(x))
                sq += (x - s.mean) * (x - s.mean);
        s.sd = n > 1 ? std::sqrt(sq / (n - 1)) : 0.0;
        return s;
    }

    template <typename... Args>
    std::string fmt(const char* format, Args... args)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof buffer, format, args...);
        return buffer;
    }

    std::string num(double v) { return std::isnan(v) ? "nan" : fmt("%.2f", v); }
    std::string pct(double f) { return std::isnan(f) ? "n/a" : fmt("%.0f%%", 100.0 * f); }

    std::string row(const char* name, double baseline, const std::vector<double>& random, double rank)
    {
        const Spread s = spreadOf(random);
        return fmt("  %-14s %9s %11s %7s   [%s, %s] %8s\n", name, num(baseline).c_str(), num(s.mean).c_str(),
                   num(s.sd).c_str(), num(s.low).c_str(), num(s.high).c_str(), pct(rank).c_str());
    }
}

ControlResult runDirectionControl(const AlignedData& data, const Params& params, const NnfxFactoryMaker& make,
                                  const NnfxOptions& options, int seeds, const GridOptions& grid)
{
    if (seeds < 1)
        throw std::invalid_argument("the control needs at least one random run");
    if (options.exitOnCross)
        throw std::invalid_argument("the control cannot use the cross-back exit (switch it off for both arms)");
    if (options.randomSeed)
        throw std::invalid_argument("the control chooses the random seeds itself");

    ControlResult out;
    out.baseline = runArm(data, params, make(options), grid);

    out.random.resize(static_cast<std::size_t>(seeds));
    parallelFor(out.random.size(), grid.threads, [&](std::size_t i)
    {
        NnfxOptions o = options;
        o.randomSeed = static_cast<std::uint64_t>(i + 1);
        out.random[i] = runArm(data, params, make(o), grid);
    });

    out.sharpeRank = percentileRank(out.baseline.sharpe, column(out.random, &ArmResult::sharpe));
    out.expectancyRank = percentileRank(out.baseline.expectancyR, column(out.random, &ArmResult::expectancyR));
    out.totalRRank = percentileRank(out.baseline.totalR, column(out.random, &ArmResult::totalR));
    return out;
}

std::string formatControl(const std::string& label, const ControlResult& r)
{
    std::string out = fmt("  DIRECTION CONTROL, %s: the baseline's direction vs %zu random-direction runs (same signals, exits and costs)\n",
                          label.c_str(), r.random.size());
    out += fmt("  %-14s %9s %11s %7s   %-16s %8s\n", "", "baseline", "random mean", "sd", "random range", "baseline beats");
    out += row("sharpe", r.baseline.sharpe, column(r.random, &ArmResult::sharpe), r.sharpeRank);
    out += row("expectancy R", r.baseline.expectancyR, column(r.random, &ArmResult::expectancyR), r.expectancyRank);
    out += row("total R", r.baseline.totalR, column(r.random, &ArmResult::totalR), r.totalRRank);
    return out;
}
