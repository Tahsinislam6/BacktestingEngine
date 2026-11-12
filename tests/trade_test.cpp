#include <gtest/gtest.h>

#include "trade.hpp"

TEST(TradeTest, SignIsPlusOneForLong)
{
    EXPECT_DOUBLE_EQ(sign(Direction::Long), 1.0);
}

TEST(TradeTest, SignIsMinusOneForShort)
{
    EXPECT_DOUBLE_EQ(sign(Direction::Short), -1.0);
}

TEST(TradeTest, DefaultTradeIsLongWithNoStops)
{
    Trade t;

    EXPECT_EQ(t.id, 0);
    EXPECT_EQ(t.direction, Direction::Long);
    EXPECT_DOUBLE_EQ(t.quantity, 0.0);
    EXPECT_DOUBLE_EQ(t.entryPrice, 0.0);
    EXPECT_EQ(t.entryTime, 0);
    EXPECT_FALSE(t.stopLoss.has_value());
    EXPECT_FALSE(t.takeProfit.has_value());
}

TEST(TradeTest, StopLossAndTakeProfitCanBeSet)
{
    Trade t;
    t.stopLoss = 95.0;
    t.takeProfit = 110.0;

    ASSERT_TRUE(t.stopLoss.has_value());
    ASSERT_TRUE(t.takeProfit.has_value());
    EXPECT_DOUBLE_EQ(*t.stopLoss, 95.0);
    EXPECT_DOUBLE_EQ(*t.takeProfit, 110.0);
}

TEST(TradeTest, DefaultClosedTradeExitsOnSignal)
{
    ClosedTrade c;

    EXPECT_EQ(c.reason, ExitReason::Signal);
    EXPECT_DOUBLE_EQ(c.pnl, 0.0);
}

TEST(TradeTest, LongProfitsWhenPriceRises)
{
    Trade t;
    t.direction = Direction::Long;
    t.quantity = 2;
    t.entryPrice = 100;

    EXPECT_DOUBLE_EQ(pnlOf(t, 110), 20.0);
}

TEST(TradeTest, LongLosesWhenPriceFalls)
{
    Trade t;
    t.direction = Direction::Long;
    t.quantity = 2;
    t.entryPrice = 100;

    EXPECT_DOUBLE_EQ(pnlOf(t, 90), -20.0);
}

TEST(TradeTest, ShortLosesWhenPriceRises)
{
    Trade t;
    t.direction = Direction::Short;
    t.quantity = 2;
    t.entryPrice = 100;

    EXPECT_DOUBLE_EQ(pnlOf(t, 110), -20.0);
}

TEST(TradeTest, ShortProfitsWhenPriceFalls)
{
    Trade t;
    t.direction = Direction::Short;
    t.quantity = 2;
    t.entryPrice = 100;

    EXPECT_DOUBLE_EQ(pnlOf(t, 90), 20.0);
}

TEST(TradeTest, PnlIsZeroWhenExitEqualsEntry)
{
    Trade t;
    t.direction = Direction::Short;
    t.quantity = 5;
    t.entryPrice = 100;

    EXPECT_DOUBLE_EQ(pnlOf(t, 100), 0.0);
}

// ---------- asset ----------

TEST(TradeTest, AssetDefaultsToZero)
{
    Trade t;

    EXPECT_EQ(t.asset, 0);
}

TEST(TradeTest, PnlDoesNotDependOnTheAsset)
{
    Trade a;
    a.entryPrice = 100;
    a.quantity = 2;
    Trade b = a;
    b.asset = 7;

    EXPECT_DOUBLE_EQ(pnlOf(a, 110), pnlOf(b, 110));
}
