#include "report.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace
{
    std::string num(double v)
    {
        if (std::isnan(v))
            return "nan";
        if (std::isinf(v))
            return v > 0 ? "inf" : "-inf";
        std::ostringstream out;
        out.precision(10);
        out << v;
        return out.str();
    }

    // printf into a std::string
    template <typename... Args>
    std::string fmt(const char* format, Args... args)
    {
        char buffer[512];
        std::snprintf(buffer, sizeof buffer, format, args...);
        return buffer;
    }

    std::string pct(double fraction)
    {
        if (std::isnan(fraction))
            return "nan";
        return fmt("%.1f%%", 100.0 * fraction);
    }

    std::string number(double v, const char* format = "%.2f")
    {
        if (std::isnan(v))
            return "nan";
        if (std::isinf(v))
            return v > 0 ? "inf" : "-inf";
        return fmt(format, v);
    }
}

std::string paramsText(const Params& params)
{
    if (params.values().empty())
        return "(no parameters)";

    std::string out;
    for (const auto& [name, value] : params.values())
    {
        if (!out.empty())
            out += ", ";
        out += name + "=" + num(value);
    }
    return out;
}

std::string csvField(const std::string& s)
{
    if (s.find_first_of(",\"\n") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char c : s)
        out += c == '"' ? "\"\"" : std::string(1, c);
    return out + "\"";
}

void writeTextFile(const std::string& path, const std::string& text)
{
    std::ofstream file(path, std::ios::trunc);
    if (!file)
        throw std::runtime_error("Cannot write " + path);
    file << text;
    file.close();
    if (!file)
        throw std::runtime_error("Failed while writing " + path);
}

std::vector<AssetReport> perAssetReport(const AlignedData& data, const BacktestResult& result,
                                        const std::vector<std::vector<Pivot>>& pivots,
                                        const std::vector<double>& spreads)
{
    if (pivots.size() != data.symbols.size())
        throw std::invalid_argument("need one pivot list per asset");

    std::vector<AssetReport> reports;
    for (std::size_t a = 0; a < data.symbols.size(); ++a)
    {
        const int asset = static_cast<int>(a);
        const std::vector<Bar> bars = barsOf(data, asset);

        AssetReport r;
        r.symbol = data.symbols[a];
        const double spread = a < spreads.size() ? spreads[a] : 0.0;
        for (const ClosedTrade& c : result.closedTrades)
        {
            if (c.trade.asset != asset)
                continue;
            ++r.trades;
            r.pnl += c.pnl;                                   // already includes the trade's swap
            r.swap += c.trade.swap;
            r.costPaid += spread * c.trade.quantity;
        }
        for (const Trade& t : result.openTrades)
        {
            if (t.asset != asset)
                continue;
            ++r.trades;
            r.costPaid += spread / 2.0 * t.quantity;
            r.swap += t.swap;
            r.pnl += t.swap;
            if (!bars.empty())
                r.pnl += pnlOf(t, bars.back().close);
        }
        r.capture = captureEfficiency(bars, pivots[a], result.closedTrades, result.openTrades, asset);
        reports.push_back(r);
    }
    return reports;
}

std::string assetTable(const std::vector<AssetReport>& assets)
{
    if (assets.empty())
        return "  (no assets)\n";

    std::string out = fmt("  %-12s %6s %12s %10s %10s %9s %8s\n", "asset", "trades", "profit", "cost", "swap", "capture", "legs ok");
    for (const AssetReport& a : assets)
        out += fmt("  %-12s %6d %12s %10s %10s %9s %8s\n", a.symbol.c_str(), a.trades, number(a.pnl).c_str(),
                   number(a.costPaid).c_str(), number(a.swap).c_str(), number(a.capture.efficiency).c_str(),
                   pct(a.capture.rightShare).c_str());
    return out;
}

SystemRun runSystem(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                    const RunOptions& options)
{
    std::unique_ptr<MultiStrategy> strategy = factory(params, data.symbols.size());
    if (!strategy)
        throw std::invalid_argument("the strategy factory returned no strategy");

    const std::vector<double> spreads = spreadsFor(options.costs, data.symbols);
    const BacktestResult result = runMultiBacktest(data, *strategy, options.startingCash, spreads,
                                                   makeSwapSchedule(options.swap, data.symbols, data));
    const std::vector<std::vector<Pivot>> pivots = zigzagAll(data, options.zigzagMultiple, options.atrPeriod);

    SystemRun run;
    run.evaluation.params = params;
    run.evaluation.metrics = computeMetrics(result, options.startingCash, options.periodsPerYear);
    run.evaluation.capture = captureEfficiency(data, pivots, result);
    run.assets = perAssetReport(data, result, pivots, spreads);

    const RReport r = computeRReport(result.closedTrades, static_cast<int>(result.openTrades.size()));
    if (r.all.trades > 0)
        run.r = r;
    return run;
}

namespace
{
    std::string rRow(const char* label, const RStats& s)
    {
        return fmt("  %-12s %6d %7s %9s %9s %11s %9s\n", label, s.trades, pct(s.winRate).c_str(),
                   number(s.avgWinR).c_str(), number(s.avgLossR).c_str(), number(s.expectancyR).c_str(),
                   number(s.totalR).c_str());
    }

    std::string exitText(const char* label, const ExitStats& e)
    {
        return fmt("%s %d (%s R avg)", label, e.count, e.count > 0 ? number(e.avgR()).c_str() : "-");
    }

    // 1R = the amount a trade risked to its stop.
    std::string rTable(const RReport& r)
    {
        std::string out = "  R-MULTIPLES (1R = the amount risked to the stop):\n";
        out += fmt("  %-12s %6s %7s %9s %9s %11s %9s\n", "", "trades", "win", "avg win", "avg loss", "expectancy", "total R");
        out += rRow("all", r.all) + rRow("target half", r.targetHalf) + rRow("runner", r.runner);
        out += "  exits: " + exitText("stop", r.all.stop) + " | " + exitText("target", r.all.target) + " | " +
               exitText("breakeven", r.all.breakeven) + " | " + exitText("trailing", r.all.trailing) + " | " +
               exitText("indicator", r.all.indicator) + " | " + exitText("cross-back", r.all.signal) + "\n";
        out += fmt("  %d trades were still open when the data ended and are not counted above.\n", r.openAtEnd);
        return out;
    }
}

std::string formatSystemRun(const std::string& label, const SystemRun& run)
{
    std::string upper = label;
    for (char& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    const Metrics& m = run.evaluation.metrics;
    std::string out = upper + " (" + paramsText(run.evaluation.params) + "):\n";
    out += fmt("  return %s, sharpe %s, max drawdown %s, %d trades, capture %s\n", pct(m.totalReturn).c_str(),
               number(m.sharpe).c_str(), pct(m.maxDrawdown).c_str(), m.trades, number(run.evaluation.capture.efficiency).c_str());
    out += assetTable(run.assets);
    if (run.r)
        out += rTable(*run.r);
    return out;
}
