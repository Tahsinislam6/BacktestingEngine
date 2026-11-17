#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "components.hpp"
#include "components/aroon.hpp"

// Aroon as the MT4 file "Aroon.mq4" computes it (see include/components/aroon.hpp for the quirks copied from it).
// The numbers below are worked by hand from that source, not taken from a terminal:
//   window = the last `period` CLOSES, newest first (age 0 = the newest bar)
//   up   = 100 * (period - age of the highest close) / period      a tie keeps the NEWEST bar
//   down = 100 * (period - age of the lowest close)  / period      a tie keeps the NEWEST bar
//   MT4 writes 0 for the first `period` bars, so the first real value is on bar period + 1.
// The component's state is +1 when up > down, -1 when up < down and 0 when they are equal.

namespace
{
Bar barAt(double c)
{
    Bar b;
    b.open = b.high = b.low = b.close = c;
    return b;
}

using components::aroon::Aroon;

Aroon feed(int period, const std::vector<double>& closes)
{
    Aroon a(period);
    for (double c : closes)
        a.update(barAt(c));
    return a;
}
}

TEST(AroonTest, IsAStateComponent)
{
    static_assert(StateIndicator<Aroon>);
    SUCCEED();
}

TEST(AroonTest, NotReadyUntilPeriodPlusOneBarsHaveBeenSeen)
{
    Aroon a(3);
    for (double c : {10.0, 12.0, 11.0})
    {
        a.update(barAt(c));
        EXPECT_FALSE(a.ready());
        EXPECT_THROW(a.state(), std::logic_error);
        EXPECT_THROW(a.up(), std::logic_error);
        EXPECT_THROW(a.down(), std::logic_error);
    }
    a.update(barAt(9));
    EXPECT_TRUE(a.ready());
}

TEST(AroonTest, MatchesTheHandWorkedMt4Values)
{
    // closes, oldest first: 10 12 11 9 9 13, period 3
    Aroon a(3);
    for (double c : {10.0, 12.0, 11.0})
        a.update(barAt(c));

    // bar 4: window 12 11 9 (newest first: 9, 11, 12): high 12 is age 2, low 9 is age 0
    a.update(barAt(9));
    EXPECT_NEAR(a.up(), 100.0 / 3, 1e-9);
    EXPECT_NEAR(a.down(), 100.0, 1e-9);
    EXPECT_EQ(a.state(), -1);

    // bar 5: window 11 9 9 (newest first: 9, 9, 11): high 11 is age 2; the low 9 appears at ages 0 and 1, the newest wins
    a.update(barAt(9));
    EXPECT_NEAR(a.up(), 100.0 / 3, 1e-9);
    EXPECT_NEAR(a.down(), 100.0, 1e-9);
    EXPECT_EQ(a.state(), -1);

    // bar 6: window 9 9 13 (newest first: 13, 9, 9): high 13 is age 0; the low 9 appears at ages 1 and 2, the newest (age 1) wins
    a.update(barAt(13));
    EXPECT_NEAR(a.up(), 100.0, 1e-9);
    EXPECT_NEAR(a.down(), 200.0 / 3, 1e-9);
    EXPECT_EQ(a.state(), 1);
}

TEST(AroonTest, TiedHighsKeepTheNewest)
{
    // window (newest first) 15, 10, 15: the high 15 is at ages 0 and 2; the newest wins -> up = 100, not 33.3
    const Aroon a = feed(3, {99, 15, 10, 15});
    EXPECT_NEAR(a.up(), 100.0, 1e-9);
    EXPECT_NEAR(a.down(), 200.0 / 3, 1e-9);       // low 10 at age 1
}

TEST(AroonTest, TheOldestBarOfTheWindowIsNotCounted)
{
    // bars 20 10 11 12, period 3: the window is the last three closes 10 11 12; the 20 has aged out
    const Aroon a = feed(3, {20, 10, 11, 12});
    EXPECT_NEAR(a.up(), 100.0, 1e-9);             // 12 is the newest
    EXPECT_NEAR(a.down(), 100.0 / 3, 1e-9);       // 10 is age 2
    EXPECT_EQ(a.state(), 1);
}

TEST(AroonTest, AWindowIsExactlyPeriodBarsLong)
{
    // period 3, closes 5 20 10 11: the window 20 10 11 has its high 20 at age 2 (still inside)
    EXPECT_NEAR(feed(3, {5, 20, 10, 11}).up(), 100.0 / 3, 1e-9);
    // one bar later the 20 is age 3 and has left; the window is 10 11 12 and the high is the newest
    EXPECT_NEAR(feed(3, {5, 20, 10, 11, 12}).up(), 100.0, 1e-9);
}

TEST(AroonTest, AFlatMarketIsNeutral)
{
    const Aroon a = feed(3, {5, 5, 5, 5, 5});
    EXPECT_NEAR(a.up(), 100.0, 1e-9);
    EXPECT_NEAR(a.down(), 100.0, 1e-9);
    EXPECT_EQ(a.state(), 0);
}

TEST(AroonTest, OnlyTheCloseMatters)
{
    // MT4's file reads Close[] for both the high and the low; the bars' highs and lows are ignored.
    // The highs and lows are pushed around unevenly, so using either one would change which bar is the extreme.
    Aroon a(3), b(3);
    int i = 0;
    for (double c : {10.0, 12.0, 11.0, 9.0, 9.0, 13.0, 8.0, 14.0})
    {
        a.update(barAt(c));
        Bar wide = barAt(c);
        wide.high = c + 40.0 * ((i * 7) % 5);
        wide.low = c - 40.0 * ((i * 3) % 4);
        b.update(wide);
        ++i;
        if (a.ready())
        {
            EXPECT_DOUBLE_EQ(a.up(), b.up());
            EXPECT_DOUBLE_EQ(a.down(), b.down());
        }
    }
}

TEST(AroonTest, ARisingMarketIsBullishAndAFallingOneBearish)
{
    std::vector<double> up, down;
    for (int i = 0; i < 30; ++i)
    {
        up.push_back(100 + i);
        down.push_back(100 - i);
    }
    EXPECT_EQ(feed(14, up).state(), 1);
    EXPECT_EQ(feed(14, down).state(), -1);
}

TEST(AroonTest, CopiesAreIndependent)
{
    Aroon a = feed(3, {10, 12, 11, 9});
    Aroon b = a;
    b.update(barAt(100));
    EXPECT_EQ(a.state(), -1);                      // unchanged by the copy's new bar
    EXPECT_EQ(b.state(), 1);
}

TEST(AroonTest, RejectsABadPeriod)
{
    EXPECT_THROW(Aroon(0), std::invalid_argument);
    EXPECT_THROW(Aroon(-3), std::invalid_argument);
}

TEST(AroonTest, TheSpecUsesMt4sDefaultAndTheRightWarmUp)
{
    using Spec = components::aroon::Spec;
    EXPECT_EQ(Spec::defaults().getInt("period"), 14);
    EXPECT_EQ(Spec::warmup(Spec::defaults()), 15u);
    Aroon a = Spec::make(Spec::defaults());
    for (int i = 0; i < 14; ++i)
    {
        EXPECT_FALSE(a.ready());
        a.update(barAt(100 + i));
    }
    a.update(barAt(120));
    EXPECT_TRUE(a.ready());
}
