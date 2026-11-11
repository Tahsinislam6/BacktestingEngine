#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "strategy.hpp"

namespace
{
// Opens one long trade if nothing is open, otherwise does nothing.
class BuyOnceStrategy : public Strategy
{
public:
    std::vector<Order> onBar(const Bar&, const Portfolio& portfolio) override
    {
        if (portfolio.openTrades().empty())
            return { openOrder(Direction::Long, 1) };
        return {};
    }
};

// Closes every open trade it can see.
class CloseAllStrategy : public Strategy
{
public:
    std::vector<Order> onBar(const Bar&, const Portfolio& portfolio) override
    {
        std::vector<Order> orders;
        for (const Trade& t : portfolio.openTrades())
            orders.push_back(closeOrder(t.id));
        return orders;
    }
};

// Counts how many bars it has seen.
class CountingStrategy : public Strategy
{
public:
    std::vector<Order> onBar(const Bar&, const Portfolio&) override
    {
        ++calls;
        return {};
    }

    int calls{0};
};
}

TEST(StrategyTest, BuyOnceOpensWhenNothingIsOpen)
{
    Portfolio p(10000.0);
    BuyOnceStrategy s;
    Bar bar;

    auto orders = s.onBar(bar, p);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].type, OrderType::Open);
    EXPECT_EQ(orders[0].direction, Direction::Long);
    EXPECT_DOUBLE_EQ(orders[0].quantity, 1.0);
}

TEST(StrategyTest, BuyOnceDoesNothingWhenATradeIsOpen)
{
    Portfolio p(10000.0);
    BuyOnceStrategy s;
    Bar bar;
    p.open(Direction::Long, 1, 100, 1);

    EXPECT_TRUE(s.onBar(bar, p).empty());
}

TEST(StrategyTest, CloseAllReturnsOneOrderPerOpenTrade)
{
    Portfolio p(10000.0);
    int a = p.open(Direction::Long, 1, 100, 1);
    int b = p.open(Direction::Short, 2, 100, 2);
    CloseAllStrategy s;
    Bar bar;

    auto orders = s.onBar(bar, p);

    ASSERT_EQ(orders.size(), 2u);
    EXPECT_EQ(orders[0].type, OrderType::Close);
    EXPECT_EQ(orders[0].tradeId, a);
    EXPECT_EQ(orders[1].type, OrderType::Close);
    EXPECT_EQ(orders[1].tradeId, b);
}

TEST(StrategyTest, CloseAllReturnsNothingWhenNothingIsOpen)
{
    Portfolio p(10000.0);
    CloseAllStrategy s;
    Bar bar;

    EXPECT_TRUE(s.onBar(bar, p).empty());
}

TEST(StrategyTest, StrategyDoesNotChangeThePortfolio)
{
    Portfolio p(10000.0);
    p.open(Direction::Long, 1, 100, 1);
    CloseAllStrategy s;
    Bar bar;

    s.onBar(bar, p);

    // The strategy only returns orders, so the trade is still open.
    EXPECT_EQ(p.openTrades().size(), 1u);
}

// This is the reason Strategy is a base class: the caller only needs a
// Strategy pointer, and the right onBar runs for the real object.
TEST(StrategyTest, WorksThroughABasePointer)
{
    std::unique_ptr<Strategy> s = std::make_unique<BuyOnceStrategy>();
    Portfolio p(10000.0);
    Bar bar;

    auto orders = s->onBar(bar, p);

    ASSERT_EQ(orders.size(), 1u);
    EXPECT_EQ(orders[0].type, OrderType::Open);
}

TEST(StrategyTest, OnBarKeepsStateBetweenCalls)
{
    CountingStrategy s;
    Portfolio p(10000.0);
    Bar bar;

    s.onBar(bar, p);
    s.onBar(bar, p);
    s.onBar(bar, p);

    EXPECT_EQ(s.calls, 3);
}
