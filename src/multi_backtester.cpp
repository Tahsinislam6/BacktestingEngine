#include "multi_backtester.hpp"

#include <algorithm>

namespace
{
    // This asset's bar in the snapshot, or nullptr if the asset doesn't exist or has no bar today.
    const Bar* barOf(const Snapshot& snapshot, int asset)
    {
        if (asset < 0 || static_cast<std::size_t>(asset) >= snapshot.bars.size())
            return nullptr;

        const std::optional<Bar>& slot = snapshot.bars[asset];
        return slot ? &*slot : nullptr;
    }

    // Same rules as the single-asset backtester.
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

    void fill(Portfolio& portfolio, const Order& order, const Snapshot& snapshot)
    {
        if (order.type == OrderType::Open)
        {
            const Bar* bar = barOf(snapshot, order.asset);
            if (!bar)
                return;   // no bar for that asset today (or unknown asset): ignore the order

            portfolio.open(order.direction, order.quantity, bar->close, bar->timestamp,
                           order.stopLoss, order.takeProfit, order.asset);
        }
        else
        {
            // A close order only has a trade id, so look the trade up to learn its asset.
            const std::vector<Trade>& trades = portfolio.openTrades();
            auto it = std::find_if(trades.begin(), trades.end(),
                [&](const Trade& t) { return t.id == order.tradeId; });
            if (it == trades.end())
                return;   // unknown or already closed id: ignore

            const Bar* bar = barOf(snapshot, it->asset);
            if (!bar)
                return;   // that asset has no bar today: the trade stays open

            portfolio.close(order.tradeId, bar->close, bar->timestamp, ExitReason::Signal);
        }
    }
}

BacktestResult runMultiBacktest(const AlignedData& data, MultiStrategy& strategy, double startingCash)
{
    Portfolio portfolio(startingCash);
    BacktestResult result;

    // Last known close of every asset, used to value open trades.
    std::vector<double> marks(data.symbols.size(), 0.0);

    for (const Snapshot& snapshot : data.snapshots)
    {
        // 1. Refresh the marks for assets that have a bar today.
        for (std::size_t a = 0; a < snapshot.bars.size() && a < marks.size(); ++a)
        {
            if (snapshot.bars[a])
                marks[a] = snapshot.bars[a]->close;
        }

        // 2. Stops and targets: each trade is checked against its own asset's bar.
        //    Copy the open trades first, because closing a trade changes the list.
        const std::vector<Trade> open = portfolio.openTrades();
        for (const Trade& t : open)
        {
            if (const Bar* bar = barOf(snapshot, t.asset))
                checkStops(portfolio, t, *bar);
        }

        // 3. Strategy decides, then its orders are filled in the order it returned them.
        for (const Order& order : strategy.onSnapshot(snapshot, portfolio))
            fill(portfolio, order, snapshot);

        // 4. Record equity using the marks.
        result.equityCurve.push_back(portfolio.equity(marks));
    }

    result.closedTrades = portfolio.closedTrades();
    result.openTrades = portfolio.openTrades();
    result.finalEquity = result.equityCurve.empty() ? startingCash : result.equityCurve.back();

    return result;
}
