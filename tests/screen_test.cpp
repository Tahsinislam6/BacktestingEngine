#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "screen.hpp"

// screenBaselines runs the whole NNFX screen for every baseline it is given, in one go:
//   study on the in-sample assets (no cross-back exit) -> the winner on the out-of-sample assets ->
//   the random-direction control on both -> one row each, best first.
// Stand-in baselines: FixedLine at 100. The trending data rewards the baseline's direction, the reversing data
// punishes it (see control_test.cpp).

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

BaselineInfo fixedLine(const std::string& name)
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
    return b;
}

BaselineInfo broken(const std::string& name)
{
    BaselineInfo b = fixedLine(name);
    b.nnfx = [](const NnfxOptions&) -> StrategyFactory
    {
        return [](const Params&, std::size_t) -> std::unique_ptr<MultiStrategy>
        {
            throw std::runtime_error("this indicator blew up");
        };
    };
    return b;
}

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

ScreenOptions options(int runs = 30)
{
    ScreenOptions o;
    o.controlRuns = runs;
    o.nnfx.atrPeriod = 3;
    o.nnfx.maxDistanceAtr = 2.0;
    o.nnfx.exitOnCross = false;
    o.study.validation.optimise.grid.threads = 1;
    o.study.validation.optimise.minTrades = 5;
    o.minBeats = 0.9;
    o.minExpectancy = 0.0;
    return o;
}
}

TEST(ScreenTest, GivesOneRowPerBaselineNamedAsGiven)
{
    const auto rows = screenBaselines({fixedLine("alpha"), fixedLine("beta")}, dataOf(trending(40)),
                                      dataOf(trending(30)), options());

    ASSERT_EQ(rows.size(), 2u);
    std::vector<std::string> names{rows[0].name, rows[1].name};
    EXPECT_NE(std::find(names.begin(), names.end(), "alpha"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "beta"), names.end());
}

TEST(ScreenTest, FillsInTheWinnerAndTheOutOfSampleControl)
{
    const auto rows = screenBaselines({fixedLine("alpha")}, dataOf(trending(40)), dataOf(trending(30)), options());

    const ScreenRow& r = rows[0];
    EXPECT_TRUE(r.error.empty());
    EXPECT_EQ(r.params, "level=100");
    EXPECT_FALSE(std::isnan(r.inSampleSharpe));
    EXPECT_FALSE(std::isnan(r.oosSharpe));
    EXPECT_FALSE(std::isnan(r.oosExpectancyR));
    EXPECT_GT(r.oosTrades, 0);
    EXPECT_FALSE(std::isnan(r.oosBeatsExpectancy));
    EXPECT_FALSE(std::isnan(r.oosBeatsSharpe));
    EXPECT_FALSE(std::isnan(r.inSampleBeatsExpectancy));
}

TEST(ScreenTest, ABaselineWhoseDirectionIsRightIsACandidate)
{
    const auto rows = screenBaselines({fixedLine("alpha")}, dataOf(trending(40)), dataOf(trending(30)), options());

    EXPECT_GE(rows[0].oosBeatsExpectancy, 0.9);
    EXPECT_GT(rows[0].oosExpectancyR, 0.0);
    EXPECT_TRUE(rows[0].candidate);
}

TEST(ScreenTest, ABaselineWhoseDirectionIsWrongIsNot)
{
    const auto rows = screenBaselines({fixedLine("alpha")}, dataOf(reversing(40)), dataOf(reversing(30)), options());

    EXPECT_LT(rows[0].oosBeatsExpectancy, 0.1);
    EXPECT_FALSE(rows[0].candidate);
}

TEST(ScreenTest, TheVerdictUsesTheOutOfSampleDataNotTheInSampleData)
{
    // tuned on data where its direction is wrong, tested on data where it is right: still a candidate
    const auto rows = screenBaselines({fixedLine("alpha")}, dataOf(reversing(40)), dataOf(trending(30)), options());

    EXPECT_LT(rows[0].inSampleBeatsExpectancy, 0.1);
    EXPECT_GT(rows[0].oosBeatsExpectancy, 0.9);
    EXPECT_TRUE(rows[0].candidate);

    // and the other way round
    const auto flipped = screenBaselines({fixedLine("alpha")}, dataOf(trending(40)), dataOf(reversing(30)), options());
    EXPECT_GT(flipped[0].inSampleBeatsExpectancy, 0.9);
    EXPECT_FALSE(flipped[0].candidate);
}

TEST(ScreenTest, TheThresholdsDecideTheVerdict)
{
    ScreenOptions strictBeats = options();
    strictBeats.minBeats = 1.01;                       // impossible
    EXPECT_FALSE(screenBaselines({fixedLine("a")}, dataOf(trending(40)), dataOf(trending(30)), strictBeats)[0].candidate);

    ScreenOptions strictR = options();
    strictR.minExpectancy = 1000.0;                    // impossible
    EXPECT_FALSE(screenBaselines({fixedLine("a")}, dataOf(trending(40)), dataOf(trending(30)), strictR)[0].candidate);
}

TEST(ScreenTest, RowsAreSortedByTheOutOfSampleBeatsBestFirst)
{
    // the same baseline on trending data is first; there is only one dataset per call, so use two runs and merge
    auto good = screenBaselines({fixedLine("good")}, dataOf(trending(40)), dataOf(trending(30)), options());
    auto bad = screenBaselines({fixedLine("bad")}, dataOf(reversing(40)), dataOf(reversing(30)), options());
    ASSERT_GT(good[0].oosBeatsExpectancy, bad[0].oosBeatsExpectancy);

    std::vector<ScreenRow> rows{bad[0], good[0]};
    sortScreenRows(rows);

    EXPECT_EQ(rows[0].name, "good");
    EXPECT_EQ(rows[1].name, "bad");
}

TEST(ScreenTest, RowsWithoutAResultGoLastAndEqualOnesKeepTheirOrder)
{
    ScreenRow a, b, failed, c;
    a.name = "a"; b.name = "b"; c.name = "c"; failed.name = "failed";
    a.oosBeatsExpectancy = b.oosBeatsExpectancy = 0.5;
    c.oosBeatsExpectancy = 0.7;
    failed.error = "boom";
    failed.oosBeatsExpectancy = std::nan("");
    std::vector<ScreenRow> rows{failed, a, b, c};

    sortScreenRows(rows);

    EXPECT_EQ(rows[0].name, "c");
    EXPECT_EQ(rows[1].name, "a");
    EXPECT_EQ(rows[2].name, "b");
    EXPECT_EQ(rows[3].name, "failed");
}

TEST(ScreenTest, ABaselineThatThrowsIsReportedAndTheOthersStillRun)
{
    const auto rows = screenBaselines({broken("bad"), fixedLine("ok")}, dataOf(trending(40)), dataOf(trending(30)),
                                      options());

    ASSERT_EQ(rows.size(), 2u);
    const ScreenRow* bad = rows[0].name == "bad" ? &rows[0] : &rows[1];
    const ScreenRow* ok = rows[0].name == "ok" ? &rows[0] : &rows[1];
    EXPECT_NE(bad->error.find("blew up"), std::string::npos);
    EXPECT_FALSE(bad->candidate);
    EXPECT_TRUE(ok->error.empty());
}

TEST(ScreenTest, ABaselineWithNoWinnerIsReportedNotFatal)
{
    ScreenOptions o = options();
    o.study.validation.optimise.minTrades = 100000;       // nothing can qualify

    const auto rows = screenBaselines({fixedLine("alpha")}, dataOf(trending(40)), dataOf(trending(30)), o);

    ASSERT_EQ(rows.size(), 1u);
    EXPECT_FALSE(rows[0].error.empty());
    EXPECT_FALSE(rows[0].candidate);
}

TEST(ScreenTest, RejectsOptionsTheControlCannotUse)
{
    ScreenOptions crossBack = options();
    crossBack.nnfx.exitOnCross = true;
    EXPECT_THROW(screenBaselines({fixedLine("a")}, dataOf(trending(40)), dataOf(trending(30)), crossBack),
                 std::invalid_argument);

    ScreenOptions noRuns = options(0);
    EXPECT_THROW(screenBaselines({fixedLine("a")}, dataOf(trending(40)), dataOf(trending(30)), noRuns),
                 std::invalid_argument);
}

TEST(ScreenTest, TheResultDoesNotDependOnTheThreadCount)
{
    ScreenOptions one = options();
    ScreenOptions many = options();
    many.study.validation.optimise.grid.threads = 4;

    const auto a = screenBaselines({fixedLine("alpha")}, dataOf(trending(40)), dataOf(trending(30)), one);
    const auto b = screenBaselines({fixedLine("alpha")}, dataOf(trending(40)), dataOf(trending(30)), many);

    EXPECT_DOUBLE_EQ(a[0].oosExpectancyR, b[0].oosExpectancyR);
    EXPECT_DOUBLE_EQ(a[0].oosBeatsExpectancy, b[0].oosBeatsExpectancy);
}

// ---------- output ----------

TEST(ScreenOutputTest, TheTableListsEveryBaselineAndMarksCandidates)
{
    const auto rows = screenBaselines({fixedLine("alpha"), broken("bad")}, dataOf(trending(40)), dataOf(trending(30)),
                                      options());

    const std::string text = screenTable(rows, options());

    EXPECT_NE(text.find("alpha"), std::string::npos);
    EXPECT_NE(text.find("bad"), std::string::npos);
    EXPECT_NE(text.find("candidate"), std::string::npos);
    EXPECT_NE(text.find("blew up"), std::string::npos);
    EXPECT_NE(text.find("90%"), std::string::npos);                // the thresholds are stated
}

TEST(ScreenOutputTest, TheCsvHasAHeaderAndOneLinePerBaseline)
{
    const auto rows = screenBaselines({fixedLine("alpha"), fixedLine("beta")}, dataOf(trending(40)),
                                      dataOf(trending(30)), options());

    const std::string csv = screenCsv(rows);

    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < csv.size())
    {
        const std::size_t end = csv.find('\n', start);
        lines.push_back(csv.substr(start, end - start));
        start = end + 1;
    }
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0].rfind("baseline,", 0), 0u);
    EXPECT_NE(lines[0].find("oos_beats_expectancy"), std::string::npos);
    EXPECT_NE(lines[0].find("candidate"), std::string::npos);
    EXPECT_EQ(lines[1].rfind(rows[0].name + ",", 0), 0u);

    const auto commas = [](const std::string& s) { return std::count(s.begin(), s.end(), ','); };
    EXPECT_EQ(commas(lines[1]), commas(lines[0]));
    EXPECT_EQ(commas(lines[2]), commas(lines[0]));
}

TEST(ScreenOutputTest, ACsvFieldWithACommaIsQuoted)
{
    ScreenRow r;
    r.name = "x";
    r.params = "a=1, b=2";
    r.error = "bad, very bad";

    const std::string csv = screenCsv({r});

    EXPECT_NE(csv.find("\"a=1, b=2\""), std::string::npos);
    EXPECT_NE(csv.find("\"bad, very bad\""), std::string::npos);
}

TEST(ScreenOutputTest, UndefinedNumbersAreWrittenAsNan)
{
    ScreenRow r;
    r.name = "x";
    r.error = "failed";

    EXPECT_NE(screenCsv({r}).find("nan"), std::string::npos);
}
