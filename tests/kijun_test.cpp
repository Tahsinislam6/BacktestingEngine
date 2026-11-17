#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <stdexcept>
#include <vector>

#include "baseline.hpp"
#include "baselines/kijun.hpp"

// Kijun-sen = the middle of the highest high and the lowest low of the last `period` bars.

namespace
{
Bar hl(double high, double low)
{
    Bar b;
    b.high = high;
    b.low = low;
    b.open = b.close = (high + low) / 2;
    return b;
}
}

using baselines::kijun::Kijun;

TEST(KijunTest, IsABaselineLine)
{
    static_assert(BaselineLine<Kijun>);
    SUCCEED();
}

TEST(KijunTest, NotReadyUntilPeriodBarsHaveBeenSeen)
{
    Kijun k(3);
    EXPECT_FALSE(k.ready());
    k.update(hl(10, 8));
    k.update(hl(12, 9));
    EXPECT_FALSE(k.ready());
    k.update(hl(11, 7));
    EXPECT_TRUE(k.ready());
}

TEST(KijunTest, ValueBeforeReadyThrows)
{
    Kijun k(3);
    k.update(hl(10, 8));
    EXPECT_THROW(k.value(), std::logic_error);
}

TEST(KijunTest, MiddleOfTheHighestHighAndLowestLow)
{
    Kijun k(3);
    k.update(hl(10, 8));
    k.update(hl(12, 9));
    k.update(hl(11, 7));
    EXPECT_DOUBLE_EQ(k.value(), (12 + 7) / 2.0);    // 9.5

    k.update(hl(13, 10));                           // window: bars 2..4
    EXPECT_DOUBLE_EQ(k.value(), (13 + 7) / 2.0);    // 10

    k.update(hl(9, 6));                             // window: bars 3..5
    EXPECT_DOUBLE_EQ(k.value(), (13 + 6) / 2.0);    // 9.5
}

TEST(KijunTest, AnOldExtremeLeavesTheWindow)
{
    Kijun k(3);
    k.update(hl(20, 1));     // the extremes
    k.update(hl(5, 4));
    k.update(hl(5, 4));
    EXPECT_DOUBLE_EQ(k.value(), (20 + 1) / 2.0);

    k.update(hl(5, 4));      // bar 1 has left the window
    EXPECT_DOUBLE_EQ(k.value(), (5 + 4) / 2.0);
}

TEST(KijunTest, EqualExtremesExpireOneByOne)
{
    Kijun k(2);
    k.update(hl(10, 1));
    k.update(hl(10, 1));
    k.update(hl(5, 4));      // window: bars 2,3 -> high 10 (bar 2) still counts
    EXPECT_DOUBLE_EQ(k.value(), (10 + 1) / 2.0);
    k.update(hl(5, 4));      // window: bars 3,4
    EXPECT_DOUBLE_EQ(k.value(), (5 + 4) / 2.0);
}

TEST(KijunTest, PeriodOneIsTheMiddleOfTheCurrentBar)
{
    Kijun k(1);
    k.update(hl(10, 6));
    EXPECT_TRUE(k.ready());
    EXPECT_DOUBLE_EQ(k.value(), 8.0);
    k.update(hl(20, 18));
    EXPECT_DOUBLE_EQ(k.value(), 19.0);
}

TEST(KijunTest, RejectsAPeriodBelowOne)
{
    EXPECT_THROW(Kijun(0), std::invalid_argument);
    EXPECT_THROW(Kijun(-5), std::invalid_argument);
}

TEST(KijunTest, MatchesABruteForceCalculationOnRandomBars)
{
    std::mt19937 gen(7);
    std::uniform_real_distribution<double> price(90, 110), width(0, 3);
    for (int period : {1, 2, 5, 26})
    {
        Kijun k(period);
        std::vector<Bar> bars;
        for (int i = 0; i < 300; ++i)
        {
            const double low = price(gen);
            bars.push_back(hl(low + width(gen), low));
            k.update(bars.back());
            if (i + 1 < period)
            {
                EXPECT_FALSE(k.ready());
                continue;
            }
            double hi = bars[i].high, lo = bars[i].low;
            for (int j = i - period + 1; j <= i; ++j)
            {
                hi = std::max(hi, bars[j].high);
                lo = std::min(lo, bars[j].low);
            }
            ASSERT_TRUE(k.ready());
            ASSERT_DOUBLE_EQ(k.value(), (hi + lo) / 2.0) << "period " << period << " bar " << i;
        }
    }
}

TEST(KijunTest, CopiesAreIndependent)
{
    Kijun a(2);
    a.update(hl(10, 0));
    Kijun b = a;
    b.update(hl(100, 90));
    a.update(hl(4, 2));
    EXPECT_DOUBLE_EQ(a.value(), (10 + 0) / 2.0);
}
