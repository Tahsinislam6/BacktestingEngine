#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "backtester.hpp"

// These tests define how runBacktest() must behave.
//
// Expected API (include/backtester.hpp):
//
//   struct BacktestResult {
//       std::vector<double>      equityCurve;   // one value per bar, marked at that bar's close
//       std::vector<ClosedTrade> closedTrades;  // in the order they closed
//       std::vector<Trade>       openTrades;    // still open after the last bar
//       double                   finalEquity{}; // equity after the last bar
//   };
//
//   BacktestResult runBacktest(const BarSeries& series,
//                              Strategy& strategy,
//                              double startingCash);
//
// Rules:
//   1. Each bar: check stop-loss / take-profit, then call the strategy,
//      then fill its orders, then record equity.
//   2. Orders fill at the SAME bar's close. Entry time = that bar's timestamp.
//   3. Stops are checked before the strategy runs, and never on the bar a
//      trade was opened (it is not open yet when the check happens).
//   4. Long stop hit when low <= stop. Long target hit when high >= target.
//      Short stop hit when high >= stop. Short target hit when low <= target.
//   5. A gap past a level fills at the bar's open, not at the level.
//   6. If one bar hits both stop and target, the stop wins.
//   7. A close order for an unknown or already-closed id is ignored.

namespace
{
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

// A bar where every price is the same.
Bar flatBar(std::int64_t t, double price)
{
    return makeBar(t, price, price, price, price);
}

BarSeries makeSeries(std::vector<Bar> bars)
{
    BarSeries s;
    s.bars = std::move(bars);
    return s;
}

// Plays back a fixed list of orders, one list per bar, and records how many
// trades were open each time it was asked.
class ScriptedStrategy : public Strategy
{
public:
    explicit ScriptedStrategy(std::vector<std::vector<Order>> script = {})
        : script_(std::move(script)) {}

    std::vector<Order> onBar(const Bar&, const Portfolio& portfolio) override
    {
        openCounts.push_back(portfolio.openTrades().size());
        std::size_t i = calls++;
        if (i < script_.size())
            return script_[i];
        return {};
    }

    std::size_t calls{0};
    std::vector<std::size_t> openCounts;

private:
    std::vector<std::vector<Order>> script_;
};

constexpr double kCash = 10000.0;
}

// ---------- basics ----------

TEST(BacktesterTest, EmptySeriesGivesStartingCash)
{
    ScriptedStrategy s;

    BacktestResult r = runBacktest(makeSeries({}), s, kCash);

    EXPECT_TRUE(r.equityCurve.empty());
    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_TRUE(r.openTrades.empty());
    EXPECT_DOUBLE_EQ(r.finalEquity, kCash);
    EXPECT_EQ(s.calls, 0u);
}

TEST(BacktesterTest, EquityCurveHasOneEntryPerBar)
{
    ScriptedStrategy s;

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), flatBar(2, 101), flatBar(3, 102), flatBar(4, 103)}),
        s, kCash);

    EXPECT_EQ(r.equityCurve.size(), 4u);
}

TEST(BacktesterTest, StrategyIsCalledOncePerBar)
{
    ScriptedStrategy s;

    runBacktest(makeSeries({flatBar(1, 100), flatBar(2, 101), flatBar(3, 102)}), s, kCash);

    EXPECT_EQ(s.calls, 3u);
}

TEST(BacktesterTest, DoingNothingKeepsEquityFlat)
{
    ScriptedStrategy s;

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), flatBar(2, 150), flatBar(3, 80)}), s, kCash);

    for (double e : r.equityCurve)
        EXPECT_DOUBLE_EQ(e, kCash);
    EXPECT_DOUBLE_EQ(r.finalEquity, kCash);
}

TEST(BacktesterTest, FinalEquityEqualsLastPointOfCurve)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), flatBar(2, 107)}), s, kCash);

    EXPECT_DOUBLE_EQ(r.finalEquity, r.equityCurve.back());
}

// ---------- fills at the same bar's close ----------

TEST(BacktesterTest, OpenOrderFillsAtSameBarClose)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 2)}});

    BacktestResult r = runBacktest(
        makeSeries({makeBar(10, 99, 105, 98, 100), flatBar(20, 110)}), s, kCash);

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.openTrades[0].entryPrice, 100.0);   // bar 0 close, not its open
    EXPECT_EQ(r.openTrades[0].entryTime, 10);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Long);
    EXPECT_DOUBLE_EQ(r.openTrades[0].quantity, 2.0);
}

TEST(BacktesterTest, EquityOnTheFillBarIsUnchanged)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1)}});

    BacktestResult r = runBacktest(makeSeries({flatBar(1, 100), flatBar(2, 110)}), s, kCash);

    EXPECT_DOUBLE_EQ(r.equityCurve[0], kCash);        // bought at this close, no profit yet
    EXPECT_DOUBLE_EQ(r.equityCurve[1], kCash + 10.0); // price rose 10
}

TEST(BacktesterTest, ShortProfitsWhenPriceFalls)
{
    ScriptedStrategy s({{openOrder(Direction::Short, 1)}});

    BacktestResult r = runBacktest(makeSeries({flatBar(1, 100), flatBar(2, 90)}), s, kCash);

    EXPECT_DOUBLE_EQ(r.equityCurve[1], kCash + 10.0);
}

TEST(BacktesterTest, CloseOrderFillsAtSameBarClose)
{
    // bar 0: open (id 1).  bar 1: close id 1.
    ScriptedStrategy s({{openOrder(Direction::Long, 1)}, {closeOrder(1)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 105, 115, 104, 110), flatBar(3, 130)}),
        s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    const ClosedTrade& c = r.closedTrades[0];
    EXPECT_EQ(c.trade.id, 1);
    EXPECT_DOUBLE_EQ(c.exitPrice, 110.0);
    EXPECT_EQ(c.exitTime, 2);
    EXPECT_EQ(c.reason, ExitReason::Signal);
    EXPECT_DOUBLE_EQ(c.pnl, 10.0);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(BacktesterTest, ClosedProfitStaysInEquityAfterward)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1)}, {closeOrder(1)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), flatBar(2, 110), flatBar(3, 500)}), s, kCash);

    // After closing, later price moves no longer matter.
    EXPECT_DOUBLE_EQ(r.equityCurve[2], kCash + 10.0);
    EXPECT_DOUBLE_EQ(r.finalEquity, kCash + 10.0);
}

TEST(BacktesterTest, TradesLeftOpenAreMarkedAtTheLastClose)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 3)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), flatBar(2, 104)}), s, kCash);

    EXPECT_EQ(r.openTrades.size(), 1u);
    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_DOUBLE_EQ(r.finalEquity, kCash + 12.0);   // 3 * (104 - 100)
}

// ---------- multiple orders and ids ----------

TEST(BacktesterTest, MultipleOrdersInOneBarAreAllFilled)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1), openOrder(Direction::Short, 2)}});

    BacktestResult r = runBacktest(makeSeries({flatBar(1, 100)}), s, kCash);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].id, 1);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Long);
    EXPECT_EQ(r.openTrades[1].id, 2);
    EXPECT_EQ(r.openTrades[1].direction, Direction::Short);
}

TEST(BacktesterTest, CloseOrderClosesOnlyTheSpecifiedTrade)
{
    // bar 0: open two trades (ids 1, 2).  bar 1: close id 1 only.
    ScriptedStrategy s({{openOrder(Direction::Long, 1), openOrder(Direction::Long, 1)},
                        {closeOrder(1)}});

    BacktestResult r = runBacktest(makeSeries({flatBar(1, 100), flatBar(2, 110)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].trade.id, 1);
    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_EQ(r.openTrades[0].id, 2);
}

TEST(BacktesterTest, UnknownCloseIdIsIgnored)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1)}, {closeOrder(999)}});

    BacktestResult r = runBacktest(makeSeries({flatBar(1, 100), flatBar(2, 110)}), s, kCash);

    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_EQ(r.openTrades.size(), 1u);
}

TEST(BacktesterTest, StrategySeesTradesItOpenedEarlier)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1)}});

    runBacktest(makeSeries({flatBar(1, 100), flatBar(2, 100), flatBar(3, 100)}), s, kCash);

    ASSERT_EQ(s.openCounts.size(), 3u);
    EXPECT_EQ(s.openCounts[0], 0u);   // nothing open before the first order
    EXPECT_EQ(s.openCounts[1], 1u);
    EXPECT_EQ(s.openCounts[2], 1u);
}

// ---------- stop loss ----------

TEST(BacktesterTest, LongStopLossFillsAtTheStop)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1, 95.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 101, 94, 98)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 95.0);
    EXPECT_EQ(r.closedTrades[0].exitTime, 2);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::StopLoss);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].pnl, -5.0);
    EXPECT_DOUBLE_EQ(r.equityCurve[1], kCash - 5.0);   // filled at 95, not at the 98 close
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(BacktesterTest, ShortStopLossFillsAtTheStop)
{
    ScriptedStrategy s({{openOrder(Direction::Short, 1, 105.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 106, 99, 100)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 105.0);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::StopLoss);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].pnl, -5.0);
}

TEST(BacktesterTest, StopLossTriggersWhenLowEqualsTheStop)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1, 95.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 101, 95, 99)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::StopLoss);
}

TEST(BacktesterTest, StopLossNotTriggeredWhenPriceStaysAbove)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1, 95.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 104, 95.5, 99)}), s, kCash);

    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_EQ(r.openTrades.size(), 1u);
}

// ---------- take profit ----------

TEST(BacktesterTest, LongTakeProfitFillsAtTheTarget)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1, std::nullopt, 110.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 111, 99, 105)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 110.0);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].pnl, 10.0);
}

TEST(BacktesterTest, ShortTakeProfitFillsAtTheTarget)
{
    ScriptedStrategy s({{openOrder(Direction::Short, 1, std::nullopt, 90.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 101, 89, 95)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 90.0);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].pnl, 10.0);
}

// ---------- gaps ----------

TEST(BacktesterTest, GapBelowLongStopFillsAtTheOpen)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1, 95.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 90, 92, 88, 91)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 90.0);   // worse than the 95 stop
    EXPECT_DOUBLE_EQ(r.closedTrades[0].pnl, -10.0);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::StopLoss);
}

TEST(BacktesterTest, GapAboveShortStopFillsAtTheOpen)
{
    ScriptedStrategy s({{openOrder(Direction::Short, 1, 105.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 108, 110, 107, 109)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 108.0);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].pnl, -8.0);
}

TEST(BacktesterTest, GapAboveLongTargetFillsAtTheOpen)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1, std::nullopt, 110.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 115, 120, 114, 118)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 115.0);   // better than the 110 target
    EXPECT_DOUBLE_EQ(r.closedTrades[0].pnl, 15.0);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
}

TEST(BacktesterTest, GapBelowShortTargetFillsAtTheOpen)
{
    ScriptedStrategy s({{openOrder(Direction::Short, 1, std::nullopt, 90.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 85, 86, 83, 84)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 85.0);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].pnl, 15.0);
}

// ---------- stop and target in the same bar ----------

TEST(BacktesterTest, WhenBothAreHitTheStopWins)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1, 95.0, 110.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 111, 94, 100)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::StopLoss);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 95.0);
}

// ---------- timing ----------

TEST(BacktesterTest, StopIsNotCheckedOnTheBarTheTradeOpens)
{
    // Bar 0's low (90) is below the 95 stop, but the trade only opens
    // at bar 0's close, so it must survive bar 0.
    ScriptedStrategy s({{openOrder(Direction::Long, 1, 95.0)}});

    BacktestResult r = runBacktest(
        makeSeries({makeBar(1, 100, 100, 90, 100), makeBar(2, 100, 101, 96, 100)}),
        s, kCash);

    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_EQ(r.openTrades.size(), 1u);
}

TEST(BacktesterTest, StopRunsBeforeTheStrategySeesTheBar)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 1, 95.0)}});

    runBacktest(makeSeries({flatBar(1, 100), makeBar(2, 100, 101, 94, 98)}), s, kCash);

    ASSERT_EQ(s.openCounts.size(), 2u);
    EXPECT_EQ(s.openCounts[0], 0u);
    EXPECT_EQ(s.openCounts[1], 0u);   // already stopped out when the strategy runs
}

TEST(BacktesterTest, CloseOrderForAnAlreadyStoppedTradeIsIgnored)
{
    // Bar 1: the stop hits first, then the strategy tries to close id 1.
    ScriptedStrategy s({{openOrder(Direction::Long, 1, 95.0)}, {closeOrder(1)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 101, 94, 98)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::StopLoss);
}

TEST(BacktesterTest, OnlyTheTradeThatHitItsStopIsClosed)
{
    // id 1 has a stop at 95, id 2 has none.
    ScriptedStrategy s({{openOrder(Direction::Long, 1, 95.0), openOrder(Direction::Long, 1)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 101, 94, 98)}), s, kCash);

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].trade.id, 1);
    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_EQ(r.openTrades[0].id, 2);
}

TEST(BacktesterTest, StopLossPnlIsInFinalEquity)
{
    ScriptedStrategy s({{openOrder(Direction::Long, 2, 95.0)}});

    BacktestResult r = runBacktest(
        makeSeries({flatBar(1, 100), makeBar(2, 100, 101, 94, 98), flatBar(3, 200)}),
        s, kCash);

    EXPECT_DOUBLE_EQ(r.finalEquity, kCash - 10.0);   // 2 * (95 - 100), later price is irrelevant
}
