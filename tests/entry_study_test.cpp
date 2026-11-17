#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <random>

#include "component_control.hpp"
#include "entry_study.hpp"

// The entry study asks, before any stop, target or trade: when the baseline crosses (and the don't-chase filter lets
// the signal through), where does price go over the next 1, 2, 3, 5, 10, 20 and 40 bars, in ATR units, in the
// signal's direction - and is that better than the same signals with a coin-flip direction?
//
//   signal   exactly the NnfxStrategy one: the close crosses the line (the first bar after warm-up never signals),
//            the line and the ATR are ready, ATR > 0, and |close - line| <= maxDistanceAtr * ATR
//   forward  (close `h` bars later - close of the signal bar) / ATR of the signal bar, times the direction; only bars
//            the asset actually has count as bars
//   race     from the signal close: does the target (targetAtr * ATR) or the stop (stopAtr * ATR) come first, over the
//            next 40 bars, stop first if one bar reaches both
//   excursion best (MFE) and worst (MAE) move in the signal's favour / against it over the next 20 bars, in ATR
//
// Stand-in baseline: a line fixed at 100. ATR period 1 makes the ATR the bar's true range.

namespace
{
class FixedLine : public AnyLine
{
public:
    void update(const Bar&) override {}
    bool ready() const override { return true; }
    double value() const override { return 100.0; }
    std::unique_ptr<AnyLine> clone() const override { return std::make_unique<FixedLine>(*this); }
};

// a line that returns the given values, one per bar (the last one repeats)
class ScriptedLine : public AnyLine
{
public:
    explicit ScriptedLine(std::vector<double> values) : values_(std::move(values)) {}
    void update(const Bar&) override { ++seen_; }
    bool ready() const override { return seen_ > 0; }
    double value() const override { return values_[std::min(seen_, values_.size()) - 1]; }
    std::unique_ptr<AnyLine> clone() const override { return std::make_unique<ScriptedLine>(*this); }
private:
    std::vector<double> values_;
    std::size_t seen_{0};
};

BaselineInfo fixedBaseline()
{
    BaselineInfo b;
    b.name = "fixed";
    b.makeLine = [](const Params&) -> std::unique_ptr<AnyLine> { return std::make_unique<FixedLine>(); };
    return b;
}

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

// every snapshot has a bar for every asset
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

EntryStudyOptions options()
{
    EntryStudyOptions o;
    o.atrPeriod = 1;
    return o;
}

EntryStudyResult study(const std::vector<Bar>& bars, int runs = 200, const EntryStudyOptions& o = options(),
                       const std::vector<double>& spreads = {})
{
    return runEntryStudy(dataOf({bars}), fixedBaseline(), Params{}, o, runs, spreads);
}

const HorizonResult& at(const EntryStudyResult& r, int horizon)
{
    for (const HorizonResult& h : r.horizons)
        if (h.horizon == horizon)
            return h;
    throw std::logic_error("no such horizon");
}

// bar 0 below the line, bar 1 crosses up with ATR (true range) 3; then the bars of the hand-worked example
const Bar below = ohlc(98, 99, 97, 98);
const Bar crossUp = ohlc(98, 101, 98, 100.5);
const Bar b2 = ohlc(100.6, 104, 100.4, 103);
const Bar b3 = ohlc(103, 106, 102.5, 105);
const Bar b4 = ohlc(105, 105, 102, 102.5);

// the hand-worked series: the signal bar is index 1; 20 flat bars at 102.5 follow the three above (25 bars in all)
std::vector<Bar> handWorked()
{
    std::vector<Bar> bars{below, crossUp, b2, b3, b4};
    for (int i = 0; i < 20; ++i)
        bars.push_back(flat(102.5));
    return bars;
}

// price ramps 90 -> 110 -> 90 in steps of 2 around the line at 100: every cross is followed by a move the same way
std::vector<Bar> trending(int cycles)
{
    std::vector<Bar> bars;
    for (int c = 0; c < cycles; ++c)
    {
        for (int p = 90; p <= 110; p += 2) bars.push_back(flat(p));
        for (int p = 108; p >= 92; p -= 2) bars.push_back(flat(p));
    }
    return bars;
}

// the same idea with 40-bar legs and steps of 1 (ATR 1): after a cross the next 20 bars keep going the same way
std::vector<Bar> longTrending(int cycles)
{
    std::vector<Bar> bars;
    for (int c = 0; c < cycles; ++c)
    {
        for (int p = 80; p <= 120; ++p) bars.push_back(flat(p));
        for (int p = 119; p >= 81; --p) bars.push_back(flat(p));
    }
    return bars;
}

// price jumps 98, 102, 98, 102 ...: every cross is followed by a move straight back
std::vector<Bar> reversing(int n)
{
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i)
        bars.push_back(flat(i % 2 == 0 ? 98 : 102));
    return bars;
}
}

// ---------- signals ----------

TEST(EntryStudyTest, NoEventsWhilePriceStaysOnOneSide)
{
    const EntryStudyResult r = study({below, ohlc(98, 99, 97, 98.5), ohlc(98, 99, 97, 97)});

    EXPECT_EQ(r.events, 0);
    for (const HorizonResult& h : r.horizons)
    {
        EXPECT_EQ(h.events, 0);
        EXPECT_TRUE(std::isnan(h.meanAtr));
        EXPECT_TRUE(std::isnan(h.beats));
    }
    EXPECT_TRUE(std::isnan(r.targetFirst));
    EXPECT_EQ(r.excursionEvents, 0);
}

TEST(EntryStudyTest, TheFirstBarNeverSignalsEvenWhenAboveTheLine)
{
    EXPECT_EQ(study({ohlc(104, 106, 103, 105), ohlc(105, 107, 104, 106)}).events, 0);
}

TEST(EntryStudyTest, ACrossIsOneEvent)
{
    EXPECT_EQ(study(handWorked()).events, 1);
}

TEST(EntryStudyTest, EveryCrossIsAnEventInBothDirections)
{
    // 98, 102, 98, 102, 98: four crosses after the first bar
    EXPECT_EQ(study(reversing(5)).events, 4);
}

TEST(EntryStudyTest, EachAssetHasItsOwnSignals)
{
    const std::vector<Bar> bars = handWorked();
    const EntryStudyResult r = runEntryStudy(dataOf({bars, bars}), fixedBaseline(), Params{}, options(), 50);

    EXPECT_EQ(r.events, 2);
    EXPECT_EQ(at(r, 1).events, 2);
}

TEST(EntryStudyTest, ACrossFurtherThanTheChaseDistanceIsSkipped)
{
    // two quiet bars (true range 1), then a jump to 105: ATR(2) = (1 + 7) / 2 = 4 and the close is 5 from the line
    const std::vector<Bar> bars{ohlc(98, 98.5, 97.5, 98), ohlc(98, 98.5, 97.5, 98), ohlc(98, 105, 98, 105), flat(106)};
    EntryStudyOptions o = options();
    o.atrPeriod = 2;

    EXPECT_EQ(study(bars, 20, o).events, 0);

    o.maxDistanceAtr = std::nullopt;
    EXPECT_EQ(study(bars, 20, o).events, 1);

    o.maxDistanceAtr = 1.5;                       // 5 <= 1.5 * 4
    EXPECT_EQ(study(bars, 20, o).events, 1);
}

TEST(EntryStudyTest, ACrossExactlyAtTheChaseDistanceIsKept)
{
    // flat bars at 98 and 102: true range 4, the close is 2 from the line; with a distance of 0.5 ATR that is exactly 2
    EntryStudyOptions o = options();
    o.maxDistanceAtr = 0.5;
    EXPECT_EQ(study(reversing(3), 20, o).events, 2);
    o.maxDistanceAtr = 0.49;
    EXPECT_EQ(study(reversing(3), 20, o).events, 0);
}

TEST(EntryStudyTest, ASignalNeedsAPositiveAtr)
{
    // flat bars at 100 (true range 0, so the ATR is 0) while the line moves from above the price to below it
    BaselineInfo moving;
    moving.name = "moving";
    moving.makeLine = [](const Params&) -> std::unique_ptr<AnyLine> { return std::make_unique<ScriptedLine>(std::vector<double>{101, 99}); };
    const std::vector<Bar> bars{flat(100), flat(100), flat(100)};
    EntryStudyOptions o = options();
    o.maxDistanceAtr = std::nullopt;                 // so the don't-chase filter cannot be what rejects it

    const EntryStudyResult r = runEntryStudy(dataOf({bars}), moving, Params{}, o, 10);

    EXPECT_EQ(r.events, 0);
}

// ---------- forward moves ----------

TEST(EntryStudyTest, ForwardMovesMatchTheHandWorkedValues)
{
    const EntryStudyResult r = study(handWorked());

    // signal close 100.5, ATR 3; later closes 103, 105, 102.5, then 102.5 for ever
    EXPECT_NEAR(at(r, 1).meanAtr, (103 - 100.5) / 3, 1e-9);
    EXPECT_NEAR(at(r, 2).meanAtr, (105 - 100.5) / 3, 1e-9);
    EXPECT_NEAR(at(r, 3).meanAtr, (102.5 - 100.5) / 3, 1e-9);
    EXPECT_NEAR(at(r, 5).meanAtr, (102.5 - 100.5) / 3, 1e-9);
    EXPECT_NEAR(at(r, 10).meanAtr, (102.5 - 100.5) / 3, 1e-9);
    EXPECT_NEAR(at(r, 20).meanAtr, (102.5 - 100.5) / 3, 1e-9);
    EXPECT_EQ(at(r, 1).events, 1);
}

TEST(EntryStudyTest, AHorizonBeyondTheDataHasNoEvents)
{
    const EntryStudyResult r = study(handWorked());       // 23 bars after the signal: 40 is out of reach

    EXPECT_EQ(at(r, 20).events, 1);
    EXPECT_EQ(at(r, 40).events, 0);
    EXPECT_TRUE(std::isnan(at(r, 40).meanAtr));
}

TEST(EntryStudyTest, TheHorizonsAreTheFixedList)
{
    const EntryStudyResult r = study(handWorked());

    ASSERT_EQ(r.horizons.size(), 7u);
    const int expected[] = {1, 2, 3, 5, 10, 20, 40};
    for (std::size_t i = 0; i < 7; ++i)
        EXPECT_EQ(r.horizons[i].horizon, expected[i]);
}

TEST(EntryStudyTest, AShortSignalMovesInTheOppositeSign)
{
    // above the line, then a cross down with true range 3: signal close 99.5, later closes lower are a gain
    const std::vector<Bar> bars{ohlc(102, 103, 101, 102), ohlc(102, 102, 99, 99.5), ohlc(99.4, 99.5, 96, 97)};
    const EntryStudyResult r = study(bars);

    EXPECT_EQ(r.events, 1);
    EXPECT_NEAR(at(r, 1).meanAtr, (99.5 - 97) / 3, 1e-9);      // +0.8333: price fell after a short signal
    EXPECT_NEAR(at(r, 1).shareUp, 1.0, 1e-12);
}

TEST(EntryStudyTest, ShareUpIsTheShareOfEventsThatMovedInTheSignalsDirection)
{
    // bar 1 is a long signal; bar 3 (close 97, true range 6) is a cross down
    // long: the next close 103 is a gain. short: the next close 99 is above 97, a loss
    const std::vector<Bar> bars{below, crossUp, flat(103), flat(97), flat(99)};
    const EntryStudyResult r = study(bars);

    EXPECT_EQ(at(r, 1).events, 2);
    EXPECT_NEAR(at(r, 1).shareUp, 0.5, 1e-12);
}

TEST(EntryStudyTest, BarsTheAssetDoesNotHaveAreNotCounted)
{
    // snapshot 3 has no bar for the asset: the next bar it has is the first bar after the signal
    AlignedData d;
    d.symbols = {"A0"};
    auto add = [&](std::int64_t t, std::optional<Bar> bar)
    {
        Snapshot s;
        s.timestamp = t;
        if (bar) { bar->timestamp = t; }
        s.bars = {bar};
        d.snapshots.push_back(s);
    };
    add(1, below);
    add(2, crossUp);
    add(3, std::nullopt);
    add(4, b2);

    const EntryStudyResult r = runEntryStudy(d, fixedBaseline(), Params{}, options(), 20);

    EXPECT_EQ(r.events, 1);
    EXPECT_NEAR(at(r, 1).meanAtr, (103 - 100.5) / 3, 1e-9);
}

TEST(EntryStudyTest, TheSpreadIsTakenOffAsAFullRoundTrip)
{
    // spread 0.3 against an ATR of 3 is 0.1 ATR
    const EntryStudyResult r = study(handWorked(), 50, options(), {0.3});

    EXPECT_NEAR(at(r, 1).netMeanAtr, (103 - 100.5) / 3 - 0.1, 1e-9);
    EXPECT_NEAR(at(r, 1).meanAtr, (103 - 100.5) / 3, 1e-9);        // the gross move is untouched
}

TEST(EntryStudyTest, WithoutSpreadsTheNetMoveIsTheGrossMove)
{
    const EntryStudyResult r = study(handWorked());

    EXPECT_DOUBLE_EQ(at(r, 2).netMeanAtr, at(r, 2).meanAtr);
}

// ---------- the random-direction comparison ----------

TEST(EntryStudyTest, FollowingTheCrossBeatsRandomDirectionsOnTrendingData)
{
    const EntryStudyResult r = study(trending(12), 300);

    for (int h : {1, 2, 3})
    {
        EXPECT_NEAR(at(r, h).meanAtr, static_cast<double>(h), 1e-9) << h;     // +1 ATR per bar
        EXPECT_GT(at(r, h).beats, 0.99) << h;
        EXPECT_NEAR(at(r, h).shareUp, 1.0, 1e-12) << h;
    }
    EXPECT_LT(std::abs(at(r, 1).randomMeanAtr), 0.3);                     // coin flips average about zero
}

TEST(EntryStudyTest, FollowingTheCrossLosesToRandomDirectionsOnReversingData)
{
    const EntryStudyResult r = study(reversing(40), 300);

    EXPECT_NEAR(at(r, 1).meanAtr, -1.0, 1e-9);
    EXPECT_LT(at(r, 1).beats, 0.02);
    EXPECT_NEAR(at(r, 1).shareUp, 0.0, 1e-12);
}

TEST(EntryStudyTest, TheRandomRunsAreRepeatable)
{
    const EntryStudyResult a = study(trending(6), 100);
    const EntryStudyResult b = study(trending(6), 100);

    EXPECT_DOUBLE_EQ(at(a, 2).randomMeanAtr, at(b, 2).randomMeanAtr);
    EXPECT_DOUBLE_EQ(at(a, 2).beats, at(b, 2).beats);
    EXPECT_DOUBLE_EQ(a.randomTargetFirst, b.randomTargetFirst);
}

TEST(EntryStudyTest, MoreRandomRunsGiveAnotherButSimilarAverage)
{
    const EntryStudyResult a = study(trending(12), 100);
    const EntryStudyResult b = study(trending(12), 400);

    EXPECT_NE(at(a, 2).randomMeanAtr, at(b, 2).randomMeanAtr);
    EXPECT_LT(std::abs(at(a, 2).randomMeanAtr - at(b, 2).randomMeanAtr), 0.6);
}

// ---------- the target / stop race ----------

TEST(EntryStudyTest, TheRaceMatchesTheHandWorkedValues)
{
    // long from 100.5, ATR 3: target 103.5, stop 96: bar 2's high 104 reaches the target first.
    // The same signal as a short: stop 105, target 97.5: bar 3's high 106 reaches the stop first.
    const EntryStudyResult r = study(handWorked(), 400);

    EXPECT_DOUBLE_EQ(r.targetFirst, 1.0);
    EXPECT_DOUBLE_EQ(r.stopFirst, 0.0);
    EXPECT_DOUBLE_EQ(r.neither, 0.0);
    EXPECT_NEAR(r.raceR, 1.0 / 1.5, 1e-12);                          // (1 * 1.0 - 0 * 1.5) / 1.5
    EXPECT_NEAR(r.randomTargetFirst, 0.5, 0.1);                      // a coin flip is the long or the short
}

TEST(EntryStudyTest, TheStopWinsWhenOneBarReachesBoth)
{
    // long from 100.5, ATR 3 (target 103.5, stop 96); the next bar spans 95 to 104
    const std::vector<Bar> bars{below, crossUp, ohlc(100, 104, 95, 100)};
    const EntryStudyResult r = study(bars);

    EXPECT_DOUBLE_EQ(r.stopFirst, 1.0);
    EXPECT_DOUBLE_EQ(r.targetFirst, 0.0);
}

TEST(EntryStudyTest, TheRaceLooksExactlyFortyBarsAhead)
{
    // long from 100.5 (target 103.5): a bar reaching it 40 bars after the signal counts, 41 bars after does not
    auto series = [](int spikeAt)
    {
        std::vector<Bar> bars{below, crossUp};
        for (int i = 2; i < 60; ++i)
            bars.push_back(i == spikeAt ? ohlc(100.5, 104, 100.5, 100.5) : flat(100.5));
        return bars;
    };

    EXPECT_DOUBLE_EQ(study(series(41), 20).targetFirst, 1.0);        // signal at index 1, spike 40 bars later
    EXPECT_DOUBLE_EQ(study(series(42), 20).targetFirst, 0.0);        // 41 bars later: neither
    EXPECT_DOUBLE_EQ(study(series(42), 20).neither, 1.0);
}

TEST(EntryStudyTest, AnEventThatReachesNeitherIsCounted)
{
    // 98 / 102 reversals with true range 4: the target 4 ATR... a long from 102 needs 106 or 96: neither is touched
    const EntryStudyResult r = study(reversing(6));

    EXPECT_DOUBLE_EQ(r.neither, 1.0);
    EXPECT_DOUBLE_EQ(r.targetFirst, 0.0);
    EXPECT_DOUBLE_EQ(r.stopFirst, 0.0);
}

TEST(EntryStudyTest, FollowingTheCrossWinsTheRaceMoreOftenOnTrendingData)
{
    const EntryStudyResult r = study(trending(12), 300);

    EXPECT_DOUBLE_EQ(r.targetFirst, 1.0);
    EXPECT_GT(r.targetBeats, 0.99);
    EXPECT_GT(r.raceR, r.randomRaceR);
}

TEST(EntryStudyTest, TheRaceUsesTheStopAndTargetFromTheOptions)
{
    // a target of 0.5 ATR is 102 (from 100.5 with ATR 3), reached by bar 2's high of 104;
    // a stop of 0.1 ATR is 100.2, which bar 2's low of 100.4 does not touch: the target comes first
    EntryStudyOptions o = options();
    o.targetAtr = 0.5;
    o.stopAtr = 0.1;
    EXPECT_DOUBLE_EQ(study(handWorked(), 50, o).targetFirst, 1.0);

    o.stopAtr = 0.03;                                  // the stop is 100.41, which bar 2's low of 100.4 reaches: the stop first
    EXPECT_DOUBLE_EQ(study(handWorked(), 50, o).stopFirst, 1.0);
}

// ---------- excursions ----------

TEST(EntryStudyTest, ExcursionsMatchTheHandWorkedValues)
{
    const EntryStudyResult r = study(handWorked(), 400);

    // bars 2..21 after the signal: highest high 106, lowest low 100.4; signal close 100.5, ATR 3
    EXPECT_EQ(r.excursionEvents, 1);
    EXPECT_NEAR(r.mfeAtr, (106 - 100.5) / 3, 1e-9);
    EXPECT_NEAR(r.maeAtr, (100.5 - 100.4) / 3, 1e-9);
}

TEST(EntryStudyTest, ARandomDirectionSwapsTheFavourableAndAdverseSides)
{
    // with one event and a coin flip the average random MFE is about the mean of the two sides
    const EntryStudyResult r = study(handWorked(), 600);

    EXPECT_NEAR(r.randomMfeAtr, (r.mfeAtr + r.maeAtr) / 2, 0.15);
    EXPECT_NEAR(r.randomMaeAtr, (r.mfeAtr + r.maeAtr) / 2, 0.15);
}

TEST(EntryStudyTest, EventsWithoutTwentyBarsAfterThemHaveNoExcursion)
{
    // only 3 bars follow the signal
    const EntryStudyResult r = study({below, crossUp, b2, b3, b4});

    EXPECT_EQ(r.excursionEvents, 0);
    EXPECT_TRUE(std::isnan(r.mfeAtr));
}

TEST(EntryStudyTest, TheExcursionLooksExactlyTwentyBarsAhead)
{
    // long from 100.5, ATR 3: a high of 110 on the 20th bar after the signal counts, on the 21st it does not
    auto series = [](int spikeAt)
    {
        std::vector<Bar> bars{below, crossUp};
        for (int i = 2; i < 40; ++i)
            bars.push_back(i == spikeAt ? ohlc(100.5, 110, 100.5, 100.5) : flat(100.5));
        return bars;
    };

    EXPECT_NEAR(study(series(21), 20).mfeAtr, (110 - 100.5) / 3, 1e-9);      // signal at index 1, spike at index 21
    EXPECT_NEAR(study(series(22), 20).mfeAtr, 0.0, 1e-9);
}

TEST(EntryStudyTest, AMoveOfExactlyZeroIsNotAMoveInTheSignalsDirection)
{
    // the close after the signal equals the signal close
    const EntryStudyResult r = study({below, crossUp, flat(100.5)});

    EXPECT_EQ(at(r, 1).events, 1);
    EXPECT_DOUBLE_EQ(at(r, 1).meanAtr, 0.0);
    EXPECT_DOUBLE_EQ(at(r, 1).shareUp, 0.0);
}

TEST(EntryStudyTest, ExcursionsAreNeverNegative)
{
    // price only drifts down after a long signal, but stays above the line (no second signal): the best excursion
    // is 0, not a negative number
    std::vector<Bar> bars{below, crossUp};
    for (int i = 0; i < 25; ++i)
        bars.push_back(flat(100.5 - 0.019 * (i + 1)));      // 100.481 ... 100.025: never above the signal close
    const EntryStudyResult r = study(bars);

    EXPECT_EQ(r.events, 1);
    EXPECT_GE(r.mfeAtr, 0.0);
    EXPECT_DOUBLE_EQ(r.mfeAtr, 0.0);
    EXPECT_GT(r.maeAtr, 0.0);
}

TEST(EntryStudyTest, FollowingTheCrossHasABetterExcursionBalanceOnTrendingData)
{
    // after a cross at 101 the next 20 closes run up to 120 (19 ATR) and never go back below 101
    const EntryStudyResult r = study(longTrending(6), 300);

    EXPECT_GT(r.excursionEvents, 5);
    EXPECT_NEAR(r.mfeAtr, 19.0, 1e-9);
    EXPECT_NEAR(r.maeAtr, 0.0, 1e-9);
    EXPECT_GT(r.excursionBeats, 0.99);
}

// ---------- input checks and the text ----------

TEST(EntryStudyTest, RejectsBadOptionsAndRunCounts)
{
    EXPECT_THROW(study(handWorked(), 0), std::invalid_argument);
    EntryStudyOptions o = options();
    o.atrPeriod = 0;
    EXPECT_THROW(study(handWorked(), 10, o), std::invalid_argument);
    o = options();
    o.stopAtr = 0;
    EXPECT_THROW(study(handWorked(), 10, o), std::invalid_argument);
    o = options();
    o.targetAtr = -1;
    EXPECT_THROW(study(handWorked(), 10, o), std::invalid_argument);
    o = options();
    o.maxDistanceAtr = 0.0;
    EXPECT_THROW(study(handWorked(), 10, o), std::invalid_argument);
    o = options();
    o.confirmationWindow = 0;
    EXPECT_THROW(study(handWorked(), 10, o), std::invalid_argument);
}

TEST(EntryStudyTest, TheTextNamesTheDataTheHorizonsAndTheRace)
{
    const std::string text = formatEntryStudy("out-of-sample", study(trending(6), 100));

    EXPECT_NE(text.find("out-of-sample"), std::string::npos);
    EXPECT_NE(text.find("events"), std::string::npos);
    for (const char* h : {" 1 ", " 20 ", " 40 "})
        EXPECT_NE(text.find(h), std::string::npos) << h;
    EXPECT_NE(text.find("target"), std::string::npos);
    EXPECT_NE(text.find("beats"), std::string::npos);
}

TEST(EntryStudyTest, TheTextSaysNotAvailableInsteadOfNan)
{
    const std::string text = formatEntryStudy("in-sample", study({below, ohlc(98, 99, 97, 98.5)}));

    EXPECT_EQ(text.find("nan"), std::string::npos) << text;
    EXPECT_NE(text.find("n/a"), std::string::npos);
}

// ---------- components: a confirmation and a filter decide which crosses become entries ----------
//
// As in NnfxStrategy: a cross starts a pending signal that lasts confirmationWindow bars (1 without a confirmation).
// On each of those bars, in order, the entry is taken if the ATR is positive, the close is within the chase distance,
// the confirmation agrees with the direction and the filter allows. The forward moves are then measured from the
// ENTRY bar (its close and its ATR). Components that are not ready yet hide the bar completely, so a cross that happens
// before they are ready is not seen.

namespace
{
// a State that returns the scripted value for each bar in turn (the last one repeats); ready after `warmup` bars
class ScriptedState : public AnyState
{
public:
    explicit ScriptedState(std::vector<int> script, std::size_t warmup = 1) : script_(std::move(script)), warmup_(warmup) {}
    void update(const Bar&) override { ++seen_; }
    bool ready() const override { return seen_ >= warmup_; }
    int state() const override { return script_[std::min(seen_, script_.size()) - 1]; }
    std::size_t warmupBars() const override { return warmup_; }
    std::unique_ptr<AnyState> clone() const override { return std::make_unique<ScriptedState>(*this); }
private:
    std::vector<int> script_;
    std::size_t warmup_;
    std::size_t seen_{0};
};

// a State / Gate that reads its answer for each bar from a list made in advance (so a test can build an oracle)
class ListedState : public AnyState
{
public:
    explicit ListedState(std::shared_ptr<const std::vector<int>> list) : list_(std::move(list)) {}
    void update(const Bar&) override { ++seen_; }
    bool ready() const override { return seen_ >= 1; }
    int state() const override { return (*list_)[seen_ - 1]; }
    std::size_t warmupBars() const override { return 1; }
    std::unique_ptr<AnyState> clone() const override { return std::make_unique<ListedState>(*this); }
private:
    std::shared_ptr<const std::vector<int>> list_;
    std::size_t seen_{0};
};

class ListedGate : public AnyGate
{
public:
    explicit ListedGate(std::shared_ptr<const std::vector<int>> list) : list_(std::move(list)) {}
    void update(const Bar&) override { ++seen_; }
    bool ready() const override { return seen_ >= 1; }
    bool allows() const override { return (*list_)[seen_ - 1] != 0; }
    std::size_t warmupBars() const override { return 1; }
    std::unique_ptr<AnyGate> clone() const override { return std::make_unique<ListedGate>(*this); }
private:
    std::shared_ptr<const std::vector<int>> list_;
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

// the cross is bar 1 (close 100.5, ATR 3); bar 2 closes 101.5 with ATR 1.6 (1.5 from the line: inside the chase distance)
const Bar b2c = ohlc(100.6, 102, 100.4, 101.5);

EntryStudyOptions confirmedBy(std::vector<int> script, int window = 7)
{
    EntryStudyOptions o = options();
    o.components.confirmation = std::make_shared<ScriptedState>(std::move(script));
    o.confirmationWindow = window;
    return o;
}
}

TEST(EntryStudyComponentsTest, ASignalIsCountedEvenWhenNoEntryFollows)
{
    EXPECT_EQ(study(handWorked()).signals, 1);
    const EntryStudyResult r = study({below, crossUp}, 20, confirmedBy({-1, -1}, 1));
    EXPECT_EQ(r.signals, 1);
    EXPECT_EQ(r.events, 0);
}

TEST(EntryStudyComponentsTest, AConfirmationOnTheCrossBarEntersOnTheCrossBar)
{
    // entry at bar 1: close 100.5, ATR 3; the next close 101.5 is +1 / 3
    const EntryStudyResult r = study({below, crossUp, b2c}, 20, confirmedBy({-1, +1, +1}));

    EXPECT_EQ(r.events, 1);
    EXPECT_NEAR(at(r, 1).meanAtr, (101.5 - 100.5) / 3, 1e-9);
}

TEST(EntryStudyComponentsTest, ALaterConfirmationEntersOnTheConfirmingBarAndIsMeasuredFromThere)
{
    // the confirmation agrees on bar 2: entry at 101.5 with ATR 1.6; the next close 103 is +1.5 / 1.6
    const EntryStudyResult r = study({below, crossUp, b2c, flat(103)}, 20, confirmedBy({-1, -1, +1, +1}));

    EXPECT_EQ(r.signals, 1);
    EXPECT_EQ(r.events, 1);
    EXPECT_NEAR(at(r, 1).meanAtr, (103 - 101.5) / 1.6, 1e-9);
}

TEST(EntryStudyComponentsTest, TheSignalExpiresWhenTheWindowRunsOut)
{
    // the confirmation only agrees on bar 3; the cross bar is bar 1
    EntryStudyOptions late = confirmedBy({-1, -1, -1, +1}, 2);        // bars 1 and 2 only
    late.maxDistanceAtr = std::nullopt;
    EXPECT_EQ(study({below, crossUp, b2c, flat(103), flat(104)}, 20, late).events, 0);

    EntryStudyOptions fits = confirmedBy({-1, -1, -1, +1}, 3);        // bars 1, 2 and 3
    fits.maxDistanceAtr = std::nullopt;
    EXPECT_EQ(study({below, crossUp, b2c, flat(103), flat(104)}, 20, fits).events, 1);
}

TEST(EntryStudyComponentsTest, ADisagreeingConfirmationBlocksTheEntry)
{
    EXPECT_EQ(study({below, crossUp, b2c}, 20, confirmedBy({-1, -1, -1})).events, 0);
    EXPECT_EQ(study({below, crossUp, b2c}, 20, confirmedBy({0, 0, 0})).events, 0);       // neutral is not agreement
}

TEST(EntryStudyComponentsTest, AShortNeedsAConfirmationThatIsBearish)
{
    // a cross down at bar 1: a bullish confirmation does not agree, a bearish one does
    const std::vector<Bar> bars{ohlc(102, 103, 101, 102), ohlc(102, 102, 99, 99.5), flat(97)};
    EXPECT_EQ(study(bars, 20, confirmedBy({+1, +1, +1})).events, 0);
    EXPECT_EQ(study(bars, 20, confirmedBy({-1, -1, -1})).events, 1);
}

TEST(EntryStudyComponentsTest, WithoutAConfirmationTheWindowIsOneBar)
{
    // a filter alone does not extend the signal: the gate is closed on the cross bar and opens on the next
    EntryStudyOptions o = options();
    o.components.filter = std::make_shared<GateAt>(101);
    EXPECT_EQ(study({below, crossUp, b2c}, 20, o).events, 0);
}

TEST(EntryStudyComponentsTest, AFilterThatAllowsLetsTheCrossThrough)
{
    EntryStudyOptions o = options();
    o.components.filter = std::make_shared<GateAt>(100);
    EXPECT_EQ(study({below, crossUp, b2c}, 20, o).events, 1);

    o.components.filter = std::make_shared<GateAt>(100.6);
    EXPECT_EQ(study({below, crossUp, b2c}, 20, o).events, 0);
}

TEST(EntryStudyComponentsTest, BothMustPassOnTheSameBar)
{
    // the confirmation agrees from bar 2; the gate only allows from bar 3: the entry is on bar 3
    EntryStudyOptions o = confirmedBy({-1, -1, +1, +1, +1});
    o.maxDistanceAtr = std::nullopt;
    o.components.filter = std::make_shared<GateAt>(103);
    const EntryStudyResult r = study({below, crossUp, b2c, flat(103), flat(104)}, 20, o);

    EXPECT_EQ(r.events, 1);
    EXPECT_NEAR(at(r, 1).meanAtr, (104 - 103) / 1.5, 1e-9);      // entry on bar 3: close 103, true range 1.5
}

TEST(EntryStudyComponentsTest, TheChaseDistanceStillAppliesOnTheConfirmingBar)
{
    // confirmation on bar 3 (close 103, ATR 1.5): 3 from the line is further than 1.5, so the default filter refuses it
    EXPECT_EQ(study({below, crossUp, b2c, flat(103), flat(104)}, 20, confirmedBy({-1, -1, -1, +1, +1})).events, 0);
}

TEST(EntryStudyComponentsTest, ComponentsThatAreNotReadyHideTheBarsSoEarlyCrossesAreNotSeen)
{
    // the confirmation is ready after 3 bars; the cross on bar 1 happens before that and is never seen
    EntryStudyOptions o = options();
    o.components.confirmation = std::make_shared<ScriptedState>(std::vector<int>{+1}, 3);
    const EntryStudyResult r = study({below, crossUp, b2c, flat(102), flat(103)}, 20, o);

    EXPECT_EQ(r.signals, 0);
    EXPECT_EQ(r.events, 0);
}

TEST(EntryStudyComponentsTest, AFilterThatIsNotReadyHidesTheBarsToo)
{
    // the gate is ready after 3 bars: the cross on bar 1 is never seen, however open the gate would have been
    EntryStudyOptions o = options();
    o.components.filter = std::make_shared<GateAt>(0, 3);
    const EntryStudyResult r = study({below, crossUp, b2c, flat(102), flat(103)}, 20, o);

    EXPECT_EQ(r.signals, 0);
    EXPECT_EQ(r.events, 0);
}

TEST(EntryStudyComponentsTest, TheEntryIsMeasuredFromTheEntryBarsCloseAndAtr)
{
    // confirmed on bar 2: close 101.5 and ATR 1.6, not the cross bar's 100.5 and 3
    const EntryStudyResult r = study({below, crossUp, b2c, flat(103)}, 20, confirmedBy({-1, -1, +1, +1}));

    EXPECT_NEAR(at(r, 1).meanAtr, 1.5 / 1.6, 1e-9);
    EXPECT_NEAR(at(r, 1).netMeanAtr, 1.5 / 1.6, 1e-9);        // no spread given
}

TEST(EntryStudyComponentsTest, TheOptionsAreNotConsumedSoRunsRepeat)
{
    const EntryStudyOptions o = confirmedBy({-1, -1, +1, +1});
    const EntryStudyResult a = study({below, crossUp, b2c, flat(103)}, 20, o);
    const EntryStudyResult b = study({below, crossUp, b2c, flat(103)}, 20, o);

    EXPECT_EQ(a.events, 1);
    EXPECT_EQ(b.events, 1);
    EXPECT_DOUBLE_EQ(at(a, 1).meanAtr, at(b, 1).meanAtr);
}

// ---------- the component control on the entry study ----------
//
// Does the real component pick better entries than a random stand-in of the same frequency and persistence?
//   real      the options as given
//   without   the same options without this role's component
//   random    this role's component replaced by a stand-in, one run per seed (1, 2, ...)
// An arm is the study with the real signal directions (no coin flips). The real arm's mean move at each horizon, its
// race R and its excursion balance are ranked among the random arms.

namespace
{
struct Walk
{
    std::vector<Bar> bars;
    std::vector<double> closes;
};

// a mean-reverting walk around the line at 100, so it keeps crossing
Walk walk(unsigned seed, int n = 1500)
{
    std::mt19937 gen(seed);
    std::normal_distribution<double> noise(0.0, 1.0);
    Walk w;
    double p = 100;
    for (int i = 0; i < n; ++i)
    {
        p = 100 + 0.9 * (p - 100) + noise(gen);
        w.closes.push_back(p);
        w.bars.push_back(flat(p));
    }
    return w;
}

int sign(double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }

EntryStudyOptions walkOptions()
{
    EntryStudyOptions o = options();
    o.maxDistanceAtr = std::nullopt;
    o.confirmationWindow = 1;
    return o;
}

// confirms the bars whose close is followed 3 bars later by a move that way: sees the future
std::shared_ptr<const std::vector<int>> futureStates(const Walk& w, int sgn)
{
    auto list = std::make_shared<std::vector<int>>(w.closes.size(), 0);
    for (std::size_t i = 0; i + 3 < w.closes.size(); ++i)
        (*list)[i] = sgn * sign(w.closes[i + 3] - w.closes[i]);
    return list;
}

EntryControlResult control(const Walk& w, const EntryStudyOptions& o, ComponentRole role, int seeds = 60)
{
    return runEntryComponentControl(dataOf({w.bars}), fixedBaseline(), Params{}, o, role, seeds);
}
}

TEST(EntryControlTest, AConfirmationThatSeesTheFutureBeatsRandomConfirmations)
{
    const Walk w = walk(3);
    EntryStudyOptions o = walkOptions();
    o.components.confirmation = std::make_shared<ListedState>(futureStates(w, +1));

    const EntryControlResult r = control(w, o, ComponentRole::Confirmation);

    EXPECT_EQ(r.random.size(), 60u);
    EXPECT_GT(r.real.events, 10);
    EXPECT_GT(r.real.meanAtr[2], r.without.meanAtr[2]);             // horizon 3
    EXPECT_GT(r.real.meanAtr[2], 0.3);
    EXPECT_GT(r.meanBeats[2], 0.95);
    EXPECT_FALSE(std::isnan(r.balanceBeats));                       // the 20-bar balance is only ranked, not asserted
    EXPECT_FALSE(std::isnan(r.raceBeats));
}

TEST(EntryControlTest, AConfirmationThatSeesTheFutureTheWrongWayLosesToRandomOnes)
{
    const Walk w = walk(3);
    EntryStudyOptions o = walkOptions();
    o.components.confirmation = std::make_shared<ListedState>(futureStates(w, -1));

    const EntryControlResult r = control(w, o, ComponentRole::Confirmation);

    EXPECT_LT(r.real.meanAtr[2], r.without.meanAtr[2]);
    EXPECT_LT(r.meanBeats[2], 0.05);
}

TEST(EntryControlTest, AFilterThatKeepsTheCrossesThatContinueBeatsRandomFilters)
{
    // allows the bars where the next 3 bars move away from the line in the direction of the cross
    const Walk w = walk(5);
    auto allowed = std::make_shared<std::vector<int>>(w.closes.size(), 0);
    for (std::size_t i = 0; i + 3 < w.closes.size(); ++i)
        (*allowed)[i] = sign((w.closes[i + 3] - w.closes[i]) * (w.closes[i] - 100)) > 0 ? 1 : 0;
    EntryStudyOptions o = walkOptions();
    o.components.filter = std::make_shared<ListedGate>(allowed);

    const EntryControlResult r = control(w, o, ComponentRole::Filter);

    EXPECT_GT(r.real.events, 10);
    EXPECT_GT(r.real.meanAtr[2], r.without.meanAtr[2]);
    EXPECT_GT(r.meanBeats[2], 0.95);
}

TEST(EntryControlTest, TheRealArmIsTheStudyWithTheComponentsAndTheWithoutArmIsTheStudyWithout)
{
    const Walk w = walk(3);
    EntryStudyOptions o = walkOptions();
    o.components.confirmation = std::make_shared<ListedState>(futureStates(w, +1));

    const EntryControlResult c = control(w, o, ComponentRole::Confirmation, 5);
    const EntryStudyResult with = runEntryStudy(dataOf({w.bars}), fixedBaseline(), Params{}, o, 5);
    EntryStudyOptions none = o;
    none.components.confirmation.reset();
    const EntryStudyResult without = runEntryStudy(dataOf({w.bars}), fixedBaseline(), Params{}, none, 5);

    EXPECT_EQ(c.real.events, with.events);
    EXPECT_EQ(c.without.events, without.events);
    for (std::size_t h = 0; h < with.horizons.size(); ++h)
    {
        EXPECT_DOUBLE_EQ(c.real.meanAtr[h], with.horizons[h].meanAtr);
        EXPECT_DOUBLE_EQ(c.without.meanAtr[h], without.horizons[h].meanAtr);
    }
    EXPECT_DOUBLE_EQ(c.real.raceR, with.raceR);
    EXPECT_NEAR(c.real.balance, with.mfeAtr - with.maeAtr, 1e-12);        // the same sum in a different order
}

TEST(EntryControlTest, OnlyTheChosenRolesComponentIsReplaced)
{
    // a confirmation that sees the future plus a filter that never allows after bar 50: controlling the confirmation
    // leaves the filter alone in every arm, so no arm has an entry after bar 50
    const Walk w = walk(3);
    auto allowed = std::make_shared<std::vector<int>>(w.closes.size(), 0);
    for (int i = 0; i < 50; ++i)
        (*allowed)[i] = 1;
    EntryStudyOptions o = walkOptions();
    o.components.confirmation = std::make_shared<ListedState>(futureStates(w, +1));
    o.components.filter = std::make_shared<ListedGate>(allowed);

    const EntryControlResult c = control(w, o, ComponentRole::Confirmation, 10);

    EXPECT_LT(c.real.events, 15);                                   // only the first 50 bars could enter
    EXPECT_LT(c.without.events, 15);
    for (const EntryArm& a : c.random)
        EXPECT_LT(a.events, 15);
}

TEST(EntryControlTest, TheRandomArmsAreRepeatableAndDifferFromEachOther)
{
    const Walk w = walk(3);
    EntryStudyOptions o = walkOptions();
    o.components.confirmation = std::make_shared<ListedState>(futureStates(w, +1));

    const EntryControlResult a = control(w, o, ComponentRole::Confirmation, 20);
    const EntryControlResult b = control(w, o, ComponentRole::Confirmation, 20);

    ASSERT_EQ(a.random.size(), b.random.size());
    bool anyDifferent = false;
    for (std::size_t i = 0; i < a.random.size(); ++i)
    {
        EXPECT_EQ(a.random[i].events, b.random[i].events);
        EXPECT_DOUBLE_EQ(a.random[i].meanAtr[2], b.random[i].meanAtr[2]);
        if (a.random[i].events != a.random[0].events)
            anyDifferent = true;
    }
    EXPECT_TRUE(anyDifferent);
    EXPECT_FALSE(a.behaviour.empty());
}

TEST(EntryControlTest, RejectsABadRoleAMissingComponentAndABadSeedCount)
{
    const Walk w = walk(3);
    EntryStudyOptions o = walkOptions();
    EXPECT_THROW(control(w, o, ComponentRole::Confirmation), std::invalid_argument);          // no confirmation given

    o.components.confirmation = std::make_shared<ListedState>(futureStates(w, +1));
    o.components.exitIndicator = std::make_shared<ListedState>(futureStates(w, +1));
    EXPECT_THROW(control(w, o, ComponentRole::ExitIndicator), std::invalid_argument);         // exits are not entries, even when present
    EXPECT_THROW(control(w, o, ComponentRole::Confirmation, 0), std::invalid_argument);
}

TEST(EntryControlTest, TheTextNamesTheComponentTheRoleAndTheRanks)
{
    const Walk w = walk(3);
    EntryStudyOptions o = walkOptions();
    o.components.confirmation = std::make_shared<ListedState>(futureStates(w, +1));
    const std::string text = formatEntryControl("out-of-sample", "oracle", ComponentRole::Confirmation,
                                                control(w, o, ComponentRole::Confirmation, 20));

    EXPECT_NE(text.find("out-of-sample"), std::string::npos);
    EXPECT_NE(text.find("oracle"), std::string::npos);
    EXPECT_NE(text.find("confirmation"), std::string::npos);
    EXPECT_NE(text.find("beats"), std::string::npos);
    EXPECT_NE(text.find("race"), std::string::npos);
    EXPECT_EQ(text.find("nan"), std::string::npos) << text;
}

// ---------- other signals: breakouts and momentum flips ----------
//
// The same study with another kind of signal; nothing here needs a baseline line, and the don't-chase filter (a distance
// from the line) does not apply. Signals:
//   breakout N   the close is above the highest high (below the lowest low) of the previous N bars; a break counts only
//                if the last break the same way was more than N bars ago, so a run of breaks is one signal
//   momentum N   the sign of (close - close N bars ago) flips; a return of exactly zero changes nothing
// Entries, windows, components and everything measured afterwards work as for crosses.

namespace
{
EntryStudyResult signalStudy(const std::vector<Bar>& bars, SignalKind kind, int lookback, int runs = 20,
                             EntryStudyOptions o = options())
{
    o.kind = kind;
    o.lookback = lookback;
    return runSignalStudy(dataOf({bars}), o, runs);
}

std::vector<Bar> flats(std::initializer_list<double> prices)
{
    std::vector<Bar> bars;
    for (double p : prices)
        bars.push_back(flat(p));
    return bars;
}
}

TEST(SignalStudyTest, ABreakoutAboveThePreviousHighsIsALongSignal)
{
    // lookback 3: bar 4 closes 101, above the highest high (100) of bars 1-3; the next close 102 is +1 ATR (true range 1)
    const EntryStudyResult r = signalStudy(flats({100, 100, 100, 100, 101, 102}), SignalKind::Breakout, 3);

    EXPECT_EQ(r.signals, 1);                       // bar 5 (102) breaks 101 again, but within 3 bars of the last break
    EXPECT_EQ(r.events, 1);
    EXPECT_NEAR(at(r, 1).meanAtr, 1.0, 1e-9);
}

TEST(SignalStudyTest, ABreakoutBelowThePreviousLowsIsAShortSignal)
{
    const EntryStudyResult r = signalStudy(flats({100, 100, 100, 100, 99, 98}), SignalKind::Breakout, 3);

    EXPECT_EQ(r.events, 1);
    EXPECT_NEAR(at(r, 1).meanAtr, 1.0, 1e-9);      // price fell after a short signal: a gain
}

TEST(SignalStudyTest, ABreakNeedsAFullLookbackOfBars)
{
    // lookback 3 needs 3 previous bars: a jump on bar 3 has only bars 0-2 before it... that is 3 bars, so it counts;
    // on bar 2 it does not
    EXPECT_EQ(signalStudy(flats({100, 100, 101}), SignalKind::Breakout, 3).events, 0);
    EXPECT_EQ(signalStudy(flats({100, 100, 100, 101}), SignalKind::Breakout, 3).events, 1);
}

TEST(SignalStudyTest, TheBreakMustExceedNotJustEqualThePreviousExtreme)
{
    EXPECT_EQ(signalStudy(flats({100, 101, 100, 100, 101}), SignalKind::Breakout, 3).events, 0);     // equals the earlier 101
    EXPECT_EQ(signalStudy(flats({100, 101, 100, 100, 102}), SignalKind::Breakout, 3).events, 1);
}

TEST(SignalStudyTest, TheWholeLookbackWindowCounts)
{
    // lookback 3, the bar being tested is bar 4 and its window is bars 1, 2 and 3. A high of 101 anywhere in the window
    // stops a close of 101 from being a break: first bar of the window, middle bar, last bar
    EXPECT_EQ(signalStudy(flats({100, 101, 100, 100, 101}), SignalKind::Breakout, 3).events, 0);        // bar 1
    EXPECT_EQ(signalStudy(flats({100, 100, 101, 100, 101}), SignalKind::Breakout, 3).events, 0);        // bar 2
    // a spike on bar 0 is outside the window of bar 4, so a close of 101 breaks the three bars 1-3 (and nothing else does)
    EXPECT_EQ(signalStudy(flats({101, 100, 100, 100, 101}), SignalKind::Breakout, 3).events, 1);
}

TEST(SignalStudyTest, ASecondBreakTheSameWayNeedsMoreThanTheLookbackBetween)
{
    // breaks on bar 4 and bar 8 (4 bars later, more than 3): two signals. The version with a break on bar 7 (3 later) has one.
    const EntryStudyResult far = signalStudy(flats({100, 100, 100, 100, 101, 101, 101, 101, 102}), SignalKind::Breakout, 3);
    const EntryStudyResult near = signalStudy(flats({100, 100, 100, 100, 101, 101, 101, 102}), SignalKind::Breakout, 3);

    EXPECT_EQ(far.events, 2);
    EXPECT_EQ(near.events, 1);
}

TEST(SignalStudyTest, ABreakTheOtherWayIsAlwaysANewSignal)
{
    // an up break on bar 4, then a down break (below every low of bars 3-5 ... 100) on bar 6
    const EntryStudyResult r = signalStudy(flats({100, 100, 100, 100, 101, 101, 99}), SignalKind::Breakout, 3);

    EXPECT_EQ(r.events, 2);
}

TEST(SignalStudyTest, AMomentumFlipIsASignalInTheNewDirection)
{
    // lookback 2: returns over 2 bars: bar 2 +2 (the first sign: no flip), bar 3 0 (ignored), bar 4 -2 (a flip down),
    // bar 5 -1 (same sign), bar 6 +3 (a flip up)
    const EntryStudyResult r = signalStudy(flats({100, 101, 102, 101, 99, 98, 101, 102}), SignalKind::Momentum, 2);

    EXPECT_EQ(r.events, 2);
    // the down flip at bar 4 (close 99, true range 2): the next close 98 is a gain of 0.5 ATR for a short.
    // the up flip at bar 6 (close 101, true range 3): the next close 102 is a gain of 1/3.
    EXPECT_NEAR(at(r, 1).meanAtr, (0.5 + 1.0 / 3) / 2, 1e-9);
}

TEST(SignalStudyTest, AZeroReturnDoesNotChangeTheLastSign)
{
    // +, 0, +: no flip
    EXPECT_EQ(signalStudy(flats({100, 101, 102, 101, 102, 103}), SignalKind::Momentum, 2).events, 0);
}

TEST(SignalStudyTest, TheFirstSignOfMomentumIsNotAFlip)
{
    EXPECT_EQ(signalStudy(flats({100, 100, 101}), SignalKind::Momentum, 2).events, 0);
}

TEST(SignalStudyTest, TheChaseFilterDoesNotApplyWithoutALine)
{
    EntryStudyOptions o = options();
    o.maxDistanceAtr = 0.0001;
    EXPECT_EQ(signalStudy(flats({100, 100, 100, 100, 101, 102}), SignalKind::Breakout, 3, 20, o).events, 1);
}

TEST(SignalStudyTest, ComponentsAndTheWindowWorkWithAnyKindOfSignal)
{
    // the breakout is on bar 4 (101); the confirmation only agrees on bar 5 (102), still inside the window of 7:
    // entry on bar 5 with close 102 and true range 1; the next close 104 is +2
    EntryStudyOptions o = options();
    o.components.confirmation = std::make_shared<ScriptedState>(std::vector<int>{-1, -1, -1, -1, -1, +1, +1});
    o.confirmationWindow = 7;
    const EntryStudyResult r = signalStudy(flats({100, 100, 100, 100, 101, 102, 104}), SignalKind::Breakout, 3, 20, o);

    EXPECT_EQ(r.signals, 1);
    EXPECT_EQ(r.events, 1);
    EXPECT_NEAR(at(r, 1).meanAtr, 2.0, 1e-9);
}

TEST(SignalStudyTest, EachAssetHasItsOwnBreakouts)
{
    const std::vector<Bar> bars = flats({100, 100, 100, 100, 101, 102});
    EntryStudyOptions o = options();
    o.kind = SignalKind::Breakout;
    o.lookback = 3;

    EXPECT_EQ(runSignalStudy(dataOf({bars, bars}), o, 20).events, 2);
}

TEST(SignalStudyTest, FollowingBreakoutsBeatsRandomDirectionsWhenTheyRun)
{
    // a staircase up then down: every break is followed by more of the same
    std::vector<Bar> bars;
    for (int rep = 0; rep < 8; ++rep)
    {
        for (int p = 100; p < 130; ++p) bars.push_back(flat(p));
        for (int p = 130; p > 100; --p) bars.push_back(flat(p));
    }
    const EntryStudyResult r = signalStudy(bars, SignalKind::Breakout, 5, 300);

    EXPECT_GT(r.events, 5);
    EXPECT_GT(at(r, 3).meanAtr, 2.9);                // +1 ATR per bar
    EXPECT_GT(at(r, 3).beats, 0.99);
}

TEST(SignalStudyTest, TheKindsAreKeptApart)
{
    // a baseline run refuses another kind of signal, and a signal run refuses the baseline kind
    EntryStudyOptions o = options();
    o.kind = SignalKind::Breakout;
    EXPECT_THROW(runEntryStudy(dataOf({handWorked()}), fixedBaseline(), Params{}, o, 10), std::invalid_argument);

    o.kind = SignalKind::Cross;
    EXPECT_THROW(runSignalStudy(dataOf({handWorked()}), o, 10), std::invalid_argument);
}

TEST(SignalStudyTest, RejectsABadLookback)
{
    EntryStudyOptions o = options();
    o.kind = SignalKind::Breakout;
    o.lookback = 0;
    EXPECT_THROW(runSignalStudy(dataOf({handWorked()}), o, 10), std::invalid_argument);
    o.kind = SignalKind::Momentum;
    o.lookback = -3;
    EXPECT_THROW(runSignalStudy(dataOf({handWorked()}), o, 10), std::invalid_argument);
}

TEST(SignalStudyTest, TheKindsHaveNames)
{
    EXPECT_STREQ(signalKindName(SignalKind::Cross), "cross");
    EXPECT_STREQ(signalKindName(SignalKind::Breakout), "breakout");
    EXPECT_STREQ(signalKindName(SignalKind::Momentum), "momentum");
}
