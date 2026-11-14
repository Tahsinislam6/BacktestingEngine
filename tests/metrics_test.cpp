#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "metrics.hpp"

// These tests build BacktestResult by hand (a few equity numbers, a few trades),
// so every expected value can be checked with a calculator.
//
// Per-bar return:  eq[i] / eq[i-1] - 1, where the bar before the first one is startingCash.

namespace
{
const double kInf = std::numeric_limits<double>::infinity();

BacktestResult curve(std::vector<double> equity)
{
    BacktestResult r;
    r.equityCurve = std::move(equity);
    r.finalEquity = r.equityCurve.empty() ? 0.0 : r.equityCurve.back();
    return r;
}

ClosedTrade trade(double pnl, std::int64_t entry = 0, std::int64_t exit = 0,
                  Direction d = Direction::Long)
{
    ClosedTrade t;
    t.trade.direction = d;
    t.trade.entryTime = entry;
    t.exitTime = exit;
    t.pnl = pnl;
    return t;
}

BacktestResult withTrades(std::vector<double> pnls)
{
    BacktestResult r;
    for (double p : pnls)
        r.closedTrades.push_back(trade(p));
    return r;
}
}

// ---------- total return ----------

TEST(MetricsTotalReturnTest, Gain)
{
    EXPECT_NEAR(computeMetrics(curve({105, 110}), 100).totalReturn, 0.10, 1e-12);
}

TEST(MetricsTotalReturnTest, Loss)
{
    EXPECT_NEAR(computeMetrics(curve({90, 80}), 100).totalReturn, -0.20, 1e-12);
}

TEST(MetricsTotalReturnTest, Flat)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({100, 100}), 100).totalReturn, 0.0);
}

TEST(MetricsTotalReturnTest, UsesTheLastPointNotThePeak)
{
    EXPECT_NEAR(computeMetrics(curve({150, 120}), 100).totalReturn, 0.20, 1e-12);
}

// ---------- max drawdown ----------

TEST(MetricsDrawdownTest, ZeroWhenEquityOnlyRises)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({101, 102, 103}), 100).maxDrawdown, 0.0);
}

TEST(MetricsDrawdownTest, SingleDip)
{
    // peak 120, trough 90: (120 - 90) / 120
    EXPECT_NEAR(computeMetrics(curve({110, 120, 90}), 100).maxDrawdown, 0.25, 1e-12);
}

TEST(MetricsDrawdownTest, TheLargerOfTwoDipsWins)
{
    // dip 1: (120 - 108) / 120 = 0.10    dip 2: (130 - 104) / 130 = 0.20
    EXPECT_NEAR(computeMetrics(curve({100, 120, 108, 130, 104}), 100).maxDrawdown, 0.20, 1e-12);
}

TEST(MetricsDrawdownTest, NeverRecoveringStillCounts)
{
    EXPECT_NEAR(computeMetrics(curve({100, 50, 60}), 100).maxDrawdown, 0.50, 1e-12);
}

TEST(MetricsDrawdownTest, FallFromStartingCashCountsEvenIfTheCurveNeverRose)
{
    // the first point is already below the 100 we started with
    EXPECT_NEAR(computeMetrics(curve({90, 95}), 100).maxDrawdown, 0.10, 1e-12);
}

TEST(MetricsDrawdownTest, EmptyCurveIsZero)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({}), 100).maxDrawdown, 0.0);
}

// ---------- CAGR ----------

TEST(MetricsCagrTest, OneYearOfBarsEqualsTotalReturn)
{
    std::vector<double> eq(251, 100.0);
    eq.push_back(110.0);                      // 252 bars = one year

    EXPECT_NEAR(computeMetrics(curve(eq), 100, 252).cagr, 0.10, 1e-12);
}

TEST(MetricsCagrTest, TwoYearsOfBarsTakesTheRoot)
{
    // 2 bars with 1 bar per year = 2 years; 1.21 total => 1.1 per year
    EXPECT_NEAR(computeMetrics(curve({110, 121}), 100, 1).cagr, 0.10, 1e-12);
}

TEST(MetricsCagrTest, HalfAYearCompoundsUp)
{
    // 1 bar with 2 bars per year = half a year; +10% in half a year => 1.1^2 - 1
    EXPECT_NEAR(computeMetrics(curve({110}), 100, 2).cagr, 0.21, 1e-12);
}

TEST(MetricsCagrTest, EmptyCurveIsZero)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({}), 100).cagr, 0.0);
}

// ---------- Sharpe ----------

TEST(MetricsSharpeTest, HandComputedExample)
{
    // returns +0.1, -0.1, +0.1: mean 0.1/3, sample stdev 0.2/sqrt(3) => sqrt(3)/6
    const double expected = std::sqrt(3.0) / 6.0;

    EXPECT_NEAR(computeMetrics(curve({110, 99, 108.9}), 100, 1).sharpe, expected, 1e-9);
}

TEST(MetricsSharpeTest, AnnualisingMultipliesBySqrtOfPeriods)
{
    const double one  = computeMetrics(curve({110, 99, 108.9}), 100, 1).sharpe;
    const double four = computeMetrics(curve({110, 99, 108.9}), 100, 4).sharpe;

    EXPECT_NEAR(four, 2.0 * one, 1e-9);
}

TEST(MetricsSharpeTest, DefaultPeriodsPerYearIs252)
{
    const BacktestResult r = curve({110, 99, 108.9});

    EXPECT_DOUBLE_EQ(computeMetrics(r, 100).sharpe, computeMetrics(r, 100, 252).sharpe);
}

TEST(MetricsSharpeTest, ConstantReturnsGiveZeroNotInfinity)
{
    // every bar doubles: returns are exactly 1, 1, 1 so the stdev is 0
    EXPECT_DOUBLE_EQ(computeMetrics(curve({200, 400, 800}), 100, 1).sharpe, 0.0);
}

TEST(MetricsSharpeTest, FlatCurveIsZero)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({100, 100, 100}), 100).sharpe, 0.0);
}

TEST(MetricsSharpeTest, OneBarIsZero)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({110}), 100).sharpe, 0.0);
}

TEST(MetricsSharpeTest, EmptyCurveIsZero)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({}), 100).sharpe, 0.0);
}

TEST(MetricsSharpeTest, NegativeWhenTheMeanReturnIsNegative)
{
    EXPECT_LT(computeMetrics(curve({90, 99, 80}), 100, 1).sharpe, 0.0);
}

// ---------- Sortino ----------

TEST(MetricsSortinoTest, HandComputedExample)
{
    // returns +0.1, -0.1, +0.1: mean 0.1/3; downside = sqrt(0.01/3) => 1/sqrt(3)
    const double expected = 1.0 / std::sqrt(3.0);

    EXPECT_NEAR(computeMetrics(curve({110, 99, 108.9}), 100, 1).sortino, expected, 1e-9);
}

TEST(MetricsSortinoTest, AnnualisingMultipliesBySqrtOfPeriods)
{
    const double one = computeMetrics(curve({110, 99, 108.9}), 100, 1).sortino;
    const double nine = computeMetrics(curve({110, 99, 108.9}), 100, 9).sortino;

    EXPECT_NEAR(nine, 3.0 * one, 1e-9);
}

TEST(MetricsSortinoTest, NoDownsideMovesGivesZeroNotInfinity)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({110, 121}), 100, 1).sortino, 0.0);
}

TEST(MetricsSortinoTest, EmptyCurveIsZero)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({}), 100).sortino, 0.0);
}

// ---------- Calmar ----------

TEST(MetricsCalmarTest, CagrDividedByMaxDrawdown)
{
    // {90, 121}, 1 bar per year: 2 years, 1.21x => cagr 0.10; drawdown 10/100 = 0.10
    EXPECT_NEAR(computeMetrics(curve({90, 121}), 100, 1).calmar, 1.0, 1e-9);
}

TEST(MetricsCalmarTest, ZeroWhenThereIsNoDrawdown)
{
    EXPECT_DOUBLE_EQ(computeMetrics(curve({110, 121}), 100, 1).calmar, 0.0);
}

// ---------- trade metrics ----------

TEST(MetricsTradesTest, NoTradesMeansEveryTradeMetricIsZero)
{
    const Metrics m = computeMetrics(curve({100, 100}), 100);

    EXPECT_EQ(m.trades, 0);
    EXPECT_DOUBLE_EQ(m.winRate, 0.0);
    EXPECT_DOUBLE_EQ(m.profitFactor, 0.0);
    EXPECT_DOUBLE_EQ(m.expectancy, 0.0);
    EXPECT_DOUBLE_EQ(m.avgHoldingSeconds, 0.0);
}

TEST(MetricsTradesTest, CountsClosedTrades)
{
    EXPECT_EQ(computeMetrics(withTrades({1, -1, 2}), 100).trades, 3);
}

TEST(MetricsTradesTest, WinRateWithAMix)
{
    EXPECT_DOUBLE_EQ(computeMetrics(withTrades({100, -50, 50, -25}), 100).winRate, 0.5);
}

TEST(MetricsTradesTest, WinRateAllWinnersAndAllLosers)
{
    EXPECT_DOUBLE_EQ(computeMetrics(withTrades({10, 20}), 100).winRate, 1.0);
    EXPECT_DOUBLE_EQ(computeMetrics(withTrades({-10, -20}), 100).winRate, 0.0);
}

TEST(MetricsTradesTest, ABreakEvenTradeCountsAsATradeButNotAWin)
{
    const Metrics m = computeMetrics(withTrades({10, 0}), 100);

    EXPECT_EQ(m.trades, 2);
    EXPECT_DOUBLE_EQ(m.winRate, 0.5);
}

TEST(MetricsTradesTest, ProfitFactorIsGrossProfitOverGrossLoss)
{
    // profit 150, loss 75
    EXPECT_DOUBLE_EQ(computeMetrics(withTrades({100, -50, 50, -25}), 100).profitFactor, 2.0);
}

TEST(MetricsTradesTest, ProfitFactorIsInfinityWithProfitAndNoLosses)
{
    EXPECT_EQ(computeMetrics(withTrades({10, 20}), 100).profitFactor, kInf);
}

TEST(MetricsTradesTest, ProfitFactorIsZeroWithOnlyLosses)
{
    EXPECT_DOUBLE_EQ(computeMetrics(withTrades({-10, -20}), 100).profitFactor, 0.0);
}

TEST(MetricsTradesTest, ProfitFactorIsZeroWhenEveryTradeIsBreakEven)
{
    EXPECT_DOUBLE_EQ(computeMetrics(withTrades({0, 0}), 100).profitFactor, 0.0);
}

TEST(MetricsTradesTest, ExpectancyIsTheAveragePnl)
{
    // (100 - 50 + 50 - 25) / 4
    EXPECT_DOUBLE_EQ(computeMetrics(withTrades({100, -50, 50, -25}), 100).expectancy, 18.75);
}

TEST(MetricsTradesTest, AverageHoldingTime)
{
    BacktestResult r;
    r.closedTrades.push_back(trade(1, 0, 100));     // held 100
    r.closedTrades.push_back(trade(1, 100, 400));   // held 300

    EXPECT_DOUBLE_EQ(computeMetrics(r, 100).avgHoldingSeconds, 200.0);
}

TEST(MetricsTradesTest, ShortTradesUseTheirStoredPnl)
{
    BacktestResult r;
    r.closedTrades.push_back(trade(40, 0, 10, Direction::Short));
    r.closedTrades.push_back(trade(-10, 0, 10, Direction::Short));

    const Metrics m = computeMetrics(r, 100);

    EXPECT_DOUBLE_EQ(m.winRate, 0.5);
    EXPECT_DOUBLE_EQ(m.profitFactor, 4.0);
    EXPECT_DOUBLE_EQ(m.expectancy, 15.0);
}

TEST(MetricsTradesTest, OpenTradesAreIgnoredByTradeMetrics)
{
    BacktestResult r = curve({100, 100});
    r.openTrades.push_back(Trade{});

    EXPECT_EQ(computeMetrics(r, 100).trades, 0);
}

TEST(MetricsTradesTest, TradeMetricsDoNotDependOnTheEquityCurve)
{
    BacktestResult r = withTrades({10, -5});     // no equity curve at all

    const Metrics m = computeMetrics(r, 100);

    EXPECT_EQ(m.trades, 2);
    EXPECT_DOUBLE_EQ(m.totalReturn, 0.0);
}
