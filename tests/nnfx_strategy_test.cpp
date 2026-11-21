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
//   Breakeven once the take-profit half has been hit, the runner's stop is moved to its entry price (a real stop order:
//             it fills at that price during a later bar, or at the open if the bar gaps through it)
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
//
// Once the take-profit half is gone the runner's stop is MOVED to its entry price. That is a stop order like the
// first one: it fills at that price (at the open if the bar gaps through it), not at a later close.

TEST(NnfxStrategyTest, AfterTheTargetTheRunnersStopMovesToItsEntry)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103)});

    ASSERT_EQ(r.closedTrades.size(), 1u);                 // the target half
    ASSERT_EQ(r.openTrades.size(), 1u);                   // the runner
    EXPECT_DOUBLE_EQ(*r.openTrades[0].stopLoss, 100.5);
    EXPECT_EQ(r.openTrades[0].stopReason, ExitReason::Breakeven);
    EXPECT_DOUBLE_EQ(*r.openTrades[0].initialStop, 96.0);
}

TEST(NnfxStrategyTest, TheRunnerIsStoppedAtItsEntryPriceNotAtALaterClose)
{
    // bar 3 trades down to 100 and closes at 100.4: the stop at 100.5 was hit on the way, and fills at 100.5
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 103, 100, 100.4)});

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Breakeven);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 100.5);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxStrategyTest, TheBreakevenStopAlsoFiresWhenTheLowIsExactlyTheEntryPrice)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 103, 100.5, 100.5)});

    EXPECT_EQ(r.closedTrades.size(), 2u);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxStrategyTest, AGapThroughTheBreakevenStopFillsAtTheOpen)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(99, 99.5, 98, 98.5)});

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Breakeven);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 99.0);
}

TEST(NnfxStrategyTest, TheTargetBarClosingBelowTheEntryClosesTheRunnerAtThatCloseAsBreakeven)
{
    // the target is hit and the bar then closes at 100.3, under the entry 100.5: a stop at 100.5 would be above the
    // price, so it cannot be placed - the runner is closed at the close instead
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.2, 100.3)});

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Breakeven);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 100.3);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxStrategyTest, BeforeTheTargetThereIsNoBreakevenStop)
{
    // close back below the entry (100.5) but above the line and the stop; the target was never hit
    const BacktestResult r = run({below, crossUp, ohlc(100.5, 101, 100.1, 100.3)});

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_TRUE(r.closedTrades.empty());
    for (const Trade& t : r.openTrades)
        EXPECT_DOUBLE_EQ(*t.stopLoss, 96.0);
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

    // no breakeven stop: the runner is only protected by its first stop and the trail
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 103, 100.2, 100.4)}, o);

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NE(*r.openTrades[0].stopLoss, 100.5);
}

TEST(NnfxStrategyTest, ABreakevenStopForAShortMirrorsTheLong)
{
    // short from 99.5 (stop 107, target 94.5); the target is hit (the stop moves down to 99.5), then a bar rises to 99.6
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5), ohlc(99.4, 99.5, 94, 95),
                                  ohlc(95, 99.6, 95, 99.6)});

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Breakeven);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 99.5);
}

TEST(NnfxStrategyTest, ABreakevenStopOnAShortIsNotPlacedBelowThePrice)
{
    // short from 99.5; the target bar closes at 99.7, above the entry: a buy-stop at 99.5 would be under the price
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5), ohlc(99.4, 99.8, 94, 99.7)});

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Breakeven);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 99.7);
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

TEST(NnfxRandomTest, WithTheCrossBackExitATradeClosesAtTheNextCrossWhicheverWayItWent)
{
    // bar 1 crosses up and opens a pair in a random direction; bar 2 crosses down (close 99.5), inside both stops (96 and 105).
    // Whichever way the pair went, it is closed at that close by the cross, and a new random pair is entered there.
    bool sawLong = false, sawShort = false;
    for (std::uint64_t seed = 1; seed <= 30; ++seed)
    {
        NnfxOptions o = options();
        o.randomSeed = seed;                          // exitOnCross stays on
        const BacktestResult r = run({below, crossUp, ohlc(100.5, 100.8, 99, 99.5)}, o);

        ASSERT_EQ(r.closedTrades.size(), 2u) << seed;
        for (const ClosedTrade& c : r.closedTrades)
        {
            EXPECT_EQ(c.reason, ExitReason::Signal) << seed;
            EXPECT_DOUBLE_EQ(c.exitPrice, 99.5) << seed;
            EXPECT_EQ(c.trade.direction, r.closedTrades[0].trade.direction) << seed;
        }
        EXPECT_EQ(r.openTrades.size(), 2u) << seed;
        (r.closedTrades[0].trade.direction == Direction::Long ? sawLong : sawShort) = true;
    }
    EXPECT_TRUE(sawLong);
    EXPECT_TRUE(sawShort);
}

TEST(NnfxRandomTest, WithTheCrossBackExitATradeStaysWhileThereIsNoCross)
{
    NnfxOptions o = options();
    o.randomSeed = 4;
    const BacktestResult r = run({below, crossUp, ohlc(100.5, 101, 100.2, 100.8)}, o);

    EXPECT_TRUE(r.closedTrades.empty());
    EXPECT_EQ(r.openTrades.size(), 2u);
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

// ---------- trailing stop for the runner ----------
//
// Once the target half has been hit, the runner's stop follows the best close since entry: it is moved to
// best close - trailAtr * ATR (the current ATR, a short: best close + ...) after every close, and never moves back.
// With the breakeven stop on, the stop is the better of the entry price and the trail; the reason recorded for a hit
// is Breakeven while the entry price is the better one (a tie counts as Breakeven) and Trailing once the trail is.
// Both are real stop orders: they fill at the stop price during a later bar, or at the open if it gaps through it.
// A level that is not on the right side of the close cannot be placed, so the runner is closed at the close instead.
// Off with nullopt.

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

TEST(NnfxTrailingTest, TheStopFollowsTheBestCloseByTheCurrentAtr)
{
    // after the target bar: true range 3.6, trail 0.5 * 3.6 = 1.8 below the close 103 = 101.2, better than the entry 100.5
    // after 105 (true range 3.5): 105 - 1.75 = 103.25
    const BacktestResult afterTarget = run({below, crossUp, targetHit}, trailing(0.5));
    ASSERT_EQ(afterTarget.openTrades.size(), 1u);
    EXPECT_NEAR(*afterTarget.openTrades[0].stopLoss, 101.2, 1e-9);
    EXPECT_EQ(afterTarget.openTrades[0].stopReason, ExitReason::Trailing);

    const BacktestResult afterRun = run({below, crossUp, targetHit, runsUp}, trailing(0.5));
    ASSERT_EQ(afterRun.openTrades.size(), 1u);
    EXPECT_NEAR(*afterRun.openTrades[0].stopLoss, 103.25, 1e-9);
}

TEST(NnfxTrailingTest, TheRunnerIsStoppedOutAtTheTrailingStop)
{
    // the next bar trades down to 102, through the stop at 103.25: it fills there, not at the close of 102.5
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 105, 102, 102.5)}, trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Trailing);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 103.25);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxTrailingTest, AGapThroughTheTrailingStopFillsAtTheOpen)
{
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(102, 102.5, 101.5, 102)}, trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Trailing);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 102.0);
}

TEST(NnfxTrailingTest, TheRunnerStaysWhileTheLowStaysAboveTheStop)
{
    // low 103.5 stays above the stop 103.25 and the close 104.8 is above the new level 105 - 0.5 * 2 (true range 2) = 104:
    // the runner stays and its stop rises to 104
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 105.5, 103.5, 104.8)}, trailing(0.5));

    EXPECT_EQ(r.closedTrades.size(), 1u);
    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 104.0, 1e-9);
}

TEST(NnfxTrailingTest, TheStopNeverMovesBack)
{
    // a wide bar (true range 5) would put the raw trail at 105 - 2.5 = 102.5, below the stop 103.25: it stays at 103.25
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 109, 104, 105)}, trailing(0.5));

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 103.25, 1e-9);
}

TEST(NnfxTrailingTest, TheBestCloseKeepsRisingAndTheStopFollowsIt)
{
    // closes 103, 105, 107: the stop is 107 - 0.5 * 2 (true range 2) = 106; the next bar trades down to 105.9 and is
    // stopped at 106 (from the earlier best close of 105 the stop would have been far lower and not hit)
    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 107, 105, 107), ohlc(107, 107, 105.9, 105.9)},
                                 trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Trailing);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 106.0);
}

TEST(NnfxTrailingTest, NotBeforeTheTargetHasBeenHit)
{
    // the target 103.5 is not reached (high 103); the close then falls far from the best close, but nothing is moved
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 103, 100.4, 102.9), ohlc(102.9, 103, 101, 101.2)},
                                 trailing(0.5));

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_TRUE(r.closedTrades.empty());
    for (const Trade& t : r.openTrades)
        EXPECT_DOUBLE_EQ(*t.stopLoss, 96.0);
}

TEST(NnfxTrailingTest, CanBeSwitchedOff)
{
    NnfxOptions o = options();
    o.trailAtr = std::nullopt;

    const BacktestResult r = run({below, crossUp, targetHit, runsUp, ohlc(105, 105, 102, 102.5)}, o);

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(*r.openTrades[0].stopLoss, 100.5);       // only the breakeven stop
}

TEST(NnfxTrailingTest, AShortRunnerTrailsAboveTheLowestClose)
{
    // short from 99.5 (stop 107, target 94.5); the target is hit (close 95): the stop becomes 95 + 0.5 * 5.5 = 97.75,
    // then the close runs down to 93 (true range 2): 93 + 1 = 94; the bounce bar's high 94.4 reaches it: filled at 94
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5), ohlc(99.4, 99.5, 94, 95),
                                  ohlc(95, 95, 93, 93), ohlc(93, 94.4, 93, 94.4)}, trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Trailing);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 94.0);
}

TEST(NnfxTrailingTest, WhileTheEntryPriceIsTheBetterStopTheReasonIsBreakeven)
{
    // with the default trail (1.5 * 3.6 = 5.4 below 103) the trail is below the entry: the stop is the entry price
    const BacktestResult r = run({below, crossUp, targetHit}, options());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(*r.openTrades[0].stopLoss, 100.5);
    EXPECT_EQ(r.openTrades[0].stopReason, ExitReason::Breakeven);
}

TEST(NnfxTrailingTest, ATieBetweenTheTrailAndTheEntryCountsAsBreakeven)
{
    // target bar (100.5, 104.5, 100.5, 102.5): true range 4, trail 0.5 * 4 = 2 below the close 102.5 = 100.5 = the entry
    const BacktestResult r = run({below, crossUp, ohlc(100.5, 104.5, 100.5, 102.5)}, trailing(0.5));

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(*r.openTrades[0].stopLoss, 100.5);
    EXPECT_EQ(r.openTrades[0].stopReason, ExitReason::Breakeven);
}

TEST(NnfxTrailingTest, ATrailBelowTheEntryDoesNotLoosenTheBreakevenStop)
{
    // the trail alone would put the stop at 98.75; with breakeven on the entry price is used
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 104, 102, 102.5)}, trailing(2.0));

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_GE(*r.openTrades[0].stopLoss, 100.5);
}

TEST(NnfxTrailingTest, ATrailLevelNotBelowTheCloseClosesTheRunnerAtTheClose)
{
    // the close 103.4 is the best so far (the target 103.5 is not touched); the next bar spikes to 104, hits the
    // target, and closes at 100.9. True range 3.3, trail 0.5 * 3.3 = 1.65 -> a stop at 103.4 - 1.65 = 101.75 would be
    // above the close, so it cannot be placed: the runner is closed at 100.9 as a trailing exit
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 103.4, 100.4, 103.4), ohlc(103.4, 104, 100.7, 100.9)},
                                 trailing(0.5));

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::TakeProfit);
    EXPECT_EQ(r.closedTrades[1].reason, ExitReason::Trailing);
    EXPECT_DOUBLE_EQ(r.closedTrades[1].exitPrice, 100.9);
    EXPECT_TRUE(r.openTrades.empty());
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
    // after the trailing stop (price still above the line) a later cross down opens a new short pair
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

// ---------- no scaling out ----------
//
// entry.scaleOut == false: each signal opens ONE trade with both halves' risk (2%), the same stop, no take-profit. There is
// no target to hit, so the breakeven stop is not used (it would cancel the stop at once); the trailing stop starts on the
// bar after entry instead of after the target, and, as always, never loosens the stop.

namespace
{
NnfxOptions single(double trailAtrs = 1.5)
{
    NnfxOptions o = options();
    o.entry.scaleOut = false;
    o.trailAtr = trailAtrs;
    return o;
}
}

TEST(NnfxNoScalingTest, ACrossUpOpensOneLongWithTheWholeRiskAndNoTarget)
{
    const BacktestResult r = run({below, crossUp}, single());

    ASSERT_EQ(r.openTrades.size(), 1u);
    const Trade& t = r.openTrades[0];
    EXPECT_EQ(t.direction, Direction::Long);
    EXPECT_DOUBLE_EQ(t.entryPrice, 100.5);
    EXPECT_DOUBLE_EQ(*t.stopLoss, 96.0);
    EXPECT_FALSE(t.takeProfit.has_value());
    EXPECT_NEAR(t.quantity, 2.0 * kHalf, 1e-9);
}

TEST(NnfxNoScalingTest, ACrossDownOpensOneShort)
{
    // prev close 102, true range 5 on the cross bar: entry 99.5, stop 99.5 + 7.5 = 107
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5)}, single());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Short);
    EXPECT_DOUBLE_EQ(*r.openTrades[0].stopLoss, 107.0);
    EXPECT_NEAR(r.openTrades[0].quantity, 2.0 * 1000.0 / 7.5, 1e-9);
}

TEST(NnfxNoScalingTest, AStopLossStillLosesTwoPercent)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.4, 101, 95, 100.2)}, single());

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::StopLoss);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 96.0);
    EXPECT_NEAR(r.finalEquity, 100000.0 - 2000.0, 1e-6);
}

TEST(NnfxNoScalingTest, TheTrailStartsAtOnceAndIsNotBreakeven)
{
    // bar 2: close 100.7, true range 0.8, trail 1.5 * 0.8 = 1.2 below the best close: 99.5. Breakeven (the entry 100.5)
    // would be a higher stop but is not used.
    const BacktestResult r = run({below, crossUp, ohlc(100.5, 101, 100.2, 100.7)}, single());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 99.5, 1e-9);
    EXPECT_EQ(r.openTrades[0].stopReason, ExitReason::Trailing);
}

TEST(NnfxNoScalingTest, TheStopFollowsTheBestCloseAndFillsAtTheStopPrice)
{
    // closes 103 then 105: the stop is 105 - 0.5 * 3.5 = 103.25; the next bar trades down to 102 and fills at 103.25
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 106, 102.5, 105),
                                  ohlc(105, 105, 102, 102.5)}, single(0.5));

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::Trailing);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 103.25);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxNoScalingTest, TheStopNeverMovesBack)
{
    // after the 105 close the stop is 103.25; a wide bar (true range 5) would put the raw trail at 102.5: stays 103.25
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 106, 102.5, 105),
                                  ohlc(105, 109, 104, 105)}, single(0.5));

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 103.25, 1e-9);
}

TEST(NnfxNoScalingTest, AShortTrailsAboveTheLowestClose)
{
    // short from 99.5; bar 2 closes 95 with true range 5.5: the stop is 95 + 0.5 * 5.5 = 97.75
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5), ohlc(99.4, 99.5, 94, 95)},
                                 single(0.5));

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 97.75, 1e-9);
    EXPECT_EQ(r.openTrades[0].stopReason, ExitReason::Trailing);
}

TEST(NnfxNoScalingTest, WithoutATrailTheStopStaysWhereItStarted)
{
    NnfxOptions o = single();
    o.trailAtr = std::nullopt;
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 104, 100.4, 103), ohlc(103, 106, 102.5, 105)}, o);

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(*r.openTrades[0].stopLoss, 96.0);
}

TEST(NnfxNoScalingTest, ANewSignalIsTakenOnceTheTradeIsGone)
{
    // stopped out, then the price crosses down and a short opens
    NnfxOptions o = single();
    o.exitOnCross = false;
    const BacktestResult r = run({below, crossUp, ohlc(100.4, 101, 95, 99), ohlc(99, 99.2, 98.5, 98.8),
                                  ohlc(98.8, 99.5, 97.5, 98)}, o);

    // the long was stopped at 96 on bar 2; bar 2 closed at 99 (below the line), so that bar is the cross down
    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].trade.direction, Direction::Long);
    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Short);
}

TEST(NnfxNoScalingTest, TheRandomDirectionControlAlsoOpensOneTradePerSignal)
{
    NnfxOptions o = randomOptions(5);
    o.entry.scaleOut = false;
    const BacktestResult r = run(manyCrosses(), o);

    // two signals per cycle of manyCrosses, so the number of trades equals the number of signals (not twice)
    EXPECT_EQ(r.closedTrades.size(), 240u);
    for (const ClosedTrade& c : r.closedTrades)
        EXPECT_FALSE(c.trade.takeProfit.has_value());
}

// ---------- trail only after a profit (trailAfterAtr) ----------
//
// The trader's rule: one trade, the initial stop stays where it is until a CLOSE is trailAfterAtr x the ATR AT ENTRY in profit;
// from then on the stop trails trailAtr x the ATR AT ENTRY behind the best close (it never loosens). No breakeven stop.
// Entry 100.5 with ATR 3: stop 96; with trailAfter 2 the trail starts at a close of 106.5 and sits 1.5 * 3 = 4.5 behind it.

namespace
{
NnfxOptions trailAfter(double atrs = 2.0)
{
    NnfxOptions o = options();
    o.entry.scaleOut = false;
    o.breakeven = false;
    o.trailAfterAtr = atrs;
    return o;
}
}

TEST(NnfxTrailAfterTest, TheStopStaysUntilACloseIsFarEnoughInProfit)
{
    // the high reaches 110 but the close (105) is short of 106.5: nothing moves, and there is no breakeven stop
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 110, 100.4, 105)}, trailAfter());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(*r.openTrades[0].stopLoss, 96.0);
}

TEST(NnfxTrailAfterTest, ACloseTwoAtrInProfitStartsTheTrailAtEntryAtrTimesTheTrailDistance)
{
    // close 107 >= 106.5: stop = 107 - 1.5 * 3 = 102.5
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 108, 100.4, 107)}, trailAfter());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 102.5, 1e-9);
    EXPECT_EQ(r.openTrades[0].stopReason, ExitReason::Trailing);
}

TEST(NnfxTrailAfterTest, ACloseExactlyAtTheThresholdCounts)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 107, 100.4, 106.5)}, trailAfter());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 102.0, 1e-9);
}

TEST(NnfxTrailAfterTest, TheTrailUsesTheAtrAtEntryNotTheCurrentOne)
{
    // the next bar has true range 5.2 (the current ATR) but the stop is 111 - 4.5 = 106.5, not 111 - 1.5 * 5.2 = 103.2
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 108, 100.4, 107), ohlc(107, 112, 106.8, 111)}, trailAfter());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 106.5, 1e-9);
}

TEST(NnfxTrailAfterTest, TheStopFillsAtItsPrice)
{
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 108, 100.4, 107), ohlc(107, 107.2, 102, 102.4)}, trailAfter());

    ASSERT_EQ(r.closedTrades.size(), 1u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::Trailing);
    EXPECT_DOUBLE_EQ(r.closedTrades[0].exitPrice, 102.5);
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxTrailAfterTest, TheStopNeverMovesBackAndTheTrailStaysOnOnceStarted)
{
    // best close 111 gives 106.5; a lower close (110) keeps the stop at 106.5 (the trail stays on even though 110 < 111)
    const BacktestResult r = run({below, crossUp, ohlc(100.6, 108, 100.4, 107), ohlc(107, 112, 106.8, 111),
                                  ohlc(111, 111.5, 108, 110)}, trailAfter());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 106.5, 1e-9);
}

TEST(NnfxTrailAfterTest, AShortMirrorsTheLong)
{
    // short from 99.5 with ATR 5 (stop 107): the trail starts at a close <= 99.5 - 10 = 89.5; close 89: stop = 89 + 7.5 = 96.5
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5), ohlc(99.4, 99.5, 88, 89)}, trailAfter());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 96.5, 1e-9);
    EXPECT_EQ(r.openTrades[0].stopReason, ExitReason::Trailing);
}

TEST(NnfxTrailAfterTest, AShortThatHasNotMovedFarEnoughKeepsItsStop)
{
    const BacktestResult r = run({ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5), ohlc(99.4, 99.5, 88, 90)}, trailAfter());

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_DOUBLE_EQ(*r.openTrades[0].stopLoss, 107.0);
}

TEST(NnfxTrailAfterTest, EachTradeUsesItsOwnEntryAtr)
{
    // first trade: ATR 3, stopped out at 96. Second signal (cross up again) with a different ATR must use its own.
    NnfxOptions o = trailAfter();
    o.exitOnCross = false;
    // bar 2 stops the long (low 95) and closes at 99: cross down opens a short with ATR 6 (stop 99 + 9 = 108, trail starts at <= 87)
    const BacktestResult r = run({below, crossUp, ohlc(100.4, 101, 95, 99), ohlc(99, 99.5, 85, 86)}, o);

    ASSERT_EQ(r.openTrades.size(), 1u);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Short);
    EXPECT_NEAR(*r.openTrades[0].stopLoss, 86 + 1.5 * 6, 1e-9);
}

TEST(NnfxTrailAfterTest, NeedsASingleTradeAndATrailAndAPositiveThreshold)
{
    NnfxOptions scaled = options();
    scaled.trailAfterAtr = 2.0;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), scaled), std::invalid_argument);

    NnfxOptions noTrail = trailAfter();
    noTrail.trailAtr = std::nullopt;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), noTrail), std::invalid_argument);

    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), trailAfter(0.0)), std::invalid_argument);
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), trailAfter(-1.0)), std::invalid_argument);
}

TEST(NnfxTrailAfterTest, IsOffByDefault)
{
    EXPECT_FALSE(NnfxOptions{}.trailAfterAtr.has_value());
}
