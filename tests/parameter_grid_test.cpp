#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

#include "parameter_grid.hpp"

// These tests define sweep() and toDensityGrid().
//
// A grid is a list of axes (one per parameter). sweep() runs the strategy once for every combination of axis values and
// keeps the Metrics of each run. The results are stored flat in row-major order (the LAST axis changes fastest), the same
// layout density.hpp uses, so for axes of lengths {2, 3} the run for (axis0[i], axis1[j]) is at index i * 3 + j.
//
// The test strategy opens a long of `quantity` on the first bar and closes it `hold` bars later (at that bar's close).

namespace
{
class OpenAndHold : public Strategy
{
public:
    OpenAndHold(double quantity, int hold) : quantity_(quantity), hold_(hold) {}

    std::vector<Order> onBar(const Bar&, const Portfolio& portfolio) override
    {
        if (!opened_)
        {
            opened_ = true;
            return {openOrder(Direction::Long, quantity_)};
        }
        if (!portfolio.openTrades().empty() && ++held_ >= hold_)
            return {closeOrder(portfolio.openTrades().front().id)};
        return {};
    }

private:
    double quantity_;
    int hold_;
    bool opened_{false};
    int held_{0};
};

Bar makeBar(std::int64_t t, double price)
{
    Bar b;
    b.timestamp = t;
    b.open = b.high = b.low = b.close = price;
    return b;
}

// closes 100, 110, 120, 130, 140
BarSeries risingSeries()
{
    BarSeries s;
    s.symbol = "TEST";
    s.timeframe = "D1";
    for (int i = 0; i < 5; ++i)
        s.bars.push_back(makeBar(i * kTimestampUnitsPerDay, 100.0 + 10.0 * i));
    return s;
}

StrategyFactory openAndHoldFactory()
{
    return [](const Params& p) -> std::unique_ptr<Strategy> {
        return std::make_unique<OpenAndHold>(p[0], static_cast<int>(p[1]));
    };
}
}

TEST(ParameterGrid, ShapeIsTheLengthOfEachAxis)
{
    const std::vector<ParameterAxis> axes = {{"quantity", {1, 2}}, {"hold", {1, 2, 3}}};
    EXPECT_EQ(gridShape(axes), (std::vector<std::size_t>{2, 3}));
}

TEST(ParameterGrid, EveryCombinationIsRunInRowMajorOrder)
{
    // closes 100,110,120,130,140. Opening on bar 0 (close 100) and closing `hold` bars later gains 10 * hold per unit,
    // so the expectancy (average pnl per trade, one trade here) is 10 * hold * quantity.
    const std::vector<ParameterAxis> axes = {{"quantity", {1, 2}}, {"hold", {1, 2, 3}}};
    const GridResult g = sweep(risingSeries(), axes, openAndHoldFactory(), 1000.0);

    ASSERT_EQ(g.metrics.size(), 6u);
    const double expected[6] = {10, 20, 30, 20, 40, 60};   // (q=1: h=1,2,3), (q=2: h=1,2,3)
    for (std::size_t i = 0; i < 6; ++i)
    {
        EXPECT_EQ(g.metrics[i].trades, 1) << "cell " << i;
        EXPECT_NEAR(g.metrics[i].expectancy, expected[i], 1e-9) << "cell " << i;
    }
}

TEST(ParameterGrid, TheFactoryGetsTheValuesInAxisOrder)
{
    std::vector<Params> seen;
    const StrategyFactory spy = [&seen](const Params& p) -> std::unique_ptr<Strategy> {
        seen.push_back(p);
        return std::make_unique<OpenAndHold>(1.0, 1);
    };
    sweep(risingSeries(), {{"a", {1, 2}}, {"b", {7, 8, 9}}}, spy, 1000.0);

    const std::vector<Params> expected = {{1, 7}, {1, 8}, {1, 9}, {2, 7}, {2, 8}, {2, 9}};
    EXPECT_EQ(seen, expected);
}

TEST(ParameterGrid, EachCellGetsAFreshStrategy)
{
    // If one strategy object were reused, `opened_` would stay true after the first run and the later cells would never
    // trade. Every cell having one trade shows each run starts clean.
    const GridResult g = sweep(risingSeries(), {{"quantity", {1, 1, 1}}, {"hold", {2}}}, openAndHoldFactory(), 1000.0);
    for (const Metrics& m : g.metrics)
        EXPECT_EQ(m.trades, 1);
}

TEST(ParameterGrid, OneAxisWorks)
{
    const GridResult g = sweep(risingSeries(), {{"hold", {1, 3}}},
                               [](const Params& p) -> std::unique_ptr<Strategy> {
                                   return std::make_unique<OpenAndHold>(1.0, static_cast<int>(p[0]));
                               },
                               1000.0);
    ASSERT_EQ(g.metrics.size(), 2u);
    EXPECT_NEAR(g.metrics[0].expectancy, 10.0, 1e-9);
    EXPECT_NEAR(g.metrics[1].expectancy, 30.0, 1e-9);
}

TEST(ParameterGrid, ResultKeepsTheAxes)
{
    const std::vector<ParameterAxis> axes = {{"quantity", {1, 2}}, {"hold", {1}}};
    const GridResult g = sweep(risingSeries(), axes, openAndHoldFactory(), 1000.0);
    ASSERT_EQ(g.axes.size(), 2u);
    EXPECT_EQ(g.axes[0].name, "quantity");
    EXPECT_EQ(g.axes[1].values, (std::vector<double>{1}));
}

TEST(ParameterGrid, NoAxesOrAnEmptyAxisIsRejected)
{
    EXPECT_THROW(sweep(risingSeries(), {}, openAndHoldFactory(), 1000.0), std::invalid_argument);
    EXPECT_THROW(sweep(risingSeries(), {{"a", {}}}, openAndHoldFactory(), 1000.0), std::invalid_argument);
}

TEST(ParameterGrid, ANullFactoryOrANullStrategyIsRejected)
{
    EXPECT_THROW(sweep(risingSeries(), {{"a", {1}}}, StrategyFactory{}, 1000.0), std::invalid_argument);
    const StrategyFactory returnsNull = [](const Params&) -> std::unique_ptr<Strategy> { return nullptr; };
    EXPECT_THROW(sweep(risingSeries(), {{"a", {1}}}, returnsNull, 1000.0), std::invalid_argument);
}

TEST(ParameterGrid, AnExceptionFromTheFactoryPassesThrough)
{
    const StrategyFactory bad = [](const Params&) -> std::unique_ptr<Strategy> { throw std::runtime_error("bad parameter"); };
    EXPECT_THROW(sweep(risingSeries(), {{"a", {1}}}, bad, 1000.0), std::runtime_error);
}

// ---------- toDensityGrid ----------

TEST(ToDensityGrid, TakesOneNumberPerCellFromTheMetrics)
{
    const std::vector<ParameterAxis> axes = {{"quantity", {1, 2}}, {"hold", {1, 2, 3}}};
    const GridResult g = sweep(risingSeries(), axes, openAndHoldFactory(), 1000.0);
    const DensityGrid d = toDensityGrid(g, [](const Metrics& m) { return m.expectancy; }, 1);

    EXPECT_EQ(d.shape, (std::vector<std::size_t>{2, 3}));
    ASSERT_EQ(d.values.size(), 6u);
    EXPECT_NEAR(d.values[0], 10.0, 1e-9);
    EXPECT_NEAR(d.values[5], 60.0, 1e-9);
    for (bool v : d.valid)
        EXPECT_TRUE(v);
}

TEST(ToDensityGrid, CellsWithTooFewTradesAreInvalid)
{
    const GridResult g = sweep(risingSeries(), {{"quantity", {1, 2}}, {"hold", {1, 2, 3}}}, openAndHoldFactory(), 1000.0);
    // every cell made exactly 1 trade
    const DensityGrid ok = toDensityGrid(g, [](const Metrics& m) { return m.expectancy; }, 1);
    const DensityGrid tooFew = toDensityGrid(g, [](const Metrics& m) { return m.expectancy; }, 2);
    for (bool v : ok.valid)
        EXPECT_TRUE(v);
    for (bool v : tooFew.valid)
        EXPECT_FALSE(v);
}

TEST(ToDensityGrid, ANotANumberMetricIsInvalid)
{
    const GridResult g = sweep(risingSeries(), {{"hold", {1, 2}}},
                               [](const Params& p) -> std::unique_ptr<Strategy> {
                                   return std::make_unique<OpenAndHold>(1.0, static_cast<int>(p[0]));
                               },
                               1000.0);
    const DensityGrid d = toDensityGrid(g, [](const Metrics& m) { return m.expectancy > 15.0 ? m.expectancy : std::nan(""); }, 1);
    EXPECT_FALSE(d.valid[0]);   // expectancy 10 -> NaN
    EXPECT_TRUE(d.valid[1]);    // expectancy 20
}
