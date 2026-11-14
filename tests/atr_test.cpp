#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <vector>

#include "indicators/atr.hpp"

// These tests define how Atr (include/indicators/atr.hpp) must behave.
//
//   True range of a bar  = max( high - low, |high - previousClose|, |low - previousClose| )
//                          The first bar has no previous close, so its true range is high - low.
//   First ATR            = plain average of the first `period` true ranges (ready after `period` bars).
//   Later ATR            = (previous ATR * (period - 1) + true range) / period      (Wilder smoothing)
//   value() before ready() throws std::logic_error. A period <= 0 throws std::invalid_argument.
//
// A useful trick below: with period 1, ATR equals the latest bar's true range,
// so the true-range rules can be tested one bar at a time.

namespace
{
Bar makeBar(double high, double low, double close, double open = 0.0)
{
    Bar b;
    b.open = open;
    b.high = high;
    b.low = low;
    b.close = close;
    return b;
}

// A bar with a given range around a close that stays the same, so there are no gaps.
Bar steadyBar(double range, double close = 100.0)
{
    return makeBar(close + range / 2, close - range / 2, close);
}

constexpr double kTol = 1e-9;
}

// ---------- construction ----------

TEST(AtrTest, NonPositivePeriodThrows)
{
    EXPECT_THROW(Atr(0), std::invalid_argument);
    EXPECT_THROW(Atr(-1), std::invalid_argument);
    EXPECT_THROW(Atr(-14), std::invalid_argument);
}

TEST(AtrTest, PeriodOneIsAllowed)
{
    EXPECT_NO_THROW(Atr(1));
}

TEST(AtrTest, DefaultPeriodIsFourteen)
{
    Atr atr;

    for (int i = 0; i < 13; ++i)
        atr.update(steadyBar(2));
    EXPECT_FALSE(atr.ready());

    atr.update(steadyBar(2));
    EXPECT_TRUE(atr.ready());
}

// ---------- warm-up ----------

TEST(AtrTest, IsNotReadyAtTheStart)
{
    Atr atr(3);

    EXPECT_FALSE(atr.ready());
}

TEST(AtrTest, IsNotReadyBeforePeriodBars)
{
    Atr atr(3);

    atr.update(steadyBar(2));
    EXPECT_FALSE(atr.ready());
    atr.update(steadyBar(2));
    EXPECT_FALSE(atr.ready());
}

TEST(AtrTest, IsReadyAfterExactlyPeriodBars)
{
    Atr atr(3);

    atr.update(steadyBar(2));
    atr.update(steadyBar(2));
    atr.update(steadyBar(2));

    EXPECT_TRUE(atr.ready());
}

TEST(AtrTest, StaysReadyAfterMoreBars)
{
    Atr atr(3);

    for (int i = 0; i < 20; ++i)
        atr.update(steadyBar(2));

    EXPECT_TRUE(atr.ready());
}

TEST(AtrTest, ValueBeforeReadyThrows)
{
    Atr atr(3);
    EXPECT_THROW(atr.value(), std::logic_error);

    atr.update(steadyBar(2));
    atr.update(steadyBar(2));
    EXPECT_THROW(atr.value(), std::logic_error);   // still one bar short
}

TEST(AtrTest, ValueDoesNotThrowOnceReady)
{
    Atr atr(3);
    for (int i = 0; i < 3; ++i)
        atr.update(steadyBar(2));

    EXPECT_NO_THROW(atr.value());
}

// ---------- true range (period 1: ATR == the latest true range) ----------

TEST(AtrTest, FirstBarTrueRangeIsHighMinusLow)
{
    Atr atr(1);

    atr.update(makeBar(12.0, 9.0, 11.0));

    EXPECT_NEAR(atr.value(), 3.0, kTol);
}

TEST(AtrTest, TrueRangeIsTheBarRangeWhenThereIsNoGap)
{
    Atr atr(1);
    atr.update(makeBar(10.0, 8.0, 10.0));       // previous close 10

    atr.update(makeBar(12.0, 9.0, 11.0));       // range 3; |12-10| = 2; |9-10| = 1

    EXPECT_NEAR(atr.value(), 3.0, kTol);
}

TEST(AtrTest, GapUpCountsFromThePreviousClose)
{
    Atr atr(1);
    atr.update(makeBar(10.5, 9.5, 10.0));       // previous close 10

    atr.update(makeBar(16.0, 12.0, 15.0));      // range 4, but |16-10| = 6

    EXPECT_NEAR(atr.value(), 6.0, kTol);
}

TEST(AtrTest, GapDownCountsFromThePreviousClose)
{
    Atr atr(1);
    atr.update(makeBar(20.5, 19.5, 20.0));      // previous close 20

    atr.update(makeBar(15.0, 12.0, 13.0));      // range 3, |15-20| = 5, |12-20| = 8

    EXPECT_NEAR(atr.value(), 8.0, kTol);
}

TEST(AtrTest, GapStartsFromThePreviousCloseNotThePreviousHighOrLow)
{
    Atr atr(1);
    atr.update(makeBar(30.0, 10.0, 12.0));      // wide bar, closes at 12

    atr.update(makeBar(14.0, 13.0, 13.5));      // range 1; |14-12| = 2; |13-12| = 1

    EXPECT_NEAR(atr.value(), 2.0, kTol);
}

TEST(AtrTest, OpenPriceIsIgnored)
{
    Atr a(1);
    Atr b(1);
    a.update(makeBar(10.5, 9.5, 10.0));
    b.update(makeBar(10.5, 9.5, 10.0));

    a.update(makeBar(12.0, 9.0, 11.0, /*open*/ 9.5));
    b.update(makeBar(12.0, 9.0, 11.0, /*open*/ 11.9));

    EXPECT_DOUBLE_EQ(a.value(), b.value());
}

// ---------- averaging and smoothing ----------

TEST(AtrTest, FirstValueIsThePlainAverageOfTheFirstTrueRanges)
{
    Atr atr(3);
    atr.update(makeBar(11, 9, 10));      // TR 2 (no previous close)
    atr.update(makeBar(12, 10, 11));     // TR max(2, |12-10|, |10-10|) = 2
    atr.update(makeBar(16, 12, 15));     // TR max(4, |16-11|, |12-11|)  = 5

    EXPECT_NEAR(atr.value(), (2.0 + 2.0 + 5.0) / 3.0, kTol);
}

TEST(AtrTest, LaterValuesUseWilderSmoothing)
{
    Atr atr(3);
    atr.update(makeBar(11, 9, 10));
    atr.update(makeBar(12, 10, 11));
    atr.update(makeBar(16, 12, 15));     // ATR = 3

    atr.update(makeBar(15, 14, 14.5));   // TR max(1, |15-15|, |14-15|) = 1

    EXPECT_NEAR(atr.value(), (3.0 * 2 + 1.0) / 3.0, kTol);
}

TEST(AtrTest, ConstantRangeGivesThatRange)
{
    Atr atr(14);

    for (int i = 0; i < 100; ++i)
        atr.update(steadyBar(2));

    EXPECT_NEAR(atr.value(), 2.0, kTol);
}

TEST(AtrTest, FlatMarketGivesZero)
{
    Atr atr(5);

    for (int i = 0; i < 10; ++i)
        atr.update(makeBar(50, 50, 50));

    EXPECT_NEAR(atr.value(), 0.0, kTol);
}

TEST(AtrTest, ATallBarPullsTheAtrUpAndItDecaysBack)
{
    Atr atr(5);
    for (int i = 0; i < 5; ++i)
        atr.update(steadyBar(1));
    const double calm = atr.value();

    atr.update(steadyBar(10));
    const double spike = atr.value();
    EXPECT_GT(spike, calm);

    for (int i = 0; i < 50; ++i)
        atr.update(steadyBar(1));
    EXPECT_NEAR(atr.value(), 1.0, 1e-3);
}

TEST(AtrTest, MatchesAHandCalculatedSeries)
{
    // Expected values computed independently (period 5, Wilder smoothing).
    const std::vector<Bar> bars = {
        makeBar(10.5, 9.5, 10.0),    // TR 1.0
        makeBar(11.0, 9.8, 10.6),    // TR 1.2
        makeBar(11.2, 10.4, 10.9),   // TR 0.8
        makeBar(10.9, 9.9, 10.1),    // TR 1.0
        makeBar(12.5, 10.2, 12.0),   // TR 2.4   (gap from 10.1)   -> ATR 1.28
        makeBar(12.8, 11.5, 12.2),   // TR 1.3                     -> ATR 1.284
        makeBar(12.4, 11.0, 11.2),   // TR 1.4                     -> ATR 1.3072
        makeBar(11.6, 10.1, 10.3),   // TR 1.5                     -> ATR 1.34576
        makeBar(10.4, 9.0, 9.2),     // TR 1.4                     -> ATR 1.356608
        makeBar(11.5, 9.1, 11.4),    // TR 2.4   (gap from 9.2)    -> ATR 1.5652864
    };
    const std::vector<double> expected = {1.28, 1.284, 1.3072, 1.34576, 1.356608, 1.5652864};

    Atr atr(5);
    for (std::size_t i = 0; i < bars.size(); ++i)
    {
        atr.update(bars[i]);
        if (i >= 4)
            EXPECT_NEAR(atr.value(), expected[i - 4], 1e-9) << "after bar " << (i + 1);
        else
            EXPECT_FALSE(atr.ready()) << "after bar " << (i + 1);
    }
}

// ---------- properties ----------

TEST(AtrTest, ScalesWithPrice)
{
    // Multiplying every price by 100 multiplies the ATR by 100.
    Atr small(5);
    Atr big(5);
    const std::vector<Bar> bars = {
        makeBar(10.5, 9.5, 10.0), makeBar(11.0, 9.8, 10.6), makeBar(11.2, 10.4, 10.9),
        makeBar(10.9, 9.9, 10.1), makeBar(12.5, 10.2, 12.0), makeBar(12.8, 11.5, 12.2),
    };

    for (const Bar& b : bars)
    {
        small.update(b);
        big.update(makeBar(b.high * 100, b.low * 100, b.close * 100));
    }

    EXPECT_NEAR(big.value(), small.value() * 100, 1e-7);
}

TEST(AtrTest, IsNeverNegative)
{
    Atr atr(3);
    const std::vector<Bar> bars = {
        makeBar(10, 9, 9.5), makeBar(9, 8, 8.1), makeBar(20, 7, 19), makeBar(19, 19, 19), makeBar(5, 4, 4.5),
    };

    for (const Bar& b : bars)
    {
        atr.update(b);
        if (atr.ready())
            EXPECT_GE(atr.value(), 0.0);
    }
}

TEST(AtrTest, TwoInstancesAreIndependent)
{
    Atr a(2);
    Atr b(2);

    a.update(steadyBar(2));
    a.update(steadyBar(2));
    b.update(steadyBar(8));

    EXPECT_TRUE(a.ready());
    EXPECT_FALSE(b.ready());
    EXPECT_NEAR(a.value(), 2.0, kTol);
}

TEST(AtrTest, ACopyKeepsItsOwnStateFromThePointOfTheCopy)
{
    Atr original(2);
    original.update(steadyBar(2));

    Atr copy = original;
    copy.update(steadyBar(2));        // the copy now has two bars

    EXPECT_TRUE(copy.ready());
    EXPECT_FALSE(original.ready());   // the original still has one
}

// ---------- with PerAsset ----------

TEST(AtrWithPerAssetTest, EachAssetUsesItsOwnPreviousClose)
{
    // Asset 1 misses the second snapshot. Its next true range must use ITS last close (10),
    // not anything from asset 0.
    PerAsset<Atr> atr(2, Atr(1));

    Snapshot day1;
    day1.bars = {makeBar(100.5, 99.5, 100.0), makeBar(10.5, 9.5, 10.0)};
    Snapshot day2;
    day2.bars = {makeBar(101.0, 99.0, 100.0), std::nullopt};
    Snapshot day3;
    day3.bars = {makeBar(101.0, 99.0, 100.0), makeBar(16.0, 12.0, 15.0)};

    atr.update(day1);
    atr.update(day2);
    atr.update(day3);

    EXPECT_NEAR(atr[0].value(), 2.0, kTol);
    EXPECT_NEAR(atr[1].value(), 6.0, kTol);    // max(4, |16-10|, |12-10|)
}

TEST(AtrWithPerAssetTest, AnAssetWithTooFewBarsIsNotReady)
{
    PerAsset<Atr> atr(2, Atr(3));

    for (int i = 0; i < 3; ++i)
    {
        Snapshot s;
        s.bars = {steadyBar(2), i == 0 ? std::optional<Bar>(steadyBar(2)) : std::nullopt};
        atr.update(s);
    }

    EXPECT_TRUE(atr[0].ready());
    EXPECT_FALSE(atr[1].ready());
}
