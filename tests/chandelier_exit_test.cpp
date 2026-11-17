#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "components.hpp"
#include "components/chandelier_exit.hpp"

// Chandelier Exit as the MT4 file "ChandelierExit.mq4" (MQLService) computes it (see include/components/chandelier_exit.hpp).
// The numbers below are worked by hand from that source, with small settings so they can be checked on paper:
// range 2, ATR period 2, multiplier 1.
//   atr        = simple average of the last `atrPeriod` true ranges (MT4's iATR); true range = max(H, prev C) - min(L, prev C),
//                and High - Low for the very first bar
//   long stop  = highest high of the last `range` bars - multiplier * atr
//   short stop = lowest low of the last `range` bars + multiplier * atr
//   direction  = the last one, then +1 if Close > the PREVIOUS bar's short stop, then -1 if Close < the previous bar's long stop
//   while the direction is +1 the long stop never moves down; while it is -1 the short stop never moves up
// The state is the direction: +1, -1, or 0 until the first flip.

namespace
{
Bar bar(double high, double low, double close)
{
    Bar b;
    b.open = close;
    b.high = high;
    b.low = low;
    b.close = close;
    return b;
}

using components::chandelier_exit::ChandelierExit;

ChandelierExit small() { return ChandelierExit(2, 2, 1.0); }

// the first two bars used by most tests: after them atr = 2, long stop 9, short stop 10, no direction yet
void firstTwo(ChandelierExit& c)
{
    c.update(bar(10, 8, 9));            // true range 2
    c.update(bar(11, 9, 10.5));         // true range max(11, 9) - min(9, 9) = 2
}
}

TEST(ChandelierExitTest, IsAStateComponent)
{
    static_assert(StateIndicator<ChandelierExit>);
    SUCCEED();
}

TEST(ChandelierExitTest, NotReadyUntilTheLongerWindowIsFull)
{
    ChandelierExit c(2, 4, 1.0);            // the ATR needs 4 bars, the range only 2
    for (int i = 0; i < 3; ++i)
    {
        c.update(bar(10, 8, 9));
        EXPECT_FALSE(c.ready());
        EXPECT_THROW(c.state(), std::logic_error);
        EXPECT_THROW(c.longStop(), std::logic_error);
        EXPECT_THROW(c.shortStop(), std::logic_error);
    }
    c.update(bar(10, 8, 9));
    EXPECT_TRUE(c.ready());

    ChandelierExit d(5, 2, 1.0);            // and the other way round
    for (int i = 0; i < 4; ++i)
        d.update(bar(10, 8, 9));
    EXPECT_FALSE(d.ready());
    d.update(bar(10, 8, 9));
    EXPECT_TRUE(d.ready());
}

TEST(ChandelierExitTest, FirstValuesHaveNoDirectionYet)
{
    ChandelierExit c = small();
    firstTwo(c);
    EXPECT_NEAR(c.longStop(), 9.0, 1e-9);       // high 11 - atr 2
    EXPECT_NEAR(c.shortStop(), 10.0, 1e-9);     // low 8 + atr 2
    EXPECT_EQ(c.state(), 0);
}

TEST(ChandelierExitTest, MatchesTheHandWorkedMt4Values)
{
    ChandelierExit c = small();
    firstTwo(c);

    // bar 3: tr 2.5, atr 2.25, highs 11 13, lows 9 10.5 -> raw long 10.75, raw short 11.25;
    // the close 12.5 is above the previous short stop 10 -> +1
    c.update(bar(13, 10.5, 12.5));
    EXPECT_NEAR(c.longStop(), 10.75, 1e-9);
    EXPECT_NEAR(c.shortStop(), 11.25, 1e-9);
    EXPECT_EQ(c.state(), 1);

    // bar 4: tr 2, atr 2.25, highs 13 13.5, lows 10.5 11.5 -> long 11.25 (above the old 10.75), short 12.75; still +1
    c.update(bar(13.5, 11.5, 12));
    EXPECT_NEAR(c.longStop(), 11.25, 1e-9);
    EXPECT_NEAR(c.shortStop(), 12.75, 1e-9);
    EXPECT_EQ(c.state(), 1);

    // bar 5: tr 3, atr 2.5, highs 13.5 12, lows 11.5 9 -> raw long 11, raw short 11.5;
    // the close 9.5 is below the previous long stop 11.25 -> -1; the short stop 11.5 is below the old 12.75, so it follows
    c.update(bar(12, 9, 9.5));
    EXPECT_NEAR(c.longStop(), 11.0, 1e-9);
    EXPECT_NEAR(c.shortStop(), 11.5, 1e-9);
    EXPECT_EQ(c.state(), -1);

    // bar 6: tr 4.5, atr 3.75, highs 12 14, lows 9 9.5 -> raw long 10.25, raw short 12.75;
    // the close 10 is below the previous long stop 11 -> still -1; the short stop must NOT rise from 11.5 to 12.75
    c.update(bar(14, 9.5, 10));
    EXPECT_NEAR(c.longStop(), 10.25, 1e-9);     // not ratcheted while the direction is -1
    EXPECT_NEAR(c.shortStop(), 11.5, 1e-9);
    EXPECT_EQ(c.state(), -1);

    // bar 7: tr 2, atr 3.25, highs 14 12, lows 9.5 10 -> raw long 10.75, raw short 12.75;
    // the close 11.8 is above the HELD short stop 11.5 -> +1 (it would not be above the raw 12.75)
    c.update(bar(12, 10, 11.8));
    EXPECT_NEAR(c.longStop(), 10.75, 1e-9);
    EXPECT_NEAR(c.shortStop(), 12.75, 1e-9);    // not ratcheted while the direction is +1
    EXPECT_EQ(c.state(), 1);
}

TEST(ChandelierExitTest, TheLongStopNeverMovesDownWhileLong)
{
    ChandelierExit c = small();
    firstTwo(c);
    c.update(bar(13, 10.5, 12.5));                  // +1, long stop 10.75
    // bar 4: tr 3, atr 2.75, highs 11... 13 13, lows 10.5 10 -> raw long 10.25 is lower than 10.75: held
    c.update(bar(13, 10, 12.4));
    EXPECT_NEAR(c.longStop(), 10.75, 1e-9);
    EXPECT_EQ(c.state(), 1);
    // the close 10.6 is below the held 10.75 (but above the raw 10.25): the direction flips
    c.update(bar(12.5, 10.5, 10.6));
    EXPECT_EQ(c.state(), -1);
}

TEST(ChandelierExitTest, AFlipNeedsAStrictBreak)
{
    ChandelierExit up = small();
    firstTwo(up);                                   // short stop 10
    up.update(bar(10.5, 9.5, 10));                  // the close equals the short stop: no flip
    EXPECT_EQ(up.state(), 0);

    ChandelierExit down = small();
    firstTwo(down);                                 // long stop 9
    down.update(bar(10, 8.5, 9));                   // the close equals the long stop: no flip
    EXPECT_EQ(down.state(), 0);
}

TEST(ChandelierExitTest, TheTrueRangeIncludesTheGapFromTheLastClose)
{
    // bar 1 (10, 8, 9) has tr 2; bar 2 gaps up to (14, 13, 13.5): tr = max(14, 9) - min(13, 9) = 5, not 1
    ChandelierExit c = small();
    c.update(bar(10, 8, 9));
    c.update(bar(14, 13, 13.5));
    EXPECT_NEAR(c.longStop(), 14 - 3.5, 1e-9);      // atr = (2 + 5) / 2
    EXPECT_NEAR(c.shortStop(), 8 + 3.5, 1e-9);
}

TEST(ChandelierExitTest, AnOldHighAndLowLeaveTheRangeWindow)
{
    // bar 1 has a spike high 20 and low 2 (tr 18); bars 2 and 3 are (11, 9, 10) with tr 2 each, so atr = 2 at bar 3.
    // The range window of 2 holds bars 2 and 3 only: highest high 11, lowest low 9 -> long stop 9, short stop 11.
    // A window one bar too long would still see the spike: long 18, short 4.
    ChandelierExit c = small();
    c.update(bar(20, 2, 9));
    c.update(bar(11, 9, 10));
    c.update(bar(11, 9, 10));
    EXPECT_NEAR(c.longStop(), 9.0, 1e-9);
    EXPECT_NEAR(c.shortStop(), 11.0, 1e-9);
}

TEST(ChandelierExitTest, TheAtrIsASimpleAverageNotWilders)
{
    // after the hand-worked bars 1-5 the true ranges are 2 2 2.5 2 3; a simple average of the last two is 2.5
    // (Wilder's smoothing would give a different number)
    ChandelierExit c = small();
    firstTwo(c);
    c.update(bar(13, 10.5, 12.5));
    c.update(bar(13.5, 11.5, 12));
    c.update(bar(12, 9, 9.5));
    EXPECT_NEAR(c.longStop(), 13.5 - 2.5, 1e-9);
}

TEST(ChandelierExitTest, TheMultiplierScalesTheStops)
{
    ChandelierExit c(2, 2, 2.0);
    firstTwo(c);                                    // atr 2: long 11 - 4, short 8 + 4
    EXPECT_NEAR(c.longStop(), 7.0, 1e-9);
    EXPECT_NEAR(c.shortStop(), 12.0, 1e-9);
}

TEST(ChandelierExitTest, ARisingMarketIsLongAndAFallingOneShort)
{
    ChandelierExit up(7, 9, 2.5), down(7, 9, 2.5);
    for (int i = 0; i < 60; ++i)
    {
        up.update(bar(101 + i, 99 + i, 100 + i));
        down.update(bar(101 - i, 99 - i, 100 - i));
    }
    EXPECT_EQ(up.state(), 1);
    EXPECT_EQ(down.state(), -1);
}

TEST(ChandelierExitTest, CopiesAreIndependent)
{
    ChandelierExit a = small();
    firstTwo(a);
    a.update(bar(13, 10.5, 12.5));
    ChandelierExit b = a;
    b.update(bar(12, 9, 9));
    EXPECT_EQ(a.state(), 1);
    EXPECT_EQ(b.state(), -1);
}

TEST(ChandelierExitTest, RejectsBadSettings)
{
    EXPECT_THROW(ChandelierExit(0, 9, 2.5), std::invalid_argument);
    EXPECT_THROW(ChandelierExit(7, 0, 2.5), std::invalid_argument);
    EXPECT_THROW(ChandelierExit(7, 9, 0.0), std::invalid_argument);
    EXPECT_THROW(ChandelierExit(7, 9, -1.0), std::invalid_argument);
}

TEST(ChandelierExitTest, TheSpecUsesMt4sDefaultsAndTheRightWarmUp)
{
    using Spec = components::chandelier_exit::Spec;
    EXPECT_EQ(Spec::defaults().getInt("range"), 7);
    EXPECT_EQ(Spec::defaults().getInt("atr_period"), 9);
    EXPECT_DOUBLE_EQ(Spec::defaults().get("atr_multiplier"), 2.5);
    EXPECT_EQ(Spec::warmup(Spec::defaults()), 9u);
    ChandelierExit c = Spec::make(Spec::defaults());
    for (int i = 0; i < 8; ++i)
    {
        EXPECT_FALSE(c.ready());
        c.update(bar(101 + i, 99 + i, 100 + i));
    }
    c.update(bar(110, 108, 109));
    EXPECT_TRUE(c.ready());
}
