#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "components.hpp"
#include "components/bears_bulls_impulse.hpp"

// Bears-Bulls-Impulse as the MT4 file "Bears-Bulls-Impulse.mq4" computes it (see include/components/bears_bulls_impulse.hpp).
// The numbers below are worked by hand from that source, not taken from a terminal:
//   ma    = linear weighted average (newest weight `period`, then period-1, ... 1) of the TYPICAL price (H+L+C)/3
//   bulls = High - ma        bears = Low - ma        sum = bears + bulls = High + Low - 2 ma
//   the file draws +1 when sum >= 0 (zero counts as bullish) and -1 otherwise
// Used as a state: +1 / -1; there is no neutral output.

namespace
{
Bar flat(double p)
{
    Bar b;
    b.open = b.high = b.low = b.close = p;
    return b;
}

Bar bar(double high, double low, double close)
{
    Bar b;
    b.open = close;
    b.high = high;
    b.low = low;
    b.close = close;
    return b;
}

using components::bears_bulls_impulse::BearsBullsImpulse;

BearsBullsImpulse feedFlat(int period, const std::vector<double>& prices)
{
    BearsBullsImpulse i(period);
    for (double p : prices)
        i.update(flat(p));
    return i;
}
}

TEST(BearsBullsImpulseTest, IsAStateComponent)
{
    static_assert(StateIndicator<BearsBullsImpulse>);
    SUCCEED();
}

TEST(BearsBullsImpulseTest, NotReadyUntilPeriodBarsHaveBeenSeen)
{
    BearsBullsImpulse i(3);
    for (double p : {10.0, 11.0})
    {
        i.update(flat(p));
        EXPECT_FALSE(i.ready());
        EXPECT_THROW(i.state(), std::logic_error);
        EXPECT_THROW(i.balance(), std::logic_error);
    }
    i.update(flat(12));
    EXPECT_TRUE(i.ready());
}

TEST(BearsBullsImpulseTest, MatchesTheHandWorkedMt4Values)
{
    // flat bars 10 11 12, period 3: ma = (3*12 + 2*11 + 1*10) / 6 = 68/6; sum = 2 * (12 - 68/6) = 4/3
    BearsBullsImpulse i = feedFlat(3, {10, 11, 12});
    EXPECT_NEAR(i.balance(), 4.0 / 3, 1e-9);
    EXPECT_EQ(i.state(), 1);

    // then 9: window 11 12 9, ma = (3*9 + 2*12 + 11) / 6 = 62/6; sum = 2 * (9 - 62/6) = -8/3
    i.update(flat(9));
    EXPECT_NEAR(i.balance(), -8.0 / 3, 1e-9);
    EXPECT_EQ(i.state(), -1);
}

TEST(BearsBullsImpulseTest, TheNewestBarWeighsMost)
{
    // 12 10 11: ma = (3*11 + 2*10 + 12) / 6 = 65/6 = 10.83, the close 11 is above it -> +1.
    // Reversed weights would give (11 + 20 + 36) / 6 = 11.17 and -1.
    EXPECT_EQ(feedFlat(3, {12, 10, 11}).state(), 1);
}

TEST(BearsBullsImpulseTest, TheAverageIsLinearWeightedNotSimple)
{
    // 10 12 11: LWMA = (33 + 24 + 10) / 6 = 67/6 = 11.17, the close 11 is below it -> -1.
    // A simple average is exactly 11, a tie, which would be +1.
    const BearsBullsImpulse i = feedFlat(3, {10, 12, 11});
    EXPECT_NEAR(i.balance(), 2 * (11 - 67.0 / 6), 1e-9);
    EXPECT_EQ(i.state(), -1);
}

TEST(BearsBullsImpulseTest, UsesTheTypicalPriceAndTheHighPlusLow)
{
    // two flat bars at 10, then high 12 / low 8 / close 11:
    //   typical = 31/3, ma = (3*31/3 + 2*10 + 10) / 6 = 61/6, sum = 12 + 8 - 2*61/6 = -1/3  -> -1
    // Using the close for the average (ma 10.5) would give +; using the midpoint for the bar would give 0 -> +.
    BearsBullsImpulse i(3);
    i.update(flat(10));
    i.update(flat(10));
    i.update(bar(12, 8, 11));
    EXPECT_NEAR(i.balance(), -1.0 / 3, 1e-9);
    EXPECT_EQ(i.state(), -1);
}

TEST(BearsBullsImpulseTest, OnlyTheLastPeriodBarsCount)
{
    // the 100 has left the window of 3: the three flat 10s give ma = 10 and sum = 0 -> bullish (zero counts as +1)
    const BearsBullsImpulse i = feedFlat(3, {100, 10, 10, 10});
    EXPECT_NEAR(i.balance(), 0.0, 1e-9);
    EXPECT_EQ(i.state(), 1);
}

TEST(BearsBullsImpulseTest, AZeroSumCountsAsBullishLikeTheMt4File)
{
    EXPECT_EQ(feedFlat(3, {5, 5, 5}).state(), 1);
    EXPECT_EQ(feedFlat(3, {5, 5, 5, 5, 5}).state(), 1);
}

TEST(BearsBullsImpulseTest, ARisingMarketIsBullishAndAFallingOneBearish)
{
    std::vector<double> up, down;
    for (int i = 0; i < 40; ++i)
    {
        up.push_back(100 + i);
        down.push_back(100 - i);
    }
    EXPECT_EQ(feedFlat(13, up).state(), 1);
    EXPECT_EQ(feedFlat(13, down).state(), -1);
}

TEST(BearsBullsImpulseTest, CopiesAreIndependent)
{
    BearsBullsImpulse a = feedFlat(3, {10, 11, 12});
    BearsBullsImpulse b = a;
    b.update(flat(1));
    EXPECT_EQ(a.state(), 1);
    EXPECT_EQ(b.state(), -1);
}

TEST(BearsBullsImpulseTest, RejectsABadPeriod)
{
    EXPECT_THROW(BearsBullsImpulse(0), std::invalid_argument);
    EXPECT_THROW(BearsBullsImpulse(-2), std::invalid_argument);
}

TEST(BearsBullsImpulseTest, TheSpecUsesMt4sDefaultAndTheRightWarmUp)
{
    using Spec = components::bears_bulls_impulse::Spec;
    EXPECT_EQ(Spec::defaults().getInt("period"), 13);
    EXPECT_EQ(Spec::warmup(Spec::defaults()), 13u);
    BearsBullsImpulse i = Spec::make(Spec::defaults());
    for (int k = 0; k < 12; ++k)
    {
        EXPECT_FALSE(i.ready());
        i.update(flat(100 + k));
    }
    i.update(flat(120));
    EXPECT_TRUE(i.ready());
}
