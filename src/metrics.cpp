#include "metrics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

Metrics computeMetrics(const BacktestResult& result, double startingCash, double periodsPerYear)
{
    Metrics m;
    const std::vector<double>& eq = result.equityCurve;
    if (!eq.empty())
    {
        const double n = static_cast<double>(eq.size());

        m.totalReturn = eq.back() / startingCash - 1.0;
        m.cagr = std::pow(eq.back() / startingCash, periodsPerYear / n) - 1.0;

        std::vector<double> returns;
        double peak = startingCash;
        double previous = startingCash;
        for (double e: eq)
        {
            peak = std::max(peak, e);
            m.maxDrawdown = std::max(m.maxDrawdown, (peak - e) / peak);
            returns.push_back(e / previous - 1.0);
            previous = e;
        }

        
    }

    const std::vector<ClosedTrade>& closed = result.closedTrades;
    m.trades = static_cast<int>(closed.size());

    if (!closed.empty())
    {
        double grossProfit{};
        double grossLoss{};
        double totalPnL{};
        double totalHeld{};
        int wins = 0;

        for (const ClosedTrade& t : closed)
        {
            totalPnL += t.pnl;
            totalHeld += static_cast<double>(t.exitTime - t.trade.entryTime);

            if (t.pnl > 0.0) 
            {
                ++wins;
                grossProfit += t.pnl;
            }
            else if (t.pnl < 0.0)
            {
                grossLoss -= t.pnl;
            }
        }

        const double count = static_cast<double>(closed.size());
        m.winRate = wins / count;
        m.expectancy = totalPnL / count;
        m.avgHoldingSeconds = totalHeld / count;

        if (grossLoss > 0.0)
            m.profitFactor = grossProfit / grossLoss;
        else if (grossProfit > 0.0)
            m.profitFactor = std::numeric_limits<double>::infinity();
    }

    return m;
}