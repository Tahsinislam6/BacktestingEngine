#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "backtester.hpp"
#include "multi_backtester.hpp"
#include "single_asset.hpp"

// These tests define how SingleAssetAdapter must behave (see include/single_asset.hpp).
//
// Rules:
//   1. A null strategy is refused. setSymbols accepts exactly one symbol; zero or several throw std::invalid_argument.
//   2. A snapshot where asset 0 has a bar calls the strategy's onBar with that bar and the same portfolio; the strategy's
//      orders come back unchanged.
//   3. A snapshot where asset 0 has no bar does not call the strategy and returns no orders.
//   4. Run through runMultiBacktest on one asset, the result equals runBacktest on the same bars (no spreads, no swap).

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

// Remembers every bar it was shown and the equity the portfolio had; always returns the same orders.
struct Seen
{
    std::vector<Bar> bars;
    std::vector<double> equities;
};

class RecordingStrategy : public Strategy
{
public:
    RecordingStrategy(Seen& seen, std::vector<Order> reply = {}) : seen_(seen), reply_(std::move(reply)) {}

    std::vector<Order> onBar(const Bar& bar, const Portfolio& portfolio) override
    {
        seen_.bars.push_back(bar);
        seen_.equities.push_back(portfolio.equity());
        return reply_;
    }

private:
    Seen& seen_;
    std::vector<Order> reply_;
};

// Does nothing until it has seen `buyAt` bars, then buys 10 with a stop; closes at `sellAt` bars.
class ScriptedStrategy : public Strategy
{
public:
    ScriptedStrategy(int buyAt, int sellAt, double stop) : buyAt_(buyAt), sellAt_(sellAt), stop_(stop) {}

    std::vector<Order> onBar(const Bar&, const Portfolio& portfolio) override
    {
        ++seen_;
        if (seen_ == buyAt_)
            return {openOrder(Direction::Long, 10.0, stop_)};
        if (seen_ == sellAt_ && !portfolio.openTrades().empty())
            return {closeOrder(portfolio.openTrades().front().id)};
        return {};
    }

private:
    int buyAt_;
    int sellAt_;
    double stop_;
    int seen_{0};
};

Snapshot snapshotOf(std::vector<std::optional<Bar>> bars, std::int64_t time = 1)
{
    Snapshot s;
    s.timestamp = time;
    s.bars = std::move(bars);
    return s;
}

AlignedData oneAssetData(const std::vector<Bar>& bars)
{
    AlignedData data;
    data.symbols = {"EURUSD"};
    for (const Bar& b : bars)
        data.snapshots.push_back(snapshotOf({b}, b.timestamp));
    return data;
}
}

// ---------- construction and setSymbols ----------

TEST(SingleAssetAdapter, NullStrategyThrows)
{
    EXPECT_THROW(SingleAssetAdapter(nullptr), std::invalid_argument);
}

TEST(SingleAssetAdapter, OneSymbolIsAccepted)
{
    Seen seen;
    SingleAssetAdapter adapter(std::make_unique<RecordingStrategy>(seen));
    EXPECT_NO_THROW(adapter.setSymbols({"EURUSD"}));
}

TEST(SingleAssetAdapter, TwoSymbolsThrow)
{
    Seen seen;
    SingleAssetAdapter adapter(std::make_unique<RecordingStrategy>(seen));
    EXPECT_THROW(adapter.setSymbols({"EURUSD", "GBPUSD"}), std::invalid_argument);
}

TEST(SingleAssetAdapter, NoSymbolsThrow)
{
    Seen seen;
    SingleAssetAdapter adapter(std::make_unique<RecordingStrategy>(seen));
    EXPECT_THROW(adapter.setSymbols({}), std::invalid_argument);
}

// ---------- onSnapshot ----------

TEST(SingleAssetAdapter, HandsAssetZerosBarToTheStrategy)
{
    Seen seen;
    SingleAssetAdapter adapter(std::make_unique<RecordingStrategy>(seen));
    Portfolio portfolio(kCash);

    adapter.onSnapshot(snapshotOf({makeBar(5, 1.0, 2.0, 0.5, 1.5)}), portfolio);

    ASSERT_EQ(seen.bars.size(), 1u);
    EXPECT_EQ(seen.bars[0].timestamp, 5);
    EXPECT_DOUBLE_EQ(seen.bars[0].open, 1.0);
    EXPECT_DOUBLE_EQ(seen.bars[0].high, 2.0);
    EXPECT_DOUBLE_EQ(seen.bars[0].low, 0.5);
    EXPECT_DOUBLE_EQ(seen.bars[0].close, 1.5);
}

TEST(SingleAssetAdapter, PassesThePortfolioItWasGiven)
{
    Seen seen;
    SingleAssetAdapter adapter(std::make_unique<RecordingStrategy>(seen));
    Portfolio portfolio(1234.0);

    adapter.onSnapshot(snapshotOf({makeBar(1, 1, 1, 1, 1)}), portfolio);

    ASSERT_EQ(seen.equities.size(), 1u);
    EXPECT_DOUBLE_EQ(seen.equities[0], 1234.0);
}

TEST(SingleAssetAdapter, ReturnsTheStrategysOrdersUnchanged)
{
    Seen seen;
    SingleAssetAdapter adapter(std::make_unique<RecordingStrategy>(
        seen, std::vector<Order>{openOrder(Direction::Short, 3.0, 9.0, 4.0), closeOrder(7)}));
    Portfolio portfolio(kCash);

    const std::vector<Order> orders = adapter.onSnapshot(snapshotOf({makeBar(1, 1, 1, 1, 1)}), portfolio);

    ASSERT_EQ(orders.size(), 2u);
    EXPECT_EQ(orders[0].type, OrderType::Open);
    EXPECT_EQ(orders[0].direction, Direction::Short);
    EXPECT_DOUBLE_EQ(orders[0].quantity, 3.0);
    EXPECT_EQ(orders[0].asset, 0);
    EXPECT_DOUBLE_EQ(*orders[0].stopLoss, 9.0);
    EXPECT_DOUBLE_EQ(*orders[0].takeProfit, 4.0);
    EXPECT_EQ(orders[1].type, OrderType::Close);
    EXPECT_EQ(orders[1].tradeId, 7);
}

TEST(SingleAssetAdapter, SkipsASnapshotWithoutABar)
{
    Seen seen;
    SingleAssetAdapter adapter(std::make_unique<RecordingStrategy>(seen, std::vector<Order>{closeOrder(1)}));
    Portfolio portfolio(kCash);

    const std::vector<Order> orders = adapter.onSnapshot(snapshotOf({std::nullopt}), portfolio);

    EXPECT_TRUE(seen.bars.empty());   // the strategy was not called
    EXPECT_TRUE(orders.empty());      // so its orders are not returned either
}

TEST(SingleAssetAdapter, KeepsGoingAfterAGap)
{
    Seen seen;
    SingleAssetAdapter adapter(std::make_unique<RecordingStrategy>(seen));
    Portfolio portfolio(kCash);

    adapter.onSnapshot(snapshotOf({makeBar(1, 1, 1, 1, 1)}), portfolio);
    adapter.onSnapshot(snapshotOf({std::nullopt}), portfolio);
    adapter.onSnapshot(snapshotOf({makeBar(3, 3, 3, 3, 3)}), portfolio);

    ASSERT_EQ(seen.bars.size(), 2u);
    EXPECT_EQ(seen.bars[0].timestamp, 1);
    EXPECT_EQ(seen.bars[1].timestamp, 3);
}

// ---------- inside the runner ----------

TEST(SingleAssetAdapter, MultiRunnerGivesTheSameResultAsTheSingleRunner)
{
    // A rising then falling series, with a stop that is hit on bar 6 (low 8 <= 9), after buying on bar 2.
    const std::vector<Bar> bars = {
        makeBar(1, 10, 11, 9.5, 10.5), makeBar(2, 10.5, 12, 10, 11.5), makeBar(3, 11.5, 13, 11, 12.5),
        makeBar(4, 12.5, 14, 12, 13.5), makeBar(5, 13.5, 14, 12, 12.0), makeBar(6, 12.0, 12.5, 8.0, 9.0),
        makeBar(7, 9.0, 9.5, 8.5, 9.2),
    };

    BarSeries series;
    series.symbol = "EURUSD";
    series.bars = bars;

    ScriptedStrategy forSingle(2, 5, 9.0);
    const BacktestResult expected = runBacktest(series, forSingle, kCash);

    SingleAssetAdapter adapter(std::make_unique<ScriptedStrategy>(2, 5, 9.0));
    const BacktestResult actual = runMultiBacktest(oneAssetData(bars), adapter, kCash);

    ASSERT_EQ(actual.equityCurve.size(), expected.equityCurve.size());
    for (std::size_t i = 0; i < expected.equityCurve.size(); ++i)
        EXPECT_DOUBLE_EQ(actual.equityCurve[i], expected.equityCurve[i]) << "bar " << i;

    ASSERT_EQ(actual.closedTrades.size(), expected.closedTrades.size());
    EXPECT_GE(actual.closedTrades.size(), 1u);   // the scenario really trades
    EXPECT_DOUBLE_EQ(actual.finalEquity, expected.finalEquity);
}

TEST(SingleAssetAdapter, MultiRunnerRefusesTwoAssets)
{
    AlignedData data;
    data.symbols = {"EURUSD", "GBPUSD"};
    data.snapshots.push_back(snapshotOf({makeBar(1, 1, 1, 1, 1), makeBar(1, 2, 2, 2, 2)}));

    SingleAssetAdapter adapter(std::make_unique<ScriptedStrategy>(1, 2, 0.5));
    EXPECT_THROW(runMultiBacktest(data, adapter, kCash), std::invalid_argument);
}
