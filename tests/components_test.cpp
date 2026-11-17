#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "components_registry.hpp"

// These checks run on EVERY component in include/components/ automatically, like baselines_test.cpp does for
// baselines. They check behaviour (ready after the warm-up, no hidden state, sensible values), not the maths.

namespace
{
Bar makeBar(double o, double h, double l, double c)
{
    Bar b;
    b.open = o;
    b.high = h;
    b.low = l;
    b.close = c;
    return b;
}

std::vector<Bar> walk(unsigned seed, std::size_t n, double start, double volatility)
{
    std::mt19937 gen(seed);
    std::normal_distribution<double> noise(0.0, volatility);
    std::vector<Bar> bars;
    double prev = start;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double c = prev * std::exp(noise(gen));
        bars.push_back(makeBar(prev, std::max(prev, c) * (1 + std::abs(noise(gen)) / 2),
                               std::min(prev, c) * (1 - std::abs(noise(gen)) / 2), c));
        prev = c;
    }
    return bars;
}

std::vector<Bar> flatSeries(std::size_t n) { return std::vector<Bar>(n, makeBar(1.2, 1.2, 1.2, 1.2)); }

std::vector<Bar> gappySeries(std::size_t n)
{
    std::vector<Bar> bars;
    double p = 100;
    for (std::size_t i = 0; i < n; ++i)
    {
        p *= (i % 7 == 0) ? 1.25 : 0.9;
        bars.push_back(makeBar(p, p * 1.01, p * 0.99, p));
    }
    return bars;
}

// Uniform access to either kind
struct Handle
{
    std::shared_ptr<AnyState> state;
    std::shared_ptr<AnyGate> gate;

    void update(const Bar& b) { state ? state->update(b) : gate->update(b); }
    bool ready() const { return state ? state->ready() : gate->ready(); }
    // -1/0/+1 for a state, 0/1 for a gate
    int value() const { return state ? state->state() : (gate->allows() ? 1 : 0); }
    std::size_t warmup() const { return state ? state->warmupBars() : gate->warmupBars(); }
    Handle clone() const
    {
        Handle h;
        if (state) h.state = std::shared_ptr<AnyState>(state->clone());
        else h.gate = std::shared_ptr<AnyGate>(gate->clone());
        return h;
    }
};

Handle make(const ComponentInfo& c)
{
    Handle h;
    if (c.kind == ComponentInfo::Kind::State)
        h.state = c.makeState(c.defaults);
    else
        h.gate = c.makeGate(c.defaults);
    return h;
}
}

TEST(ComponentsTest, SomeComponentsAreRegistered)
{
    EXPECT_FALSE(componentRegistry().empty());
}

TEST(ComponentsTest, NamesAreUniqueSortedAndFindable)
{
    const auto& all = componentRegistry();
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        EXPECT_FALSE(all[i].name.empty());
        if (i > 0)
            EXPECT_LT(all[i - 1].name, all[i].name);
        ASSERT_NE(findComponent(all[i].name), nullptr);
        EXPECT_NE(componentNames().find(all[i].name), std::string::npos);
        // exactly the factory of its own kind
        EXPECT_EQ(static_cast<bool>(all[i].makeState), all[i].kind == ComponentInfo::Kind::State);
        EXPECT_EQ(static_cast<bool>(all[i].makeGate), all[i].kind == ComponentInfo::Kind::Gate);
    }
    EXPECT_EQ(findComponent("no_such_component"), nullptr);
}

TEST(ComponentsTest, ReadyExactlyAfterTheDeclaredWarmUp)
{
    const std::vector<Bar> bars = walk(1, 600, 100, 0.01);
    for (const ComponentInfo& c : componentRegistry())
    {
        Handle h = make(c);
        const std::size_t warm = c.warmupBars(c.defaults);
        ASSERT_GE(warm, 1u) << c.name;
        ASSERT_LE(warm, 500u) << c.name;
        EXPECT_EQ(h.warmup(), warm) << c.name;
        for (std::size_t i = 0; i < warm; ++i)
        {
            EXPECT_FALSE(h.ready()) << c.name << " ready after only " << i << " bars";
            h.update(bars[i]);
        }
        EXPECT_TRUE(h.ready()) << c.name << " not ready after its warm-up of " << warm;
    }
}

TEST(ComponentsTest, AskingForTheValueTooEarlyThrows)
{
    for (const ComponentInfo& c : componentRegistry())
        EXPECT_THROW(make(c).value(), std::logic_error) << c.name;
}

TEST(ComponentsTest, ValuesAreValidOnEveryKindOfData)
{
    const std::vector<std::vector<Bar>> datasets = {walk(2, 800, 1.1, 0.006), walk(3, 800, 150, 0.006),
                                                    walk(4, 800, 100, 0.05), flatSeries(800), gappySeries(800)};
    for (const ComponentInfo& c : componentRegistry())
        for (const auto& bars : datasets)
        {
            Handle h = make(c);
            for (const Bar& bar : bars)
            {
                h.update(bar);
                if (h.ready())
                {
                    const int v = h.value();
                    if (c.kind == ComponentInfo::Kind::State)
                        ASSERT_TRUE(v >= -1 && v <= 1) << c.name;
                    else
                        ASSERT_TRUE(v == 0 || v == 1) << c.name;
                }
            }
            EXPECT_TRUE(h.ready()) << c.name;
        }
}

TEST(ComponentsTest, ReadyStaysTrueAndReadingChangesNothing)
{
    const std::vector<Bar> bars = walk(5, 800, 100, 0.01);
    for (const ComponentInfo& c : componentRegistry())
    {
        Handle asked = make(c), silent = make(c);
        bool was = false;
        for (const Bar& bar : bars)
        {
            asked.update(bar);
            silent.update(bar);
            if (was)
                ASSERT_TRUE(asked.ready()) << c.name;
            was = asked.ready();
            if (was)
            {
                const int first = asked.value();
                ASSERT_EQ(first, asked.value()) << c.name;
            }
        }
        EXPECT_EQ(asked.value(), silent.value()) << c.name;
    }
}

TEST(ComponentsTest, TheSameBarsAlwaysGiveTheSameResult)
{
    const std::vector<Bar> bars = walk(6, 600, 100, 0.01);
    for (const ComponentInfo& c : componentRegistry())
    {
        Handle a = make(c), b = make(c);
        std::vector<int> first;
        for (const Bar& bar : bars)
        {
            a.update(bar);
            b.update(bar);
            if (a.ready())
            {
                ASSERT_EQ(a.value(), b.value()) << c.name;
                first.push_back(a.value());
            }
        }
        Handle again = make(c);
        std::size_t k = 0;
        for (const Bar& bar : bars)
        {
            again.update(bar);
            if (again.ready())
                ASSERT_EQ(again.value(), first[k++]) << c.name;
        }
    }
}

TEST(ComponentsTest, CopiesAreIndependent)
{
    const std::vector<Bar> bars = walk(7, 500, 100, 0.01);
    const std::vector<Bar> other = walk(8, 300, 50, 0.03);
    for (const ComponentInfo& c : componentRegistry())
    {
        Handle original = make(c);
        for (std::size_t i = 0; i < 250; ++i)
            original.update(bars[i]);
        Handle copy = original.clone();
        for (const Bar& bar : other)
            copy.update(bar);

        Handle reference = make(c);
        for (std::size_t i = 0; i < 250; ++i)
            reference.update(bars[i]);
        for (std::size_t i = 250; i < bars.size(); ++i)
        {
            original.update(bars[i]);
            reference.update(bars[i]);
            if (reference.ready())
                ASSERT_EQ(original.value(), reference.value()) << c.name;
        }
    }
}

TEST(ComponentsTest, ARisingMarketIsNotBearishAndAFallingOneIsNotBullish)
{
    // for the state components: a steady climb never reads bearish, a steady fall never bullish
    for (const ComponentInfo& c : componentRegistry())
    {
        if (c.kind != ComponentInfo::Kind::State)
            continue;
        Handle up = make(c), down = make(c);
        for (int i = 0; i < 400; ++i)
        {
            const double p = 100 + 0.5 * i, q = 300 - 0.5 * i;
            up.update(makeBar(p, p + 0.2, p - 0.2, p));
            down.update(makeBar(q, q + 0.2, q - 0.2, q));
        }
        EXPECT_GE(up.value(), 0) << c.name;
        EXPECT_LE(down.value(), 0) << c.name;
    }
}
