#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "multi_backtester.hpp"
#include "nnfx_strategy.hpp"

// NnfxStrategy<B>: one signal at a time per asset.
//   Signal    the close crosses the baseline line (the first bar after warm-up never signals)
//   Entry     two trades via nnfxEntryOrders: a half with a take-profit and a runner, one shared stop
//   Chase     skip the entry if the close is more than maxDistanceAtr * ATR from the line
//   Exit      the close crosses back over the line: close both trades (and enter the other way if allowed)
//   Breakeven once the take-profit half has been hit, close the runner if the close returns to its entry price
//   A new signal is only taken when the asset has no open trades.
//
// Stand-in baseline: FixedLine at 100. ATR period 1 makes ATR = the bar's true range.

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

Bar ohlc(double o, double h, double l, double c)
{
    Bar b;
    b.open = o;
    b.high = h;
    b.low = l;
    b.close = c;
    return b;
}

Bar flat(double p) { return ohlc(p, p, p, p); }

// one asset per series; every snapshot has a bar for every asset
AlignedData dataOf(const std::vector<std::vector<Bar>>& perAsset)
{
    AlignedData d;
    for (std::size_t a = 0; a < perAsset.size(); ++a)
        d.symbols.push_back("A" + std::to_string(a));
    for (std::size_t i = 0; i < perAsset[0].size(); ++i)
    {
        Snapshot s;
        s.timestamp = static_cast<std::int64_t>(i + 1);
        for (const auto& series : perAsset)
        {
            Bar b = series[i];
            b.timestamp = s.timestamp;
            s.bars.push_back(b);
        }
        d.snapshots.push_back(s);
    }
    return d;
}

NnfxOptions options()
{
    NnfxOptions o;
    o.atrPeriod = 1;
    return o;
}

BacktestResult run(const std::vector<Bar>& bars, const NnfxOptions& o = options(), double cash = 100000.0)
{
    NnfxStrategy<FixedLine> s(1, FixedLine(100), o);
    return runMultiBacktest(dataOf({bars}), s, cash);
}

// bar 0 below the line, bar 1 crosses up with ATR (true range) 3:
//   entry 100.5, stop 100.5 - 4.5 = 96, target 103.5, each half 100000 * 1% / 4.5 units
const Bar below = ohlc(98, 99, 97, 98);
const Bar crossUp = ohlc(98, 101, 98, 100.5);
constexpr double kHalf = 1000.0 / 4.5;

}

// ---------- entry ----------

TEST(NnfxStrategyTest, NoTradesWhileThePriceStaysOnOneSide)
{
    const BacktestResult r = run({below, ohlc(98, 99, 97, 98.5), ohlc(98, 99, 97, 97)});

    EXPECT_TRUE(r.openTrades.empty());
    EXPECT_TRUE(r.closedTrades.empty());
}

TEST(NnfxStrategyTest, TheFirstBarNeverSignalsEvenWhenAboveTheLine)
{
    const BacktestResult r = run({ohlc(104, 106, 103, 105), ohlc(105, 107, 104, 106)});

    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxStrategyTest, ACrossUpOpensTwoLongsWithTheNnfxStopTargetAndSize)
{
    const BacktestResult r = run({below, crossUp});

    ASSERT_EQ(r.openTrades.size(), 2u);
    for (const Trade& t : r.openTrades)
    {
        EXPECT_EQ(t.direction, Direction::Long);
        EXPECT_DOUBLE_EQ(t.entryPrice, 100.5);
        ASSERT_TRUE(t.stopLoss);
        EXPECT_DOUBLE_EQ(*t.stopLoss, 96.0);
        EXPECT_NEAR(t.quantity, kHalf, 1e-9);
    }
    EXPECT_TRUE(r.openTrades[0].takeProfit.has_value());
    EXPECT_DOUBLE_EQ(*r.openTrades[0].takeProfit, 103.5);
    EXPECT_FALSE(r.openTrades[1].takeProfit.has_value());
}

TEST(NnfxStrategyTest, ACrossDownOpensTwoShorts)
{
    // bar 0 above the line, bar 1 crosses down: prev close 102, true range max(3, |100-102|, |97-102|) = 5
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5)});

    ASSERT_EQ(r.openTrades.size(), 2u);
    for (const Trade& t : r.openTrades)
    {
        EXPECT_EQ(t.direction, Direction::Short);
        EXPECT_DOUBLE_EQ(t.entryPrice, 99.5);
        EXPECT_DOUBLE_EQ(*t.stopLoss, 99.5 + 1.5 * 5);
    }
    EXPECT_DOUBLE_EQ(*r.openTrades[0].takeProfit, 99.5 - 5);
    EXPECT_FALSE(r.openTrades[1].takeProfit.has_value());
}

TEST(NnfxStrategyTest, NoRepeatEntriesWhileThePriceStaysAboveTheLine)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.5, 101.5, 100.2, 101), ohlc(101, 102, 100.5, 101.5)});

    EXPECT_EQ(r.openTrades.size(), 2u);
    EXPECT_TRUE(r.closedTrades.empty());
}

TEST(NnfxStrategyTest, TheSizeUsesCurrentEquity)
{
    const BacktestResult r = run({below, crossUp}, options(), 200000.0);

    EXPECT_NEAR(r.openTrades[0].quantity, 2 * kHalf, 1e-9);
}

TEST(NnfxStrategyTest, ACloseExactlyOnTheLineIsNoSignalAndKeepsTheOldSide)
{
    // below, on the line, then above: the cross is still seen
    const BacktestResult r = run({below, ohlc(98, 100, 98, 100), crossUp});

    EXPECT_EQ(r.openTrades.size(), 2u);
}

// ---------- don't chase ----------

namespace
{
// ATR period 2: two calm bars (true range 1), then one bar closing at `c` with true range c - 98:
//   ATR = (1 + (c - 98)) / 2. At c = 103: ATR 3 and the close is 3 from the line (exactly 1 ATR).
std::vector<Bar> chaseBars(double c)
{
    return {ohlc(98, 98.5, 97.5, 98), ohlc(98, 98.5, 97.5, 98), flat(c)};
}

NnfxOptions options2()
{
    NnfxOptions o;
    o.atrPeriod = 2;
    return o;
}
}

TEST(NnfxStrategyTest, TheEntryIsTakenWhenTheCloseIsExactlyOneAtrFromTheLine)
{
    EXPECT_EQ(run(chaseBars(103), options2()).openTrades.size(), 2u);
}

TEST(NnfxStrategyTest, TheEntryIsSkippedWhenTheCloseIsMoreThanOneAtrFromTheLine)
{
    const BacktestResult r = run(chaseBars(103.5), options2());

    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxStrategyTest, TheChaseLimitCanBeChangedOrSwitchedOff)
{
    NnfxOptions wide = options2();
    wide.maxDistanceAtr = 2.0;
    EXPECT_EQ(run(chaseBars(103.5), wide).openTrades.size(), 2u);

    NnfxOptions off = options2();
    off.maxDistanceAtr = std::nullopt;
    EXPECT_EQ(run(chaseBars(120), off).openTrades.size(), 2u);
}

TEST(NnfxStrategyTest, ASkippedSignalIsNotTakenLater)
{
    // skipped at 103.5; the next bars stay above the line without crossing again
    std::vector<Bar> bars = chaseBars(103.5);
    bars.push_back(flat(103.6));
    bars.push_back(flat(103.7));

    EXPECT_TRUE(run(bars, options2()).openTrades.empty());
}

// ---------- stop and target ----------

TEST(NnfxStrategyTest, TheTakeProfitHalfClosesAtTheTargetAndTheRunnerStaysOpen)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103)});

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 103.5);
    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_FALSE(r.openTrades[0].takeProfit.has_value());
}

TEST(NnfxStrategyTest, TheStopClosesBothHalves)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.4, 101, 95, 100.2)});

    ASSERT_EQ(r.closedTrades.size(), 2u);
    for (const ClosedTrade& c : r.closedTrades)
    {
        EXPECT_EQ(c.reason, ExitReason::StopLoss);
        EXPECT_DOUBLE_EQ(c.exitPrice, 96.0);
    }
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxStrategyTest, AStopLossOfTheWholeSignalLosesTwoPercent)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.4, 101, 95, 100.2)});

    EXPECT_NEAR(r.finalEquity, 100000.0 - 2 * 1000.0 * (100.5 - 96.0) / 4.5, 1e-6);   // -2000
}

// ---------- breakeven for the runner ----------

TEST(NnfxStrategyTest, AfterTheTargetTheRunnerIsClosedWhenTheCloseReturnsToItsEntry)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 103, 100, 100.4)});

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Breakeven);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 100.4);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxStrategyTest, TheBreakevenExitAlsoFiresWhenTheCloseIsExactlyAtTheEntryPrice)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 103, 100.5, 100.5)});

    EXPECT_EQ(r.closedTrades.size(), 2u);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxStrategyTest, BeforeTheTargetThereIsNoBreakevenExit)
{
    // close back below the entry (100.5) but above the line and the stop; the target was never hit
    const BacktestResult r = run({below, crossUp, ohlc(100.5, 101, 100.1, 100.3)});

    EXPECT_EQ(r.openTrades.size(), 2u);
    EXPECT_TRUE(r.closedTrades.empty());
}

TEST(NnfxStrategyTest, TheRunnerStaysOpenAboveItsEntryAfterTheTarget)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 104, 102, 102.5)});

    EXPECT_EQ(r.openTrades.size(), 1u);
}

TEST(NnfxStrategyTest, BreakevenCanBeSwitchedOff)
{
    NnfxOptions o = options();
    o.breakeven = false;

    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 103, 100.2, 100.4)}, o);

    EXPECT_EQ(r.openTrades.size(), 1u);
}

TEST(NnfxStrategyTest, ABreakevenExitForAShortMirrorsTheLong)
{
    // short from 99.5 (stop 107, target 94.5); the target is hit, then the close returns to 99.5 or above
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5), ohlc(99.4, 99.5, 94, 95),
                                  ohlc(95, 99.6, 95, 99.6)});

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Breakeven);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 99.6);
}

// ---------- the cross back ----------

TEST(NnfxStrategyTest, ACrossBackClosesBothTradesAndEntersTheOtherWay)
{
    // bar 2: close 99.5 is below the line; true range 1.8, distance to the line 0.5
    const BacktestResult r = run({below, crossUp, ohlc(100.5, 100.8, 99, 99.5)});

    ASSERT_EQ(r.closedTrades.size(), 2u);
    for (const ClosedTrade& c : r.closedTrades)
    {
        EXPECT_EQ(c.reason, ExitReason::Signal);
        EXPECT_DOUBLE_EQ(c.exitPrice, 99.5);
    }
    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Short);
    EXPECT_DOUBLE_EQ(r.openTrades[0].entryPrice, 99.5);
    EXPECT_DOUBLE_EQ(*r.openTrades[0].stopLoss, 99.5 + 1.5 * 1.8);

    const double equity = 100000.0 - 2 * kHalf * 1.0;      // both longs lost 1.0 per unit
    EXPECT_NEAR(r.openTrades[0].quantity, equity * 0.01 / (1.5 * 1.8), 1e-6);
}

TEST(NnfxStrategyTest, TheCrossBackExitIsKeptEvenWhenTheNewEntryIsTooFar)
{
    // limit 0.5 ATR: the long entry is 0.5 from the line (ATR 3) and passes; the cross back closes at 98.9,
    // 1.1 from the line with ATR 1.8, which is beyond 0.9, so the short is skipped
    NnfxOptions o = options();
    o.maxDistanceAtr = 0.5;

    const BacktestResult r = run({below, crossUp, ohlc(100.5, 100.8, 99, 98.9)}, o);

    EXPECT_EQ(r.closedTrades.size(), 2u);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxStrategyTest, WithoutTheCrossBackExitTheTradesStayAndNoNewSignalIsTaken)
{
    NnfxOptions o = options();
    o.exitOnCross = false;

    const BacktestResult r = run({below, crossUp, ohlc(100.5, 100.8, 99, 99.5)}, o);

    EXPECT_TRUE(r.closedTrades.empty());
    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Long);
}

TEST(NnfxStrategyTest, WithoutTheCrossBackExitANewSignalIsTakenOnceTheTradesAreGone)
{
    NnfxOptions o = options();
    o.exitOnCross = false;

    // long, stopped out at 96 while the close stays above the line, later a cross down and a cross up
    const BacktestResult r = run({below, crossUp, ohlc(100.4, 101, 95, 100.2), ohlc(100, 100.2, 98, 99),
                                  ohlc(99, 101.5, 99, 101)}, o);

    EXPECT_EQ(r.closedTrades.size(), 2u);     // the stopped pair
    // the cross down at bar 3 opens a short pair; the cross up at bar 4 finds it still open, so no long
    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Short);
}

// ---------- several assets ----------

TEST(NnfxStrategyTest, AssetsAreHandledIndependently)
{
    NnfxStrategy<FixedLine> s(2, FixedLine(100), options());
    const AlignedData d = dataOf({{below, below, below}, {below, crossUp, ohlc(100.5, 101, 100.2, 100.8)}});

    const BacktestResult r = runMultiBacktest(d, s, 100000.0);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].asset, 1);
    EXPECT_EQ(r.openTrades[1].asset, 1);
}

TEST(NnfxStrategyTest, AnAssetWithNoBarTodayIsSkippedAndKeepsItsSide)
{
    NnfxStrategy<FixedLine> s(1, FixedLine(100), options());
    AlignedData d = dataOf({{below, below, crossUp}});
    d.snapshots[1].bars[0].reset();       // no bar on day 2

    const BacktestResult r = runMultiBacktest(d, s, 100000.0);

    EXPECT_EQ(r.openTrades.size(), 2u);
}

// ---------- setup ----------

TEST(NnfxStrategyTest, RejectsBadOptions)
{
    NnfxOptions o = options();
    o.atrPeriod = 0;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), o), std::invalid_argument);

    o = options();
    o.maxDistanceAtr = 0.0;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), o), std::invalid_argument);

    o = options();
    o.entry.stopAtr = -1;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), o), std::invalid_argument);
}

TEST(NnfxStrategyTest, DefaultsAreTheUsualNnfxOnes)
{
    const NnfxOptions o;
    EXPECT_EQ(o.atrPeriod, 14);
    ASSERT_TRUE(o.maxDistanceAtr.has_value());
    EXPECT_DOUBLE_EQ(*o.maxDistanceAtr, 1.0);
    EXPECT_TRUE(o.exitOnCross);
    EXPECT_TRUE(o.breakeven);
    ASSERT_TRUE(o.trailAtr.has_value());
    EXPECT_DOUBLE_EQ(*o.trailAtr, 1.5);
    EXPECT_DOUBLE_EQ(o.entry.riskPerHalf, 0.01);
}

// ---------- random direction (the control) ----------
//
// NnfxOptions::randomSeed set: the signal TIMING is still the baseline cross, but each entry's direction is a
// coin flip (seeded, so repeatable). Everything else (stop, target, size, chase filter, breakeven) is unchanged.
// It needs exitOnCross == false: a cross-back exit is meaningless for a trade that may start against the line.

namespace
{
NnfxOptions randomOptions(std::uint64_t seed)
{
    NnfxOptions o = options();
    o.exitOnCross = false;
    o.randomSeed = seed;
    return o;
}

// Many signals. Every cross (up, then down) opens a pair, and the very next bar is wide enough to hit the stop of
// either direction, so the strategy is flat again before the next cross.
std::vector<Bar> manyCrosses()
{
    std::vector<Bar> bars{ohlc(98, 99, 97, 98)};
    for (int k = 0; k < 120; ++k)
    {
        bars.push_back(ohlc(98, 101, 98, 100.5));        // cross up
        bars.push_back(ohlc(100.5, 108, 90, 100.6));     // wide, stays above the line
        bars.push_back(ohlc(100.6, 101, 97, 98));        // cross down
        bars.push_back(ohlc(98, 108, 90, 98.1));         // wide, stays below the line
    }
    return bars;
}
}

TEST(NnfxRandomTest, RejectsTheCrossBackExit)
{
    NnfxOptions o = options();
    o.randomSeed = 1;
    o.exitOnCross = true;

    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), o), std::invalid_argument);
}

TEST(NnfxRandomTest, TheSameSeedGivesTheSameTrades)
{
    const BacktestResult a = run(manyCrosses(), randomOptions(5));
    const BacktestResult b = run(manyCrosses(), randomOptions(5));

    ASSERT_EQ(a.closedTrades.size(), b.closedTrades.size());
    ASSERT_GT(a.closedTrades.size(), 20u);
    for (std::size_t i = 0; i < a.closedTrades.size(); ++i)
    {
        EXPECT_EQ(a.closedTrades[i].trade.direction, b.closedTrades[i].trade.direction);
        EXPECT_DOUBLE_EQ(a.closedTrades[i].pnl, b.closedTrades[i].pnl);
    }
}

TEST(NnfxRandomTest, DifferentSeedsGiveDifferentDirections)
{
    const BacktestResult a = run(manyCrosses(), randomOptions(1));
    const BacktestResult b = run(manyCrosses(), randomOptions(2));

    bool anyDifferent = false;
    for (std::size_t i = 0; i < std::min(a.closedTrades.size(), b.closedTrades.size()); ++i)
        anyDifferent = anyDifferent || a.closedTrades[i].trade.direction != b.closedTrades[i].trade.direction;
    EXPECT_TRUE(anyDifferent);
}

TEST(NnfxRandomTest, AboutHalfTheEntriesAreLong)
{
    const BacktestResult r = run(manyCrosses(), randomOptions(3));

    int longs = 0, total = 0;
    for (const ClosedTrade& c : r.closedTrades)
    {
        ++total;
        longs += c.trade.direction == Direction::Long;
    }
    ASSERT_GT(total, 100);
    EXPECT_GT(longs, total * 35 / 100);
    EXPECT_LT(longs, total * 65 / 100);
}

TEST(NnfxRandomTest, TheTimingAndTheSizeAreTheBaselinesOwn)
{
    // the first signal: same bar, same entry price, same quantity and stop distance as without the coin
    NnfxOptions plain = options();
    plain.exitOnCross = false;
    const BacktestResult base = run({below, crossUp}, plain);

    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        const BacktestResult r = run({below, crossUp}, randomOptions(seed));
        ASSERT_EQ(r.openTrades.size(), 2u) << seed;
        EXPECT_EQ(r.openTrades[0].entryTime, base.openTrades[0].entryTime);
        EXPECT_DOUBLE_EQ(r.openTrades[0].entryPrice, 100.5);
        EXPECT_NEAR(r.openTrades[0].quantity, base.openTrades[0].quantity, 1e-9);
        EXPECT_NEAR(std::abs(*r.openTrades[0].stopLoss - 100.5), 4.5, 1e-9);
        EXPECT_NEAR(std::abs(*r.openTrades[0].takeProfit - 100.5), 3.0, 1e-9);
    }
}

TEST(NnfxRandomTest, BothDirectionsOccurOverSeeds)
{
    bool sawLong = false, sawShort = false;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        const BacktestResult r = run({below, crossUp}, randomOptions(seed));
        sawLong = sawLong || r.openTrades[0].direction == Direction::Long;
        sawShort = sawShort || r.openTrades[0].direction == Direction::Short;
    }
    EXPECT_TRUE(sawLong);
    EXPECT_TRUE(sawShort);
}

TEST(NnfxRandomTest, TheChaseFilterStillApplies)
{
    NnfxOptions o = options2();
    o.exitOnCross = false;
    o.randomSeed = 4;

    EXPECT_TRUE(run(chaseBars(103.5), o).openTrades.empty());
    EXPECT_EQ(run(chaseBars(103), o).openTrades.size(), 2u);
}

// ---------- entries that cannot be sized are skipped, not fatal ----------

TEST(NnfxStrategyTest, NoEntryIsTakenWithoutEquity)
{
    const BacktestResult r = run({below, crossUp}, options(), 0.0);

    EXPECT_TRUE(r.openTrades.empty());
}

// ---------- trailing exit for the runner ----------
//
// Once the target half has been hit, the runner is closed when its close falls trailAtr * ATR (the current ATR)
// below the highest close since entry (a short: rises above the lowest close). Same side effects as the
// breakeven exit: it is checked on the close. Breakeven takes precedence when both apply. Off with nullopt.

namespace
{
NnfxOptions trailing(double atrs)
{
    NnfxOptions o = options();
    o.trailAtr = atrs;
    return o;
}

// long from 100.5 (stop 96, target 103.5); the target is hit on bar 2, the close then runs up to 105
const Bar targetHit = ohlc(100.6, 104, 100.4, 103);
const Bar runsUp = ohlc(103, 106, 102.5, 105);        // true range 3.5
}

TEST(NnfxTrailingTest, TheRunnerIsClosedWhenTheCloseFallsAnAtrMultipleBelowTheBestClose)
{
    // after 105 the next bar has true range 3, so the trail is 0.5 * 3 = 1.5 below 105 = 103.5; it closes at 102.5
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 105, 102, 102.5)}, trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Trailing);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 102.5);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxTrailingTest, TheRunnerStaysWhileTheCloseIsWithinTheTrail)
{
    // true range 3 -> trail 0.5 * 3 = 1.5 below the best close 105 = 103.5; it closes at 104
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 105, 102, 104)}, trailing(0.5));

    EXPECT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.openTrades.size(), 1u);
}

TEST(NnfxTrailingTest, TheBestCloseKeepsRisingAndTheTrailFollowsIt)
{
    // closes 103, 105, 107, then 105.9 (true range 1.1): the trail is 0.55 below 107 = 106.45 -> exit;
    // measured from the earlier best close of 105 (104.45) it would have stayed
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 107, 105, 107), ohlc(107, 107, 105.9, 105.9)},
                                 trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Trailing);
}

TEST(NnfxTrailingTest, NotBeforeTheTargetHasBeenHit)
{
    // the target 103.5 is not reached (high 103); the close then falls more than the trail from the best close
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 103, 100.4, 102.9), ohlc(102.9, 103, 101, 101.2)},
                                 trailing(0.5));

    EXPECT_EQ(r.openTrades.size(), 2u);
    EXPECT_TRUE(r.closedTrades.empty());
}

TEST(NnfxTrailingTest, CanBeSwitchedOff)
{
    NnfxOptions o = options();
    o.trailAtr = std::nullopt;

    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 105, 102, 102.5)}, o);

    EXPECT_EQ(r.openTrades.size(), 1u);
}

TEST(NnfxTrailingTest, AShortRunnerTrailsAboveTheLowestClose)
{
    // short from 99.5 (stop 107, target 94.5); the target is hit and the close runs down to 93, then bounces to 94.4:
    // true range 1.4 on the bounce bar -> trail 0.5 * 1.4 = 0.7 above 93 = 93.7 -> exit at 94.4
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5), ohlc(99.4, 99.5, 94, 95),
                                  ohlc(95, 95, 93, 93), ohlc(93, 94.4, 93, 94.4)}, trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Trailing);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 94.4);
}

TEST(NnfxTrailingTest, BreakevenTakesPrecedenceWhenTheCloseIsBackAtTheEntry)
{
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 105, 100, 100.4)}, trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Breakeven);
}

TEST(NnfxTrailingTest, AlsoWorksWithTheCrossBackExitOn)
{
    NnfxOptions o = trailing(0.5);
    o.exitOnCross = true;

    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 105, 102, 102.5)}, o);

    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Trailing);
}

TEST(NnfxTrailingTest, ANewSignalCanBeTakenOnceTheRunnerIsGone)
{
    // after the trailing exit (price still above the line), a later cross down and a cross up each find the strategy
    // flat or not; here: the cross down at the end opens a new short pair
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 105, 102, 102.5), ohlc(102.5, 102.5, 97.5, 98)},
                                 trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Short);
}

TEST(NnfxTrailingTest, RejectsANonPositiveTrail)
{
    NnfxOptions o = options();
    o.trailAtr = 0.0;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), o), std::invalid_argument);
    o.trailAtr = -1.0;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), o), std::invalid_argument);
}
