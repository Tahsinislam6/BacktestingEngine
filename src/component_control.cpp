#include "component_control.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace
{
    template <typename... Args>
    std::string fmt(const char* format, Args... args)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof buffer, format, args...);
        return buffer;
    }

    std::string num(double v, const char* format = "%.2f") { return std::isnan(v) ? "nan" : fmt(format, v); }
    std::string pct(double f) { return std::isnan(f) ? "n/a" : fmt("%.0f%%", 100.0 * f); }

    NnfxOptions withComponents(NnfxOptions o, NnfxComponents c)
    {
        o.components = std::move(c);
        return o;
    }

    std::string describe(const ChainStats& s, bool isGate)
    {
        double stays = 0;
        for (std::size_t i = 0; i < s.frequency.size(); ++i)
            stays += s.frequency[i] * s.transition[i][i];
        std::string text;
        if (isGate)
            text = "allows " + pct(s.frequency[1]) + " of the time";
        else
            text = "+1 " + pct(s.frequency[2]) + ", -1 " + pct(s.frequency[0]) + ", neutral " + pct(s.frequency[1]);
        return text + "; keeps its state " + pct(stays) + " of bars";
    }

    std::vector<double> column(const std::vector<ArmResult>& arms, double ArmResult::*member)
    {
        std::vector<double> out;
        for (const ArmResult& a : arms)
            out.push_back(a.*member);
        return out;
    }

    void spread(const std::vector<double>& v, double& mean, double& sd, double& low, double& high)
    {
        int n = 0;
        mean = sd = 0;
        low = std::numeric_limits<double>::infinity();
        high = -low;
        for (double x : v)
        {
            if (std::isnan(x))
                continue;
            ++n;
            mean += x;
            low = std::min(low, x);
            high = std::max(high, x);
        }
        if (n == 0)
        {
            mean = sd = low = high = std::nan("");
            return;
        }
        mean /= n;
        double sq = 0;
        for (double x : v)
            if (!std::isnan(x))
                sq += (x - mean) * (x - mean);
        sd = n > 1 ? std::sqrt(sq / (n - 1)) : 0.0;
    }

    std::string row(const char* name, double real, double none, const std::vector<double>& random, double rank, const char* f = "%.2f")
    {
        double mean, sd, low, high;
        spread(random, mean, sd, low, high);
        return fmt("  %-14s %9s %9s %11s %7s   [%s, %s] %8s\n", name, num(real, f).c_str(), num(none, f).c_str(),
                   num(mean, f).c_str(), num(sd, f).c_str(), num(low, f).c_str(), num(high, f).c_str(), pct(rank).c_str());
    }
}

const char* roleName(ComponentRole role)
{
    switch (role)
    {
        case ComponentRole::Confirmation: return "confirmation";
        case ComponentRole::ExitIndicator: return "exit indicator";
        case ComponentRole::Filter: return "filter";
    }
    return "";
}

bool hasRole(const NnfxComponents& c, ComponentRole role)
{
    switch (role)
    {
        case ComponentRole::Confirmation: return static_cast<bool>(c.confirmation);
        case ComponentRole::ExitIndicator: return static_cast<bool>(c.exitIndicator);
        case ComponentRole::Filter: return static_cast<bool>(c.filter);
    }
    return false;
}

NnfxComponents withoutRole(NnfxComponents c, ComponentRole role)
{
    switch (role)
    {
        case ComponentRole::Confirmation: c.confirmation.reset(); break;
        case ComponentRole::ExitIndicator: c.exitIndicator.reset(); break;
        case ComponentRole::Filter: c.filter.reset(); break;
    }
    return c;
}

StandIn measureStandIn(const NnfxComponents& c, ComponentRole role, const AlignedData& data)
{
    if (!hasRole(c, role))
        throw std::invalid_argument(std::string("there is no ") + roleName(role) + " to measure");
    StandIn s;
    if (role == ComponentRole::Filter)
    {
        s.isGate = true;
        s.stats = measureGate(*c.filter, data);
        s.warmup = c.filter->warmupBars();
    }
    else
    {
        const auto& real = role == ComponentRole::Confirmation ? c.confirmation : c.exitIndicator;
        s.stats = measureState(*real, data);
        s.warmup = real->warmupBars();
    }
    s.behaviour = describe(s.stats, s.isGate);
    return s;
}

NnfxComponents withStandIn(NnfxComponents c, ComponentRole role, const StandIn& s, std::uint64_t seed)
{
    switch (role)
    {
        case ComponentRole::Confirmation: c.confirmation = randomState(s.stats, s.warmup, seed); break;
        case ComponentRole::ExitIndicator: c.exitIndicator = randomState(s.stats, s.warmup, seed); break;
        case ComponentRole::Filter: c.filter = randomGate(s.stats, s.warmup, seed); break;
    }
    return c;
}

ComponentControlResult runComponentControl(const AlignedData& data, const Params& params, const NnfxFactoryMaker& make,
                                           const NnfxOptions& options, ComponentRole role, int seeds,
                                           const RunOptions& grid)
{
    if (seeds < 1)
        throw std::invalid_argument("the component control needs at least one random run");
    if (!hasRole(options.components, role))
        throw std::invalid_argument(std::string("there is no ") + roleName(role) + " to control");

    const StandIn stand = measureStandIn(options.components, role, data);

    ComponentControlResult out;
    out.behaviour = stand.behaviour;
    out.real = runArm(data, params, make(options), grid);
    out.without = runArm(data, params, make(withComponents(options, withoutRole(options.components, role))), grid);

    out.random.resize(static_cast<std::size_t>(seeds));
    parallelFor(out.random.size(), grid.threads, [&](std::size_t i)
    {
        out.random[i] = runArm(data, params, make(withComponents(options, withStandIn(options.components, role, stand, static_cast<std::uint64_t>(i + 1)))), grid);
    });

    out.sharpeRank = percentileRank(out.real.sharpe, column(out.random, &ArmResult::sharpe));
    out.expectancyRank = percentileRank(out.real.expectancyR, column(out.random, &ArmResult::expectancyR));
    out.totalRRank = percentileRank(out.real.totalR, column(out.random, &ArmResult::totalR));
    return out;
}

std::string formatComponentControl(const std::string& label, const std::string& componentName, ComponentRole role,
                                   const ComponentControlResult& r)
{
    std::string out = fmt("  COMPONENT CONTROL, %s: %s '%s' vs %zu random %ss that behave the same\n", label.c_str(),
                          roleName(role), componentName.c_str(), r.random.size(), roleName(role));
    out += "  (matched: " + r.behaviour + ")\n";
    out += fmt("  %-14s %9s %9s %11s %7s   %-16s %8s\n", "", "with it", "without", "random mean", "sd", "random range",
               "it beats");
    out += row("sharpe", r.real.sharpe, r.without.sharpe, column(r.random, &ArmResult::sharpe), r.sharpeRank);
    out += row("expectancy R", r.real.expectancyR, r.without.expectancyR, column(r.random, &ArmResult::expectancyR), r.expectancyRank, "%.3f");
    out += row("total R", r.real.totalR, r.without.totalR, column(r.random, &ArmResult::totalR), r.totalRRank);

    double meanTrades = 0;
    for (const ArmResult& a : r.random)
        meanTrades += a.trades;
    meanTrades /= std::max<std::size_t>(1, r.random.size());
    out += fmt("  trades: with it %d, without %d, random mean %.0f\n", r.real.trades, r.without.trades, meanTrades);
    return out;
}
