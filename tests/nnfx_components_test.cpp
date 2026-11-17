#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "multi_backtester.hpp"
#include "nnfx_strategy.hpp"

// The optional parts of NnfxStrategy, one at a time. Stand-ins: FixedLine at 100 as the baseline, and components
// driven by the close:
//   ByClose   a State: +1 when the close is above `level`, -1 when below, 0 when equal
//   GateAt    a Gate: allows when the close is at or above `level`
// ATR period 1 (ATR = the bar's true range) unless a test says otherwise.
//
//   Confirmation    a baseline cross opens a pending signal for confirmationWindow bars (the cross bar counts as the
//                   first); the entry is taken on the first bar where the confirmation's state matches the direction
//                   and the other checks pass. A cross the other way cancels it.
//   Filter          the gate must allow at the moment of entry
//   One-candle rule the window of a plain signal is 2 bars instead of 1
//   Exit indicator  an open trade is closed (reason Indicator) when the state is the opposite of its direction
//   Continuation    after the trades are gone, with the price still on the same side as the last signal, enter again
//                   (not on the bar right after the trades ended)
//   Limits          maxOpenRisk (fraction of equity), maxPerCurrency (net exposure per currency, by pair name)

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

// a State that returns the scripted value for each bar in turn (the last one repeats)
class ScriptedState : public AnyState
{
public:
    explicit ScriptedState(std::vector<int> script) : script_(std::move(script)) {}
    void update(const Bar&) override { ++seen_; }
    bool ready() const override { return true; }
    int state() const override { return script_[std::min(seen_, script_.size()) - 1]; }
    std::size_t warmupBars() const override { return 1; }
    std::unique_ptr<AnyState> clone() const override { return std::make_unique<ScriptedState>(*this); }
private:
    std::vector<int> script_;
    std::size_t seen_{0};
};

class GateAt : public AnyGate
{
public:
    explicit GateAt(double level) : level_(level) {}
    void update(const Bar& b) override { close_ = b.close; }
    bool ready() const override { return true; }
    bool allows() const override { return close_ >= level_; }
    std::size_t warmupBars() const override { return 1; }
    std::unique_ptr<AnyGate> clone() const override { return std::make_unique<GateAt>(*this); }
private:
    double level_;
    double close_{0};
};

Bar ohlc(double o, double h, double l, double c)
{
    Bar b;
    b.open = o; b.high = h; b.low = l; b.close = c;
    return b;
}
Bar flat(double p) { return ohlc(p, p, p, p); }

AlignedData dataOf(const std::vector<std::string>& symbols, const std::vector<std::vector<Bar>>& perAsset)
{
    AlignedData d;
    d.symbols = symbols;
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

NnfxOptions base()
{
    NnfxOptions o;
    o.atrPeriod = 1;
    o.trailAtr = std::nullopt;       // keep the trades simple
    return o;
}

BacktestResult run(const std::vector<Bar>& bars, const NnfxOptions& o, double cash = 100000.0)
{
    NnfxStrategy<FixedLine> s(1, FixedLine(100), o);
    return runMultiBacktest(dataOf({"AAABBB"}, {bars}), s, cash);
}

std::shared_ptr<const AnyState> byClose(double level) { return std::make_shared<ByClose>(level); }

const Bar below = ohlc(98, 99, 97, 98);
const Bar crossUp = ohlc(98, 101, 98, 100.5);                 // true range 3, entry 100.5 (stop 96, target 103.5)
const Bar above2 = ohlc(100.5, 102, 100.5, 101.5);            // true range 1.5
const Bar above3 = ohlc(101.5, 102.5, 101.5, 102.4);
}

// ---------- confirmation ----------

TEST(NnfxConfirmationTest, AnAgreeingConfirmationAllowsTheEntryOnTheCrossBar)
{
    NnfxOptions o = base();
    o.components.confirmation = byClose(100);          // state +1 at 100.5

    const BacktestResult r = run({below, crossUp}, o);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].entryTime, 2);
}

TEST(NnfxConfirmationTest, ADisagreeingConfirmationDelaysTheEntryUntilItAgrees)
{
    NnfxOptions o = base();
    o.components.confirmation = byClose(101);          // -1 at 100.5, +1 at 101.5

    const BacktestResult r = run({below, crossUp, above2}, o);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].entryTime, 3);
    EXPECT_DOUBLE_EQ(r.openTrades[0].entryPrice, 101.5);
}

TEST(NnfxConfirmationTest, NoEntryIsTakenAfterTheWindowHasRunOut)
{
    // the cross bar counts as the first bar of the window: window 2 = the cross bar and one more
    NnfxOptions o = base();
    o.components.confirmation = byClose(102);          // agrees only at 102.4 (bar 4)
    o.maxDistanceAtr = std::nullopt;                   // 2.4 from the line with an ATR of 1: not what is tested here
    o.confirmationWindow = 2;
    EXPECT_TRUE(run({below, crossUp, above2, above3}, o).openTrades.empty());

    o.confirmationWindow = 3;                          // bars 2, 3, 4: agrees on the third
    // cross bar 2 (-1), bar 3 (-1), bar 4 (+1: 102.4 > 102)
    EXPECT_EQ(run({below, crossUp, above2, above3}, o).openTrades.size(), 2u);
}

TEST(NnfxConfirmationTest, ACrossBackCancelsThePendingSignal)
{
    NnfxOptions o = base();
    o.components.confirmation = byClose(101);          // never agrees with a long before the price drops
    // cross up (pending long), then back below the line: the short signal needs a -1 state, and -1 is exactly there
    const BacktestResult r = run({below, crossUp, ohlc(100.5, 100.8, 98, 98.5), ohlc(98.5, 99.5, 98.5, 99.4)}, o);

    // the short is confirmed on the cross-down bar (state -1 at 98.5); no long is ever opened
    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].direction, Direction::Short);
}

TEST(NnfxConfirmationTest, AShortNeedsABearishState)
{
    NnfxOptions o = base();
    o.components.confirmation = byClose(99);           // +1 at 99.5: bullish, so a short is not confirmed
    const std::vector<Bar> bars{ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5)};

    EXPECT_TRUE(run(bars, o).openTrades.empty());

    o.components.confirmation = byClose(100);          // -1 at 99.5
    EXPECT_EQ(run(bars, o).openTrades.size(), 2u);
}

TEST(NnfxConfirmationTest, ANeutralStateDoesNotConfirm)
{
    NnfxOptions o = base();
    o.components.confirmation = byClose(100.5);        // state 0 at exactly 100.5

    EXPECT_TRUE(run({below, crossUp}, o).openTrades.empty());
}

TEST(NnfxConfirmationTest, AssetsWaitForAConfirmationThatIsNotReadyYet)
{
    NnfxOptions o = base();
    o.components.confirmation = std::make_shared<ByClose>(100, 5);     // ready after 5 bars

    EXPECT_TRUE(run({below, crossUp}, o).openTrades.empty());
}

TEST(NnfxConfirmationTest, TheWindowAppliesToTheSizeAndStopOfTheDelayedEntry)
{
    // the delayed entry is sized from its own bar: entry 101.5, ATR = true range 1.5 -> stop distance 2.25
    NnfxOptions o = base();
    o.components.confirmation = byClose(101);

    const BacktestResult r = run({below, crossUp, above2}, o);

    EXPECT_NEAR(*r.openTrades[0].stopLoss, 101.5 - 2.25, 1e-9);
}

// ---------- filter ----------

TEST(NnfxFilterTest, AGateThatAllowsDoesNotChangeAnything)
{
    NnfxOptions o = base();
    o.components.filter = std::make_shared<GateAt>(0);

    EXPECT_EQ(run({below, crossUp}, o).openTrades.size(), 2u);
}

TEST(NnfxFilterTest, AGateThatBlocksStopsTheEntryAndAPlainSignalIsLost)
{
    NnfxOptions o = base();
    o.components.filter = std::make_shared<GateAt>(1000);

    EXPECT_TRUE(run({below, crossUp, above2}, o).openTrades.empty());
}

TEST(NnfxFilterTest, ABlockedSignalCanBeTakenWithinTheWindowOnceTheGateOpens)
{
    NnfxOptions o = base();
    o.components.filter = std::make_shared<GateAt>(101);          // closed at 100.5, open at 101.5
    o.oneCandleRule = true;

    const BacktestResult r = run({below, crossUp, above2}, o);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].entryTime, 3);
}

// ---------- one-candle rule ----------

namespace
{
// ATR period 2: calm, calm, then a close of 103.5 (3.5 from the line, ATR 3.25: too far), then 102 (ATR 2.375: 2 away)
std::vector<Bar> farThenNear()
{
    return {ohlc(98, 98.5, 97.5, 98), ohlc(98, 98.5, 97.5, 98), flat(103.5), flat(102)};
}

NnfxOptions period2()
{
    NnfxOptions o = base();
    o.atrPeriod = 2;
    return o;
}
}

TEST(NnfxOneCandleTest, ATooFarSignalCanBeTakenOnTheNextBarWhenPriceComesBack)
{
    NnfxOptions o = period2();
    o.oneCandleRule = true;

    const BacktestResult r = run(farThenNear(), o);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].entryTime, 4);
    EXPECT_DOUBLE_EQ(r.openTrades[0].entryPrice, 102.0);
}

TEST(NnfxOneCandleTest, WithoutTheRuleTheSignalIsLost)
{
    EXPECT_TRUE(run(farThenNear(), period2()).openTrades.empty());
}

TEST(NnfxOneCandleTest, OnlyOneExtraBar)
{
    NnfxOptions o = period2();
    o.oneCandleRule = true;
    // too far on the cross bar and on the next; the bar after that would pass (ATR 3.84, 2.0 from the line)
    std::vector<Bar> bars{ohlc(98, 98.5, 97.5, 98), ohlc(98, 98.5, 97.5, 98), flat(103.5), flat(103.4),
                          ohlc(103.4, 106, 100, 102.0)};

    EXPECT_TRUE(run(bars, o).openTrades.empty());
}

// ---------- exit indicator ----------

TEST(NnfxExitIndicatorTest, ATurnAgainstTheTradeClosesBothHalves)
{
    NnfxOptions o = base();
    o.components.exitIndicator = byClose(100.4);                  // +1 at 100.5, -1 at 100.3

    const BacktestResult r = run({below, crossUp, ohlc(100.5, 100.6, 100.2, 100.3)}, o);

    ASSERT_EQ(r.closedTrades.size(), 2u);
    for (const ClosedTrade& c : r.closedTrades)
    {
        EXPECT_EQ(c.reason, ExitReason::Indicator);
        EXPECT_DOUBLE_EQ(c.exitPrice, 100.3);
    }
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxExitIndicatorTest, ASameDirectionOrNeutralStateDoesNotClose)
{
    NnfxOptions o = base();
    o.components.exitIndicator = byClose(100.4);
    EXPECT_EQ(run({below, crossUp, above2}, o).openTrades.size(), 2u);             // +1 still

    o.components.exitIndicator = byClose(100.3);
    EXPECT_EQ(run({below, crossUp, ohlc(100.5, 100.6, 100.2, 100.3)}, o).openTrades.size(), 2u);   // 0: neutral
}

TEST(NnfxExitIndicatorTest, AShortIsClosedByABullishState)
{
    NnfxOptions o = base();
    o.components.exitIndicator = byClose(99.0);                   // -1 below 99, +1 above
    // short from 98.8 ... build: above the line, then a cross down to 98.8 (state -1), then back to 99.4 (+1)
    const std::vector<Bar> bars{ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 98.8), ohlc(98.8, 99.5, 98.7, 99.4)};

    const BacktestResult r = run(bars, o);

    ASSERT_EQ(r.closedTrades.size(), 2u);
    EXPECT_EQ(r.closedTrades[0].reason, ExitReason::Indicator);
}

TEST(NnfxExitIndicatorTest, WithoutTradesItDoesNothing)
{
    NnfxOptions o = base();
    o.components.exitIndicator = byClose(1000);

    EXPECT_TRUE(run({below, below, below}, o).closedTrades.empty());
}

// ---------- continuation ----------

namespace
{
// long from 100.5 (stop 96), stopped out on bar 3 while the close stays above the line; then calm bars above it
std::vector<Bar> stoppedThenCalm()
{
    return {below, crossUp, ohlc(100.4, 101, 95, 100.2), ohlc(100.2, 100.8, 100.0, 100.5), ohlc(100.5, 100.9, 100.2, 100.6)};
}
}

TEST(NnfxContinuationTest, ReEntersLaterWhilePriceStaysOnTheSameSideOfTheLine)
{
    NnfxOptions o = base();
    o.continuation = true;

    const BacktestResult r = run(stoppedThenCalm(), o);

    ASSERT_EQ(r.closedTrades.size(), 2u);                         // the stopped pair
    ASSERT_EQ(r.openTrades.size(), 2u);                           // the continuation pair
    EXPECT_EQ(r.openTrades[0].direction, Direction::Long);
    EXPECT_EQ(r.openTrades[0].entryTime, 5);                      // not on bar 3 (stopped) or bar 4 (the bar after)
}

TEST(NnfxContinuationTest, IsOffByDefault)
{
    EXPECT_TRUE(run(stoppedThenCalm(), base()).openTrades.empty());
}

TEST(NnfxContinuationTest, NeedsAPreviousSignal)
{
    NnfxOptions o = base();
    o.continuation = true;

    EXPECT_TRUE(run({below, below, below, below}, o).openTrades.empty());
}

TEST(NnfxContinuationTest, IsForgottenWhenThePriceCrossesBack)
{
    NnfxOptions o = base();
    o.continuation = true;
    o.components.confirmation = byClose(50);                      // always +1: agrees with longs only
    // long, stopped out, the price then crosses below the line (a cross-down: the short needs a -1 state: never),
    // then back above without a new cross-up being a "continuation" of the old long
    const std::vector<Bar> bars{below, crossUp, ohlc(100.4, 101, 95, 100.2), ohlc(100.2, 100.3, 99, 99.5),
                                ohlc(99.5, 99.9, 99.2, 99.6), ohlc(99.6, 99.8, 99.3, 99.4)};

    EXPECT_TRUE(run(bars, o).openTrades.empty());
}

TEST(NnfxContinuationTest, ACrossForgetsTheLastSignalSoAFailedNewSignalIsNotRevivedLater)
{
    NnfxOptions o = base();
    o.continuation = true;
    o.confirmationWindow = 1;
    // bar 1 long confirmed (+1); bar 2 stopped; bar 3 cross down and bar 4 cross up are both unconfirmed (0);
    // bars 5 and 6 stay above the line and the state is +1 on bar 6: only a stale "last signal" could re-enter
    o.components.confirmation = std::make_shared<ScriptedState>(std::vector<int>{0, 1, 1, 0, 0, 0, 1});
    const std::vector<Bar> bars{below, crossUp, ohlc(100.4, 101, 95, 100.2), ohlc(100.2, 100.3, 98, 98.5),
                                ohlc(98.5, 101, 98.5, 100.5), ohlc(100.5, 100.9, 100.2, 100.6),
                                ohlc(100.6, 101.0, 100.2, 100.65)};

    const BacktestResult r = run(bars, o);

    EXPECT_EQ(r.closedTrades.size(), 2u);              // only the first, stopped pair
    EXPECT_TRUE(r.openTrades.empty());
}

TEST(NnfxContinuationTest, UsesTheConfirmationAndTheFilter)
{
    NnfxOptions o = base();
    o.continuation = true;
    o.components.confirmation = byClose(101);                     // the first entry needs +1: 100.5 is -1, so wait
    // bars: cross at 100.5 (no confirm), bar 3 close 101.5 (confirm: enter), stopped later ... only check the gate here
    NnfxOptions gated = base();
    gated.continuation = true;
    gated.components.filter = std::make_shared<GateAt>(1000);

    EXPECT_TRUE(run(stoppedThenCalm(), gated).openTrades.empty());
}

TEST(NnfxContinuationTest, TheChaseLimitStillApplies)
{
    NnfxOptions o = base();
    o.continuation = true;
    o.maxDistanceAtr = 0.1;                                       // 0.5 from the line with ATR 0.8: beyond 0.08

    EXPECT_TRUE(run(stoppedThenCalm(), o).openTrades.empty());
}

// ---------- portfolio limits ----------

namespace
{
// two pairs crossing up on the same bar
BacktestResult runTwo(const std::vector<std::string>& symbols, const NnfxOptions& o,
                      std::vector<Bar> first, std::vector<Bar> second)
{
    NnfxStrategy<FixedLine> s(2, FixedLine(100), o);
    return runMultiBacktest(dataOf(symbols, {first, second}), s, 100000.0);
}
}

TEST(NnfxLimitsTest, WithoutLimitsBothSignalsAreTaken)
{
    const BacktestResult r = runTwo({"EURUSD", "GBPUSD"}, base(), {below, crossUp}, {below, crossUp});

    EXPECT_EQ(r.openTrades.size(), 4u);
}

TEST(NnfxLimitsTest, TheOpenRiskCapBlocksTheSignalThatWouldExceedIt)
{
    NnfxOptions o = base();
    o.maxOpenRisk = 0.03;                                         // a signal risks 2%: one fits, two (4%) do not

    const BacktestResult r = runTwo({"EURUSD", "GBPUSD"}, o, {below, crossUp}, {below, crossUp});

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].asset, 0);                          // the first pair, in order
}

TEST(NnfxLimitsTest, ACapAboveTheTotalAllowsBoth)
{
    NnfxOptions o = base();
    o.maxOpenRisk = 0.04;

    EXPECT_EQ(runTwo({"EURUSD", "GBPUSD"}, o, {below, crossUp}, {below, crossUp}).openTrades.size(), 4u);
}

TEST(NnfxLimitsTest, RiskIsFreedWhenTradesClose)
{
    NnfxOptions o = base();
    o.maxOpenRisk = 0.03;
    // asset 0 is stopped out on bar 3; asset 1's signal on bar 4 then fits
    const std::vector<Bar> first{below, crossUp, ohlc(100.4, 101, 95, 100.2), ohlc(100.2, 100.4, 100.1, 100.3), ohlc(100.3, 100.4, 100.1, 100.2)};
    const std::vector<Bar> second{below, below, below, below, crossUp};

    const BacktestResult r = runTwo({"EURUSD", "GBPUSD"}, o, first, second);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].asset, 1);
}

TEST(NnfxLimitsTest, RiskFreedByAnExitInTheSameSnapshotIsAvailableToTheNextAsset)
{
    NnfxOptions o = base();
    o.maxOpenRisk = 0.03;
    o.components.exitIndicator = byClose(100.4);        // asset 0 is closed on bar 3 (100.3 < 100.4)
    // asset 0: long on bar 2, exit signal on bar 3; asset 1: crosses up on bar 3, in the same snapshot
    const std::vector<Bar> first{below, crossUp, ohlc(100.5, 100.6, 100.2, 100.3)};
    const std::vector<Bar> second{below, below, crossUp};

    const BacktestResult r = runTwo({"EURUSD", "GBPUSD"}, o, first, second);

    ASSERT_EQ(r.closedTrades.size(), 2u);
    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].asset, 1);
}

TEST(NnfxLimitsTest, TheCurrencyCapBlocksASecondLongOnTheSameCurrencyLeg)
{
    NnfxOptions o = base();
    o.maxPerCurrency = 1;           // two longs on EURUSD and GBPUSD are both short USD: USD exposure -2

    const BacktestResult r = runTwo({"EURUSD", "GBPUSD"}, o, {below, crossUp}, {below, crossUp});

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].asset, 0);
}

TEST(NnfxLimitsTest, ExposureFreedByAnExitInTheSameSnapshotIsAvailableToTheNextAsset)
{
    NnfxOptions o = base();
    o.maxPerCurrency = 1;
    o.components.exitIndicator = byClose(100.4);
    // EURUSD long is closed on bar 3; GBPUSD (also short USD) crosses up on that same bar
    const std::vector<Bar> first{below, crossUp, ohlc(100.5, 100.6, 100.2, 100.3)};
    const std::vector<Bar> second{below, below, crossUp};

    const BacktestResult r = runTwo({"EURUSD", "GBPUSD"}, o, first, second);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].asset, 1);
}

TEST(NnfxLimitsTest, OppositeDirectionsOnTheSameCurrencyOffsetEachOther)
{
    NnfxOptions o = base();
    o.maxPerCurrency = 1;
    // EURUSD long (short USD) and GBPUSD short (long USD): USD nets to zero
    const std::vector<Bar> up{below, crossUp};
    const std::vector<Bar> down{ohlc(102, 103, 101, 102), ohlc(102, 102, 97, 99.5)};

    EXPECT_EQ(runTwo({"EURUSD", "GBPUSD"}, o, up, down).openTrades.size(), 4u);
}

TEST(NnfxLimitsTest, TheBaseCurrencyIsCountedToo)
{
    NnfxOptions o = base();
    o.maxPerCurrency = 1;           // EURUSD long and EURGBP long are both long EUR: EUR exposure +2

    EXPECT_EQ(runTwo({"EURUSD", "EURGBP"}, o, {below, crossUp}, {below, crossUp}).openTrades.size(), 2u);
}

TEST(NnfxLimitsTest, UnrelatedPairsAreNotLimited)
{
    NnfxOptions o = base();
    o.maxPerCurrency = 1;

    EXPECT_EQ(runTwo({"EURUSD", "AUDNZD"}, o, {below, crossUp}, {below, crossUp}).openTrades.size(), 4u);
}

TEST(NnfxLimitsTest, ACapOfTwoAllowsTwoOnTheSameCurrency)
{
    NnfxOptions o = base();
    o.maxPerCurrency = 2;

    EXPECT_EQ(runTwo({"EURUSD", "GBPUSD"}, o, {below, crossUp}, {below, crossUp}).openTrades.size(), 4u);
}

TEST(NnfxLimitsTest, PairsWithUnknownNamesAreNotLimitedByCurrency)
{
    NnfxOptions o = base();
    o.maxPerCurrency = 1;

    EXPECT_EQ(runTwo({"A0", "A1"}, o, {below, crossUp}, {below, crossUp}).openTrades.size(), 4u);
}

// ---------- setup ----------

TEST(NnfxComponentsSetupTest, RejectsBadValues)
{
    NnfxOptions o = base();
    o.confirmationWindow = 0;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), o), std::invalid_argument);

    o = base();
    o.maxOpenRisk = 0.0;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), o), std::invalid_argument);

    o = base();
    o.maxPerCurrency = 0;
    EXPECT_THROW(NnfxStrategy<FixedLine>(1, FixedLine(100), o), std::invalid_argument);
}

TEST(NnfxComponentsSetupTest, DefaultsChangeNothing)
{
    const NnfxOptions o;
    EXPECT_EQ(o.confirmationWindow, 7);
    EXPECT_FALSE(o.oneCandleRule);
    EXPECT_FALSE(o.continuation);
    EXPECT_FALSE(o.maxOpenRisk.has_value());
    EXPECT_FALSE(o.maxPerCurrency.has_value());
    EXPECT_EQ(o.components.confirmation, nullptr);
    EXPECT_EQ(o.components.exitIndicator, nullptr);
    EXPECT_EQ(o.components.filter, nullptr);
}

TEST(NnfxComponentsSetupTest, EachAssetGetsItsOwnCopyOfTheComponent)
{
    // the components keep per-asset state: asset 1's rising closes must not leak into asset 0's confirmation
    NnfxOptions o = base();
    o.components.confirmation = byClose(100);
    NnfxStrategy<FixedLine> s(2, FixedLine(100), o);

    const BacktestResult r = runMultiBacktest(dataOf({"EURUSD", "GBPUSD"}, {{below, crossUp}, {below, ohlc(98, 98.5, 97, 97.5)}}), s, 100000.0);

    ASSERT_EQ(r.openTrades.size(), 2u);
    EXPECT_EQ(r.openTrades[0].asset, 0);
}
