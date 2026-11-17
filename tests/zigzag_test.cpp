#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "zigzag.hpp"

// ZigZag turns a price series into a list of confirmed swing pivots (alternating low / high).
// A pivot is confirmed once price has reversed by at least the threshold; the move after the last
// pivot is not confirmed yet, so it is not returned.

namespace
{
std::vector<double> constant(std::size_t n, double value)
{
    return std::vector<double>(n, value);
}

// Bars whose open = high = low = close, so the true range is just the close-to-close change.
std::vector<Bar> barsOf(const std::vector<double>& closes)
{
    std::vector<Bar> bars;
    for (std::size_t i = 0; i < closes.size(); ++i)
    {
        Bar b;
        b.timestamp = static_cast<std::int64_t>(i);
        b.open = b.high = b.low = b.close = closes[i];
        bars.push_back(b);
    }
    return bars;
}

void expectPivot(const Pivot& p, std::size_t index, double price, bool isHigh)
{
    EXPECT_EQ(p.index, index);
    EXPECT_DOUBLE_EQ(p.price, price);
    EXPECT_EQ(p.isHigh, isHigh);
}

// A deterministic wobbly price series.
std::vector<double> randomWalk(unsigned seed, std::size_t n)
{
    std::mt19937 gen(seed);
    std::normal_distribution<double> step(0.0, 2.0);
    std::vector<double> closes{100.0};
    for (std::size_t i = 1; i < n; ++i)
        closes.push_back(closes.back() + step(gen));
    return closes;
}
}

// ---------- zigzagPivots: basic shapes ----------

TEST(ZigZagPivotsTest, EmptyInputGivesNoPivots)
{
    EXPECT_TRUE(zigzagPivots({}, {}).empty());
}

TEST(ZigZagPivotsTest, OneBarGivesNoPivots)
{
    EXPECT_TRUE(zigzagPivots({100}, {5}).empty());
}

TEST(ZigZagPivotsTest, FlatPricesGiveNoPivots)
{
    EXPECT_TRUE(zigzagPivots(constant(10, 100), constant(10, 5)).empty());
}

TEST(ZigZagPivotsTest, FindsAlternatingPivotsAndLeavesTheLastMoveUnconfirmed)
{
    const std::vector<double> closes{100, 110, 100, 110, 100};

    const std::vector<Pivot> p = zigzagPivots(closes, constant(5, 5));

    ASSERT_EQ(p.size(), 4u);                 // the final 100 is not confirmed
    expectPivot(p[0], 0, 100, false);
    expectPivot(p[1], 1, 110, true);
    expectPivot(p[2], 2, 100, false);
    expectPivot(p[3], 3, 110, true);
}

TEST(ZigZagPivotsTest, AMonotonicTrendHasOnlyItsStartingPivot)
{
    const std::vector<double> closes{100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110};

    const std::vector<Pivot> p = zigzagPivots(closes, constant(closes.size(), 5));

    ASSERT_EQ(p.size(), 1u);
    expectPivot(p[0], 0, 100, false);
}

TEST(ZigZagPivotsTest, CanStartWithAHigh)
{
    const std::vector<Pivot> p = zigzagPivots({100, 90, 100}, constant(3, 5));

    ASSERT_EQ(p.size(), 2u);
    expectPivot(p[0], 0, 100, true);
    expectPivot(p[1], 1, 90, false);
}

TEST(ZigZagPivotsTest, ThePivotIsTheExtremeNotTheBarThatConfirmedIt)
{
    // 120 is the top; it is only confirmed at 105
    const std::vector<Pivot> p = zigzagPivots({100, 110, 120, 105}, constant(4, 5));

    ASSERT_EQ(p.size(), 2u);
    expectPivot(p[0], 0, 100, false);
    expectPivot(p[1], 2, 120, true);
}

// ---------- zigzagPivots: threshold rules ----------

TEST(ZigZagPivotsTest, AReversalOfExactlyTheThresholdCounts)
{
    const std::vector<Pivot> p = zigzagPivots({100, 105, 100}, constant(3, 5));

    ASSERT_EQ(p.size(), 2u);
    expectPivot(p[0], 0, 100, false);
    expectPivot(p[1], 1, 105, true);
}

TEST(ZigZagPivotsTest, AReversalSmallerThanTheThresholdIsIgnored)
{
    // the dip to 106 is only 4 below 110
    const std::vector<Pivot> p = zigzagPivots({100, 110, 106, 112, 100}, constant(5, 5));

    ASSERT_EQ(p.size(), 2u);
    expectPivot(p[0], 0, 100, false);
    expectPivot(p[1], 3, 112, true);
}

TEST(ZigZagPivotsTest, ABiggerThresholdFindsFewerPivots)
{
    const std::vector<double> closes = randomWalk(7, 300);

    const std::size_t small = zigzagPivots(closes, constant(closes.size(), 2)).size();
    const std::size_t large = zigzagPivots(closes, constant(closes.size(), 8)).size();

    EXPECT_GT(small, large);
}

TEST(ZigZagPivotsTest, ThresholdsCanDifferFromBarToBar)
{
    // at bar 1 the threshold is 5, so the first low is confirmed; at bar 2 it is 50, so no high yet
    const std::vector<Pivot> p = zigzagPivots({100, 110, 100}, {0, 5, 50});

    ASSERT_EQ(p.size(), 1u);
    expectPivot(p[0], 0, 100, false);
}

TEST(ZigZagPivotsTest, ABarWithAnUnknownThresholdCannotConfirmAnything)
{
    const std::vector<double> closes{100, 110, 100, 110, 100};

    // thresholds unknown until bar 3
    const std::vector<Pivot> p = zigzagPivots(closes, {0, 0, 0, 5, 5});

    ASSERT_EQ(p.size(), 2u);
    expectPivot(p[0], 0, 100, false);
    expectPivot(p[1], 1, 110, true);       // the real top, even though it was long before confirmation
}

TEST(ZigZagPivotsTest, NaNAndNegativeThresholdsAreUnknown)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();

    EXPECT_TRUE(zigzagPivots({100, 110, 100}, {nan, nan, nan}).empty());
    EXPECT_TRUE(zigzagPivots({100, 110, 100}, {-5, -5, -5}).empty());
}

TEST(ZigZagPivotsTest, WhenBothReversalsAreVisibleAtOnceTheEarlierExtremeComesFirst)
{
    // low at 0, high at 1, then a fall: both confirmed at bar 2
    const std::vector<Pivot> a = zigzagPivots({100, 120, 110}, {0, 0, 5});
    ASSERT_EQ(a.size(), 2u);
    expectPivot(a[0], 0, 100, false);
    expectPivot(a[1], 1, 120, true);

    // high at 0, low at 1, then a rise
    const std::vector<Pivot> b = zigzagPivots({100, 80, 90}, {0, 0, 5});
    ASSERT_EQ(b.size(), 2u);
    expectPivot(b[0], 0, 100, true);
    expectPivot(b[1], 1, 80, false);
}

TEST(ZigZagPivotsTest, MismatchedLengthsThrow)
{
    EXPECT_THROW(zigzagPivots({100, 110}, {5}), std::invalid_argument);
}

// ---------- zigzagPivots: properties that must hold on any data ----------

TEST(ZigZagPivotsPropertyTest, PivotsAlternateAndGoForwardInTime)
{
    for (unsigned seed = 1; seed <= 20; ++seed)
    {
        const std::vector<double> closes = randomWalk(seed, 400);
        const std::vector<Pivot> p = zigzagPivots(closes, constant(closes.size(), 3));

        for (std::size_t k = 1; k < p.size(); ++k)
        {
            EXPECT_GT(p[k].index, p[k - 1].index);
            EXPECT_NE(p[k].isHigh, p[k - 1].isHigh);
        }
    }
}

TEST(ZigZagPivotsPropertyTest, EveryLegIsAtLeastTheThreshold)
{
    for (unsigned seed = 1; seed <= 20; ++seed)
    {
        const std::vector<double> closes = randomWalk(seed, 400);
        const std::vector<Pivot> p = zigzagPivots(closes, constant(closes.size(), 3));

        for (std::size_t k = 1; k < p.size(); ++k)
            EXPECT_GE(std::abs(p[k].price - p[k - 1].price), 3.0);
    }
}

TEST(ZigZagPivotsPropertyTest, PivotPricesAreTheClosesAtTheirIndex)
{
    const std::vector<double> closes = randomWalk(3, 400);

    for (const Pivot& pivot : zigzagPivots(closes, constant(closes.size(), 3)))
        EXPECT_DOUBLE_EQ(pivot.price, closes[pivot.index]);
}

TEST(ZigZagPivotsPropertyTest, EachPivotIsTheExtremeBetweenItsNeighbours)
{
    for (unsigned seed = 1; seed <= 20; ++seed)
    {
        const std::vector<double> closes = randomWalk(seed, 400);
        const std::vector<Pivot> p = zigzagPivots(closes, constant(closes.size(), 3));

        for (std::size_t k = 1; k + 1 < p.size(); ++k)
        {
            for (std::size_t i = p[k - 1].index; i <= p[k + 1].index; ++i)
            {
                if (p[k].isHigh)
                    EXPECT_LE(closes[i], p[k].price);
                else
                    EXPECT_GE(closes[i], p[k].price);
            }
        }
    }
}

// ---------- zigzag: thresholds from ATR ----------

TEST(ZigZagTest, FindsPivotsUsingAtrThresholds)
{
    // steps of 10, so the ATR(2) settles near 10; with a multiple of 1 a 10-point reversal confirms
    const std::vector<Bar> bars = barsOf({100, 110, 120, 130, 120, 110, 100, 110, 120, 130});

    const std::vector<Pivot> p = zigzag(bars, 1.0, 2);

    ASSERT_EQ(p.size(), 3u);
    expectPivot(p[0], 0, 100, false);
    expectPivot(p[1], 3, 130, true);
    expectPivot(p[2], 6, 100, false);
}

TEST(ZigZagTest, ABiggerMultipleFindsFewerPivots)
{
    const std::vector<Bar> bars = barsOf(randomWalk(11, 500));

    EXPECT_GT(zigzag(bars, 1.0, 14).size(), zigzag(bars, 4.0, 14).size());
}

TEST(ZigZagTest, GivesNoPivotsWhileTheAtrIsStillWarmingUp)
{
    // only 3 bars with a 14-bar ATR: nothing can be confirmed
    EXPECT_TRUE(zigzag(barsOf({100, 200, 100}), 1.0, 14).empty());
}

TEST(ZigZagTest, EmptyBarsGiveNoPivots)
{
    EXPECT_TRUE(zigzag({}).empty());
}

TEST(ZigZagTest, RejectsABadMultipleOrPeriod)
{
    const std::vector<Bar> bars = barsOf({100, 110, 120});

    EXPECT_THROW(zigzag(bars, 0.0, 14), std::invalid_argument);
    EXPECT_THROW(zigzag(bars, -1.0, 14), std::invalid_argument);
    EXPECT_THROW(zigzag(bars, 3.0, 0), std::invalid_argument);
}
