#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include "random_component.hpp"

// A random stand-in for a real component: a Markov chain with the same state frequencies and the same persistence
// (transition probabilities) as the real component showed on the data. measureState / measureGate read those
// numbers off the real component run over the data; randomState / randomGate build the stand-in.
//
// Stand-in real components: ByClose (a State: +1 above `level`, -1 below, 0 equal) and GateAt (a Gate).

namespace
{
class ByClose : public AnyState
{
public:
    explicit ByClose(double level, std::size_t warmup = 1) : level_(level), warmup_(warmup) {}
    void update(const Bar& b) override { close_ = b.close; ++seen_; }
    bool ready() const override { return seen_ >= warmup_; }
    int state() const override { return close_ > level_ ? 1 : close_ < level_ ? -1 : 0; }
    std::size_t warmupBars() const override { return warmup_; }
    std::unique_ptr<AnyState> clone() const override { return std::make_unique<ByClose>(*this); }
private:
    double level_;
    std::size_t warmup_;
    double close_{0};
    std::size_t seen_{0};
};

class GateAt : public AnyGate
{
public:
    explicit GateAt(double level, std::size_t warmup = 1) : level_(level), warmup_(warmup) {}
    void update(const Bar& b) override { close_ = b.close; ++seen_; }
    bool ready() const override { return seen_ >= warmup_; }
    bool allows() const override { return close_ >= level_; }
    std::size_t warmupBars() const override { return warmup_; }
    std::unique_ptr<AnyGate> clone() const override { return std::make_unique<GateAt>(*this); }
private:
    double level_;
    std::size_t warmup_;
    double close_{0};
    std::size_t seen_{0};
};

AlignedData dataOf(const std::vector<std::vector<double>>& closesPerAsset)
{
    AlignedData d;
    for (std::size_t a = 0; a < closesPerAsset.size(); ++a)
        d.symbols.push_back("A" + std::to_string(a));
    for (std::size_t i = 0; i < closesPerAsset[0].size(); ++i)
    {
        Snapshot s;
        s.timestamp = static_cast<std::int64_t>(i + 1);
        for (const auto& series : closesPerAsset)
        {
            Bar b;
            b.timestamp = s.timestamp;
            b.open = b.high = b.low = b.close = series[i];
            s.bars.push_back(b);
        }
        d.snapshots.push_back(s);
    }
    return d;
}

// the states of a long run of a stand-in
std::vector<int> run(AnyState& s, std::size_t bars)
{
    std::vector<int> out;
    Bar b;
    for (std::size_t i = 0; i < bars; ++i)
    {
        s.update(b);
        if (s.ready())
            out.push_back(s.state());
    }
    return out;
}
}

// ---------- measuring a state component ----------

TEST(MeasureStateTest, CountsHowOftenEachStateOccurs)
{
    // closes 90, 110, 90, 110: states -1, +1, -1, +1
    const ChainStats s = measureState(ByClose(100), dataOf({{90, 110, 90, 110}}));

    EXPECT_EQ(s.observations, 4u);
    ASSERT_EQ(s.frequency.size(), 3u);                     // index 0 = -1, 1 = 0, 2 = +1
    EXPECT_DOUBLE_EQ(s.frequency[0], 0.5);
    EXPECT_DOUBLE_EQ(s.frequency[1], 0.0);
    EXPECT_DOUBLE_EQ(s.frequency[2], 0.5);
}

TEST(MeasureStateTest, CountsTransitionsBetweenConsecutiveBars)
{
    // states -1, -1, -1, +1, +1, -1: leaving -1 three times (2 stay, 1 to +1), leaving +1 twice (1 stay, 1 to -1)
    const ChainStats s = measureState(ByClose(100), dataOf({{90, 90, 90, 110, 110, 90}}));

    EXPECT_DOUBLE_EQ(s.transition[0][0], 2.0 / 3);
    EXPECT_DOUBLE_EQ(s.transition[0][2], 1.0 / 3);
    EXPECT_DOUBLE_EQ(s.transition[2][2], 0.5);
    EXPECT_DOUBLE_EQ(s.transition[2][0], 0.5);
}

TEST(MeasureStateTest, TransitionRowsAddUpToOne)
{
    const ChainStats s = measureState(ByClose(100), dataOf({{90, 110, 110, 90, 90, 110, 100, 90, 110}}));

    for (const auto& row : s.transition)
    {
        double sum = 0;
        for (double p : row)
            sum += p;
        EXPECT_NEAR(sum, 1.0, 1e-12);
    }
}

TEST(MeasureStateTest, OnlyBarsAfterTheWarmUpCount)
{
    // warm-up 3: the first two bars are not ready; the states seen are those of bars 3.. (110, 110, 90)
    const ChainStats s = measureState(ByClose(100, 3), dataOf({{90, 90, 110, 110, 90}}));

    EXPECT_EQ(s.observations, 3u);
    EXPECT_DOUBLE_EQ(s.frequency[2], 2.0 / 3);
    EXPECT_DOUBLE_EQ(s.frequency[0], 1.0 / 3);
}

TEST(MeasureStateTest, AssetsArePooledButTransitionsNeverCrossBetweenAssets)
{
    // asset 0 always +1, asset 1 always -1: no transition between the two
    const ChainStats s = measureState(ByClose(100), dataOf({{110, 110, 110}, {90, 90, 90}}));

    EXPECT_EQ(s.observations, 6u);
    EXPECT_DOUBLE_EQ(s.frequency[0], 0.5);
    EXPECT_DOUBLE_EQ(s.frequency[2], 0.5);
    EXPECT_DOUBLE_EQ(s.transition[0][0], 1.0);
    EXPECT_DOUBLE_EQ(s.transition[2][2], 1.0);
}

TEST(MeasureStateTest, ASkippedBarBreaksNoTransitionButIsNotCounted)
{
    AlignedData d = dataOf({{90, 110, 90}});
    d.snapshots[1].bars[0].reset();

    const ChainStats s = measureState(ByClose(100), d);

    EXPECT_EQ(s.observations, 2u);
    EXPECT_DOUBLE_EQ(s.transition[0][0], 1.0);             // the two recorded bars follow each other
}

TEST(MeasureStateTest, AStateNeverSeenLeavingFallsBackToTheFrequencies)
{
    // +1 only ever appears on the last bar, so nothing is seen leaving it
    const ChainStats s = measureState(ByClose(100), dataOf({{90, 90, 90, 110}}));

    EXPECT_DOUBLE_EQ(s.transition[2][0], s.frequency[0]);
    EXPECT_DOUBLE_EQ(s.transition[2][2], s.frequency[2]);
}

TEST(MeasureStateTest, ANeverReadyComponentGivesUniformStatistics)
{
    const ChainStats s = measureState(ByClose(100, 50), dataOf({{90, 110, 90}}));

    EXPECT_EQ(s.observations, 0u);
    EXPECT_NEAR(s.frequency[0], 1.0 / 3, 1e-12);
    EXPECT_NEAR(s.transition[1][2], 1.0 / 3, 1e-12);
}

TEST(MeasureStateTest, DoesNotChangeTheComponentItIsGiven)
{
    ByClose real(100);
    measureState(real, dataOf({{90, 110, 90}}));

    EXPECT_FALSE(real.ready());                            // the prototype was only cloned
}

// ---------- measuring a gate ----------

TEST(MeasureGateTest, CountsThePassRateAndThePersistence)
{
    // closes 90, 110, 110, 90 with the gate at 100: blocked, open, open, blocked
    const ChainStats g = measureGate(GateAt(100), dataOf({{90, 110, 110, 90}}));

    ASSERT_EQ(g.frequency.size(), 2u);                     // index 0 = blocked, 1 = open
    EXPECT_DOUBLE_EQ(g.frequency[1], 0.5);
    EXPECT_DOUBLE_EQ(g.transition[1][1], 0.5);             // open -> open once, open -> blocked once
    EXPECT_DOUBLE_EQ(g.transition[0][1], 1.0);             // blocked -> open (the only leaving from blocked)
}

// ---------- the random stand-ins ----------

namespace
{
// a chain that stays in its state with probability 0.9 and otherwise moves to the other of -1 / +1
ChainStats persistent()
{
    ChainStats s;
    s.observations = 1000;
    s.frequency = {0.5, 0.0, 0.5};
    s.transition = {{0.9, 0.0, 0.1}, {0.0, 1.0, 0.0}, {0.1, 0.0, 0.9}};
    return s;
}
}

TEST(RandomStateTest, IsReadyAfterTheGivenWarmUpAndNotBefore)
{
    auto r = randomState(persistent(), 5, 1);
    Bar b;
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_FALSE(r->ready());
        EXPECT_THROW(r->state(), std::logic_error);
        r->update(b);
    }
    r->update(b);
    EXPECT_TRUE(r->ready());
    EXPECT_EQ(r->warmupBars(), 5u);
    EXPECT_NO_THROW(r->state());
}

TEST(RandomStateTest, TheSameSeedGivesTheSameSequenceAndDifferentSeedsDiffer)
{
    auto a = randomState(persistent(), 1, 7);
    auto b = randomState(persistent(), 1, 7);
    auto c = randomState(persistent(), 1, 8);

    const auto sa = run(*a, 500), sb = run(*b, 500), sc = run(*c, 500);

    EXPECT_EQ(sa, sb);
    EXPECT_NE(sa, sc);
}

TEST(RandomStateTest, KeepsTheFrequenciesAndThePersistenceOfTheOriginal)
{
    auto r = randomState(persistent(), 1, 3);
    const std::vector<int> s = run(*r, 40000);

    int plus = 0, stays = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        plus += s[i] == 1;
        if (i > 0)
            stays += s[i] == s[i - 1];
    }
    EXPECT_NEAR(static_cast<double>(plus) / s.size(), 0.5, 0.05);
    EXPECT_NEAR(static_cast<double>(stays) / (s.size() - 1), 0.9, 0.01);
}

TEST(RandomStateTest, OnlyProducesStatesThatOccurred)
{
    ChainStats s;
    s.observations = 10;
    s.frequency = {0.5, 0.0, 0.5};                          // never neutral
    s.transition = {{0.5, 0.0, 0.5}, {1.0 / 3, 1.0 / 3, 1.0 / 3}, {0.5, 0.0, 0.5}};
    auto r = randomState(s, 1, 4);

    std::set<int> seen;
    for (int v : run(*r, 5000))
        seen.insert(v);

    EXPECT_EQ(seen, (std::set<int>{-1, 1}));
}

TEST(RandomStateTest, EachCloneIsItsOwnIndependentButRepeatableStream)
{
    auto proto = randomState(persistent(), 1, 11);
    auto c0 = proto->clone(), c1 = proto->clone();
    auto protoAgain = randomState(persistent(), 1, 11);
    auto d0 = protoAgain->clone(), d1 = protoAgain->clone();

    const auto s0 = run(*c0, 400), s1 = run(*c1, 400);

    EXPECT_NE(s0, s1);                                      // different assets get different streams
    EXPECT_EQ(s0, run(*d0, 400));                           // but the same seed repeats them
    EXPECT_EQ(s1, run(*d1, 400));
}

TEST(RandomStateTest, ACloneStartsFromScratch)
{
    auto proto = randomState(persistent(), 3, 5);
    auto c = proto->clone();
    Bar b;
    for (int i = 0; i < 10; ++i)
        c->update(b);

    EXPECT_FALSE(proto->ready());
    EXPECT_FALSE(proto->clone()->ready());
}

TEST(RandomGateTest, KeepsThePassRateAndThePersistence)
{
    ChainStats g;
    g.observations = 100;
    g.frequency = {0.75, 0.25};
    g.transition = {{0.95, 0.05 }, {0.15, 0.85}};            // stationary share open = 0.05 / (0.05 + 0.15) = 0.25
    auto r = randomGate(g, 1, 2);
    Bar b;
    int open = 0, stays = 0, n = 0;
    bool prev = false;
    for (int i = 0; i < 40000; ++i)
    {
        r->update(b);
        const bool now = r->allows();
        open += now;
        if (i > 0)
            stays += now == prev;
        prev = now;
        ++n;
    }
    EXPECT_NEAR(static_cast<double>(open) / n, 0.25, 0.03);
    EXPECT_NEAR(static_cast<double>(stays) / (n - 1), 0.75 * 0.95 + 0.25 * 0.85, 0.01);
}

TEST(RandomGateTest, IsNotReadyBeforeItsWarmUpAndCloneIsDeterministic)
{
    ChainStats g;
    g.observations = 10;
    g.frequency = {0.5, 0.5};
    g.transition = {{0.5, 0.5}, {0.5, 0.5}};
    auto r = randomGate(g, 4, 9);
    EXPECT_FALSE(r->ready());
    EXPECT_THROW(r->allows(), std::logic_error);
    EXPECT_EQ(r->warmupBars(), 4u);

    auto p1 = randomGate(g, 1, 9), p2 = randomGate(g, 1, 9);
    auto a = p1->clone(), b = p2->clone();
    Bar bar;
    for (int i = 0; i < 100; ++i)
    {
        a->update(bar);
        b->update(bar);
        EXPECT_EQ(a->allows(), b->allows());
    }
}

TEST(RandomStateTest, WorksAsTheRealThingsStandIn)
{
    // measured on a real component, then used with the same warm-up
    const ChainStats s = measureState(ByClose(100, 3), dataOf({{90, 110, 90, 110, 90, 110}}));
    auto r = randomState(s, 3, 1);

    EXPECT_EQ(r->warmupBars(), 3u);
}
