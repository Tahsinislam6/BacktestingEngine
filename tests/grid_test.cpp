#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "grid.hpp"

// The grid tests every baseline x C1 pair in one batch (phase one of the plan, see README.md):
//   - each pair is run on the selection data (the in-sample and the out-of-sample folders) with fixed parameters, and
//     scored by how far its out-of-sample expectancy is above its own random-direction runs (same signals, stops, costs)
//   - the multiple-testing correction: the best of M pairs beats luck by chance far more often than one pair, so the
//     score is also judged against ALL pairs' random runs pooled, raised to the number of pairs tried
//   - a pair that survives is a finalist; each finalist is run ONCE on the holdout with the same parameters
// Stand-ins: FixedLine at 100 as the baseline, and a C1 with a fixed state. The trending data rewards the baseline's
// direction, the reversing data punishes it (see control_test.cpp).

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

GridBaseline fixedLine(const std::string& name)
{
    BaselineInfo b;
    b.name = name;
    b.ranges = {{"level", 100, 100, 1}};
    b.warmupBars = [](const Params&) { return std::size_t{1}; };
    b.nnfx = [](const NnfxOptions& o) -> StrategyFactory
    {
        return [o](const Params& p, std::size_t assets) -> std::unique_ptr<MultiStrategy>
        {
            return std::make_unique<NnfxStrategy<FixedLine>>(assets, FixedLine(p.get("level")), o);
        };
    };
    return {b, Params{{"level", 100}}};
}

GridBaseline broken(const std::string& name)
{
    GridBaseline g = fixedLine(name);
    g.info.nnfx = [](const NnfxOptions&) -> StrategyFactory
    {
        return [](const Params&, std::size_t) -> std::unique_ptr<MultiStrategy>
        {
            throw std::runtime_error("this indicator blew up");
        };
    };
    return g;
}

// a C1 that always says bullish: it lets longs through and blocks shorts
class Bullish : public AnyState
{
public:
    void update(const Bar&) override {}
    bool ready() const override { return true; }
    int state() const override { return 1; }
    std::size_t warmupBars() const override { return 1; }
    std::unique_ptr<AnyState> clone() const override { return std::make_unique<Bullish>(); }
};

GridC1 bullish(const std::string& name) { return {name, std::make_shared<Bullish>()}; }

AlignedData dataOf(const std::vector<double>& closes)
{
    AlignedData d;
    d.symbols = {"A0"};
    for (std::size_t i = 0; i < closes.size(); ++i)
    {
        Snapshot s;
        s.timestamp = static_cast<std::int64_t>(i + 1);
        Bar b;
        b.timestamp = s.timestamp;
        b.open = b.high = b.low = b.close = closes[i];
        s.bars.push_back(b);
        d.snapshots.push_back(s);
    }
    return d;
}

std::vector<double> trending(int legs)
{
    std::vector<double> c{90};
    for (int leg = 0; leg < legs; ++leg)
        for (int i = 0; i < 10; ++i)
            c.push_back(c.back() + (leg % 2 == 0 ? 2.0 : -2.0));
    return c;
}

std::vector<double> reversing(int cycles)
{
    std::vector<double> c;
    for (int k = 0; k < cycles; ++k)
    {
        for (int i = 0; i < 5; ++i)
            c.push_back(90);
        c.push_back(101);
        c.push_back(90);
    }
    return c;
}

// a trend followed by reversals: the baseline still beats luck (5 cycles) or earns a loss (20 cycles)
std::vector<double> trendThenReversing(int legs, int cycles)
{
    std::vector<double> c = trending(legs);
    const std::vector<double> r = reversing(cycles);
    c.insert(c.end(), r.begin(), r.end());
    return c;
}

GridOptions options(int runs = 30)
{
    GridOptions o;
    o.controlRuns = runs;
    o.nnfx.atrPeriod = 3;
    o.nnfx.maxDistanceAtr = 2.0;
    o.nnfx.exitOnCross = false;
    o.run.threads = 1;
    o.minBeats = 0.9;
    o.minExpectancy = 0.0;
    return o;
}

std::vector<GridRow> grid(const std::vector<GridBaseline>& b, const std::vector<GridC1>& c, const AlignedData& in,
                          const AlignedData& out, const GridOptions& o = options(), const AlignedData* holdout = nullptr)
{
    return runGrid(b, c, in, out, o, holdout);
}

const GridRow& rowOf(const std::vector<GridRow>& rows, const std::string& baseline, const std::string& c1)
{
    for (const GridRow& r : rows)
        if (r.baseline == baseline && r.c1 == c1)
            return r;
    throw std::runtime_error("no such row");
}
}

// ---------- the multiple-testing correction ----------

TEST(GridCorrectionTest, AStandardScoreIsMeasuredInRandomStandardDeviations)
{
    EXPECT_NEAR(standardScore(4.0, {1.0, 2.0, 3.0}), 2.0, 1e-12);          // mean 2, sd 1
    EXPECT_NEAR(standardScore(0.0, {1.0, 2.0, 3.0}), -2.0, 1e-12);
    EXPECT_NEAR(standardScore(10.0, {0.0, 10.0}), 5.0 / std::sqrt(50.0), 1e-12);        // mean 5, sd sqrt(50)
}

TEST(GridCorrectionTest, AStandardScoreIgnoresNanSamplesAndNeedsTwoOfThem)
{
    EXPECT_NEAR(standardScore(4.0, {1.0, std::nan(""), 2.0, 3.0}), 2.0, 1e-12);
    EXPECT_TRUE(std::isnan(standardScore(4.0, {1.0})));
    EXPECT_TRUE(std::isnan(standardScore(4.0, {1.0, std::nan("")})));
    EXPECT_TRUE(std::isnan(standardScore(4.0, {})));
    EXPECT_TRUE(std::isnan(standardScore(std::nan(""), {1.0, 2.0, 3.0})));
}

TEST(GridCorrectionTest, AStandardScoreIsUndefinedWhenTheRandomRunsAllAgree)
{
    EXPECT_TRUE(std::isnan(standardScore(4.0, {2.0, 2.0, 2.0})));
}

TEST(GridCorrectionTest, TheNullScoresStandardiseEachSampleAgainstItself)
{
    const std::vector<double> z = nullScores({1.0, 2.0, 3.0});
    ASSERT_EQ(z.size(), 3u);
    EXPECT_NEAR(z[0], -1.0, 1e-12);
    EXPECT_NEAR(z[1], 0.0, 1e-12);
    EXPECT_NEAR(z[2], 1.0, 1e-12);
    EXPECT_TRUE(nullScores({2.0, 2.0}).empty());                           // no spread: nothing to standardise
}

TEST(GridCorrectionTest, TheAdjustedBeatsAreTheShareOfLuckBeatenRaisedToTheNumberOfTries)
{
    const std::vector<double> luck{0.0, 1.0, 2.0, 3.0};                    // n = 4
    // above all of it: (4 - 0) / (4 + 1) = 0.8 (a finite sample can never prove "never"), one try or two
    EXPECT_NEAR(familyAdjustedBeats(10.0, luck, 1), 0.8, 1e-12);
    EXPECT_NEAR(familyAdjustedBeats(10.0, luck, 2), 0.64, 1e-12);
    // two of the four are at or above 1.5: (4 - 2) / 5 = 0.4
    EXPECT_NEAR(familyAdjustedBeats(1.5, luck, 1), 0.4, 1e-12);
    EXPECT_NEAR(familyAdjustedBeats(1.5, luck, 3), 0.064, 1e-12);
}

TEST(GridCorrectionTest, ATieWithTheLuckCountsAgainstThePair)
{
    // 2 and 3 are at or above 2: (4 - 2) / 5 = 0.4, not the 0.6 a half-tie would give
    EXPECT_NEAR(familyAdjustedBeats(2.0, {0.0, 1.0, 2.0, 3.0}, 1), 0.4, 1e-12);
}

TEST(GridCorrectionTest, MoreTriesMeanALowerAdjustedScore)
{
    const std::vector<double> luck{0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0};
    EXPECT_GT(familyAdjustedBeats(7.5, luck, 1), familyAdjustedBeats(7.5, luck, 5));
    EXPECT_GT(familyAdjustedBeats(7.5, luck, 5), familyAdjustedBeats(7.5, luck, 50));
}

TEST(GridCorrectionTest, TheAdjustedBeatsAreUndefinedWithoutAScoreOrLuck)
{
    EXPECT_TRUE(std::isnan(familyAdjustedBeats(std::nan(""), {0.0, 1.0}, 1)));
    EXPECT_TRUE(std::isnan(familyAdjustedBeats(1.0, {}, 1)));
    EXPECT_THROW(familyAdjustedBeats(1.0, {0.0, 1.0}, 0), std::invalid_argument);
}

// ---------- the batch ----------

TEST(GridTest, GivesOneRowPerBaselineAndC1Pair)
{
    const auto rows = grid({fixedLine("alpha"), fixedLine("beta")}, {bullish("c1"), bullish("c2"), bullish("c3")},
                           dataOf(trending(40)), dataOf(trending(30)));

    ASSERT_EQ(rows.size(), 6u);
    for (const char* b : {"alpha", "beta"})
        for (const char* c : {"c1", "c2", "c3"})
            EXPECT_NO_THROW(rowOf(rows, b, c)) << b << " " << c;
}

TEST(GridTest, FillsInTheNumbersOfAPair)
{
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)));

    const GridRow& r = rows[0];
    EXPECT_TRUE(r.error.empty());
    EXPECT_EQ(r.params, "level=100");
    EXPECT_GT(r.oosTrades, 0);
    EXPECT_FALSE(std::isnan(r.oosExpectancyR));
    EXPECT_FALSE(std::isnan(r.oosBeatsExpectancy));
    EXPECT_FALSE(std::isnan(r.oosScore));
    EXPECT_FALSE(std::isnan(r.oosAdjustedBeats));
    EXPECT_GT(r.inSampleTrades, 0);
    EXPECT_FALSE(std::isnan(r.inSampleExpectancyR));
    EXPECT_EQ(r.familySize, 1);
}

TEST(GridTest, APairWhoseDirectionIsRightIsAFinalist)
{
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)));

    EXPECT_GE(rows[0].oosBeatsExpectancy, 0.9);
    EXPECT_GT(rows[0].oosExpectancyR, 0.0);
    EXPECT_GT(rows[0].inSampleExpectancyR, 0.0);
    EXPECT_TRUE(rows[0].passedStageOne);
}

TEST(GridTest, APairWhoseDirectionIsWrongIsNot)
{
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(reversing(40)), dataOf(reversing(30)));

    EXPECT_LT(rows[0].oosBeatsExpectancy, 0.1);
    EXPECT_FALSE(rows[0].passedStageOne);
    EXPECT_FALSE(rows[0].candidate);
}

TEST(GridTest, TheInSampleFolderMustAgreeWithTheOutOfSampleFolder)
{
    // right on the out-of-sample pairs, wrong on the in-sample ones: no replication, no finalist
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(reversing(40)), dataOf(trending(30)));

    EXPECT_GT(rows[0].oosBeatsExpectancy, 0.9);
    EXPECT_LT(rows[0].inSampleExpectancyR, 0.0);
    EXPECT_FALSE(rows[0].passedStageOne);

    // and the other way round
    const auto flipped = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(reversing(30)));
    EXPECT_GT(flipped[0].inSampleExpectancyR, 0.0);
    EXPECT_FALSE(flipped[0].passedStageOne);
}

TEST(GridTest, TheThresholdsDecideTheVerdict)
{
    GridOptions strictBeats = options();
    strictBeats.minBeats = 1.01;                       // impossible
    EXPECT_FALSE(grid({fixedLine("a")}, {bullish("c")}, dataOf(trending(40)), dataOf(trending(30)), strictBeats)[0].passedStageOne);

    GridOptions strictR = options();
    strictR.minExpectancy = 1000.0;                    // impossible
    EXPECT_FALSE(grid({fixedLine("a")}, {bullish("c")}, dataOf(trending(40)), dataOf(trending(30)), strictR)[0].passedStageOne);
}

TEST(GridTest, ABrokenPairIsReportedAndTheOthersStillRun)
{
    const auto rows = grid({broken("bad"), fixedLine("ok")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)));

    ASSERT_EQ(rows.size(), 2u);
    const GridRow& bad = rowOf(rows, "bad", "c1");
    const GridRow& ok = rowOf(rows, "ok", "c1");
    EXPECT_NE(bad.error.find("blew up"), std::string::npos);
    EXPECT_FALSE(bad.passedStageOne);
    EXPECT_TRUE(ok.error.empty());
}

TEST(GridTest, RowsAreSortedByTheAdjustedBeatsBestFirstAndFailuresLast)
{
    const auto rows = grid({broken("bad"), fixedLine("ok")}, {bullish("c1"), bullish("c2")}, dataOf(trending(40)),
                           dataOf(trending(30)));
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].baseline, "ok");
    EXPECT_EQ(rows[1].baseline, "ok");
    EXPECT_EQ(rows[2].baseline, "bad");
    EXPECT_EQ(rows[3].baseline, "bad");
    EXPECT_GE(rows[0].oosAdjustedBeats, rows[1].oosAdjustedBeats);
}

TEST(GridTest, RowsComeBackBestAdjustedBeatsFirst)
{
    // lines at 108, 100 and 92 given in the worst-first order; on this data the line at 92 does best and 108 worst
    std::vector<GridBaseline> baselines;
    for (int level : {108, 100, 92})
    {
        GridBaseline b = fixedLine("l" + std::to_string(level));
        b.params = Params{{"level", static_cast<double>(level)}};
        b.info.ranges = {{"level", 90, 110, 1}};
        baselines.push_back(b);
    }
    const auto rows = grid(baselines, {bullish("c1")}, dataOf(trendThenReversing(30, 8)), dataOf(trendThenReversing(30, 8)));

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].baseline, "l92");
    EXPECT_EQ(rows[2].baseline, "l108");
    EXPECT_GT(rows[0].oosAdjustedBeats, rows[1].oosAdjustedBeats);
    EXPECT_GT(rows[1].oosAdjustedBeats, rows[2].oosAdjustedBeats);
}

TEST(GridTest, EqualAdjustedBeatsAreOrderedByTheScore)
{
    // the lines at 104 and 92 both reach the same adjusted beats (the pooled luck cannot tell them apart) but 92 has the higher z
    std::vector<GridBaseline> baselines;
    for (int level : {104, 92})
    {
        GridBaseline b = fixedLine("l" + std::to_string(level));
        b.params = Params{{"level", static_cast<double>(level)}};
        b.info.ranges = {{"level", 90, 110, 1}};
        baselines.push_back(b);
    }
    const auto rows = grid(baselines, {bullish("c1")}, dataOf(trendThenReversing(30, 8)), dataOf(trendThenReversing(30, 8)));

    ASSERT_EQ(rows.size(), 2u);
    ASSERT_DOUBLE_EQ(rows[0].oosAdjustedBeats, rows[1].oosAdjustedBeats);
    EXPECT_EQ(rows[0].baseline, "l92");
    EXPECT_GT(rows[0].oosScore, rows[1].oosScore);
}

TEST(GridTest, TheGridPutsInItsOwnC1SoTheOptionsMustNotHaveOne)
{
    GridOptions withOne = options();
    withOne.nnfx.components.confirmation = std::make_shared<Bullish>();
    EXPECT_THROW(grid({fixedLine("a")}, {bullish("c")}, dataOf(trending(40)), dataOf(trending(30)), withOne),
                 std::invalid_argument);
}

TEST(GridTest, ResultsDoNotDependOnTheThreadCount)
{
    GridOptions one = options();
    GridOptions many = options();
    many.run.threads = 4;

    const auto a = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), one);
    const auto b = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), many);

    EXPECT_DOUBLE_EQ(a[0].oosExpectancyR, b[0].oosExpectancyR);
    EXPECT_DOUBLE_EQ(a[0].oosBeatsExpectancy, b[0].oosBeatsExpectancy);
    EXPECT_DOUBLE_EQ(a[0].oosScore, b[0].oosScore);
}

TEST(GridTest, TheC1IsTheOnlyThingThatDiffersBetweenPairs)
{
    // a C1 that is always bearish blocks the longs, so it must give different trades from the always-bullish one
    class Bearish : public AnyState
    {
    public:
        void update(const Bar&) override {}
        bool ready() const override { return true; }
        int state() const override { return -1; }
        std::size_t warmupBars() const override { return 1; }
        std::unique_ptr<AnyState> clone() const override { return std::make_unique<Bearish>(); }
    };
    const auto rows = grid({fixedLine("alpha")}, {bullish("bull"), {"bear", std::make_shared<Bearish>()}},
                           dataOf(trending(40)), dataOf(trending(30)));
    EXPECT_NE(rowOf(rows, "alpha", "bull").oosExpectancyR, rowOf(rows, "alpha", "bear").oosExpectancyR);
}

TEST(GridTest, RejectsOptionsAndInputsTheControlCannotUse)
{
    const AlignedData in = dataOf(trending(40)), out = dataOf(trending(30));
    GridOptions crossBack = options();
    crossBack.nnfx.exitOnCross = true;
    EXPECT_THROW(grid({fixedLine("a")}, {bullish("c")}, in, out, crossBack), std::invalid_argument);
    EXPECT_THROW(grid({fixedLine("a")}, {bullish("c")}, in, out, options(0)), std::invalid_argument);
    GridOptions negative = options();
    negative.priorTries = -1;
    EXPECT_THROW(grid({fixedLine("a"), fixedLine("b")}, {bullish("c")}, in, out, negative), std::invalid_argument);
    EXPECT_THROW(grid({}, {bullish("c")}, in, out), std::invalid_argument);
    EXPECT_THROW(grid({fixedLine("a")}, {}, in, out), std::invalid_argument);
}

// ---------- the tries ----------

TEST(GridTriesTest, ARowCarriesItsScoreItsAdjustedBeatsAndTheFamilySize)
{
    const auto rows = grid({fixedLine("alpha"), fixedLine("beta")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)));
    ASSERT_EQ(rows.size(), 2u);
    for (const GridRow& r : rows)
    {
        EXPECT_GT(r.oosScore, 0.0);
        EXPECT_EQ(r.familySize, 2);
        EXPECT_FALSE(std::isnan(r.oosAdjustedBeats));
        EXPECT_LE(r.oosAdjustedBeats, r.oosBeatsExpectancy + 1e-9);          // never kinder than the plain rank
    }
}

TEST(GridTriesTest, EarlierTriesCountInTheFamily)
{
    GridOptions o = options();
    o.priorTries = 5;
    const auto rows = grid({fixedLine("alpha"), fixedLine("beta")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), o);
    EXPECT_EQ(rows[0].familySize, 7);
}

TEST(GridTriesTest, EveryPairCountsAsATryEvenABrokenOne)
{
    const auto rows = grid({broken("bad"), fixedLine("ok")}, {bullish("c1"), bullish("c2")}, dataOf(trending(40)),
                           dataOf(trending(30)));
    EXPECT_EQ(rowOf(rows, "ok", "c1").familySize, 4);
}

TEST(GridTriesTest, ALongListOfTriesTurnsAFinalistIntoANonFinalist)
{
    GridOptions few = options();
    GridOptions many = options();
    many.priorTries = 100000;

    const auto a = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), few);
    const auto b = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), many);

    EXPECT_TRUE(a[0].passedStageOne);
    EXPECT_GE(b[0].oosBeatsExpectancy, 0.9);                // the plain rank is the same ...
    EXPECT_LT(b[0].oosAdjustedBeats, 0.9);                  // ... the adjusted one is not
    EXPECT_FALSE(b[0].passedStageOne);
}

TEST(GridTriesTest, TheMinimumBeatsAppliesToTheAdjustedScoreToo)
{
    GridOptions strict = options();
    strict.minBeats = 0.99;                                 // with 30 random runs the adjusted score cannot reach it
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), strict);
    EXPECT_GE(rows[0].oosBeatsExpectancy, 0.9);
    EXPECT_LT(rows[0].oosAdjustedBeats, 0.99);
    EXPECT_FALSE(rows[0].passedStageOne);
}

// ---------- the holdout ----------
// A finalist is run ONCE with the parameters it was given on a holdout data set nothing was chosen on. Pairs that are not
// finalists never touch it.

TEST(GridHoldoutTest, WithoutAHoldoutThereAreFinalistsButNoCandidates)
{
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)));
    EXPECT_TRUE(rows[0].passedStageOne);
    EXPECT_FALSE(rows[0].candidate);                        // not confirmed
    EXPECT_TRUE(std::isnan(rows[0].holdoutExpectancyR));
    EXPECT_TRUE(std::isnan(rows[0].holdoutBeatsExpectancy));
    EXPECT_EQ(rows[0].holdoutTrades, 0);
}

TEST(GridHoldoutTest, AFinalistThatAlsoWinsOnTheHoldoutIsACandidate)
{
    const AlignedData holdout = dataOf(trending(30));
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), options(), &holdout);

    EXPECT_TRUE(rows[0].passedStageOne);
    EXPECT_GT(rows[0].holdoutExpectancyR, 0.0);
    EXPECT_GT(rows[0].holdoutTrades, 0);
    EXPECT_GE(rows[0].holdoutBeatsExpectancy, 0.9);
    EXPECT_TRUE(rows[0].candidate);
}

TEST(GridHoldoutTest, AFinalistThatLosesOnTheHoldoutIsNotACandidate)
{
    const AlignedData holdout = dataOf(reversing(30));
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), options(), &holdout);

    EXPECT_TRUE(rows[0].passedStageOne);
    EXPECT_LT(rows[0].holdoutBeatsExpectancy, 0.1);
    EXPECT_FALSE(rows[0].candidate);
}

TEST(GridHoldoutTest, TheHoldoutNeedsTheExpectancyBar)
{
    // out-of-sample 1.0R per trade; on the holdout 0.4R with the best possible rank: the 0.6R bar is passed first, missed second
    GridOptions o = options();
    o.minExpectancy = 0.6;
    const AlignedData holdout = dataOf(trendThenReversing(30, 5));
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), o, &holdout);

    EXPECT_TRUE(rows[0].passedStageOne);
    EXPECT_GE(rows[0].holdoutBeatsExpectancy, 0.9);
    EXPECT_GT(rows[0].holdoutExpectancyR, 0.0);
    EXPECT_LT(rows[0].holdoutExpectancyR, 0.6);
    EXPECT_FALSE(rows[0].candidate);
}

TEST(GridHoldoutTest, TheHoldoutNeedsTheRankToo)
{
    // a bar of -1R is cleared on the holdout (-0.4R) but the rank among the random runs is 30%
    GridOptions o = options();
    o.minExpectancy = -1.0;
    const AlignedData holdout = dataOf(trendThenReversing(30, 20));
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), o, &holdout);

    EXPECT_TRUE(rows[0].passedStageOne);
    EXPECT_GT(rows[0].holdoutExpectancyR, -1.0);
    EXPECT_LT(rows[0].holdoutBeatsExpectancy, 0.9);
    EXPECT_FALSE(rows[0].candidate);
}

TEST(GridHoldoutTest, APairThatFailsTheFirstStageNeverTouchesTheHoldout)
{
    const AlignedData holdout = dataOf(trending(30));
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(reversing(30)), options(), &holdout);

    EXPECT_FALSE(rows[0].passedStageOne);
    EXPECT_TRUE(std::isnan(rows[0].holdoutExpectancyR));
    EXPECT_EQ(rows[0].holdoutTrades, 0);
    EXPECT_FALSE(rows[0].candidate);
}

TEST(GridHoldoutTest, TheHoldoutUsesThePairsParametersAndTheExpectancyRank)
{
    // the holdout result must equal that pair run there. On this data the rank by Sharpe (3%) differs from the rank by expectancy (13%).
    GridOptions o = options();
    o.minExpectancy = -1.0;
    const AlignedData holdout = dataOf(trendThenReversing(30, 16));
    const auto rows = grid({fixedLine("alpha")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), o, &holdout);

    NnfxOptions withC1 = o.nnfx;
    withC1.components.confirmation = std::make_shared<Bullish>();
    const ControlResult direct = runDirectionControl(holdout, Params{{"level", 100}}, fixedLine("alpha").info.nnfx, withC1,
                                                     o.controlRuns);
    ASSERT_NE(direct.expectancyRank, direct.sharpeRank);
    EXPECT_DOUBLE_EQ(rows[0].holdoutExpectancyR, direct.baseline.expectancyR);
    EXPECT_EQ(rows[0].holdoutTrades, direct.baseline.trades);
    EXPECT_DOUBLE_EQ(rows[0].holdoutBeatsExpectancy, direct.expectancyRank);
}

TEST(GridHoldoutTest, EachFinalistIsRunWithItsOwnParameters)
{
    // two finalists whose lines sit at different levels: on the holdout each must give what ITS level gives
    GridBaseline low = fixedLine("low");
    low.params = Params{{"level", 96}};
    low.info.ranges = {{"level", 90, 110, 1}};
    GridBaseline high = fixedLine("high");
    high.params = Params{{"level", 104}};
    high.info.ranges = {{"level", 90, 110, 1}};
    const AlignedData holdout = dataOf(trendThenReversing(30, 5));
    const auto rows = grid({low, high}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), options(), &holdout);

    ASSERT_TRUE(rowOf(rows, "low", "c1").passedStageOne);
    ASSERT_TRUE(rowOf(rows, "high", "c1").passedStageOne);
    NnfxOptions withC1 = options().nnfx;
    withC1.components.confirmation = std::make_shared<Bullish>();
    for (const GridBaseline* b : {&low, &high})
    {
        const ControlResult direct = runDirectionControl(holdout, b->params, b->info.nnfx, withC1, options().controlRuns);
        EXPECT_DOUBLE_EQ(rowOf(rows, b->info.name, "c1").holdoutExpectancyR, direct.baseline.expectancyR) << b->info.name;
        EXPECT_EQ(rowOf(rows, b->info.name, "c1").holdoutTrades, direct.baseline.trades) << b->info.name;
    }
    EXPECT_NE(rowOf(rows, "low", "c1").holdoutExpectancyR, rowOf(rows, "high", "c1").holdoutExpectancyR);
}

TEST(GridHoldoutTest, WithManyFinalistsTheHoldoutBarRisesToAllowForThem)
{
    // three identical finalists: the plain bar 0.9 becomes 0.9^(1/3) = 0.9655; one or two finalists keep 0.9
    const AlignedData holdout = dataOf(trending(30));
    const auto three = grid({fixedLine("a"), fixedLine("b"), fixedLine("c")}, {bullish("c1")}, dataOf(trending(40)),
                            dataOf(trending(30)), options(), &holdout);
    for (const GridRow& r : three)
    {
        ASSERT_TRUE(r.passedStageOne);
        EXPECT_NEAR(r.holdoutBar, std::pow(0.9, 1.0 / 3.0), 1e-12);
    }
    const auto two = grid({fixedLine("a"), fixedLine("b")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)),
                          options(), &holdout);
    for (const GridRow& r : two)
        EXPECT_DOUBLE_EQ(r.holdoutBar, 0.9);
    const auto one = grid({fixedLine("a")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), options(), &holdout);
    EXPECT_DOUBLE_EQ(one[0].holdoutBar, 0.9);
}

TEST(GridHoldoutTest, TheRaisedBarCanFailAFinalistThatWouldPassThePlainOne)
{
    // the holdout is a trend then 8 reversal cycles: the pair earns 0.045R and beats 93.3% of its 30 random runs.
    // One finalist needs 90% and passes; with three identical finalists the bar is 0.9^(1/3) = 96.6% and all three fail.
    const AlignedData holdout = dataOf(trendThenReversing(30, 8));
    const auto one = grid({fixedLine("a")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)), options(), &holdout);
    ASSERT_TRUE(one[0].passedStageOne);
    EXPECT_NEAR(one[0].holdoutBeatsExpectancy, 28.0 / 30.0, 1e-12);
    EXPECT_GE(one[0].holdoutExpectancyR, 0.0);
    EXPECT_TRUE(one[0].candidate);

    const auto three = grid({fixedLine("a"), fixedLine("b"), fixedLine("c")}, {bullish("c1")}, dataOf(trending(40)),
                            dataOf(trending(30)), options(), &holdout);
    for (const GridRow& r : three)
    {
        ASSERT_TRUE(r.passedStageOne);
        EXPECT_NEAR(r.holdoutBeatsExpectancy, 28.0 / 30.0, 1e-12);
        EXPECT_GT(r.holdoutBar, r.holdoutBeatsExpectancy);
        EXPECT_FALSE(r.candidate);
    }
}

// ---------- output ----------

TEST(GridOutputTest, TheTableSaysWhatHappenedAndHowManyWereTried)
{
    const AlignedData good = dataOf(trending(30));
    const AlignedData bad = dataOf(reversing(30));
    const AlignedData in = dataOf(trending(40)), out = dataOf(trending(30));
    const std::string confirmed = gridTable(grid({fixedLine("alpha")}, {bullish("c1")}, in, out, options(), &good), options());
    const std::string failed = gridTable(grid({fixedLine("alpha")}, {bullish("c1")}, in, out, options(), &bad), options());
    const std::string none = gridTable(grid({fixedLine("alpha")}, {bullish("c1")}, in, out), options());

    EXPECT_NE(confirmed.find("CANDIDATE"), std::string::npos);
    EXPECT_EQ(confirmed.find("failed holdout"), std::string::npos);
    EXPECT_NE(failed.find("failed holdout"), std::string::npos);
    EXPECT_EQ(failed.find("CANDIDATE"), std::string::npos);
    EXPECT_NE(none.find("finalist (no holdout given)"), std::string::npos);
    EXPECT_NE(none.find("pairs tried"), std::string::npos);
    EXPECT_NE(none.find("alpha"), std::string::npos);
    EXPECT_NE(none.find("c1"), std::string::npos);
}

TEST(GridOutputTest, TheTableCountsFinalistsAndCandidates)
{
    const AlignedData holdout = dataOf(trending(30));
    const auto rows = grid({fixedLine("alpha"), broken("bad")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)),
                           options(), &holdout);
    const std::string text = gridTable(rows, options());
    EXPECT_NE(text.find("1 finalist"), std::string::npos);
    EXPECT_NE(text.find("1 candidate"), std::string::npos);
    EXPECT_NE(text.find("blew up"), std::string::npos);
}

namespace
{
// how many table lines mention the parameters "level=100" (one per listed pair)
std::size_t listedPairs(const std::string& text)
{
    std::size_t n = 0, at = 0;
    while ((at = text.find("level=100", at)) != std::string::npos)
    {
        ++n;
        at += 9;
    }
    return n;
}
}

TEST(GridOutputTest, TheTableOnlyListsTheTopRowsButCountsThemAll)
{
    std::vector<GridBaseline> baselines;
    for (int i = 0; i < 14; ++i)
        baselines.push_back(fixedLine("b" + std::to_string(i)));
    const auto rows = grid(baselines, {bullish("c1")}, dataOf(reversing(40)), dataOf(reversing(30)));      // nobody is a finalist
    const std::string text = gridTable(rows, options(), 5);
    EXPECT_NE(text.find("14 pairs tried"), std::string::npos);
    EXPECT_EQ(listedPairs(text), 5u);
    EXPECT_NE(text.find("grid log"), std::string::npos);                  // says where the full table is
}

TEST(GridOutputTest, FinalistsAreNeverHiddenByTheRowLimit)
{
    std::vector<GridBaseline> baselines;
    for (int i = 0; i < 14; ++i)
        baselines.push_back(fixedLine("b" + std::to_string(i)));
    const auto rows = grid(baselines, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)));
    ASSERT_EQ(std::count_if(rows.begin(), rows.end(), [](const GridRow& r) { return r.passedStageOne; }), 14);
    EXPECT_EQ(listedPairs(gridTable(rows, options(), 5)), 14u);
}

TEST(GridOutputTest, TheCsvHasAHeaderAndOneLinePerPair)
{
    const auto rows = grid({fixedLine("alpha"), fixedLine("beta")}, {bullish("c1")}, dataOf(trending(40)), dataOf(trending(30)));

    const std::string csv = gridCsv(rows);

    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < csv.size())
    {
        const std::size_t end = csv.find('\n', start);
        lines.push_back(csv.substr(start, end - start));
        start = end + 1;
    }
    ASSERT_EQ(lines.size(), 3u);
    for (const char* column : {"baseline", "params", "c1", "oos_expectancy_r", "oos_beats_expectancy", "oos_score",
                               "oos_adjusted_beats", "in_sample_expectancy_r", "family_size", "passed_stage_one",
                               "holdout_expectancy_r", "holdout_trades", "holdout_beats_expectancy", "holdout_bar",
                               "candidate", "error"})
        EXPECT_NE(lines[0].find(column), std::string::npos) << column;
    EXPECT_EQ(lines[1].rfind(rows[0].baseline + ",", 0), 0u);

    const auto commas = [](const std::string& s) { return std::count(s.begin(), s.end(), ','); };
    EXPECT_EQ(commas(lines[1]), commas(lines[0]));
    EXPECT_EQ(commas(lines[2]), commas(lines[0]));
}

TEST(GridOutputTest, ACsvFieldWithACommaIsQuotedAndUndefinedNumbersAreNan)
{
    GridRow r;
    r.baseline = "x";
    r.params = "a=1, b=2";
    r.c1 = "c";
    r.error = "bad, very bad";

    const std::string csv = gridCsv({r});

    EXPECT_NE(csv.find("\"a=1, b=2\""), std::string::npos);
    EXPECT_NE(csv.find("\"bad, very bad\""), std::string::npos);
    EXPECT_NE(csv.find("nan"), std::string::npos);
}

// ---------- the plan file ----------

TEST(GridPlanTest, ReadsBaselinesWithParametersAndTheC1Pool)
{
    const GridPlan plan = parseGridPlan(
        "# phase one\n"
        "baseline kijun period=26\n"
        "baseline kama period=15,slow=30\n"
        "baseline sma   period = 20\n"
        "c1 rsi\n"
        "c1 aroon\n");

    ASSERT_EQ(plan.baselines.size(), 3u);
    EXPECT_EQ(plan.baselines[0].first, "kijun");
    EXPECT_EQ(plan.baselines[0].second, "period=26");
    EXPECT_EQ(plan.baselines[1].second, "period=15,slow=30");
    EXPECT_EQ(plan.baselines[2].first, "sma");
    EXPECT_EQ(plan.baselines[2].second, "period = 20");
    ASSERT_EQ(plan.c1s.size(), 2u);
    EXPECT_EQ(plan.c1s[0], "rsi");
    EXPECT_EQ(plan.c1s[1], "aroon");
}

TEST(GridPlanTest, IgnoresBlankLinesCommentsAndWindowsLineEndings)
{
    const GridPlan plan = parseGridPlan("\n# c\n\nbaseline kijun period=26   # the standard one\r\nc1 rsi\r\n\n");
    ASSERT_EQ(plan.baselines.size(), 1u);
    EXPECT_EQ(plan.baselines[0].second, "period=26");
    ASSERT_EQ(plan.c1s.size(), 1u);
    EXPECT_EQ(plan.c1s[0], "rsi");
}

TEST(GridPlanTest, ABaselineWithoutParametersHasEmptyText)
{
    const GridPlan plan = parseGridPlan("baseline foo\nc1 rsi\n");
    EXPECT_EQ(plan.baselines[0].first, "foo");
    EXPECT_EQ(plan.baselines[0].second, "");
}

TEST(GridPlanTest, TheErrorNamesTheLine)
{
    try
    {
        parseGridPlan("baseline kijun period=26\nc1 rsi\nwhat is this\n");
        FAIL() << "should have thrown";
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("line 3"), std::string::npos) << e.what();
    }
}

TEST(GridPlanTest, RejectsBadPlans)
{
    for (const char* bad : {"baseline\nc1 rsi\n",                                   // no name
                            "baseline kijun period=26\nc1\n",                       // no c1 name
                            "baseline kijun period=26\nc1 rsi extra\n",             // junk after the c1 name
                            "baseline kijun period=26\nbaseline kijun period=27\nc1 rsi\n",   // twice
                            "baseline kijun period=26\nc1 rsi\nc1 rsi\n",           // twice
                            "baseline kijun period=26\n",                           // no c1
                            "c1 rsi\n",                                             // no baseline
                            "",                                                     // nothing
                            "# only a comment\n"})
        EXPECT_THROW(parseGridPlan(bad), std::invalid_argument) << bad;
}

TEST(GridPlanTest, ResolvesNamesAgainstTheRegistries)
{
    const ResolvedGrid g = resolveGridPlan(parseGridPlan("baseline kijun period=26\nbaseline ema period=50\nc1 rsi\nc1 aroon\n"));
    ASSERT_EQ(g.baselines.size(), 2u);
    EXPECT_EQ(g.baselines[0].info.name, "kijun");
    EXPECT_DOUBLE_EQ(g.baselines[0].params.get("period"), 26.0);
    ASSERT_EQ(g.c1s.size(), 2u);
    EXPECT_EQ(g.c1s[0].name, "rsi");
    EXPECT_NE(g.c1s[0].prototype, nullptr);
}

TEST(GridPlanTest, ResolvingRejectsUnknownNamesBadParametersAndGates)
{
    EXPECT_THROW(resolveGridPlan(parseGridPlan("baseline nope period=26\nc1 rsi\n")), std::invalid_argument);
    EXPECT_THROW(resolveGridPlan(parseGridPlan("baseline kijun period=26\nc1 nope\n")), std::invalid_argument);
    EXPECT_THROW(resolveGridPlan(parseGridPlan("baseline kijun period=99999\nc1 rsi\n")), std::invalid_argument);
    EXPECT_THROW(resolveGridPlan(parseGridPlan("baseline kijun speed=1\nc1 rsi\n")), std::invalid_argument);
    EXPECT_THROW(resolveGridPlan(parseGridPlan("baseline kijun\nc1 rsi\n")), std::invalid_argument);                 // params missing
    EXPECT_THROW(resolveGridPlan(parseGridPlan("baseline kijun period=26\nc1 atr_expansion\n")), std::invalid_argument);   // a gate
}

TEST(GridPlanTest, ThePhaseOnePlanParsesAndResolvesAgainstTheRegistries)
{
    // phase1/plan.txt is the pre-registered plan: this catches a typo, a parameter outside its range, or a component that
    // was renamed. Skipped if the file is not there (e.g. the tests are run from a copy of the build).
    const std::string path = std::string(PROJECT_DIR) + "/phase1/plan.txt";
    if (!std::ifstream(path))
        GTEST_SKIP() << path << " not found";
    const GridPlan plan = loadGridPlan(path);
    const ResolvedGrid grid = resolveGridPlan(plan);
    EXPECT_EQ(grid.baselines.size(), plan.baselines.size());
    EXPECT_EQ(grid.c1s.size(), plan.c1s.size());
    // every baseline in the registry has a row, and no row is repeated
    EXPECT_EQ(plan.baselines.size(), baselineRegistry().size());
    EXPECT_GE(plan.c1s.size(), 1u);
}

TEST(GridPlanTest, PlanBHasTheSameBaselinesAsTheFrozenPlanAndThirteenC1s)
{
    // phase1/plan_13c1.txt: same 28 baselines with the same parameters, 13 confirmations (the frozen plan's 8 + 5 new)
    const std::string dir = std::string(PROJECT_DIR) + "/phase1/";
    if (!std::ifstream(dir + "plan.txt") || !std::ifstream(dir + "plan_13c1.txt"))
        GTEST_SKIP() << "plan files not found";
    const GridPlan frozen = loadGridPlan(dir + "plan.txt");
    const GridPlan planB = loadGridPlan(dir + "plan_13c1.txt");

    EXPECT_EQ(planB.baselines, frozen.baselines);                     // names and parameter text, in the same order
    ASSERT_EQ(planB.c1s.size(), 13u);
    for (std::size_t i = 0; i < frozen.c1s.size(); ++i)
        EXPECT_EQ(planB.c1s[i], frozen.c1s[i]) << i;                  // the old pool comes first, unchanged
    const ResolvedGrid resolved = resolveGridPlan(planB);
    EXPECT_EQ(resolved.baselines.size() * resolved.c1s.size(), 364u);
}

TEST(GridPlanTest, PlanCHasTheSameBaselinesAsTheFrozenPlanAndOnlyLwpi)
{
    const std::string dir = std::string(PROJECT_DIR) + "/phase1/";
    if (!std::ifstream(dir + "plan.txt") || !std::ifstream(dir + "plan_lwpi.txt"))
        GTEST_SKIP() << "plan files not found";
    const GridPlan frozen = loadGridPlan(dir + "plan.txt");
    const GridPlan planC = loadGridPlan(dir + "plan_lwpi.txt");

    EXPECT_EQ(planC.baselines, frozen.baselines);
    ASSERT_EQ(planC.c1s.size(), 1u);
    EXPECT_EQ(planC.c1s[0], "lwpi");
    const ResolvedGrid resolved = resolveGridPlan(planC);
    EXPECT_EQ(resolved.baselines.size() * resolved.c1s.size(), 28u);
}
