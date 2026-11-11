#include <gtest/gtest.h>

#include "portfolio.hpp"

TEST(PortfolioTest, EquityWithNoTradesIsStartingCash)
{
    Portfolio p(10000.0);

    EXPECT_DOUBLE_EQ(p.equity(100.0), 10000.0);
    EXPECT_DOUBLE_EQ(p.equity(999.0), 10000.0);
}

TEST(PortfolioTest, OpenReturnsDifferentIds)
{
    Portfolio p(10000.0);

    int a = p.open(Direction::Long, 1, 100, 1);
    int b = p.open(Direction::Short, 1, 100, 2);

    EXPECT_NE(a, b);
}

TEST(PortfolioTest, FirstIdIsOne)
{
    Portfolio p(10000.0);

    EXPECT_EQ(p.open(Direction::Long, 1, 100, 1), 1);
    EXPECT_EQ(p.open(Direction::Long, 1, 100, 2), 2);
}

TEST(PortfolioTest, OpenDoesNotChangeEquityAtEntryPrice)
{
    Portfolio p(10000.0);
    p.open(Direction::Long, 2, 100, 1);

    EXPECT_DOUBLE_EQ(p.equity(100.0), 10000.0);
}

TEST(PortfolioTest, LongEquityRisesWhenPriceRises)
{
    Portfolio p(10000.0);
    p.open(Direction::Long, 2, 100, 1);

    EXPECT_DOUBLE_EQ(p.equity(110.0), 10020.0);
}

TEST(PortfolioTest, LongEquityFallsWhenPriceFalls)
{
    Portfolio p(10000.0);
    p.open(Direction::Long, 2, 100, 1);

    EXPECT_DOUBLE_EQ(p.equity(90.0), 9980.0);
}

TEST(PortfolioTest, ShortEquityRisesWhenPriceFalls)
{
    Portfolio p(10000.0);
    p.open(Direction::Short, 2, 100, 1);

    EXPECT_DOUBLE_EQ(p.equity(90.0), 10020.0);
}

TEST(PortfolioTest, ShortEquityFallsWhenPriceRises)
{
    Portfolio p(10000.0);
    p.open(Direction::Short, 2, 100, 1);

    EXPECT_DOUBLE_EQ(p.equity(110.0), 9980.0);
}

TEST(PortfolioTest, EquitySumsAllOpenTrades)
{
    Portfolio p(10000.0);
    p.open(Direction::Long, 1, 100, 1);    // +10 at 110
    p.open(Direction::Short, 3, 100, 2);   // -30 at 110

    EXPECT_DOUBLE_EQ(p.equity(110.0), 9980.0);
}

TEST(PortfolioTest, CloseReturnsTrueForKnownId)
{
    Portfolio p(10000.0);
    int id = p.open(Direction::Long, 1, 100, 1);

    EXPECT_TRUE(p.close(id, 110, 2, ExitReason::Signal));
}

TEST(PortfolioTest, CloseReturnsFalseForUnknownId)
{
    Portfolio p(10000.0);
    p.open(Direction::Long, 1, 100, 1);

    EXPECT_FALSE(p.close(999, 110, 2, ExitReason::Signal));
}

TEST(PortfolioTest, FailedCloseChangesNothing)
{
    Portfolio p(10000.0);
    p.open(Direction::Long, 2, 100, 1);

    p.close(999, 110, 2, ExitReason::Signal);

    EXPECT_DOUBLE_EQ(p.equity(110.0), 10020.0);
}

TEST(PortfolioTest, CloseLongBanksProfit)
{
    Portfolio p(10000.0);
    int id = p.open(Direction::Long, 2, 100, 1);

    p.close(id, 110, 2, ExitReason::Signal);

    // No open trades left, so equity is the same at any price.
    EXPECT_DOUBLE_EQ(p.equity(50.0), 10020.0);
    EXPECT_DOUBLE_EQ(p.equity(500.0), 10020.0);
}

TEST(PortfolioTest, CloseShortBanksProfit)
{
    Portfolio p(10000.0);
    int id = p.open(Direction::Short, 2, 100, 1);

    p.close(id, 90, 2, ExitReason::Signal);

    EXPECT_DOUBLE_EQ(p.equity(123.0), 10020.0);
}

TEST(PortfolioTest, CloseLosingTradeBanksLoss)
{
    Portfolio p(10000.0);
    int id = p.open(Direction::Long, 2, 100, 1);

    p.close(id, 90, 2, ExitReason::StopLoss);

    EXPECT_DOUBLE_EQ(p.equity(123.0), 9980.0);
}

TEST(PortfolioTest, ClosingTwiceFailsTheSecondTime)
{
    Portfolio p(10000.0);
    int id = p.open(Direction::Long, 1, 100, 1);

    EXPECT_TRUE(p.close(id, 110, 2, ExitReason::Signal));
    EXPECT_FALSE(p.close(id, 120, 3, ExitReason::Signal));
    EXPECT_DOUBLE_EQ(p.equity(0.0), 10010.0);   // second close did nothing
}

TEST(PortfolioTest, ClosingOneTradeLeavesTheOthersOpen)
{
    Portfolio p(10000.0);
    int a = p.open(Direction::Long, 1, 100, 1);
    p.open(Direction::Long, 1, 100, 2);

    p.close(a, 110, 3, ExitReason::Signal);   // banks +10

    // Trade b is still open: at 120 it adds +20 on top of the banked +10.
    EXPECT_DOUBLE_EQ(p.equity(120.0), 10030.0);
}

TEST(PortfolioTest, IdsAreNotReusedAfterClose)
{
    Portfolio p(10000.0);
    int a = p.open(Direction::Long, 1, 100, 1);
    p.close(a, 100, 2, ExitReason::Signal);

    int b = p.open(Direction::Long, 1, 100, 3);

    EXPECT_NE(a, b);
}

TEST(PortfolioTest, OpenAcceptsStopLossAndTakeProfit)
{
    Portfolio p(10000.0);

    int id = p.open(Direction::Long, 1, 100, 1, 95.0, 110.0);

    EXPECT_TRUE(p.close(id, 100, 2, ExitReason::Signal));
}

TEST(PortfolioTest, OpenTradesListsOpenedTrades)
{
    Portfolio p(10000.0);
    int id = p.open(Direction::Short, 2, 100, 5, 105.0, 90.0);

    ASSERT_EQ(p.openTrades().size(), 1u);
    const Trade& t = p.openTrades()[0];
    EXPECT_EQ(t.id, id);
    EXPECT_EQ(t.direction, Direction::Short);
    EXPECT_DOUBLE_EQ(t.quantity, 2.0);
    EXPECT_DOUBLE_EQ(t.entryPrice, 100.0);
    EXPECT_EQ(t.entryTime, 5);
    EXPECT_DOUBLE_EQ(*t.stopLoss, 105.0);
    EXPECT_DOUBLE_EQ(*t.takeProfit, 90.0);
}

TEST(PortfolioTest, CloseMovesTradeToClosedList)
{
    Portfolio p(10000.0);
    int id = p.open(Direction::Long, 2, 100, 1);

    p.close(id, 110, 9, ExitReason::TakeProfit);

    EXPECT_TRUE(p.openTrades().empty());
    ASSERT_EQ(p.closedTrades().size(), 1u);
    const ClosedTrade& c = p.closedTrades()[0];
    EXPECT_EQ(c.trade.id, id);
    EXPECT_DOUBLE_EQ(c.exitPrice, 110.0);
    EXPECT_EQ(c.exitTime, 9);
    EXPECT_EQ(c.reason, ExitReason::TakeProfit);
    EXPECT_DOUBLE_EQ(c.pnl, 20.0);
}
