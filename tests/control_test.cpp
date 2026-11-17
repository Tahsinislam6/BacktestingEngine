#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "control.hpp"

// runDirectionControl: the baseline arm against `seeds` random-direction arms with identical exits.
// Stand-in baseline: FixedLine at 100 (params["level"]).

namespace
{
class FixedLine
{
public:
    explicit FixedLine(double level) : level_(level) {}
    void update(const Bar&) {}
    bool ready() const { return true; }
    double value() const { return level_; }
private:
    double level_;
};

const NnfxFactoryMaker maker = [](const NnfxOptions& o) -> StrategyFactory
{
    return [o](const Params& p, std::size_t assets) -> std::unique_ptr<MultiStrategy>
    {
        return std::make_unique<NnfxStrategy<FixedLine>>(assets, FixedLine(p.get("level")), o);
    };
};

AlignedData dataOf(const std::vector<double>& closes)
{
    AlignedData d;
    d.symbols = {"A0"};
    for (std::size_t i = 0; i < closes.size(); ++i)
    {
        Snapshot s;
        s.timestamp = static_cast<std::int64_t>(i + 1);
        Bar b;
        b.timestamp = s.timestamp;
        b.open = b.high = b.low = b.close = closes[i];
        s.bars.push_back(b);
        d.snapshots.push_back(s);
    }
    return d;
}

// legs of 10 bars at +-2 around the line at 100: after every cross the price keeps going the same way
std::vector<double> trending(int legs)
{
    std::vector<double> c{90};
    for (int leg = 0; leg < legs; ++leg)
        for (int i = 0; i < 10; ++i)
            c.push_back(c.back() + (leg % 2 == 0 ? 2.0 : -2.0));
    return c;
}

// calm at 90, a spike to 101 (a cross up), then straight back to 90 (a cross down): after every cross
// the price reverses at once, so the baseline's side loses
std::vector<double> reversing(int cycles)
{
    std::vector<double> c;
    for (int k = 0; k < cycles; ++k)
    {
        for (int i = 0; i < 5; ++i)
            c.push_back(90);
        c.push_back(101);
        c.push_back(90);
    }
    return c;
}

NnfxOptions exitsOnly(int atrPeriod = 3)
{
    NnfxOptions o;
    o.atrPeriod = atrPeriod;
    o.exitOnCross = false;
    o.maxDistanceAtr = 2.0;           // the first legs start before the ATR has settled
    return o;
}

Params level100()
{
    Params p;
    p.set("level", 100);
    return p;
}

RunOptions grid(int threads = 1)
{
    RunOptions g;
    g.threads = threads;
    return g;
}
}

// ---------- percentileRank ----------

TEST(PercentileRankTest, IsTheShareOfSamplesBelowTheValue)
{
    EXPECT_DOUBLE_EQ(percentileRank(5, {1, 2, 3, 4}), 1.0);
    EXPECT_DOUBLE_EQ(percentileRank(0, {1, 2, 3, 4}), 0.0);
    EXPECT_DOUBLE_EQ(percentileRank(2.5, {1, 2, 3, 4}), 0.5);
}

TEST(PercentileRankTest, TiesCountHalf)
{
    EXPECT_DOUBLE_EQ(percentileRank(2, {1, 2, 3, 4}), (1 + 0.5) / 4);
    EXPECT_DOUBLE_EQ(percentileRank(7, {7, 7, 7}), 0.5);
}

TEST(PercentileRankTest, NanSamplesAreIgnored)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_DOUBLE_EQ(percentileRank(2.5, {1, nan, 2, 3, nan, 4}), 0.5);
}

TEST(PercentileRankTest, IsNanWhenThereIsNothingToCompare)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(std::isnan(percentileRank(1, {})));
    EXPECT_TRUE(std::isnan(percentileRank(1, {nan, nan})));
    EXPECT_TRUE(std::isnan(percentileRank(nan, {1, 2})));
}

// ---------- runDirectionControl ----------

TEST(ControlTest, RunsTheBaselineOnceAndTheRandomArmOncePerSeed)
{
    const ControlResult r = runDirectionControl(dataOf(trending(20)), level100(), maker, exitsOnly(), 12, grid());

    EXPECT_EQ(r.random.size(), 12u);
    EXPECT_GT(r.baseline.trades, 10);
    for (const ArmResult& a : r.random)
        EXPECT_GT(a.trades, 0);
}

TEST(ControlTest, ADirectionThatIsRightBeatsTheCoinFlips)
{
    // after each cross the price carries on: the baseline's side is the winning one
    const ControlResult r = runDirectionControl(dataOf(trending(40)), level100(), maker, exitsOnly(), 40, grid());

    EXPECT_GT(r.baseline.expectancyR, 0.0);
    EXPECT_GT(r.expectancyRank, 0.95);
    EXPECT_GT(r.totalRRank, 0.95);
}

TEST(ControlTest, ADirectionThatIsWrongLosesToTheCoinFlips)
{
    // the price always reverses right after a cross: the baseline's side is the losing one
    const ControlResult r = runDirectionControl(dataOf(reversing(40)), level100(), maker, exitsOnly(), 40, grid());

    EXPECT_LT(r.baseline.expectancyR, 0.0);
    EXPECT_LT(r.expectancyRank, 0.05);
}

TEST(ControlTest, TheRandomRunsDifferFromEachOther)
{
    const ControlResult r = runDirectionControl(dataOf(trending(40)), level100(), maker, exitsOnly(), 20, grid());

    bool anyDifferent = false;
    for (const ArmResult& a : r.random)
        anyDifferent = anyDifferent || a.totalR != r.random[0].totalR;
    EXPECT_TRUE(anyDifferent);
}

TEST(ControlTest, TheResultsDoNotDependOnTheThreadCount)
{
    const AlignedData d = dataOf(trending(30));
    const ControlResult one = runDirectionControl(d, level100(), maker, exitsOnly(), 16, grid(1));
    const ControlResult many = runDirectionControl(d, level100(), maker, exitsOnly(), 16, grid(4));

    EXPECT_DOUBLE_EQ(one.baseline.totalR, many.baseline.totalR);
    ASSERT_EQ(one.random.size(), many.random.size());
    for (std::size_t i = 0; i < one.random.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(one.random[i].totalR, many.random[i].totalR);
        EXPECT_DOUBLE_EQ(one.random[i].sharpe, many.random[i].sharpe);
    }
    EXPECT_DOUBLE_EQ(one.expectancyRank, many.expectancyRank);
}

TEST(ControlTest, TheSameCallTwiceGivesTheSameResult)
{
    const AlignedData d = dataOf(trending(30));
    const ControlResult a = runDirectionControl(d, level100(), maker, exitsOnly(), 10, grid());
    const ControlResult b = runDirectionControl(d, level100(), maker, exitsOnly(), 10, grid());

    EXPECT_DOUBLE_EQ(a.random[3].totalR, b.random[3].totalR);
    EXPECT_DOUBLE_EQ(a.sharpeRank, b.sharpeRank);
}

TEST(ControlTest, CostsApplyToBothArms)
{
    const AlignedData d = dataOf(trending(30));
    RunOptions costly = grid();
    costly.costs.defaultPips = 1000.0;                // 0.1 price units

    const ControlResult free = runDirectionControl(d, level100(), maker, exitsOnly(), 5, grid());
    const ControlResult paid = runDirectionControl(d, level100(), maker, exitsOnly(), 5, costly);

    EXPECT_LT(paid.baseline.totalR, free.baseline.totalR);
    EXPECT_LT(paid.random[0].totalR, free.random[0].totalR);
}

TEST(ControlTest, RejectsWhatCannotBeComparedLikeForLike)
{
    const AlignedData d = dataOf(trending(10));

    EXPECT_THROW(runDirectionControl(d, level100(), maker, exitsOnly(), 0, grid()), std::invalid_argument);
    NnfxOptions crossBack = exitsOnly();
    crossBack.exitOnCross = true;
    EXPECT_THROW(runDirectionControl(d, level100(), maker, crossBack, 5, grid()), std::invalid_argument);
    NnfxOptions seeded = exitsOnly();
    seeded.randomSeed = 3;
    EXPECT_THROW(runDirectionControl(d, level100(), maker, seeded, 5, grid()), std::invalid_argument);
}

// ---------- formatControl ----------

TEST(FormatControlTest, ShowsTheBaselineTheRandomRunsAndTheRank)
{
    const ControlResult r = runDirectionControl(dataOf(trending(40)), level100(), maker, exitsOnly(), 20, grid());

    const std::string text = formatControl("out-of-sample", r);

    EXPECT_NE(text.find("out-of-sample"), std::string::npos);
    EXPECT_NE(text.find("20 random"), std::string::npos);
    EXPECT_NE(text.find("sharpe"), std::string::npos);
    EXPECT_NE(text.find("expectancy"), std::string::npos);
    EXPECT_NE(text.find("total R"), std::string::npos);
    EXPECT_NE(text.find("%"), std::string::npos);
}

// ---------- swap in the arms ----------

TEST(ControlSwapTest, EveryArmPaysTheSwapOfTheOptions)
{
    // days one apart and a pair name, so the rate difference applies; a huge difference moves every arm's R
    AlignedData d = dataOf(trending(30));
    d.symbols = {"EURUSD"};
    for (std::size_t i = 0; i < d.snapshots.size(); ++i)
    {
        const std::int64_t t = static_cast<std::int64_t>(i) * kTimestampUnitsPerDay;
        d.snapshots[i].timestamp = t;
        d.snapshots[i].bars[0]->timestamp = t;
    }
    NnfxOptions nnfx;
    nnfx.atrPeriod = 3;
    nnfx.maxDistanceAtr = 2.0;
    nnfx.exitOnCross = false;
    RunOptions plain;
    plain.threads = 1;
    RunOptions swapped = plain;
    swapped.swap.rates["EUR"] = {{0, 40.0}};
    swapped.swap.rates["USD"] = {{0, 0.0}};

    const ControlResult a = runDirectionControl(d, Params{{"level", 100}}, maker, nnfx, 5, plain);
    const ControlResult b = runDirectionControl(d, Params{{"level", 100}}, maker, nnfx, 5, swapped);

    EXPECT_NE(a.baseline.expectancyR, b.baseline.expectancyR);
    ASSERT_EQ(a.random.size(), b.random.size());
    for (std::size_t i = 0; i < a.random.size(); ++i)
        EXPECT_NE(a.random[i].expectancyR, b.random[i].expectancyR) << i;
}
