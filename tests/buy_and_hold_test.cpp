#include <gtest/gtest.h>

#include <vector>

#include "backtester.hpp"
#include "buy_and_hold.hpp"
#include "registry.hpp"

// BuyAndHold is the one generic strategy in the public repo: the baseline every real strategy is compared with.
//
// Rules:
//   1. On the first bar it opens one long trade worth `fraction` of the equity (quantity = fraction x equity / close).
//   2. It never trades again and never closes, so the trade is still open after the last bar.
//   3. registerBuiltins() puts it in a registry as "buy_and_hold" with one parameter, "fraction" (0..1, default 1).

namespace
{
Bar bar(std::int64_t t, double close)
{
    Bar b;
    b.timestamp = t;
    b.open = b.high = b.low = b.close = close;
    return b;
}

BarSeries series(const std::vector<double>& closes)
{
    BarSeries s;
    std::int64_t t = 0;
    for (double c : closes)
        s.bars.push_back(bar(t += 86'400'000, c));
    return s;
}
}

TEST(BuyAndHold, OpensOneLongTradeOfTheWholeEquityOnTheFirstBar)
{
    BuyAndHold strategy(1.0);
    const BacktestResult r = runBacktest(series({100, 110, 120}), strategy, 1000.0);

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_EQ(r.openTrades[0].direction, Direction::Long);
    EXPECT_DOUBLE_EQ(r.openTrades[0].quantity, 10.0);        // 1000 / 100
    EXPECT_DOUBLE_EQ(r.openTrades[0].entryPrice, 100.0);
}

TEST(BuyAndHold, EquityFollowsThePrice)
{
    BuyAndHold strategy(1.0);
    const BacktestResult r = runBacktest(series({100, 110, 120}), strategy, 1000.0);

    ASSERT_EQ(r.equityCurve.size(), 3u);
    EXPECT_DOUBLE_EQ(r.equityCurve[0], 1000.0);
    EXPECT_DOUBLE_EQ(r.equityCurve[1], 1100.0);
    EXPECT_DOUBLE_EQ(r.finalEquity, 1200.0);
}

TEST(BuyAndHold, FractionSizesTheTrade)
{
    BuyAndHold strategy(0.5);
    const BacktestResult r = runBacktest(series({100, 120}), strategy, 1000.0);

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.openTrades[0].quantity, 5.0);          // 0.5 x 1000 / 100
    EXPECT_DOUBLE_EQ(r.finalEquity, 1100.0);                  // cash 1000 + 5 x 20
}

TEST(BuyAndHold, DoesNotBuyAgain)
{
    BuyAndHold strategy(1.0);
    const BacktestResult r = runBacktest(series({100, 50, 200, 10}), strategy, 1000.0);
    EXPECT_EQ(r.openTrades.size(), 1u);
}

TEST(BuyAndHold, EmptySeriesDoesNothing)
{
    BuyAndHold strategy(1.0);
    const BacktestResult r = runBacktest(series({}), strategy, 1000.0);
    EXPECT_TRUE(r.openTrades.empty());
    EXPECT_DOUBLE_EQ(r.finalEquity, 1000.0);
}

TEST(BuyAndHold, RejectsFractionOutsideZeroToOne)
{
    EXPECT_THROW(BuyAndHold(-0.1), std::invalid_argument);
    EXPECT_THROW(BuyAndHold(1.1), std::invalid_argument);
}

TEST(BuyAndHold, RegisteredAsBuiltin)
{
    StrategyRegistry registry;
    registerBuiltins(registry);

    ASSERT_TRUE(registry.has("buy_and_hold"));
    const StrategyInfo& info = registry.list()[0];
    ASSERT_EQ(info.params.size(), 1u);
    EXPECT_EQ(info.params[0].name, "fraction");
    EXPECT_DOUBLE_EQ(info.params[0].defaultValue, 1.0);

    auto strategy = registry.create("buy_and_hold", {{"fraction", 0.5}});
    const BacktestResult r = runBacktest(series({100, 120}), *strategy, 1000.0);
    EXPECT_DOUBLE_EQ(r.openTrades[0].quantity, 5.0);
}

TEST(BuyAndHold, RegisterBuiltinsTwiceIsAnError)
{
    StrategyRegistry registry;
    registerBuiltins(registry);
    EXPECT_THROW(registerBuiltins(registry), std::invalid_argument);
}
