#include <gtest/gtest.h>
#include "order.hpp"

TEST(OrderTest, OpenOrderSetsFields)
{
    Order o = openOrder(Direction::Short, 2, 105.0, 90.0);

    EXPECT_EQ(o.type, OrderType::Open);
    EXPECT_EQ(o.direction, Direction::Short);
    EXPECT_DOUBLE_EQ(o.quantity, 2.0);
    ASSERT_TRUE(o.stopLoss.has_value());
    EXPECT_DOUBLE_EQ(*o.stopLoss, 105.0);
    ASSERT_TRUE(o.takeProfit.has_value());
    EXPECT_DOUBLE_EQ(*o.takeProfit, 90.0);
}

TEST(OrderTest, OpenOrderWithoutStopsHasNone)
{
    Order o = openOrder(Direction::Long, 1);

    EXPECT_FALSE(o.stopLoss.has_value());
    EXPECT_FALSE(o.takeProfit.has_value());
}

TEST(OrderTest, CloseOrderSetsTypeAndId)
{
    Order o = closeOrder(7);

    EXPECT_EQ(o.type, OrderType::Close);
    EXPECT_EQ(o.tradeId, 7);
}

// ---------- asset ----------

TEST(OrderTest, AssetDefaultsToZero)
{
    Order plain;
    Order opened = openOrder(Direction::Long, 1);

    EXPECT_EQ(plain.asset, 0);
    EXPECT_EQ(opened.asset, 0);
}

TEST(OrderTest, OpenOrderCarriesTheAsset)
{
    Order o = openOrder(Direction::Long, 1, std::nullopt, std::nullopt, 3);

    EXPECT_EQ(o.asset, 3);
    EXPECT_EQ(o.type, OrderType::Open);
}

TEST(OrderTest, AssetDoesNotDisturbTheOtherFields)
{
    Order o = openOrder(Direction::Short, 2, 105.0, 90.0, 4);

    EXPECT_EQ(o.direction, Direction::Short);
    EXPECT_DOUBLE_EQ(o.quantity, 2.0);
    ASSERT_TRUE(o.stopLoss.has_value());
    EXPECT_DOUBLE_EQ(*o.stopLoss, 105.0);
    ASSERT_TRUE(o.takeProfit.has_value());
    EXPECT_DOUBLE_EQ(*o.takeProfit, 90.0);
    EXPECT_EQ(o.asset, 4);
}

TEST(OrderTest, CloseOrderNamesTheTradeNotTheAsset)
{
    Order o = closeOrder(9);

    EXPECT_EQ(o.type, OrderType::Close);
    EXPECT_EQ(o.tradeId, 9);
    EXPECT_EQ(o.asset, 0);
}
