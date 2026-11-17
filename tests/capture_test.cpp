#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "capture.hpp"
#include "flip_strategy.hpp"
#include "multi_backtester.hpp"

// Capture efficiency = profit earned inside the ZigZag legs / total size of the legs.
//
// Most tests use one price path:
//
//   index   0    1    2    3    4    5    6
//   close  100  110  120  130  120  110  100
//
// with pivots low@0, high@3, low@6, so two legs of equal size, ln(1.3) each:
// leg 1 goes up (bars 1..3) and leg 2 goes down (bars 4..6).

namespace
{
const double kLeg = std::log(1.3);

std::vector<Bar> barsFrom(const std::vector<double>& closes)
{
    std::vector<Bar> bars;
    for (std::size_t i = 0; i < closes.size(); ++i)
    {
        Bar b;
        b.timestamp = static_cast<std::int64_t>(i);       // the timestamp is the index
        b.open = b.high = b.low = b.close = closes[i];
        bars.push_back(b);
    }
    return bars;
}

const std::vector<double> kCloses{100, 110, 120, 130, 120, 110, 100};

const std::vector<Pivot> kPivots{{0, 100, false}, {3, 130, true}, {6, 100, false}};

ClosedTrade closedTrade(Direction d, int entryIndex, double entryPrice, int exitIndex, double exitPrice,
                        int asset = 0)
{
    ClosedTrade c;
    c.trade.asset = asset;
    c.trade.direction = d;
    c.trade.quantity = 1;
    c.trade.entryTime = entryIndex;
    c.trade.entryPrice = entryPrice;
    c.exitTime = exitIndex;
    c.exitPrice = exitPrice;
    return c;
}

Trade openTrade(Direction d, int entryIndex, double entryPrice, int asset = 0)
{
    Trade t;
    t.asset = asset;
    t.direction = d;
    t.quantity = 1;
    t.entryTime = entryIndex;
    t.entryPrice = entryPrice;
    return t;
}

CaptureResult run(const std::vector<ClosedTrade>& closed, const std::vector<Trade>& open = {})
{
    return captureEfficiency(barsFrom(kCloses), kPivots, closed, open);
}

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
}

// ---------- one asset ----------

TEST(CaptureTest, ThePerfectTraderCapturesEverything)
{
    // long the whole rise, short the whole fall
    const CaptureResult r = run({closedTrade(Direction::Long, 0, 100, 3, 130),
                                 closedTrade(Direction::Short, 3, 130, 6, 100)});

    EXPECT_NEAR(r.available, 2 * kLeg, 1e-12);
    EXPECT_NEAR(r.captured, 2 * kLeg, 1e-12);
    EXPECT_NEAR(r.efficiency, 1.0, 1e-12);
    EXPECT_EQ(r.legs, 2);
    EXPECT_EQ(r.legsRight, 2);
    EXPECT_DOUBLE_EQ(r.rightShare, 1.0);
}

TEST(CaptureTest, TheAlwaysWrongTraderGetsMinusOne)
{
    const CaptureResult r = run({closedTrade(Direction::Short, 0, 100, 3, 130),
                                 closedTrade(Direction::Long, 3, 130, 6, 100)});

    EXPECT_NEAR(r.efficiency, -1.0, 1e-12);
    EXPECT_EQ(r.legsRight, 0);
}

TEST(CaptureTest, NoTradesCapturesNothingButTheLegsStillExist)
{
    const CaptureResult r = run({});

    EXPECT_DOUBLE_EQ(r.captured, 0.0);
    EXPECT_NEAR(r.available, 2 * kLeg, 1e-12);
    EXPECT_DOUBLE_EQ(r.efficiency, 0.0);
    EXPECT_EQ(r.legs, 2);
    EXPECT_EQ(r.legsRight, 0);
    EXPECT_DOUBLE_EQ(r.rightShare, 0.0);
}

TEST(CaptureTest, BuyAndHoldThroughAnUpLegAndADownLegNetsZero)
{
    const CaptureResult r = run({}, {openTrade(Direction::Long, 0, 100)});

    EXPECT_NEAR(r.captured, 0.0, 1e-12);
    EXPECT_NEAR(r.efficiency, 0.0, 1e-12);
    EXPECT_EQ(r.legsRight, 1);              // right in the rising leg only
    EXPECT_DOUBLE_EQ(r.rightShare, 0.5);
}

TEST(CaptureTest, CapturingHalfOfTheFirstLegIsAFractionOfTheTotal)
{
    // long from 100 to 110 only
    const CaptureResult r = run({closedTrade(Direction::Long, 0, 100, 1, 110)});

    EXPECT_NEAR(r.captured, std::log(110.0 / 100.0), 1e-12);
    EXPECT_NEAR(r.efficiency, std::log(1.1) / (2 * kLeg), 1e-12);
    EXPECT_EQ(r.legsRight, 1);
}

TEST(CaptureTest, ATradeThatStartsMidLegOnlyEarnsTheRestOfTheLeg)
{
    const CaptureResult r = run({closedTrade(Direction::Long, 1, 110, 3, 130)});

    EXPECT_NEAR(r.captured, std::log(130.0 / 110.0), 1e-12);
}

TEST(CaptureTest, ATradeAcrossAPivotIsSplitBetweenTheLegs)
{
    // long from 120 (bar 2) to 120 (bar 4): up to 130, then back down
    const CaptureResult r = run({closedTrade(Direction::Long, 2, 120, 4, 120)});

    EXPECT_NEAR(r.captured, 0.0, 1e-12);
    EXPECT_EQ(r.legsRight, 1);              // it earned in leg 1 and lost in leg 2
}

TEST(CaptureTest, AnExitPriceAwayFromTheCloseIsUsedOnTheExitBar)
{
    // stopped out on bar 1 at 105 although that bar closed at 110
    const CaptureResult r = run({closedTrade(Direction::Long, 0, 100, 1, 105)});

    EXPECT_NEAR(r.captured, std::log(105.0 / 100.0), 1e-12);
}

TEST(CaptureTest, ShortTradesEarnWhenPriceFalls)
{
    const CaptureResult r = run({closedTrade(Direction::Short, 3, 130, 4, 120)});

    EXPECT_NEAR(r.captured, std::log(130.0 / 120.0), 1e-12);
}

TEST(CaptureTest, OpenTradesAreMarkedAtTheLastClose)
{
    // short from the top, still open at the end
    const CaptureResult r = run({}, {openTrade(Direction::Short, 3, 130)});

    EXPECT_NEAR(r.captured, std::log(130.0 / 100.0), 1e-12);
    EXPECT_NEAR(r.efficiency, 0.5, 1e-12);
}

TEST(CaptureTest, TwoTradesAtOnceAdd)
{
    const CaptureResult r = run({closedTrade(Direction::Long, 0, 100, 3, 130),
                                 closedTrade(Direction::Long, 0, 100, 3, 130)});

    EXPECT_NEAR(r.captured, 2 * kLeg, 1e-12);
}

TEST(CaptureTest, OnlyTradesOnTheRequestedAssetCount)
{
    const std::vector<ClosedTrade> closed{closedTrade(Direction::Long, 0, 100, 3, 130, 1)};

    const CaptureResult asset0 = captureEfficiency(barsFrom(kCloses), kPivots, closed, {}, 0);
    const CaptureResult asset1 = captureEfficiency(barsFrom(kCloses), kPivots, closed, {}, 1);

    EXPECT_DOUBLE_EQ(asset0.captured, 0.0);
    EXPECT_NEAR(asset1.captured, kLeg, 1e-12);
}

TEST(CaptureTest, TimeOutsideTheFirstAndLastPivotCountsForNothing)
{
    // pivots only up to bar 3: a trade over bars 3..6 is outside every leg
    const std::vector<Pivot> pivots{{0, 100, false}, {3, 130, true}};

    const CaptureResult r = captureEfficiency(barsFrom(kCloses), pivots,
        {closedTrade(Direction::Short, 3, 130, 6, 100)}, {});

    EXPECT_DOUBLE_EQ(r.captured, 0.0);
    EXPECT_EQ(r.legs, 1);
}

TEST(CaptureTest, FewerThanTwoPivotsMeansNoLegs)
{
    const std::vector<ClosedTrade> closed{closedTrade(Direction::Long, 0, 100, 3, 130)};

    for (const std::vector<Pivot>& pivots : {std::vector<Pivot>{}, std::vector<Pivot>{{0, 100, false}}})
    {
        const CaptureResult r = captureEfficiency(barsFrom(kCloses), pivots, closed, {});

        EXPECT_EQ(r.legs, 0);
        EXPECT_DOUBLE_EQ(r.available, 0.0);
        EXPECT_DOUBLE_EQ(r.efficiency, 0.0);
        EXPECT_DOUBLE_EQ(r.rightShare, 0.0);
    }
}

TEST(CaptureTest, ATradeWhoseTimesAreNotBarTimestampsIsIgnored)
{
    const CaptureResult r = run({closedTrade(Direction::Long, 0, 100, 99, 130)});

    EXPECT_DOUBLE_EQ(r.captured, 0.0);
}

TEST(CaptureTest, EmptyBarsGiveAnEmptyResult)
{
    const CaptureResult r = captureEfficiency({}, kPivots, {}, {});

    EXPECT_EQ(r.legs, 0);
    EXPECT_DOUBLE_EQ(r.efficiency, 0.0);
}

// ---------- helpers on aligned data ----------

namespace
{
// rows[day][asset]
AlignedData dataOf(const std::vector<std::vector<std::optional<double>>>& rows)
{
    AlignedData d;
    for (std::size_t a = 0; a < rows[0].size(); ++a)
        d.symbols.push_back("A" + std::to_string(a));
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        Snapshot s;
        s.timestamp = static_cast<std::int64_t>(i);
        for (const auto& c : rows[i])
        {
            if (c)
            {
                Bar b;
                b.timestamp = s.timestamp;
                b.open = b.high = b.low = b.close = *c;
                s.bars.push_back(b);
            }
            else
                s.bars.push_back(std::nullopt);
        }
        d.snapshots.push_back(s);
    }
    return d;
}

AlignedData twoCopiesOfThePath()
{
    std::vector<std::vector<std::optional<double>>> rows;
    for (double c : kCloses)
        rows.push_back({c, c});
    return dataOf(rows);
}
}

TEST(BarsOfTest, ReturnsOneAssetsBarsInOrder)
{
    const AlignedData d = dataOf({{1, 10}, {2, 20}, {3, 30}});

    const std::vector<Bar> bars = barsOf(d, 1);

    ASSERT_EQ(bars.size(), 3u);
    EXPECT_DOUBLE_EQ(bars[0].close, 10);
    EXPECT_DOUBLE_EQ(bars[2].close, 30);
}

TEST(BarsOfTest, SkipsDaysWhenTheAssetHasNoBar)
{
    const AlignedData d = dataOf({{1, 10}, {2, std::nullopt}, {3, 30}});

    const std::vector<Bar> bars = barsOf(d, 1);

    ASSERT_EQ(bars.size(), 2u);
    EXPECT_EQ(bars[0].timestamp, 0);
    EXPECT_EQ(bars[1].timestamp, 2);
}

TEST(BarsOfTest, AnUnknownAssetGivesNoBars)
{
    const AlignedData d = dataOf({{1, 10}});

    EXPECT_TRUE(barsOf(d, 5).empty());
    EXPECT_TRUE(barsOf(d, -1).empty());
}

TEST(ZigZagAllTest, GivesOnePivotListPerAsset)
{
    const std::vector<std::vector<Pivot>> all = zigzagAll(twoCopiesOfThePath(), 1.0, 2);

    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].size(), all[1].size());
    EXPECT_FALSE(all[0].empty());
}

// ---------- all assets together ----------

TEST(CaptureAllAssetsTest, SumsTheAssetsBeforeDividing)
{
    const AlignedData d = twoCopiesOfThePath();
    const std::vector<std::vector<Pivot>> pivots{kPivots, kPivots};

    BacktestResult result;
    result.closedTrades.push_back(closedTrade(Direction::Long, 0, 100, 3, 130, 0));   // asset 0 only

    const CaptureResult r = captureEfficiency(d, pivots, result);

    EXPECT_NEAR(r.available, 4 * kLeg, 1e-12);       // 2 assets x 2 legs
    EXPECT_NEAR(r.captured, kLeg, 1e-12);
    EXPECT_NEAR(r.efficiency, 0.25, 1e-12);
    EXPECT_EQ(r.legs, 4);
    EXPECT_EQ(r.legsRight, 1);
    EXPECT_DOUBLE_EQ(r.rightShare, 0.25);
}

TEST(CaptureAllAssetsTest, NeedsOnePivotListPerAsset)
{
    const AlignedData d = twoCopiesOfThePath();

    EXPECT_THROW(captureEfficiency(d, {kPivots}, BacktestResult{}), std::invalid_argument);
}

TEST(CaptureAllAssetsTest, ScoresARealBacktestRun)
{
    // FixedLine(105): short at 100, flips long at 110, flips short at 100 (the last trade stays open).
    //   bar 1:  the short loses ln(110/100)                      (inside leg 1)
    //   bars 2-3: the long earns ln(130/110)                     (inside leg 1)
    //   bars 4-6: the long falls from 130 to 100: ln(100/130)    (inside leg 2)
    const AlignedData d = twoCopiesOfThePath();
    FlipStrategy<FixedLine> strategy(2, FixedLine(105));
    const BacktestResult run = runMultiBacktest(d, strategy, 100000.0);

    const CaptureResult r = captureEfficiency(d, {kPivots, kPivots}, run);

    const double perAsset = -std::log(1.1) + std::log(130.0 / 110.0) + std::log(100.0 / 130.0);
    EXPECT_NEAR(r.captured, 2 * perAsset, 1e-9);
    EXPECT_NEAR(r.available, 4 * kLeg, 1e-12);
    EXPECT_NEAR(r.efficiency, perAsset / (2 * kLeg), 1e-9);
    EXPECT_EQ(r.legs, 4);
}
