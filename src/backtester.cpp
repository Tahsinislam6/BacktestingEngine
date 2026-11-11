#include "backtester.hpp"

#include <algorithm>

namespace
{
    void checkStops(Portfolio& portfolio, const Trade& t, const Bar& bar)
    {
        const bool isLong = t.direction == Direction::Long;
        const bool stopHit = t.stopLoss && (isLong ? bar.low <= *t.stopLoss : bar.high >= *t.stopLoss);
        const bool targetHit = t.takeProfit && (isLong ? bar.high >= *t.takeProfit : bar.low <= *t.takeProfit);

        if (stopHit) // sl has priority if both sl and tp hit
        {
            // get worse price between bar open or stop loss
            double price = isLong ? std::min(*t.stopLoss, bar.open) : std::max(*t.stopLoss, bar.open);
            portfolio.close(t.id, price, bar.timestamp, ExitReason::StopLoss);
        }
        else if (targetHit)
        {
            // get better price between bar open or take profit
            double price = isLong ? std::max(*t.takeProfit, bar.open) : std::min(*t.takeProfit, bar.open);
            portfolio.close(t.id, price, bar.timestamp, ExitReason::TakeProfit);
        }
    }

    void fill(Portfolio& portfolio, const Order& order, const Bar& bar)
    {
        if (order.type == OrderType::Open)
            portfolio.open(order.direction, order.quantity, bar.close, bar.timestamp, order.stopLoss, order.takeProfit);
        else 
            portfolio.close(order.tradeId, bar.close, bar.timestamp, ExitReason::Signal);
    }
}

BacktestResult runBacktest(const BarSeries& series, Strategy& strategy, double startingCash)
{
    Portfolio portfolio(startingCash);
    BacktestResult result;

    for(const Bar& bar : series.bars)
    {
        const std::vector<Trade> snapshot = portfolio.openTrades();
        for (const Trade& t : snapshot)
            checkStops(portfolio, t, bar);

        for (const Order& order : strategy.onBar(bar, portfolio))
            fill(portfolio, order, bar);

        result.equityCurve.push_back(portfolio.equity(bar.close));
    }

    result.closedTrades = portfolio.closedTrades();
    result.openTrades = portfolio.openTrades();
    result.finalEquity = result.equityCurve.empty() ? startingCash : result.equityCurve.back();

    return result;
}