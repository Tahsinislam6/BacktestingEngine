#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flip_strategy.hpp"
#include "study.hpp"

// runStudy chooses parameters on the in-sample assets, checks them on later years of the same assets, and
// runs the winner once on out-of-sample assets. Stand-in baseline: FixedLine, a flat line at params["level"].
//
// Data: a rising line (close = 100 + 2*i, 51 days). A level far below the price is long all the way
// (best); levels 50, 70 and 90 tie, and the first (50) wins.

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

const StrategyFactory factory = [](const Params& p, std::size_t assets) -> std::unique_ptr<MultiStrategy>
{
    return std::make_unique<FlipStrategy<FixedLine>>(assets, FixedLine(p.get("level")));
};

const std::vector<ParamRange> ranges{{"level", 50, 150, 20}};

AlignedData dataOf(const std::vector<std::vector<double>>& closesPerAsset)
{
    AlignedData d;
    for (std::size_t a = 0; a < closesPerAsset.size(); ++a)
        d.symbols.push_back("A" + std::to_string(a));
    for (std::size_t i = 0; i < closesPerAsset[0].size(); ++i)
    {
        Snapshot s;
        s.timestamp = static_cast<std::int64_t>(i);
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

std::vector<double> line(double from, double step, int n = 51)
{
    std::vector<double> v;
    for (int i = 0; i < n; ++i)
        v.push_back(from + step * i);
    return v;
}

AlignedData rising() { return dataOf({line(100, 2)}); }
AlignedData falling() { return dataOf({line(200, -2)}); }

StudyOptions options(int minTrades = 0)
{
    StudyOptions o;
    o.validation.optimise.grid.threads = 1;
    o.validation.optimise.objective = Objective::TotalReturn;
    o.validation.optimise.minTrades = minTrades;
    return o;
}
}

// ---------- runStudy ----------

TEST(StudyTest, RanksEveryParameterSetOnTheInSampleAssets)
{
    const StudyResult s = runStudy(rising(), nullptr, ranges, factory, options());

    EXPECT_EQ(s.allRuns.size(), 6u);
    ASSERT_EQ(s.ranking.size(), 6u);
    EXPECT_DOUBLE_EQ(s.ranking[0].evaluation.params.get("level"), 50);
    EXPECT_NEAR(s.ranking[0].score, 0.10, 1e-9);                 // 100 units, 100 -> 200
}

TEST(StudyTest, ChecksTheStabilityOfTheWinner)
{
    const StudyResult s = runStudy(rising(), nullptr, ranges, factory, options());

    ASSERT_TRUE(s.stability.has_value());
    EXPECT_EQ(s.stability->neighbours, 1u);                      // level 50 is at the edge; 70 ties with it
    EXPECT_NEAR(s.stability->meanNeighbour, s.stability->score, 1e-12);
}

TEST(StudyTest, RunsTheTimeSplitOnTheInSampleAssets)
{
    StudyOptions o = options();
    o.trainFraction = 0.7;

    const StudyResult s = runStudy(rising(), nullptr, ranges, factory, o);

    EXPECT_EQ(s.timeSplit.splitIndex, 35u);                      // floor(51 * 0.7)
    ASSERT_TRUE(s.timeSplit.test.has_value());
    EXPECT_DOUBLE_EQ(s.timeSplit.test->params.get("level"), 50);
}

TEST(StudyTest, WalkForwardIsSkippedUnlessBothWindowsAreGiven)
{
    EXPECT_FALSE(runStudy(rising(), nullptr, ranges, factory, options()).walkForward.has_value());

    StudyOptions oneOnly = options();
    oneOnly.walkTrainBars = 20;
    EXPECT_FALSE(runStudy(rising(), nullptr, ranges, factory, oneOnly).walkForward.has_value());
}

TEST(StudyTest, WalkForwardRunsWhenAsked)
{
    StudyOptions o = options();
    o.walkTrainBars = 20;
    o.walkTestBars = 10;

    const StudyResult s = runStudy(rising(), nullptr, ranges, factory, o);

    ASSERT_TRUE(s.walkForward.has_value());
    EXPECT_EQ(s.walkForward->folds.size(), 3u);
}

TEST(StudyTest, ReportsTheWinnerPerInSampleAsset)
{
    const StudyResult s = runStudy(dataOf({line(100, 2), line(100, 4)}), nullptr, ranges, factory, options());

    ASSERT_EQ(s.inSampleAssets.size(), 2u);
    EXPECT_EQ(s.inSampleAssets[0].symbol, "A0");
    EXPECT_NEAR(s.inSampleAssets[0].pnl, 100.0 * 100, 1e-6);          // 100 units, +100
    EXPECT_NEAR(s.inSampleAssets[1].pnl, 100.0 * 200, 1e-6);          // 100 units, +200
}

TEST(StudyTest, WithoutOutOfSampleAssetsThatStepIsSkipped)
{
    const StudyResult s = runStudy(rising(), nullptr, ranges, factory, options());

    EXPECT_FALSE(s.outOfSample.has_value());
    EXPECT_TRUE(s.outOfSampleAssets.empty());
}

TEST(StudyTest, RunsTheWinnerOnceOnTheOutOfSampleAssets)
{
    // out of sample the price FALLS from 200 to 100: the always-long winner loses 50 units * 100
    const AlignedData out = falling();

    const StudyResult s = runStudy(rising(), &out, ranges, factory, options());

    ASSERT_TRUE(s.outOfSample.has_value());
    EXPECT_DOUBLE_EQ(s.outOfSample->params.get("level"), 50);
    EXPECT_NEAR(s.outOfSample->metrics.totalReturn, -0.05, 1e-9);
    ASSERT_EQ(s.outOfSampleAssets.size(), 1u);
    EXPECT_NEAR(s.outOfSampleAssets[0].pnl, -5000.0, 1e-6);
}

TEST(StudyTest, TheOutOfSampleAssetsDoNotInfluenceTheChoice)
{
    const AlignedData out = falling();

    const StudyResult with = runStudy(rising(), &out, ranges, factory, options());
    const StudyResult without = runStudy(rising(), nullptr, ranges, factory, options());

    ASSERT_EQ(with.ranking.size(), without.ranking.size());
    for (std::size_t i = 0; i < with.ranking.size(); ++i)
    {
        EXPECT_EQ(with.ranking[i].evaluation.params, without.ranking[i].evaluation.params);
        EXPECT_EQ(with.ranking[i].score, without.ranking[i].score);
    }
}

TEST(StudyTest, TheOutOfSampleGroupCanHaveDifferentAssets)
{
    const AlignedData out = dataOf({line(200, -2), line(100, 2), line(150, 1)});

    const StudyResult s = runStudy(rising(), &out, ranges, factory, options());

    EXPECT_EQ(s.outOfSampleAssets.size(), 3u);
}

TEST(StudyTest, WithNoWinnerNothingFurtherIsChecked)
{
    const AlignedData out = falling();

    const StudyResult s = runStudy(rising(), &out, ranges, factory, options(100));

    EXPECT_TRUE(s.ranking.empty());
    EXPECT_FALSE(s.stability.has_value());
    EXPECT_FALSE(s.outOfSample.has_value());
    EXPECT_TRUE(s.inSampleAssets.empty());
    EXPECT_FALSE(s.timeSplit.test.has_value());
}

// ---------- formatStudy ----------

TEST(FormatStudyTest, ShowsTheSectionsThatWereRun)
{
    StudyOptions o = options();
    o.walkTrainBars = 20;
    o.walkTestBars = 10;
    const AlignedData out = falling();

    const std::string text = formatStudy(runStudy(rising(), &out, ranges, factory, o), o, 5);

    EXPECT_NE(text.find("IN-SAMPLE"), std::string::npos);
    EXPECT_NE(text.find("WINNER: level=50"), std::string::npos);
    EXPECT_NE(text.find("Stability"), std::string::npos);
    EXPECT_NE(text.find("TIME SPLIT"), std::string::npos);
    EXPECT_NE(text.find("WALK-FORWARD"), std::string::npos);
    EXPECT_NE(text.find("OUT-OF-SAMPLE"), std::string::npos);
    EXPECT_NE(text.find("A0"), std::string::npos);                // the per-asset tables
}

TEST(FormatStudyTest, NoPercentageIsShownForANegativeWinnerScore)
{
    // on a falling line, levels 50 to 90 are all long the whole way down: every run loses
    StudyOptions o = options(0);
    const std::vector<ParamRange> allLong{{"level", 50, 90, 20}};

    const std::string text = formatStudy(runStudy(falling(), nullptr, allLong, factory, o), o, 5);

    EXPECT_NE(text.find("Stability"), std::string::npos);
    EXPECT_NE(text.find("n/a of the winner"), std::string::npos);
}

TEST(FormatStudyTest, LeavesOutSectionsThatWereNotRun)
{
    const StudyOptions o = options();

    const std::string text = formatStudy(runStudy(rising(), nullptr, ranges, factory, o), o, 5);

    EXPECT_EQ(text.find("WALK-FORWARD"), std::string::npos);
    EXPECT_EQ(text.find("OUT-OF-SAMPLE"), std::string::npos);
}

TEST(FormatStudyTest, SaysSoWhenThereIsNoWinner)
{
    const StudyOptions o = options(100);

    const std::string text = formatStudy(runStudy(rising(), nullptr, ranges, factory, o), o, 5);

    EXPECT_NE(text.find("No winner"), std::string::npos);
    EXPECT_EQ(text.find("WINNER:"), std::string::npos);
}

TEST(FormatStudyTest, NamesTheObjectiveAndTheMinimumTrades)
{
    StudyOptions o = options(0);
    o.validation.optimise.objective = Objective::Sortino;

    const std::string text = formatStudy(runStudy(rising(), nullptr, ranges, factory, o), o, 5);

    EXPECT_NE(text.find("ranked by sortino"), std::string::npos);
    EXPECT_NE(text.find("minimum 0 trades"), std::string::npos);
}

TEST(FormatStudyTest, TheTopCountLimitsTheRankedRows)
{
    const StudyOptions o = options();

    const std::string two = formatStudy(runStudy(rising(), nullptr, ranges, factory, o), o, 2);

    EXPECT_NE(two.find("level=50"), std::string::npos);
    EXPECT_NE(two.find("level=70"), std::string::npos);
    EXPECT_EQ(two.find("level=150"), std::string::npos);
}

// ---------- R-multiples ----------

namespace
{
// Opens a long pair on the first snapshot: a half with stop 90 and target 120, a runner with stop 90.
class PairOnce : public MultiStrategy
{
public:
    std::vector<Order> onSnapshot(const Snapshot&, const Portfolio&) override
    {
        if (done_)
            return {};
        done_ = true;
        return {openOrder(Direction::Long, 1, 90.0, 120.0, 0), openOrder(Direction::Long, 1, 90.0, std::nullopt, 0)};
    }
private:
    bool done_{false};
};

const StrategyFactory pairFactory = [](const Params&, std::size_t) -> std::unique_ptr<MultiStrategy>
{
    return std::make_unique<PairOnce>();
};
}

TEST(StudyTest, WithStopsTheWinnerComesWithRMultiples)
{
    // rising 100, 102, ... : entry at 100, stop 90 (risk 10), the target 120 is hit at 120 -> +2R for that half
    const AlignedData out = rising();
    const StudyResult s = runStudy(rising(), &out, {{"x", 1, 1, 1}}, pairFactory, options());

    ASSERT_TRUE(s.inSampleR.has_value());
    EXPECT_EQ(s.inSampleR->all.target.count, 1);
    EXPECT_NEAR(s.inSampleR->targetHalf.totalR, 2.0, 1e-9);
    ASSERT_TRUE(s.outOfSampleR.has_value());
    EXPECT_EQ(s.outOfSampleR->all.target.count, 1);
}

TEST(StudyTest, WithoutStopsThereAreNoRMultiples)
{
    const StudyResult s = runStudy(rising(), nullptr, ranges, factory, options());

    EXPECT_FALSE(s.inSampleR.has_value());
    EXPECT_FALSE(s.outOfSampleR.has_value());
}

TEST(FormatStudyTest, ShowsTheRMultiplesWhenThereAreStops)
{
    const AlignedData out = rising();
    const StudyOptions o = options();

    const std::string text = formatStudy(runStudy(rising(), &out, {{"x", 1, 1, 1}}, pairFactory, o), o, 5);

    EXPECT_NE(text.find("R-MULTIPLES"), std::string::npos);
    EXPECT_NE(text.find("target half"), std::string::npos);
    EXPECT_NE(text.find("runner"), std::string::npos);
    EXPECT_NE(text.find("stop"), std::string::npos);
    EXPECT_NE(text.find("breakeven"), std::string::npos);
    EXPECT_NE(text.find("trailing"), std::string::npos);
    EXPECT_NE(text.find("still open"), std::string::npos);
}

TEST(StudyTest, TradesStillOpenAtTheEndAreCounted)
{
    // PairOnce: the target half closes at 120; the runner is still open when the data ends
    const StudyResult s = runStudy(rising(), nullptr, {{"x", 1, 1, 1}}, pairFactory, options());

    ASSERT_TRUE(s.inSampleR.has_value());
    EXPECT_EQ(s.inSampleR->openAtEnd, 1);
}

TEST(FormatStudyTest, LeavesOutTheRMultiplesWithoutStops)
{
    const StudyOptions o = options();

    const std::string text = formatStudy(runStudy(rising(), nullptr, ranges, factory, o), o, 5);

    EXPECT_EQ(text.find("R-MULTIPLES"), std::string::npos);
}
