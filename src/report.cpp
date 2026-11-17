#include "report.hpp"

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

std::string objectiveName(Objective objective)
{
    switch (objective)
    {
        case Objective::Sharpe:            return "sharpe";
        case Objective::Sortino:           return "sortino";
        case Objective::Calmar:            return "calmar";
        case Objective::TotalReturn:       return "return";
        case Objective::CaptureEfficiency: return "capture";
    }
    return "?";
}

std::string rankedCsv(const std::vector<Ranked>& ranked)
{
    std::vector<std::string> names;
    if (!ranked.empty())
        for (const auto& [name, value] : ranked[0].evaluation.params.values())
            names.push_back(name);

    std::string out = "rank,score";
    for (const std::string& n : names)
        out += "," + n;
    out += ",trades,total_return,cagr,max_drawdown,sharpe,sortino,calmar,win_rate,profit_factor,"
           "expectancy,avg_holding_days,capture_efficiency,capture_legs,right_share\n";

    for (std::size_t i = 0; i < ranked.size(); ++i)
    {
        const Evaluation& e = ranked[i].evaluation;
        out += std::to_string(i + 1) + "," + num(ranked[i].score);
        for (const std::string& n : names)
            out += "," + (e.params.has(n) ? num(e.params.get(n)) : std::string());
        out += "," + std::to_string(e.metrics.trades);
        for (double v : {e.metrics.totalReturn, e.metrics.cagr, e.metrics.maxDrawdown, e.metrics.sharpe,
                         e.metrics.sortino, e.metrics.calmar, e.metrics.winRate, e.metrics.profitFactor,
                         e.metrics.expectancy, e.metrics.avgHoldingTime / static_cast<double>(kTimestampUnitsPerDay),
                         e.capture.efficiency})
            out += "," + num(v);
        out += "," + std::to_string(e.capture.legs) + "," + num(e.capture.rightShare) + "\n";
    }
    return out;
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

std::string rankedTable(const std::vector<Ranked>& ranked, std::size_t top)
{
    if (ranked.empty())
        return "  (no run reached the minimum number of trades)\n";

    std::string out = fmt("  %4s %8s  %-28s %6s %9s %7s %8s %8s\n", "rank", "score", "parameters", "trades",
                          "return", "sharpe", "max dd", "capture");
    const std::size_t rows = std::min(top, ranked.size());
    for (std::size_t i = 0; i < rows; ++i)
    {
        const Evaluation& e = ranked[i].evaluation;
        out += fmt("  %4zu %8s  %-28s %6d %9s %7s %8s %8s\n", i + 1, number(ranked[i].score).c_str(),
                   paramsText(e.params).c_str(), e.metrics.trades, pct(e.metrics.totalReturn).c_str(),
                   number(e.metrics.sharpe).c_str(), pct(e.metrics.maxDrawdown).c_str(),
                   number(e.capture.efficiency).c_str());
    }
    return out;
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
            r.pnl += c.pnl;
            r.costPaid += spread * c.trade.quantity;
        }
        for (const Trade& t : result.openTrades)
        {
            if (t.asset != asset)
                continue;
            ++r.trades;
            r.costPaid += spread / 2.0 * t.quantity;
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

    std::string out = fmt("  %-12s %6s %12s %10s %9s %8s\n", "asset", "trades", "profit", "cost", "capture", "legs ok");
    for (const AssetReport& a : assets)
        out += fmt("  %-12s %6d %12s %10s %9s %8s\n", a.symbol.c_str(), a.trades, number(a.pnl).c_str(),
                   number(a.costPaid).c_str(), number(a.capture.efficiency).c_str(), pct(a.capture.rightShare).c_str());
    return out;
}
