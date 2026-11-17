#include <gtest/gtest.h>

#include <stdexcept>

#include "baseline.hpp"
#include "baselines/ema.hpp"

// EMA: seeded with the simple average of the first `period` closes, then
//   ema = ema + 2/(period+1) * (close - ema)

namespace
{
Bar closeOnly(double c)
{
    Bar b;
    b.open = b.high = b.low = b.close = c;
    return b;
}
}

using baselines::ema::Ema;

TEST(EmaTest, IsABaselineLine)
{
    static_assert(BaselineLine<Ema>);
    SUCCEED();
}

TEST(EmaTest, NotReadyUntilPeriodBarsHaveBeenSeen)
{
    Ema e(3);
    e.update(closeOnly(10));
    e.update(closeOnly(11));
    EXPECT_FALSE(e.ready());
    e.update(closeOnly(12));
    EXPECT_TRUE(e.ready());
}

TEST(EmaTest, ValueBeforeReadyThrows)
{
    Ema e(3);
    e.update(closeOnly(10));
    EXPECT_THROW(e.value(), std::logic_error);
}

TEST(EmaTest, StartsAtTheSimpleAverage)
{
    Ema e(3);
    for (double c : {10.0, 11.0, 12.0})
        e.update(closeOnly(c));
    EXPECT_DOUBLE_EQ(e.value(), 11.0);
}

TEST(EmaTest, FollowsTheRecursion)
{
    Ema e(3);                                  // alpha = 0.5
    for (double c : {10.0, 11.0, 12.0})
        e.update(closeOnly(c));
    e.update(closeOnly(14));
    EXPECT_DOUBLE_EQ(e.value(), 11.0 + 0.5 * (14 - 11.0));      // 12.5
    e.update(closeOnly(10));
    EXPECT_DOUBLE_EQ(e.value(), 12.5 + 0.5 * (10 - 12.5));      // 11.25
}

TEST(EmaTest, UsesTheAlphaOfThePeriod)
{
    Ema e(9);                                  // alpha = 0.2
    for (int i = 0; i < 9; ++i)
        e.update(closeOnly(100));
    e.update(closeOnly(110));
    EXPECT_DOUBLE_EQ(e.value(), 100 + 0.2 * 10);
}

TEST(EmaTest, PeriodOneIsTheClose)
{
    Ema e(1);
    e.update(closeOnly(7));
    EXPECT_DOUBLE_EQ(e.value(), 7);
    e.update(closeOnly(9));
    EXPECT_DOUBLE_EQ(e.value(), 9);
}

TEST(EmaTest, AConstantPriceGivesThatPrice)
{
    Ema e(20);
    for (int i = 0; i < 100; ++i)
        e.update(closeOnly(1.2345));
    EXPECT_NEAR(e.value(), 1.2345, 1e-12);
}

TEST(EmaTest, RejectsAPeriodBelowOne)
{
    EXPECT_THROW(Ema(0), std::invalid_argument);
    EXPECT_THROW(Ema(-3), std::invalid_argument);
}
