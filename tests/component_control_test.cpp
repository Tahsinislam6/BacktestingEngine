#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include "component_control.hpp"

// runComponentControl: does a real component add anything over a random one that behaves just as often and just
// as persistently? Arms (all over the same data, same costs, the other components unchanged):
//   real      the options as given
//   without   the options with this role's component removed
//   random    this role's component replaced by a random stand-in, once per seed
// Stand-in baseline: FixedLine at 100, on a seeded random walk that crosses the line many times.
// Stand-in components: an ORACLE that looks at the next bar (so it is informative, and could never be real).

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

// a State that reads a precomputed list: states[i] on the i-th bar
class Listed : public AnyState
{
public:
    explicit Listed(std::shared_ptr<const std::vector<int>> states) : states_(std::move(states)) {}
    void update(const Bar&) override { ++seen_; }
    bool ready() const override { return seen_ >= 1; }
    int state() const override
    {
        if (!ready())
            throw std::logic_error("not ready");
        return (*states_)[std::min(seen_, states_->size()) - 1];
    }
    std::size_t warmupBars() const override { return 1; }
    std::unique_ptr<AnyState> clone() const override { return std::make_unique<Listed>(*this); }
private:
    std::shared_ptr<const std::vector<int>> states_;
    std::size_t seen_{0};
};

class ListedGate : public AnyGate
{
public:
    explicit ListedGate(std::shared_ptr<const std::vector<int>> states) : states_(std::move(states)) {}
    void update(const Bar&) override { ++seen_; }
    bool ready() const override { return seen_ >= 1; }
    bool allows() const override { return (*states_)[std::min(seen_, states_->size()) - 1] > 0; }
    std::size_t warmupBars() const override { return 1; }
    std::unique_ptr<AnyGate> clone() const override { return std::make_unique<ListedGate>(*this); }
private:
    std::shared_ptr<const std::vector<int>> states_;
    std::size_t seen_{0};
};

struct World
{
    AlignedData data;
    std::vector<double> closes;
};

// seeded random walk around 100 with a real range on each bar
World walk(unsigned seed, int n = 1500)
{
    std::mt19937 gen(seed);
    std::normal_distribution<double> noise(0.0, 0.01);
    World w;
    w.data.symbols = {"A0"};
    double prev = 100;
    for (int i = 0; i < n; ++i)
    {
        const double c = prev * std::exp(noise(gen)) + (100 - prev) * 0.02;       // a mild pull back to 100
        Bar b;
        b.timestamp = i + 1;
        b.open = prev;
        b.close = c;
        b.high = std::max(prev, c) * 1.002;
        b.low = std::min(prev, c) * 0.998;
        Snapshot s;
        s.timestamp = b.timestamp;
        s.bars.push_back(b);
        w.data.snapshots.push_back(s);
        w.closes.push_back(c);
        prev = c;
    }
    return w;
}

// the oracle: +1 when the NEXT close is higher, -1 when lower
std::shared_ptr<std::vector<int>> nextMove(const std::vector<double>& closes, int sign = 1)
{
    auto v = std::make_shared<std::vector<int>>();
    for (std::size_t i = 0; i < closes.size(); ++i)
        v->push_back(i + 1 < closes.size() ? sign * (closes[i + 1] > closes[i] ? 1 : -1) : 0);
    return v;
}

NnfxOptions options()
{
    NnfxOptions o;
    o.atrPeriod = 5;
    o.exitOnCross = false;
    o.maxDistanceAtr = std::nullopt;
    return o;
}

Params level100()
{
    Params p;
    p.set("level", 100);
    return p;
}

GridOptions grid(int threads = 1)
{
    GridOptions g;
    g.threads = threads;
    return g;
}
}

TEST(ComponentControlTest, ARealConfirmationBeatsRandomOnesThatBehaveTheSame)
{
    // the oracle only lets a trade in when the next bar moves its way: a real edge a random confirmation cannot copy
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 40, grid());

    EXPECT_EQ(r.random.size(), 40u);
    EXPECT_GT(r.real.trades, 10);
    EXPECT_GT(r.expectancyRank, 0.9);
    EXPECT_GT(r.sharpeRank, 0.9);
}

TEST(ComponentControlTest, ARealConfirmationThatIsBackwardsLosesToRandomOnes)
{
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes, -1));

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 40, grid());

    EXPECT_LT(r.expectancyRank, 0.1);
}

TEST(ComponentControlTest, ARealFilterBeatsRandomGatesWithTheSamePassRate)
{
    // an oracle gate: it allows exactly the bars on which the unfiltered system opened a trade that turned out well
    const World w = walk(2);
    const NnfxOptions plain = options();
    NnfxStrategy<FixedLine> unfiltered(1, FixedLine(100), plain);
    const BacktestResult base = runMultiBacktest(w.data, unfiltered, 100000.0);

    auto allowed = std::make_shared<std::vector<int>>(w.closes.size(), 0);
    for (const ClosedTrade& c : base.closedTrades)
    {
        if (c.pnl > 0)
            (*allowed)[static_cast<std::size_t>(c.trade.entryTime - 1)] = 1;
    }
    NnfxOptions o = plain;
    o.components.filter = std::make_shared<ListedGate>(allowed);

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Filter, 40, grid());

    EXPECT_EQ(r.random.size(), 40u);
    EXPECT_GT(r.real.trades, 5);
    EXPECT_GT(r.real.expectancyR, r.without.expectancyR);
    EXPECT_GT(r.totalRRank, 0.9);
    EXPECT_GT(r.sharpeRank, 0.9);
    EXPECT_GE(r.expectancyRank, 0.7);           // random gates pass rarely (2-4 trades each, +-1R apiece): their expectancy is very noisy
}

TEST(ComponentControlTest, RandomGatesBlockAsOftenAsTheRealOneSoTheyTradeLessThanWithoutAFilter)
{
    const World w = walk(2);
    NnfxOptions o = options();
    o.components.filter = std::make_shared<ListedGate>(std::make_shared<std::vector<int>>(w.closes.size(), 0));   // blocks always
    // a gate that never allows: the real arm and every random arm (same statistics) trade nothing, "without" trades

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Filter, 10, grid());

    EXPECT_EQ(r.real.trades, 0);
    EXPECT_GT(r.without.trades, 10);
    for (const ArmResult& a : r.random)
        EXPECT_EQ(a.trades, 0);
}

TEST(ComponentControlTest, AnExitIndicatorCanBeControlledToo)
{
    const World w = walk(3);
    NnfxOptions o = options();
    o.components.exitIndicator = std::make_shared<Listed>(nextMove(w.closes));

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::ExitIndicator, 20, grid());

    EXPECT_EQ(r.random.size(), 20u);
    EXPECT_FALSE(std::isnan(r.sharpeRank));
}

TEST(ComponentControlTest, TheRealArmIsTheOptionsAsGiven)
{
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 5, grid());
    const ArmResult direct = runArm(w.data, level100(), maker(o), grid());

    EXPECT_DOUBLE_EQ(r.real.totalR, direct.totalR);
    EXPECT_DOUBLE_EQ(r.real.sharpe, direct.sharpe);
    EXPECT_EQ(r.real.trades, direct.trades);
}

TEST(ComponentControlTest, TheWithoutArmDropsOnlyThisRolesComponent)
{
    const World w = walk(1);
    NnfxOptions both = options();
    both.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));
    both.components.filter = std::make_shared<ListedGate>(nextMove(w.closes));

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, both, ComponentRole::Confirmation, 3, grid());

    NnfxOptions filterOnly = options();
    filterOnly.components.filter = both.components.filter;
    const ArmResult direct = runArm(w.data, level100(), maker(filterOnly), grid());
    EXPECT_DOUBLE_EQ(r.without.totalR, direct.totalR);
    EXPECT_EQ(r.without.trades, direct.trades);
}

TEST(ComponentControlTest, RandomArmsKeepTheOtherComponentsReal)
{
    // randomising the confirmation while the filter blocks everything: no random arm can trade either
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));
    o.components.filter = std::make_shared<ListedGate>(std::make_shared<std::vector<int>>(w.closes.size(), 0));   // never allows

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 10, grid());

    for (const ArmResult& a : r.random)
        EXPECT_EQ(a.trades, 0);
    EXPECT_EQ(r.real.trades, 0);
}

TEST(ComponentControlTest, TheRandomArmsTradeAboutAsOftenAsTheRealOne)
{
    // the stand-in has the oracle's state mix and persistence, so the number of trades is of the same order
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 30, grid());

    double mean = 0;
    for (const ArmResult& a : r.random)
        mean += a.trades;
    mean /= r.random.size();
    EXPECT_GT(mean, 0.5 * r.real.trades);
    EXPECT_LT(mean, 2.0 * r.real.trades);
}

TEST(ComponentControlTest, RandomArmsDifferFromEachOther)
{
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 20, grid());

    bool differ = false;
    for (const ArmResult& a : r.random)
        differ = differ || a.totalR != r.random[0].totalR;
    EXPECT_TRUE(differ);
}

TEST(ComponentControlTest, ResultsAreRepeatableAndIndependentOfTheThreadCount)
{
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));

    const ComponentControlResult a = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 12, grid(1));
    const ComponentControlResult b = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 12, grid(4));
    const ComponentControlResult c = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 12, grid(1));

    ASSERT_EQ(a.random.size(), b.random.size());
    for (std::size_t i = 0; i < a.random.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(a.random[i].totalR, b.random[i].totalR);
        EXPECT_DOUBLE_EQ(a.random[i].totalR, c.random[i].totalR);
    }
    EXPECT_DOUBLE_EQ(a.expectancyRank, b.expectancyRank);
}

TEST(ComponentControlTest, DescribesTheBehaviourItMatched)
{
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));

    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 3, grid());

    EXPECT_NE(r.behaviour.find("%"), std::string::npos);
}

TEST(ComponentControlTest, NeedsTheRolesComponentAndAtLeastOneSeed)
{
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));

    EXPECT_THROW(runComponentControl(w.data, level100(), maker, o, ComponentRole::Filter, 5, grid()), std::invalid_argument);
    EXPECT_THROW(runComponentControl(w.data, level100(), maker, o, ComponentRole::ExitIndicator, 5, grid()), std::invalid_argument);
    EXPECT_THROW(runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 0, grid()), std::invalid_argument);
}

TEST(ComponentControlTest, CostsApplyToEveryArm)
{
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));
    GridOptions costly = grid();
    costly.costs.defaultPips = 1000.0;

    const ComponentControlResult free = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 4, grid());
    const ComponentControlResult paid = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 4, costly);

    EXPECT_LT(paid.real.totalR, free.real.totalR);
    EXPECT_LT(paid.without.totalR, free.without.totalR);
    EXPECT_LT(paid.random[0].totalR, free.random[0].totalR);
}

TEST(FormatComponentControlTest, ShowsTheComponentTheRoleAndTheRank)
{
    const World w = walk(1);
    NnfxOptions o = options();
    o.components.confirmation = std::make_shared<Listed>(nextMove(w.closes));
    const ComponentControlResult r = runComponentControl(w.data, level100(), maker, o, ComponentRole::Confirmation, 10, grid());

    const std::string text = formatComponentControl("out-of-sample", "macd", ComponentRole::Confirmation, r);

    EXPECT_NE(text.find("out-of-sample"), std::string::npos);
    EXPECT_NE(text.find("macd"), std::string::npos);
    EXPECT_NE(text.find("confirmation"), std::string::npos);
    EXPECT_NE(text.find("10 random"), std::string::npos);
    EXPECT_NE(text.find("without"), std::string::npos);
    EXPECT_NE(text.find("expectancy"), std::string::npos);
    EXPECT_NE(text.find("%"), std::string::npos);
}

TEST(ComponentRoleTest, HasReadableNames)
{
    EXPECT_STREQ(roleName(ComponentRole::Confirmation), "confirmation");
    EXPECT_STREQ(roleName(ComponentRole::ExitIndicator), "exit indicator");
    EXPECT_STREQ(roleName(ComponentRole::Filter), "filter");
}
