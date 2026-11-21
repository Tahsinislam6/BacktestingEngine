#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "baseline_registry.hpp"

// These checks run on EVERY baseline in include/baselines/ automatically: a new file is covered the moment
// it is added. They catch the usual mistakes (ready too early, not finite, hidden state, shared state),
// not whether the maths is right; that needs a value test of its own.
//
// Parameter sets tried per baseline: the first, a middle and the last of its default grid.

namespace
{
std::vector<Params> sampleParams(const BaselineInfo& b)
{
    const std::vector<Params> grid = expandGrid(b.ranges);
    std::vector<Params> out{grid.front()};
    if (grid.size() > 2)
        out.push_back(grid[grid.size() / 2]);
    if (grid.size() > 1)
        out.push_back(grid.back());
    return out;
}

Bar makeBar(double o, double h, double l, double c)
{
    Bar b;
    b.open = o;
    b.high = h;
    b.low = l;
    b.close = c;
    return b;
}

// a random walk around `start`; the bars have a real range
std::vector<Bar> walk(unsigned seed, std::size_t n, double start, double volatility)
{
    std::mt19937 gen(seed);
    std::normal_distribution<double> noise(0.0, volatility);
    std::vector<Bar> bars;
    double prev = start;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double c = prev * std::exp(noise(gen));
        const double hi = std::max(prev, c) * (1 + std::abs(noise(gen)) / 2);
        const double lo = std::min(prev, c) * (1 - std::abs(noise(gen)) / 2);
        bars.push_back(makeBar(prev, hi, lo, c));
        prev = c;
    }
    return bars;
}

std::vector<Bar> flatSeries(std::size_t n, double price)
{
    return std::vector<Bar>(n, makeBar(price, price, price, price));
}

std::vector<Bar> gappySeries(std::size_t n)
{
    std::vector<Bar> bars;
    double p = 100;
    for (std::size_t i = 0; i < n; ++i)
    {
        p *= (i % 7 == 0) ? 1.25 : 0.9;      // big jumps both ways
        bars.push_back(makeBar(p, p * 1.01, p * 0.99, p));
    }
    return bars;
}

std::string describe(const BaselineInfo& b, const Params& p)
{
    return b.name + " [" + std::to_string(p.values().empty() ? 0.0 : p.values().begin()->second) + "]";
}
}

TEST(BaselinesTest, SomeBaselinesAreRegistered)
{
    EXPECT_FALSE(baselineRegistry().empty());
}

// the 55-day breakout is turtle_channel with a 55-bar trade channel AND a 55-bar stop channel (the line is the opposite 55-bar extreme)
TEST(BaselinesTest, TheTurtleChannelAllowsAFiftyFiveDayStopChannel)
{
    const BaselineInfo* b = findBaseline("turtle_channel");
    ASSERT_NE(b, nullptr);
    const Params p = parseParams("trade=55,stop=55", b->ranges);
    EXPECT_DOUBLE_EQ(p.get("stop"), 55);
    EXPECT_EQ(b->warmupBars(p), 56u);
    EXPECT_THROW(parseParams("trade=55,stop=65", b->ranges), std::invalid_argument);      // still bounded
}

TEST(BaselinesTest, NamesAreUniqueSortedAndFindable)
{
    const auto& all = baselineRegistry();
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        EXPECT_FALSE(all[i].name.empty());
        if (i > 0)
            EXPECT_LT(all[i - 1].name, all[i].name);
        ASSERT_NE(findBaseline(all[i].name), nullptr);
        EXPECT_EQ(findBaseline(all[i].name)->name, all[i].name);
        EXPECT_NE(baselineNames().find(all[i].name), std::string::npos);
    }
    EXPECT_EQ(findBaseline("no_such_baseline"), nullptr);
}

TEST(BaselinesTest, DefaultRangesAreUsable)
{
    for (const BaselineInfo& b : baselineRegistry())
    {
        ASSERT_FALSE(b.ranges.empty()) << b.name;
        const std::vector<Params> grid = expandGrid(b.ranges);      // throws on bad ranges
        EXPECT_FALSE(grid.empty()) << b.name;
        EXPECT_LE(grid.size(), 100000u) << b.name << ": the default grid is huge";
        for (const Params& p : sampleParams(b))
        {
            EXPECT_NO_THROW(b.makeLine(p)) << describe(b, p);
            const std::size_t w = b.warmupBars(p);
            EXPECT_GE(w, 1u) << describe(b, p);
            EXPECT_LE(w, 1000u) << describe(b, p) << ": more warm-up than the data has";
        }
    }
}

TEST(BaselinesTest, ReadyExactlyAfterTheDeclaredWarmUp)
{
    const std::vector<Bar> bars = walk(1, 1200, 100, 0.01);
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
        {
            auto line = b.makeLine(p);
            const std::size_t warm = b.warmupBars(p);
            for (std::size_t i = 0; i < warm; ++i)
            {
                EXPECT_FALSE(line->ready()) << describe(b, p) << " ready after only " << i << " bars";
                line->update(bars[i]);
            }
            EXPECT_TRUE(line->ready()) << describe(b, p) << " not ready after its warm-up of " << warm;
        }
}

TEST(BaselinesTest, AskingForTheValueTooEarlyThrows)
{
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
        {
            auto line = b.makeLine(p);
            EXPECT_THROW(line->value(), std::logic_error) << describe(b, p);
        }
}

TEST(BaselinesTest, ValuesAreFiniteOnEveryKindOfData)
{
    const std::vector<std::pair<std::string, std::vector<Bar>>> datasets = {
        {"fx scale", walk(2, 1200, 1.1, 0.006)},
        {"jpy scale", walk(3, 1200, 150, 0.006)},
        {"volatile", walk(4, 1200, 100, 0.05)},
        {"flat", flatSeries(1200, 1.2345)},
        {"gaps", gappySeries(1200)},
    };
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
            for (const auto& [label, bars] : datasets)
            {
                auto line = b.makeLine(p);
                for (std::size_t i = 0; i < bars.size(); ++i)
                {
                    line->update(bars[i]);
                    if (line->ready())
                        ASSERT_TRUE(std::isfinite(line->value())) << describe(b, p) << " on " << label << " bar " << i;
                }
                EXPECT_TRUE(line->ready()) << describe(b, p) << " on " << label;
            }
}

TEST(BaselinesTest, OnAFlatMarketTheLineIsThePrice)
{
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
        {
            auto line = b.makeLine(p);
            for (const Bar& bar : flatSeries(1200, 1.2345))
                line->update(bar);
            EXPECT_NEAR(line->value(), 1.2345, 1e-9) << describe(b, p);
        }
}

TEST(BaselinesTest, ReadStaysTrueOnceReady)
{
    const std::vector<Bar> bars = walk(5, 1200, 100, 0.01);
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
        {
            auto line = b.makeLine(p);
            bool wasReady = false;
            for (const Bar& bar : bars)
            {
                line->update(bar);
                if (wasReady)
                    ASSERT_TRUE(line->ready()) << describe(b, p);
                wasReady = line->ready();
            }
        }
}

TEST(BaselinesTest, AskingForTheValueChangesNothing)
{
    // value() is const: calling it any number of times, or not at all, must not change later values
    const std::vector<Bar> bars = walk(6, 800, 100, 0.01);
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
        {
            auto asked = b.makeLine(p);
            auto silent = b.makeLine(p);
            for (const Bar& bar : bars)
            {
                asked->update(bar);
                silent->update(bar);
                if (asked->ready())
                {
                    const double first = asked->value();
                    ASSERT_EQ(first, asked->value()) << describe(b, p);
                }
            }
            ASSERT_EQ(asked->value(), silent->value()) << describe(b, p);
        }
}

TEST(BaselinesTest, TheSameBarsAlwaysGiveTheSameLine)
{
    // two fresh instances, and a rerun later: no hidden or shared state
    const std::vector<Bar> bars = walk(7, 800, 100, 0.01);
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
        {
            auto a = b.makeLine(p);
            auto c = b.makeLine(p);
            std::vector<double> first;
            for (const Bar& bar : bars)
            {
                a->update(bar);
                c->update(bar);
                if (a->ready())
                {
                    ASSERT_EQ(a->value(), c->value()) << describe(b, p);
                    first.push_back(a->value());
                }
            }
            auto again = b.makeLine(p);
            std::size_t k = 0;
            for (const Bar& bar : bars)
            {
                again->update(bar);
                if (again->ready())
                    ASSERT_EQ(again->value(), first[k++]) << describe(b, p);
            }
        }
}

TEST(BaselinesTest, CopiesAreIndependent)
{
    // the engine copies a prototype once per asset
    const std::vector<Bar> bars = walk(8, 600, 100, 0.01);
    const std::vector<Bar> other = walk(9, 300, 50, 0.03);
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
        {
            auto original = b.makeLine(p);
            for (std::size_t i = 0; i < 300; ++i)
                original->update(bars[i]);

            auto copy = original->clone();
            for (const Bar& bar : other)
                copy->update(bar);                   // must not disturb the original

            auto reference = b.makeLine(p);
            for (std::size_t i = 0; i < 300; ++i)
                reference->update(bars[i]);
            for (std::size_t i = 300; i < bars.size(); ++i)
            {
                original->update(bars[i]);
                reference->update(bars[i]);
                if (reference->ready())
                    ASSERT_EQ(original->value(), reference->value()) << describe(b, p);
            }
        }
}

TEST(BaselinesTest, TheLineIsCloseToThePriceOnAWalk)
{
    // a baseline follows price: never wildly away from the range of recent closes
    const std::vector<Bar> bars = walk(10, 1200, 100, 0.01);
    double lo = bars[0].low, hi = bars[0].high;
    for (const Bar& bar : bars)
    {
        lo = std::min(lo, bar.low);
        hi = std::max(hi, bar.high);
    }
    const double span = hi - lo;
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
        {
            auto line = b.makeLine(p);
            for (const Bar& bar : bars)
            {
                line->update(bar);
                if (line->ready())
                {
                    ASSERT_GT(line->value(), lo - span) << describe(b, p);
                    ASSERT_LT(line->value(), hi + span) << describe(b, p);
                }
            }
        }
}

TEST(BaselinesTest, BothTradingModesRunWithEveryBaseline)
{
    AlignedData data;
    data.symbols = {"AAAUSD", "BBBJPY"};
    const std::vector<Bar> a = walk(11, 700, 1.1, 0.006), c = walk(12, 700, 150, 0.006);
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        Snapshot s;
        s.timestamp = static_cast<std::int64_t>(i + 1);
        s.bars = {a[i], c[i]};
        s.bars[0]->timestamp = s.bars[1]->timestamp = s.timestamp;
        data.snapshots.push_back(s);
    }
    for (const BaselineInfo& b : baselineRegistry())
        for (const Params& p : sampleParams(b))
        {
            for (const StrategyFactory& factory : {b.nnfx(NnfxOptions{})})
            {
                auto strategy = factory(p, data.symbols.size());
                ASSERT_NE(strategy, nullptr) << describe(b, p);
                const BacktestResult r = runMultiBacktest(data, *strategy, 100000.0);
                ASSERT_EQ(r.equityCurve.size(), data.snapshots.size()) << describe(b, p);
                EXPECT_TRUE(std::isfinite(r.finalEquity)) << describe(b, p);
            }
        }
}
