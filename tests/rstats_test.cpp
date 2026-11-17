#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "rstats.hpp"

// R = a trade's profit divided by what it risked: quantity * |entry price - stop loss|.
//   +1R = won what it risked, -1R = lost it (a stop hit), -0.5R = lost half of it.
// Only closed trades with a stop loss have an R. Win = R > 0, loss = R < 0; R == 0 is neither.

namespace
{
const double kNan = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

// risk = qty * |entry - stop|. `rMultiple` sets the profit to rMultiple * risk.
ClosedTrade trade(double rMultiple, ExitReason reason = ExitReason::Signal, bool hasTarget = false,
                  double entry = 100, double stop = 97, double qty = 10)
{
    ClosedTrade c;
    c.trade.entryPrice = entry;
    c.trade.stopLoss = stop;
    c.trade.initialStop = stop;
    c.trade.quantity = qty;
    if (hasTarget)
        c.trade.takeProfit = entry + 2;
    c.reason = reason;
    c.pnl = rMultiple * qty * std::abs(entry - stop);
    return c;
}
}

TEST(RStatsTest, NoTradesGivesNothingToAverage)
{
    const RStats s = computeRReport({}).all;

    EXPECT_EQ(s.trades, 0);
    EXPECT_EQ(s.wins, 0);
    EXPECT_DOUBLE_EQ(s.totalR, 0.0);
    EXPECT_TRUE(std::isnan(s.winRate));
    EXPECT_TRUE(std::isnan(s.avgWinR));
    EXPECT_TRUE(std::isnan(s.avgLossR));
    EXPECT_TRUE(std::isnan(s.expectancyR));
    EXPECT_TRUE(std::isnan(s.profitFactor));
}

TEST(RStatsTest, RIsProfitOverTheAmountRisked)
{
    // risk = 10 * 3 = 30
    EXPECT_DOUBLE_EQ(computeRReport({trade(1.0)}).all.totalR, 1.0);
    EXPECT_DOUBLE_EQ(computeRReport({trade(-1.0)}).all.totalR, -1.0);
    EXPECT_DOUBLE_EQ(computeRReport({trade(0.5)}).all.totalR, 0.5);

    ClosedTrade c = trade(0);
    c.pnl = 45;                                   // 45 / 30
    EXPECT_DOUBLE_EQ(computeRReport({c}).all.totalR, 1.5);
}

TEST(RStatsTest, TheRiskDependsOnQuantityAndStopDistance)
{
    // same profit of 60 on a risk of 10 * 3 = 30 and on 20 * 1.5 = 30: both 2R; and on 60: 1R
    ClosedTrade a = trade(0, ExitReason::Signal, false, 100, 97, 10);
    ClosedTrade b = trade(0, ExitReason::Signal, false, 100, 98.5, 20);
    ClosedTrade c = trade(0, ExitReason::Signal, false, 100, 94, 10);
    a.pnl = b.pnl = c.pnl = 60;

    EXPECT_DOUBLE_EQ(computeRReport({a}).all.totalR, 2.0);
    EXPECT_DOUBLE_EQ(computeRReport({b}).all.totalR, 2.0);
    EXPECT_DOUBLE_EQ(computeRReport({c}).all.totalR, 1.0);
}

TEST(RStatsTest, ShortTradesUseTheDistanceToTheStopAboveTheEntry)
{
    ClosedTrade c = trade(0);
    c.trade.direction = Direction::Short;
    c.trade.stopLoss = 103;                       // risk 10 * 3 = 30
    c.trade.initialStop = 103;
    c.pnl = -30;

    EXPECT_DOUBLE_EQ(computeRReport({c}).all.totalR, -1.0);
}

TEST(RStatsTest, WinRateAveragesExpectancyAndProfitFactor)
{
    const RStats s = computeRReport({trade(2), trade(1), trade(-1), trade(-1), trade(0)}).all;

    EXPECT_EQ(s.trades, 5);
    EXPECT_EQ(s.wins, 2);
    EXPECT_DOUBLE_EQ(s.winRate, 0.4);
    EXPECT_DOUBLE_EQ(s.avgWinR, 1.5);
    EXPECT_DOUBLE_EQ(s.avgLossR, -1.0);
    EXPECT_DOUBLE_EQ(s.totalR, 1.0);
    EXPECT_DOUBLE_EQ(s.expectancyR, 0.2);         // the break-even trade counts as a trade
    EXPECT_DOUBLE_EQ(s.profitFactor, 1.5);        // 3 won / 2 lost
}

TEST(RStatsTest, WithoutLossesTheProfitFactorIsInfiniteAndThereIsNoAverageLoss)
{
    const RStats s = computeRReport({trade(1), trade(2)}).all;

    EXPECT_EQ(s.profitFactor, kInf);
    EXPECT_TRUE(std::isnan(s.avgLossR));
}

TEST(RStatsTest, WithoutWinsTheProfitFactorIsZeroAndThereIsNoAverageWin)
{
    const RStats s = computeRReport({trade(-1), trade(-0.5)}).all;

    EXPECT_DOUBLE_EQ(s.profitFactor, 0.0);
    EXPECT_TRUE(std::isnan(s.avgWinR));
    EXPECT_DOUBLE_EQ(s.winRate, 0.0);
}

TEST(RStatsTest, ExitsAreCountedByReasonWithTheirTotalR)
{
    const RReport r = computeRReport({trade(-1, ExitReason::StopLoss), trade(-1.1, ExitReason::StopLoss),
                                      trade(0.7, ExitReason::TakeProfit), trade(0.1, ExitReason::Breakeven),
                                      trade(-0.4, ExitReason::Signal), trade(0.6, ExitReason::Signal)});

    EXPECT_EQ(r.all.stop.count, 2);
    EXPECT_DOUBLE_EQ(r.all.stop.totalR, -2.1);
    EXPECT_EQ(r.all.target.count, 1);
    EXPECT_DOUBLE_EQ(r.all.target.totalR, 0.7);
    EXPECT_EQ(r.all.breakeven.count, 1);
    EXPECT_DOUBLE_EQ(r.all.breakeven.totalR, 0.1);
    EXPECT_EQ(r.all.signal.count, 2);
    EXPECT_NEAR(r.all.signal.totalR, 0.2, 1e-12);
    EXPECT_EQ(r.all.stop.count + r.all.target.count + r.all.breakeven.count + r.all.signal.count +
              r.all.trailing.count, r.all.trades);
}

TEST(RStatsTest, TrailingExitsHaveTheirOwnCount)
{
    const RReport r = computeRReport({trade(1.4, ExitReason::Trailing), trade(2.0, ExitReason::Trailing),
                                      trade(-0.2, ExitReason::Signal)});

    EXPECT_EQ(r.all.trailing.count, 2);
    EXPECT_NEAR(r.all.trailing.totalR, 3.4, 1e-12);
    EXPECT_EQ(r.all.signal.count, 1);
}

TEST(RStatsTest, IndicatorExitsHaveTheirOwnCount)
{
    const RReport r = computeRReport({trade(0.3, ExitReason::Indicator), trade(-0.2, ExitReason::Indicator)});

    EXPECT_EQ(r.all.indicator.count, 2);
    EXPECT_NEAR(r.all.indicator.totalR, 0.1, 1e-12);
    EXPECT_EQ(r.all.signal.count, 0);
}

TEST(RStatsTest, TradesStillOpenAtTheEndAreCountedButNotValued)
{
    EXPECT_EQ(computeRReport({trade(1)}, 7).openAtEnd, 7);
    EXPECT_EQ(computeRReport({trade(1)}).openAtEnd, 0);
    EXPECT_EQ(computeRReport({trade(1)}, 7).all.trades, 1);
}

TEST(RStatsTest, TheTargetHalfAndTheRunnerAreReportedSeparately)
{
    const RReport r = computeRReport({trade(0.7, ExitReason::TakeProfit, true), trade(-1, ExitReason::StopLoss, true),
                                      trade(2.0, ExitReason::Signal, false), trade(-1, ExitReason::StopLoss, false),
                                      trade(0.1, ExitReason::Breakeven, false)});

    EXPECT_EQ(r.targetHalf.trades, 2);
    EXPECT_NEAR(r.targetHalf.totalR, -0.3, 1e-12);
    EXPECT_EQ(r.runner.trades, 3);
    EXPECT_NEAR(r.runner.totalR, 1.1, 1e-12);
    EXPECT_EQ(r.all.trades, 5);
    EXPECT_NEAR(r.all.totalR, 0.8, 1e-12);
}

TEST(RStatsTest, TradesWithoutAUsableStopAreLeftOutAndCounted)
{
    ClosedTrade noStop = trade(1);
    noStop.trade.stopLoss.reset();
    noStop.trade.initialStop.reset();
    ClosedTrade stopAtEntry = trade(1);
    stopAtEntry.trade.stopLoss = stopAtEntry.trade.entryPrice;
    stopAtEntry.trade.initialStop = stopAtEntry.trade.entryPrice;

    const RReport r = computeRReport({trade(1), noStop, stopAtEntry});

    EXPECT_EQ(r.all.trades, 1);
    EXPECT_EQ(r.withoutStop, 2);
    EXPECT_DOUBLE_EQ(r.all.totalR, 1.0);
}

TEST(RStatsTest, AFlipRunHasNoRMultiples)
{
    ClosedTrade c;
    c.trade.entryPrice = 100;
    c.trade.quantity = 5;
    c.pnl = 12;

    const RReport r = computeRReport({c, c});

    EXPECT_EQ(r.all.trades, 0);
    EXPECT_EQ(r.withoutStop, 2);
}

TEST(RStatsTest, RIsMeasuredFromTheStopTheTradeStartedWithNotTheMovedOne)
{
    // a runner whose stop was moved up to its entry: risk is still 10 * |100 - 97| = 30, so -3 of pnl is -0.1R
    ClosedTrade c = trade(0, ExitReason::Breakeven);
    c.trade.stopLoss = c.trade.entryPrice;          // moved to breakeven; initialStop stays 97
    c.pnl = -3;

    const RReport r = computeRReport({c});

    EXPECT_EQ(r.withoutStop, 0);
    EXPECT_EQ(r.all.trades, 1);
    EXPECT_NEAR(r.all.totalR, -0.1, 1e-12);
    EXPECT_EQ(r.all.breakeven.count, 1);
}
