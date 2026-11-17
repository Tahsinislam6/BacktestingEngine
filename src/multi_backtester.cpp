#include "multi_backtester.hpp"

#include <algorithm>
#include <cmath>

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

    double halfSpread(const std::vector<double>& spreads, int asset)
    {
        if (asset < 0 || static_cast<std::size_t>(asset) >= spreads.size())
            return 0.0;
        return spreads[asset] / 2.0;
    }

    // The price an exit gets: a long is sold (bid, lower), a short is bought back (ask, higher).
    double exitFill(Direction d, double price, double half)
    {
        return d == Direction::Long ? price - half : price + half;
    }

    // Same rules as the single-asset backtester.
    void checkStops(Portfolio& portfolio, const Trade& t, const Bar& bar, double half)
    {
        const bool isLong = t.direction == Direction::Long;
        const bool stopHit = t.stopLoss && (isLong ? bar.low <= *t.stopLoss : bar.high >= *t.stopLoss);
        const bool targetHit = t.takeProfit && (isLong ? bar.high >= *t.takeProfit : bar.low <= *t.takeProfit);

        if (stopHit) // sl has priority if both sl and tp hit
        {
            // get worse price between bar open or stop loss
            double price = isLong ? std::min(*t.stopLoss, bar.open) : std::max(*t.stopLoss, bar.open);
            portfolio.close(t.id, exitFill(t.direction, price, half), bar.timestamp, t.stopReason);
        }
        else if (targetHit)
        {
            // get better price between bar open or take profit
            double price = isLong ? std::max(*t.takeProfit, bar.open) : std::min(*t.takeProfit, bar.open);
            portfolio.close(t.id, exitFill(t.direction, price, half), bar.timestamp, ExitReason::TakeProfit);
        }
    }

    void fill(Portfolio& portfolio, const Order& order, const Snapshot& snapshot, const std::vector<double>& spreads)
    {
        if (order.type == OrderType::Open)
        {
            const Bar* bar = barOf(snapshot, order.asset);
            if (!bar)
                return;   // no bar for that asset today (or unknown asset): ignore the order

            // a long buys (pays more), a short sells (receives less)
            const double half = halfSpread(spreads, order.asset);
            const double price = order.direction == Direction::Long ? bar->close + half : bar->close - half;
            portfolio.open(order.direction, order.quantity, price, bar->timestamp,
                           order.stopLoss, order.takeProfit, order.asset);
        }
        else if (order.type == OrderType::MoveStop)
        {
            if (order.stopLoss)
                portfolio.moveStop(order.tradeId, *order.stopLoss, order.reason);   // an unknown id is ignored
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

            portfolio.close(order.tradeId, exitFill(it->direction, bar->close, halfSpread(spreads, it->asset)),
                            bar->timestamp, order.reason);
        }
    }
}

BacktestResult runMultiBacktest(const AlignedData& data, MultiStrategy& strategy, double startingCash,
                                const std::vector<double>& spreads, const SwapSchedule& swap)
{
    strategy.setSymbols(data.symbols);

    Portfolio portfolio(startingCash);
    BacktestResult result;

    // Last known close of every asset, used to value open trades.
    std::vector<double> marks(data.symbols.size(), 0.0);

    for (std::size_t index = 0; index < data.snapshots.size(); ++index)
    {
        const Snapshot& snapshot = data.snapshots[index];

        // 0. The nights since the last snapshot, for the trades that were open at its end, at the previous close.
        if (swap.active() && index > 0)
        {
            const std::int64_t gap = snapshot.timestamp - data.snapshots[index - 1].timestamp;
            const double nights = static_cast<double>(std::llround(static_cast<double>(gap) / kTimestampUnitsPerDay));
            if (nights > 0)
                for (const Trade& t : portfolio.openTrades())
                {
                    const std::size_t a = static_cast<std::size_t>(t.asset);
                    if (a >= marks.size() || marks[a] == 0.0)
                        continue;
                    const double difference = a < swap.differentialPct.size() && index - 1 < swap.differentialPct[a].size()
                                                  ? swap.differentialPct[a][index - 1] : std::nan("");
                    portfolio.accrueSwap(t.id, swapAmount(t.direction, t.quantity, marks[a], difference, swap.markupPct, nights));
                }
        }

        // 1. Refresh the marks for assets that have a bar today.
        for (std::size_t a = 0; a < snapshot.bars.size() && a < marks.size(); ++a)
        {
            if (snapshot.bars[a])
                marks[a] = snapshot.bars[a]->close;
        }

        // Give the portfolio the marks so strategies can call portfolio.equity().
        portfolio.setMarks(marks);

        // 2. Stops and targets: each trade is checked against its own asset's bar.
        //    Copy the open trades first, because closing a trade changes the list.
        const std::vector<Trade> open = portfolio.openTrades();
        for (const Trade& t : open)
        {
            if (const Bar* bar = barOf(snapshot, t.asset))
                checkStops(portfolio, t, *bar, halfSpread(spreads, t.asset));
        }

        // 3. Strategy decides, then its orders are filled in the order it returned them.
        for (const Order& order : strategy.onSnapshot(snapshot, portfolio))
            fill(portfolio, order, snapshot, spreads);

        // 4. Record equity using the marks.
        result.equityCurve.push_back(portfolio.equity());
    }

    result.closedTrades = portfolio.closedTrades();
    result.openTrades = portfolio.openTrades();
    result.finalEquity = result.equityCurve.empty() ? startingCash : result.equityCurve.back();

    return result;
}
