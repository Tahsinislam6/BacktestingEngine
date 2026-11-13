#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "multi_backtester.hpp"

// These tests define how runMultiBacktest() and the multi-asset Portfolio::equity() must behave.
//
// Expected API:
//
//   // include/portfolio.hpp: a second overload, next to equity(double)
//   double Portfolio::equity(const std::vector<double>& marks) const;
//       marks[a] is the price used to value open trades on asset a.
//       A trade whose asset has no entry in marks is valued at its entry price (zero unrealised PnL).
//
//   // include/multi_backtester.hpp
//   class MultiStrategy {
//   public:
//       virtual ~MultiStrategy() = default;
//       virtual std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio) = 0;
//   };
//
//   BacktestResult runMultiBacktest(const AlignedData& data, MultiStrategy& strategy, double startingCash);
//       (BacktestResult is the same struct runBacktest returns; the equity curve has one value per snapshot.)
//
// Rules:
//   1. Each snapshot: update the marks, check stops/targets, call the strategy, fill its orders,
//      record equity. Same order of events as the single-asset runBacktest.
//   2. Marks are the LAST KNOWN close of every asset. An asset with no bar today keeps its old mark.
//   3. Stops and targets of a trade are checked against that trade's own asset's bar, and only on
//      snapshots where that asset has a bar. The single-asset rules apply unchanged (low/high hit,
//      gap fills at the open, stop wins over target, never on the snapshot the trade was opened in).
//   4. An Open order fills at the close of its asset's bar in this snapshot (entry time = that bar's
//      timestamp). If the asset has no bar today, or the asset index does not exist, it is ignored.
//   5. A Close order fills at the close of the trade's asset's bar. If that asset has no bar today,
//      it is ignored and the trade stays open. An unknown trade id is ignored.
//   6. Orders are filled in the order the strategy returned them.
//   7. Equity = cash + unrealised PnL of every open trade at its asset's mark. Realised PnL goes to cash.

namespace
{
constexpr double kCash = 10000.0;

Bar makeBar(std::int64_t t, double o, double h, double l, double c)
{
    Bar b;
    b.timestamp = t;
    b.open = o;
    b.high = h;
    b.low = l;
    b.close = c;
    return b;
}

Bar flatBar(std::int64_t t, double price)
{
    return makeBar(t, price, price, price, price);
}

Snapshot snap(std::int64_t t, std::vector<std::optional<Bar>> bars)
{
    Snapshot s;
    s.timestamp = t;
    s.bars = std::move(bars);
    return s;
}

AlignedData makeData(std::size_t assets, std::vector<Snapshot> snapshots)
{
    AlignedData d;
    for (std::size_t i = 0; i < assets; ++i)
        d.symbols.push_back("A" + std::to_string(i));
    d.snapshots = std::move(snapshots);
    return d;
}

// Plays back a fixed list of orders, one list per call, and records what it was shown.
class ScriptedMulti : public MultiStrategy
{
public:
    explicit ScriptedMulti(std::vector<std::vector<Order>> script = {})
        : script_(std::move(script)) {}

    std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio) override
    {
        seenTimestamps.push_back(snapshot.timestamp);

        std::vector<bool> present;
        for (const auto& slot : snapshot.bars)
            present.push_back(slot.has_value());
        seenPresent.push_back(present);

        openCounts.push_back(portfolio.openTrades().size());

        const std::size_t i = calls++;
        if (i < script_.size())
            return script_[i];
        return {};
    }

    std::size_t calls{0};
    std::vector<std::int64_t> seenTimestamps;
    std::vector<std::vector<bool>> seenPresent;
    std::vector<std::size_t> openCounts;

private:
    std::vector<std::vector<Order>> script_;
};
}

// ---------- Portfolio::equity with one mark per asset ----------

TEST(PortfolioMarksTest, NoTradesIsJustCash)
{
    Portfolio p(kCash);
    const std::vector<double> marks = {100.0, 50.0};

    EXPECT_DOUBLE_EQ(p.equity(marks), kCash);
}

TEST(PortfolioMarksTest, SumsUnrealisedPnlOfEachAsset)
{
    Portfolio p(kCash);
    p.open(Direction::Long, 1, 100, 1, std::nullopt, std::nullopt, 0);    // +10 at 110
    p.open(Direction::Short, 2, 50, 1, std::nullopt, std::nullopt, 1);    // +10 at 45
    const std::vector<double> marks = {110.0, 45.0};

    EXPECT_DOUBLE_EQ(p.equity(marks), kCash + 10 + 10);
}

TEST(PortfolioMarksTest, MarksOfAssetsWithoutTradesAreIgnored)
{
    Portfolio p(kCash);
    p.open(Direction::Long, 1, 100, 1, std::nullopt, std::nullopt, 2);
    const std::vector<double> marks = {999.0, 999.0, 110.0};

    EXPECT_DOUBLE_EQ(p.equity(marks), kCash + 10);
}

TEST(PortfolioMarksTest, TradeWithoutAMarkIsValuedAtItsEntryPrice)
{
    Portfolio p(kCash);
    p.open(Direction::Long, 1, 100, 1, std::nullopt, std::nullopt, 3);
    const std::vector<double> marks = {500.0};   // nothing for asset 3

    EXPECT_DOUBLE_EQ(p.equity(marks), kCash);
}

TEST(PortfolioMarksTest, OneMarkMatchesTheSinglePriceOverload)
{
    Portfolio p(kCash);
    p.open(Direction::Long, 3, 100, 1);
    p.open(Direction::Short, 1, 100, 1);
    const std::vector<double> marks = {107.0};

    EXPECT_DOUBLE_EQ(p.equity(marks), p.equity(107.0));
}

// ---------- shape of the run ----------

TEST(MultiBacktestTest, NoSnapshotsGivesAnEmptyResult)
{
    ScriptedMulti strategy;

    BacktestResult r = runMultiBacktest(makeData(2, {}), strategy, kCash);

    EXPECT_TRUE(r.equityCurve.empty());
    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_TRUE(r.openTrades.empty());
    EXPECT_DOUBLE_EQ(r.finalEquity, kCash);
    EXPECT_EQ(strategy.calls, 0u);
}

TEST(MultiBacktestTest, WithoutTradesEquityStaysAtStartingCash)
{
    ScriptedMulti strategy;
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 105), flatBar(2, 40)}),
        snap(3, {flatBar(3, 90), std::nullopt}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.equityCurve.size(), 3u);
    for (double e : r.equityCurve)
        EXPECT_DOUBLE_EQ(e, kCash);
    EXPECT_DOUBLE_EQ(r.finalEquity, kCash);
}

TEST(MultiBacktestTest, StrategyIsCalledOncePerSnapshotInOrder)
{
    ScriptedMulti strategy;
    AlignedData data = makeData(1, {
        snap(10, {flatBar(10, 1)}),
        snap(20, {flatBar(20, 1)}),
        snap(30, {flatBar(30, 1)}),
    });

    runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(strategy.seenTimestamps.size(), 3u);
    EXPECT_EQ(strategy.seenTimestamps[0], 10);
    EXPECT_EQ(strategy.seenTimestamps[1], 20);
    EXPECT_EQ(strategy.seenTimestamps[2], 30);
}

TEST(MultiBacktestTest, StrategySeesWhichAssetsHaveABar)
{
    ScriptedMulti strategy;
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), std::nullopt}),
        snap(2, {std::nullopt, flatBar(2, 50)}),
    });

    runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(strategy.seenPresent.size(), 2u);
    EXPECT_EQ(strategy.seenPresent[0], (std::vector<bool>{true, false}));
    EXPECT_EQ(strategy.seenPresent[1], (std::vector<bool>{false, true}));
}

// ---------- opening ----------

TEST(MultiBacktestTest, OpenOrderFillsAtThatAssetsClose)
{
    ScriptedMulti strategy({{openOrder(Direction::Long, 2, std::nullopt, std::nullopt, 1)}});
    AlignedData data = makeData(2, {snap(1, {flatBar(1, 100), flatBar(1, 50)})});

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.openTrades.size(), 1u);
    const Trade& t = r.openTrades[0];
    EXPECT_EQ(t.asset, 1);
    EXPECT_DOUBLE_EQ(t.entryPrice, 50.0);
    EXPECT_DOUBLE_EQ(t.quantity, 2.0);
    EXPECT_EQ(t.entryTime, 1);
}

TEST(MultiBacktestTest, OrderWithoutAssetUsesTheFirstAsset)
{
    ScriptedMulti strategy({{openOrder(Direction::Long, 1)}});
    AlignedData data = makeData(2, {snap(1, {flatBar(1, 100), flatBar(1, 50)})});

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_EQ(r.openTrades[0].asset, 0);
    EXPECT_DOUBLE_EQ(r.openTrades[0].entryPrice, 100.0);
}

TEST(MultiBacktestTest, OpenOrderForAnAssetWithNoBarIsIgnored)
{
    ScriptedMulti strategy({{openOrder(Direction::Long, 1, std::nullopt, std::nullopt, 1)}});
    AlignedData data = makeData(2, {snap(1, {flatBar(1, 100), std::nullopt})});

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    EXPECT_TRUE(r.openTrades.empty());
}

TEST(MultiBacktestTest, OpenOrderForAnUnknownAssetIsIgnored)
{
    ScriptedMulti strategy({{
        openOrder(Direction::Long, 1, std::nullopt, std::nullopt, 5),
        openOrder(Direction::Long, 1, std::nullopt, std::nullopt, -1),
    }});
    AlignedData data = makeData(2, {snap(1, {flatBar(1, 100), flatBar(1, 50)})});

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    EXPECT_TRUE(r.openTrades.empty());
}

TEST(MultiBacktestTest, SeveralOrdersInOneSnapshotAreFilledInOrder)
{
    ScriptedMulti strategy({{
        openOrder(Direction::Long, 1, std::nullopt, std::nullopt, 0),
        openOrder(Direction::Short, 1, std::nullopt, std::nullopt, 1),
    }});
    AlignedData data = makeData(2, {snap(1, {flatBar(1, 100), flatBar(1, 50)})});

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].id, 1);
    EXPECT_EQ(r.openTrades[0].asset, 0);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Long);
    EXPECT_EQ(r.openTrades[1].id, 2);
    EXPECT_EQ(r.openTrades[1].asset, 1);
    EXPECT_EQ(r.openTrades[1].direction, Direction::Short);
}

// ---------- closing ----------

TEST(MultiBacktestTest, CloseOrderFillsAtTheTradesAssetClose)
{
    ScriptedMulti strategy({
        {openOrder(Direction::Long, 2, std::nullopt, std::nullopt, 1)},
        {closeOrder(1)},
    });
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 999), flatBar(2, 60)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    EXPECT_TRUE(r.openTrades.empty());
    ASSERT_EQ(r.closedTrades.size(), 1u);
    const ClosedTrade& c = r.closedTrades[0];
    EXPECT_EQ(c.trade.asset, 1);
    EXPECT_DOUBLE_EQ(c.exitPrice, 60.0);
    EXPECT_EQ(c.exitTime, 2);
    EXPECT_EQ(c.reason, ExitReason::Signal);
    EXPECT_DOUBLE_EQ(c.pnl, 20.0);
}

TEST(MultiBacktestTest, CloseOrderIsIgnoredWhileTheAssetHasNoBarAndWorksLater)
{
    ScriptedMulti strategy({
        {openOrder(Direction::Long, 2, std::nullopt, std::nullopt, 1)},
        {closeOrder(1)},     // asset 1 has no bar on snapshot 2
        {closeOrder(1)},     // asset 1 is back on snapshot 3
    });
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 100), std::nullopt}),
        snap(3, {flatBar(3, 100), flatBar(3, 55)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    EXPECT_EQ(strategy.openCounts[2], 1u);   // still open when the strategy ran on snapshot 3
    EXPECT_TRUE(r.openTrades.empty());
    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 55.0);
    EXPECT_EQ(r.closedTrades[0].exitTime, 3);
}

TEST(MultiBacktestTest, CloseOrderForAnUnknownIdIsIgnored)
{
    ScriptedMulti strategy({
        {openOrder(Direction::Long, 1, std::nullopt, std::nullopt, 0)},
        {closeOrder(99)},
    });
    AlignedData data = makeData(1, {
        snap(1, {flatBar(1, 100)}),
        snap(2, {flatBar(2, 110)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    EXPECT_EQ(r.openTrades.size(), 1u);
    EXPECT_TRUE(r.closedTrades.empty());
}

// ---------- equity ----------

TEST(MultiBacktestTest, EquitySumsUnrealisedPnlAcrossAssets)
{
    ScriptedMulti strategy({{
        openOrder(Direction::Long, 1, std::nullopt, std::nullopt, 0),    // opens at 100
        openOrder(Direction::Short, 2, std::nullopt, std::nullopt, 1),   // opens at 50
    }});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 110), flatBar(2, 45)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.equityCurve.size(), 2u);
    EXPECT_DOUBLE_EQ(r.equityCurve[0], kCash);
    EXPECT_DOUBLE_EQ(r.equityCurve[1], kCash + 10 + 10);
    EXPECT_DOUBLE_EQ(r.finalEquity, kCash + 20);
}

TEST(MultiBacktestTest, AnAssetWithoutABarKeepsItsLastKnownMark)
{
    ScriptedMulti strategy({{
        openOrder(Direction::Long, 1, std::nullopt, std::nullopt, 0),
        openOrder(Direction::Short, 2, std::nullopt, std::nullopt, 1),
    }});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 110), flatBar(2, 45)}),
        snap(3, {flatBar(3, 120), std::nullopt}),     // asset 1 stays marked at 45
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.equityCurve.size(), 3u);
    EXPECT_DOUBLE_EQ(r.equityCurve[2], kCash + 20 + 10);
}

TEST(MultiBacktestTest, RealisedPnlGoesToCash)
{
    ScriptedMulti strategy({
        {openOrder(Direction::Long, 1, std::nullopt, std::nullopt, 0)},
        {closeOrder(1)},
    });
    AlignedData data = makeData(1, {
        snap(1, {flatBar(1, 100)}),
        snap(2, {flatBar(2, 110)}),
        snap(3, {flatBar(3, 200)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.equityCurve.size(), 3u);
    EXPECT_DOUBLE_EQ(r.equityCurve[1], kCash + 10);
    EXPECT_DOUBLE_EQ(r.equityCurve[2], kCash + 10);   // no open trade, so the later price is irrelevant
    EXPECT_DOUBLE_EQ(r.finalEquity, kCash + 10);
}

// ---------- stops and targets ----------

TEST(MultiBacktestTest, LongStopOnAnAssetHitsAtTheStopPrice)
{
    ScriptedMulti strategy({{openOrder(Direction::Long, 1, 45.0, std::nullopt, 1)}});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 100), makeBar(2, 50, 51, 44, 48)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    const ClosedTrade& c = r.closedTrades[0];
    EXPECT_EQ(c.trade.asset, 1);
    EXPECT_EQ(c.reason, ExitReason::StopLoss);
    EXPECT_DOUBLE_EQ(c.exitPrice, 45.0);
    EXPECT_EQ(c.exitTime, 2);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(MultiBacktestTest, ShortTakeProfitOnAnAsset)
{
    ScriptedMulti strategy({{openOrder(Direction::Short, 1, std::nullopt, 45.0, 1)}});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 100), makeBar(2, 50, 51, 44, 48)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 45.0);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].pnl, 5.0);
}

TEST(MultiBacktestTest, GapThroughTheStopFillsAtTheOpen)
{
    ScriptedMulti strategy({{openOrder(Direction::Long, 1, 45.0, std::nullopt, 1)}});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 100), makeBar(2, 40, 42, 38, 41)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 40.0);
}

TEST(MultiBacktestTest, StopWinsWhenStopAndTargetAreBothHit)
{
    ScriptedMulti strategy({{openOrder(Direction::Long, 1, 45.0, 55.0, 1)}});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 100), makeBar(2, 50, 60, 40, 50)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::StopLoss);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 45.0);
}

TEST(MultiBacktestTest, StopsAreNotCheckedOnTheSnapshotTheTradeIsOpenedIn)
{
    ScriptedMulti strategy({{openOrder(Direction::Long, 1, 45.0, std::nullopt, 1)}});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), makeBar(1, 50, 60, 40, 50)}),   // this bar would hit the stop
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    EXPECT_EQ(r.openTrades.size(), 1u);
    EXPECT_TRUE(r.closedTrades.empty());
}

TEST(MultiBacktestTest, AStopOnOneAssetIgnoresTheOtherAssetsBar)
{
    // Asset 1 crashes through 95, but the stop belongs to the asset 0 trade, whose bar is calm.
    ScriptedMulti strategy({{openOrder(Direction::Long, 1, 95.0, std::nullopt, 0)}});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {makeBar(2, 100, 101, 99, 100), makeBar(2, 50, 51, 40, 50)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    EXPECT_EQ(r.openTrades.size(), 1u);
    EXPECT_TRUE(r.closedTrades.empty());
}

TEST(MultiBacktestTest, AStopOnOneAssetLeavesTheOtherAssetsTradeOpen)
{
    ScriptedMulti strategy({{
        openOrder(Direction::Long, 1, 95.0, std::nullopt, 0),
        openOrder(Direction::Long, 1, 45.0, std::nullopt, 1),
    }});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {makeBar(2, 100, 101, 99, 100), makeBar(2, 50, 51, 40, 50)}),
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].trade.asset, 1);
    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_EQ(r.openTrades[0].asset, 0);
}

TEST(MultiBacktestTest, StopsAreNotCheckedWhileTheAssetHasNoBar)
{
    ScriptedMulti strategy({{openOrder(Direction::Long, 1, 45.0, std::nullopt, 1)}});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {makeBar(2, 100, 101, 99, 100), std::nullopt}),      // no bar for asset 1: no check
        snap(3, {flatBar(3, 100), makeBar(3, 50, 51, 40, 45)}),      // asset 1 is back and hits
    });

    BacktestResult r = runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].exitTime, 3);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 45.0);
    EXPECT_DOUBLE_EQ(r.equityCurve[1], kCash);   // marked at the last known close (50), no PnL yet
}

TEST(MultiBacktestTest, StopsAreCheckedBeforeTheStrategyRuns)
{
    ScriptedMulti strategy({{openOrder(Direction::Long, 1, 45.0, std::nullopt, 1)}});
    AlignedData data = makeData(2, {
        snap(1, {flatBar(1, 100), flatBar(1, 50)}),
        snap(2, {flatBar(2, 100), makeBar(2, 50, 51, 44, 48)}),
    });

    runMultiBacktest(data, strategy, kCash);

    ASSERT_EQ(strategy.openCounts.size(), 2u);
    EXPECT_EQ(strategy.openCounts[0], 0u);
    EXPECT_EQ(strategy.openCounts[1], 0u);   // the stop already closed the trade
}

// ---------- one asset behaves like the single-asset backtester ----------

namespace
{
// Script for the single-asset strategy used in the equivalence test.
std::vector<std::vector<Order>> equivalenceScript()
{
    return {
        {openOrder(Direction::Long, 1, 96.0, 110.0)},     // bar 1: long with stop 96, target 110
        {},                                               // bar 2
        {},                                               // bar 3
        {openOrder(Direction::Short, 2)},                 // bar 4: the long was stopped out first
        {closeOrder(2)},                                  // bar 5: close the short
    };
}

class ScriptedSingle : public Strategy
{
public:
    explicit ScriptedSingle(std::vector<std::vector<Order>> script) : script_(std::move(script)) {}

    std::vector<Order> onBar(const Bar&, const Portfolio&) override
    {
        const std::size_t i = calls_++;
        return i < script_.size() ? script_[i] : std::vector<Order>{};
    }

private:
    std::vector<std::vector<Order>> script_;
    std::size_t calls_{0};
};

// Feeds asset 0 of each snapshot to a single-asset strategy.
class OneAssetAdapter : public MultiStrategy
{
public:
    explicit OneAssetAdapter(Strategy& inner) : inner_(inner) {}

    std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio) override
    {
        if (!snapshot.bars[0].has_value())
            return {};
        return inner_.onBar(*snapshot.bars[0], portfolio);
    }

private:
    Strategy& inner_;
};
}

TEST(MultiBacktestTest, OneAssetGivesTheSameResultAsTheSingleAssetBacktester)
{
    const std::vector<Bar> bars = {
        flatBar(1, 100),
        makeBar(2, 100, 102, 99, 101),
        makeBar(3, 101, 103, 100, 102),
        makeBar(4, 102, 104, 94, 95),      // low 94 <= stop 96: the long is stopped at 96
        flatBar(5, 98),
    };

    BarSeries series;
    series.bars = bars;

    AlignedData data = makeData(1, {});
    for (const Bar& b : bars)
        data.snapshots.push_back(snap(b.timestamp, {b}));

    ScriptedSingle singleStrategy(equivalenceScript());
    BacktestResult single = runBacktest(series, singleStrategy, kCash);

    ScriptedSingle innerStrategy(equivalenceScript());
    OneAssetAdapter adapter(innerStrategy);
    BacktestResult multi = runMultiBacktest(data, adapter, kCash);

    ASSERT_EQ(multi.equityCurve.size(), single.equityCurve.size());
    for (std::size_t i = 0; i < single.equityCurve.size(); ++i)
        EXPECT_DOUBLE_EQ(multi.equityCurve[i], single.equityCurve[i]) << "bar " << i;

    EXPECT_DOUBLE_EQ(multi.finalEquity, single.finalEquity);
    EXPECT_EQ(multi.openTrades.size(), single.openTrades.size());

    ASSERT_EQ(multi.closedTrades.size(), single.closedTrades.size());
    for (std::size_t i = 0; i < single.closedTrades.size(); ++i)
    {
        EXPECT_EQ(multi.closedTrades[i].reason, single.closedTrades[i].reason) << "trade " << i;
        EXPECT_DOUBLE_EQ(multi.closedTrades[i].exitPrice, single.closedTrades[i].exitPrice) << "trade " << i;
        EXPECT_DOUBLE_EQ(multi.closedTrades[i].pnl, single.closedTrades[i].pnl) << "trade " << i;
        EXPECT_EQ(multi.closedTrades[i].exitTime, single.closedTrades[i].exitTime) << "trade " << i;
    }

    // Sanity: the scenario really exercised a stop and a signal close.
    ASSERT_EQ(single.closedTrades.size(), 2u);
    EXPECT_EQ(single.closedTrades[0].reason, ExitReason::StopLoss);
    EXPECT_EQ(single.closedTrades[1].reason, ExitReason::Signal);
}
