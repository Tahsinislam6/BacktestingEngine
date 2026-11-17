#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

#include "nnfx_orders.hpp"

// nnfxEntryOrders(direction, close, atr, equity, asset, rules) builds the two open orders of one NNFX signal:
//   - both share the same stop loss, stopAtr * ATR away from the close
//   - the first has a take-profit takeProfitAtr * ATR away; the second (the runner) has none
//   - each half is sized so that hitting the stop loses riskPerHalf of equity
//       quantity = equity * riskPerHalf / (stopAtr * ATR)

namespace
{
// round numbers: stop distance 1.5 * 2 = 3, target distance 1 * 2 = 2, risk per half 1% of 100000 = 1000
NnfxEntryRules rules()
{
    NnfxEntryRules r;
    r.riskPerHalf = 0.01;
    r.stopAtr = 1.5;
    r.takeProfitAtr = 1.0;
    return r;
}
}

TEST(NnfxEntryOrdersTest, DefaultRulesAreTheUsualNnfxOnes)
{
    const NnfxEntryRules r;
    EXPECT_DOUBLE_EQ(r.riskPerHalf, 0.01);
    EXPECT_DOUBLE_EQ(r.stopAtr, 1.5);
    EXPECT_DOUBLE_EQ(r.takeProfitAtr, 1.0);
}

TEST(NnfxEntryOrdersTest, GivesTwoOpenOrders)
{
    const std::vector<Order> o = nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, rules());

    ASSERT_EQ(o.size(), 2u);
    EXPECT_EQ(o[0].type, OrderType::Open);
    EXPECT_EQ(o[1].type, OrderType::Open);
}

TEST(NnfxEntryOrdersTest, ALongHasTheStopBelowAndTheTargetAbove)
{
    const std::vector<Order> o = nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, rules());

    EXPECT_EQ(o[0].direction, Direction::Long);
    EXPECT_EQ(o[1].direction, Direction::Long);
    ASSERT_TRUE(o[0].stopLoss);
    ASSERT_TRUE(o[1].stopLoss);
    EXPECT_DOUBLE_EQ(*o[0].stopLoss, 97.0);
    EXPECT_DOUBLE_EQ(*o[1].stopLoss, 97.0);
    ASSERT_TRUE(o[0].takeProfit);
    EXPECT_DOUBLE_EQ(*o[0].takeProfit, 102.0);
}

TEST(NnfxEntryOrdersTest, AShortIsTheMirrorImage)
{
    const std::vector<Order> o = nnfxEntryOrders(Direction::Short, 100, 2, 100000, 0, rules());

    EXPECT_EQ(o[0].direction, Direction::Short);
    EXPECT_EQ(o[1].direction, Direction::Short);
    EXPECT_DOUBLE_EQ(*o[0].stopLoss, 103.0);
    EXPECT_DOUBLE_EQ(*o[1].stopLoss, 103.0);
    EXPECT_DOUBLE_EQ(*o[0].takeProfit, 98.0);
}

TEST(NnfxEntryOrdersTest, TheRunnerHasNoTakeProfit)
{
    for (Direction d : {Direction::Long, Direction::Short})
    {
        const std::vector<Order> o = nnfxEntryOrders(d, 100, 2, 100000, 0, rules());
        EXPECT_TRUE(o[0].takeProfit.has_value());
        EXPECT_FALSE(o[1].takeProfit.has_value());
    }
}

TEST(NnfxEntryOrdersTest, EachHalfRisksTheChosenShareOfEquityAtTheStop)
{
    const std::vector<Order> o = nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, rules());

    // 1000 risked / 3 stop distance
    EXPECT_NEAR(o[0].quantity, 1000.0 / 3.0, 1e-9);
    EXPECT_NEAR(o[1].quantity, 1000.0 / 3.0, 1e-9);
    EXPECT_NEAR(o[0].quantity * (100 - *o[0].stopLoss), 1000.0, 1e-9);
}

TEST(NnfxEntryOrdersTest, TheTwoHalvesTogetherRiskTwiceThat)
{
    const std::vector<Order> o = nnfxEntryOrders(Direction::Short, 100, 2, 50000, 0, rules());

    const double loss = (o[0].quantity + o[1].quantity) * (*o[0].stopLoss - 100);
    EXPECT_NEAR(loss, 2 * 0.01 * 50000, 1e-9);
}

TEST(NnfxEntryOrdersTest, SizeScalesWithEquityAndShrinksWithVolatility)
{
    const double base = nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, rules())[0].quantity;

    EXPECT_NEAR(nnfxEntryOrders(Direction::Long, 100, 2, 200000, 0, rules())[0].quantity, 2 * base, 1e-9);
    EXPECT_NEAR(nnfxEntryOrders(Direction::Long, 100, 4, 100000, 0, rules())[0].quantity, base / 2, 1e-9);
}

TEST(NnfxEntryOrdersTest, ThePriceLevelDoesNotChangeTheSize)
{
    // size depends on the stop DISTANCE only
    EXPECT_NEAR(nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, rules())[0].quantity,
                nnfxEntryOrders(Direction::Long, 5000, 2, 100000, 0, rules())[0].quantity, 1e-9);
}

TEST(NnfxEntryOrdersTest, BothOrdersCarryTheAsset)
{
    const std::vector<Order> o = nnfxEntryOrders(Direction::Long, 100, 2, 100000, 7, rules());
    EXPECT_EQ(o[0].asset, 7);
    EXPECT_EQ(o[1].asset, 7);
}

TEST(NnfxEntryOrdersTest, UsesTheGivenMultiples)
{
    NnfxEntryRules r;
    r.riskPerHalf = 0.005;
    r.stopAtr = 2.0;
    r.takeProfitAtr = 0.5;

    const std::vector<Order> o = nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, r);

    EXPECT_DOUBLE_EQ(*o[0].stopLoss, 96.0);
    EXPECT_DOUBLE_EQ(*o[0].takeProfit, 101.0);
    EXPECT_NEAR(o[0].quantity, 100000 * 0.005 / 4.0, 1e-9);
}

TEST(NnfxEntryOrdersTest, WorksWithFxScalePrices)
{
    NnfxEntryRules r = rules();
    const std::vector<Order> o = nnfxEntryOrders(Direction::Long, 1.1000, 0.0080, 100000, 0, r);

    EXPECT_NEAR(*o[0].stopLoss, 1.1 - 0.012, 1e-12);
    EXPECT_NEAR(*o[0].takeProfit, 1.1 + 0.008, 1e-12);
    EXPECT_NEAR(o[0].quantity, 1000.0 / 0.012, 1e-6);
}

TEST(NnfxEntryOrdersTest, RejectsInputsThatCannotBeSized)
{
    const NnfxEntryRules r = rules();
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 100, 0, 100000, 0, r), std::invalid_argument);     // no ATR
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 100, -1, 100000, 0, r), std::invalid_argument);
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 100, 2, 0, 0, r), std::invalid_argument);          // no equity
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 100, 2, -5, 0, r), std::invalid_argument);
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 0, 2, 100000, 0, r), std::invalid_argument);       // no price
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 100, std::nan(""), 100000, 0, r), std::invalid_argument);
}

TEST(NnfxEntryOrdersTest, RejectsBadRules)
{
    NnfxEntryRules r = rules();
    r.riskPerHalf = 0;
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, r), std::invalid_argument);
    r = rules();
    r.riskPerHalf = 0.6;     // two halves would risk more than the whole account
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, r), std::invalid_argument);
    r = rules();
    r.stopAtr = 0;
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, r), std::invalid_argument);
    r = rules();
    r.takeProfitAtr = -1;
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 100, 2, 100000, 0, r), std::invalid_argument);
}

TEST(NnfxEntryOrdersTest, ALongStopThatWouldBeBelowZeroIsRejected)
{
    // stop distance 3 on a price of 2
    EXPECT_THROW(nnfxEntryOrders(Direction::Long, 2, 2, 100000, 0, rules()), std::invalid_argument);
    EXPECT_NO_THROW(nnfxEntryOrders(Direction::Short, 2, 2, 100000, 0, rules()));
}
