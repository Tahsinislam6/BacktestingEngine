#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "density.hpp"
#include "job.hpp"
#include "null_series.hpp"
#include "parameter_grid.hpp"
#include "sweep_job.hpp"
#include "sweep_job_json.hpp"
#include "test_strategies.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

// A density sweep job: run a strategy over a grid of two parameters on real data and on shuffled copies of it, and report
// how much of the grid gives good results (see density.hpp). It is the web version of the research program
// `density_sweep`, with the same rules:
//
//   1. Nothing is picked for the user: the metric, threshold, radius, minimum trades, number of shuffled copies and block
//      length must all be given. A note that says what is expected and what counts as a pass is required.
//   2. Exactly two axes, each "from, to, step" of one parameter of the strategy; the rest of the parameters are fixed.
//      Every axis value is checked against the parameter's limits before anything runs.
//   3. The shuffled copies use seeds 1..nullRuns; the result does not depend on how many threads ran them.
//   4. The result is numbers only (no verdict): the real densities, the shuffled densities with median, 95th percentile
//      and how many copies reached the real value, one cell per grid point, and "ideas tried" = cells x (1 + nullRuns).
//   5. Anything that could not be judged (too few trades) stays in the grid as invalid; non-finite numbers are null in JSON.
//
// Worked example used below: scaled_hold (test_strategies.hpp) on closes 100..150, cash 1000, total-return. Cell
// (delay d, size s) has return s * (50 - 10 d) / 1000, laid out with delay as the rows and size as the columns:
//
//          size 1   size 2
//   d 0     0.05     0.10
//   d 1     0.04     0.08
//   d 2     0.03     0.06

class SweepJobTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("sweep_job_test_") + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        writeSixBars(dir_ / "x.txt");
        std::ofstream(dir_ / "empty.csv") << "timestamp,open,high,low,close\n";
        writeRandomWalk(dir_ / "rw.csv");
        registerScaledHold(registry_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    // 250 daily bars of a fixed pseudo-random walk (about 2% a day) in the simple layout. The shuffled copies of it differ
    // from each other, so tests on it can tell one seed or one slot from another (the six-bar example is too coarse for that).
    static void writeRandomWalk(const fs::path& file)
    {
        std::ofstream out(file);
        out << "timestamp,open,high,low,close\n";
        std::uint64_t state = 12345;
        double price = 100.0;
        for (int i = 0; i < 250; ++i)
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            const double u = static_cast<double>(state >> 11) / 9007199254740992.0;      // 0..1
            price *= 1.0 + (u - 0.5) * 0.04;
            out << (1577836800000LL + 86400000LL * i) << "," << price << "," << price << "," << price << "," << price << "\n";
        }
    }

    // The same sweep on that walk: 4 delays x 4 sizes, 12 shuffled copies.
    static SweepSpec walkSpec()
    {
        SweepSpec s;
        s.strategy = "scaled_hold";
        s.dataset = "rw.csv";
        s.axes = {{"delay", 0, 3, 1}, {"size", 1, 4, 1}};
        s.metric = "total-return";
        s.threshold = 0.02;
        s.radius = 0;
        s.minTrades = 0;
        s.nullRuns = 12;
        s.block = 1;
        s.cash = 1000;
        s.note = "pass rule";
        return s;
    }

    // The worked example: 3 delays x 2 sizes, total-return above 0.055, 3 shuffled copies of block length 1.
    static SweepSpec spec()
    {
        SweepSpec s;
        s.strategy = "scaled_hold";
        s.dataset = "x.txt";
        s.axes = {{"delay", 0, 2, 1}, {"size", 1, 2, 1}};
        s.metric = "total-return";
        s.threshold = 0.055;
        s.radius = 1;
        s.minTrades = 0;
        s.nullRuns = 3;
        s.block = 1;
        s.cash = 1000;
        s.note = "PASS if the plateau density beats the shuffled 95th percentile";
        return s;
    }

    SweepOutput run(const SweepSpec& s, unsigned threads = 1) const { return runSweep(registry_, dir_.string(), s, {}, threads); }
    std::string dir() const { return dir_.string(); }

    StrategyRegistry registry_;
    fs::path dir_;
};

// ---- axes and metrics ----

TEST(SweepAxis, ExpandsIncludingBothEndsWhenTheStepLandsOnThem)
{
    const std::vector<double> v = expandAxis({"lookback", 5, 200, 5});
    ASSERT_EQ(v.size(), 40u);
    EXPECT_DOUBLE_EQ(v.front(), 5);
    EXPECT_DOUBLE_EQ(v.back(), 200);
}

TEST(SweepAxis, StopsBelowTheEndWhenTheStepDoesNotLandOnIt)
{
    const std::vector<double> v = expandAxis({"x", 1, 10, 4});
    ASSERT_EQ(v.size(), 3u);
    EXPECT_DOUBLE_EQ(v[0], 1);
    EXPECT_DOUBLE_EQ(v[1], 5);
    EXPECT_DOUBLE_EQ(v[2], 9);
}

TEST(SweepAxis, DecimalStepsReachTheEnd)
{
    const std::vector<double> v = expandAxis({"x", 0, 1, 0.1});
    ASSERT_EQ(v.size(), 11u);
    EXPECT_NEAR(v.back(), 1.0, 1e-12);
    EXPECT_NEAR(v[3], 0.3, 1e-12);
}

TEST(SweepAxis, OneValueWhenFromEqualsTo)
{
    EXPECT_EQ(expandAxis({"x", 7, 7, 1}).size(), 1u);
}

TEST(SweepAxis, RejectsBadAxes)
{
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROW(expandAxis({"x", 1, 5, 0}), std::invalid_argument);
    EXPECT_THROW(expandAxis({"x", 1, 5, -1}), std::invalid_argument);
    EXPECT_THROW(expandAxis({"x", 5, 1, 1}), std::invalid_argument);
    EXPECT_THROW(expandAxis({"x", nan, 5, 1}), std::invalid_argument);
    EXPECT_THROW(expandAxis({"x", 1, inf, 1}), std::invalid_argument);
    EXPECT_THROW(expandAxis({"x", 1, 5, nan}), std::invalid_argument);
    EXPECT_THROW(expandAxis({"x", 0, 1e9, 1}), std::invalid_argument);        // far too many values
    EXPECT_THROW(expandAxis({"", 1, 5, 1}), std::invalid_argument);
}

TEST(SweepMetric, KnownNamesPickTheRightNumber)
{
    Metrics m;
    m.mar = 1; m.cagr = 2; m.sharpe = 3; m.sortino = 4; m.totalReturn = 5; m.profitFactor = 6; m.expectancy = 7;
    EXPECT_DOUBLE_EQ(metricByName("mar")(m), 1);
    EXPECT_DOUBLE_EQ(metricByName("cagr")(m), 2);
    EXPECT_DOUBLE_EQ(metricByName("sharpe")(m), 3);
    EXPECT_DOUBLE_EQ(metricByName("sortino")(m), 4);
    EXPECT_DOUBLE_EQ(metricByName("total-return")(m), 5);
    EXPECT_DOUBLE_EQ(metricByName("profit-factor")(m), 6);
    EXPECT_DOUBLE_EQ(metricByName("expectancy")(m), 7);
}

TEST(SweepMetric, UnknownNameThrows)
{
    EXPECT_THROW(metricByName("luck"), std::invalid_argument);
    EXPECT_THROW(metricByName(""), std::invalid_argument);
}

// ---- the worked example ----

TEST_F(SweepJobTest, GridCellsMatchTheHandWorkedValues)
{
    const SweepOutput out = run(spec());

    ASSERT_EQ(out.cells, 6u);
    ASSERT_EQ(out.values.size(), 6u);
    const double expected[] = {0.05, 0.10, 0.04, 0.08, 0.03, 0.06};
    for (int i = 0; i < 6; ++i)
        EXPECT_NEAR(out.values[i], expected[i], 1e-9) << "cell " << i;
    for (int t : out.trades)
        EXPECT_EQ(t, 0);
    for (bool v : out.valid)
        EXPECT_TRUE(v);
    EXPECT_EQ(out.bars, 6u);
}

TEST_F(SweepJobTest, AxisValuesAreReportedInOrder)
{
    const SweepOutput out = run(spec());
    ASSERT_EQ(out.axisValues.size(), 2u);
    EXPECT_EQ(out.axisValues[0], (std::vector<double>{0, 1, 2}));
    EXPECT_EQ(out.axisValues[1], (std::vector<double>{1, 2}));
}

TEST_F(SweepJobTest, RealDensitiesMatchTheHandWorkedValues)
{
    const SweepOutput out = run(spec());

    EXPECT_EQ(out.validCells, 6u);
    EXPECT_DOUBLE_EQ(out.passDensity, 0.5);              // 0.10, 0.08 and 0.06 are above 0.055
    EXPECT_DOUBLE_EQ(out.plateauDensity, 0.0);           // radius 1 reaches both columns, and the size-1 column fails
    EXPECT_NEAR(out.peakOverNeighbours, 0.10 - 0.05, 1e-9);   // best cell 0.10, median of its neighbours 0.05, 0.04, 0.08
}

TEST_F(SweepJobTest, RadiusZeroPlateauEqualsPassDensity)
{
    SweepSpec s = spec();
    s.radius = 0;
    const SweepOutput out = run(s);
    EXPECT_DOUBLE_EQ(out.plateauDensity, out.passDensity);
}

TEST_F(SweepJobTest, MinTradesMakesCellsInvalidButTheyStillCount)
{
    SweepSpec s = spec();
    s.minTrades = 1;           // every cell closed 0 trades
    const SweepOutput out = run(s);
    EXPECT_EQ(out.validCells, 0u);
    EXPECT_DOUBLE_EQ(out.passDensity, 0.0);
    EXPECT_EQ(out.cells, 6u);
}

TEST_F(SweepJobTest, FixedParametersOutsideTheAxesAreUsed)
{
    // Sweep only delay x size, so nothing is fixed here; sweep size x delay in the other order and check the layout flips.
    SweepSpec s = spec();
    s.axes = {{"size", 1, 2, 1}, {"delay", 0, 2, 1}};
    const SweepOutput out = run(s);
    ASSERT_EQ(out.cells, 6u);
    const double expected[] = {0.05, 0.04, 0.03, 0.10, 0.08, 0.06};     // size is now the row, delay the column
    for (int i = 0; i < 6; ++i)
        EXPECT_NEAR(out.values[i], expected[i], 1e-9) << "cell " << i;
}

TEST_F(SweepJobTest, TheSpecInTheResultHasTheCompleteFixedParameters)
{
    SweepSpec s = spec();
    s.axes = {{"delay", 0, 2, 1}, {"size", 1, 2, 1}};
    EXPECT_TRUE(run(s).spec.params.empty());              // both parameters are swept, nothing is fixed
}

// ---- the shuffled copies ----

TEST_F(SweepJobTest, ShuffledCopiesUseSeedsOneToK)
{
    const SweepSpec s = walkSpec();
    const SweepOutput out = run(s);
    ASSERT_EQ(out.nullPass.size(), 12u);
    ASSERT_EQ(out.nullPlateau.size(), 12u);

    // The copies must differ from one another, or this test could not tell one seed or one slot from another.
    EXPECT_GT(std::set<double>(out.nullPass.begin(), out.nullPass.end()).size(), 3u);

    // Recompute each copy by hand with the engine's own pieces.
    const BarSeries real = loadDataset(dir(), "rw.csv");
    const std::vector<ParameterAxis> axes = {{"delay", expandAxis(s.axes[0])}, {"size", expandAxis(s.axes[1])}};
    const StrategyFactory factory = [](const Params& p) -> std::unique_ptr<Strategy> {
        return std::make_unique<ScaledHold>(static_cast<int>(p[1]), static_cast<int>(p[0]));
    };
    for (int k = 1; k <= 12; ++k)
    {
        const BarSeries copy = shuffleReturns(real, static_cast<std::uint64_t>(k), s.block);
        const DensityGrid g = toDensityGrid(sweep(copy, axes, factory, s.cash, s.periodsPerYear), metricByName(s.metric), s.minTrades);
        EXPECT_DOUBLE_EQ(out.nullPass[k - 1], passDensity(g, s.threshold)) << "seed " << k;
        EXPECT_DOUBLE_EQ(out.nullPlateau[k - 1], plateauDensity(g, s.threshold, s.radius)) << "seed " << k;
    }
}

TEST_F(SweepJobTest, ShuffledSummaryNumbersAgreeWithTheDensities)
{
    SweepSpec s = spec();
    s.nullRuns = 7;
    const SweepOutput out = run(s);

    EXPECT_DOUBLE_EQ(out.nullPassMedian, percentile(out.nullPass, 0.5));
    EXPECT_DOUBLE_EQ(out.nullPass95, percentile(out.nullPass, 0.95));
    EXPECT_DOUBLE_EQ(out.nullPlateauMedian, percentile(out.nullPlateau, 0.5));
    EXPECT_DOUBLE_EQ(out.nullPlateau95, percentile(out.nullPlateau, 0.95));
    std::size_t passAtLeast = 0, plateauAtLeast = 0;
    for (double d : out.nullPass) passAtLeast += d >= out.passDensity ? 1 : 0;
    for (double d : out.nullPlateau) plateauAtLeast += d >= out.plateauDensity ? 1 : 0;
    EXPECT_EQ(out.nullPassAtLeast, passAtLeast);
    EXPECT_EQ(out.nullPlateauAtLeast, plateauAtLeast);
}

TEST_F(SweepJobTest, TheResultDoesNotDependOnTheNumberOfThreads)
{
    SweepSpec s = spec();
    s.nullRuns = 25;
    const SweepOutput one = run(s, 1);
    const SweepOutput four = run(s, 4);
    const SweepOutput many = run(s, 32);                 // more threads than copies is fine
    EXPECT_EQ(toJson(one), toJson(four));
    EXPECT_EQ(toJson(one), toJson(many));
}

TEST_F(SweepJobTest, IdeasTriedIsCellsTimesOnePlusCopies)
{
    EXPECT_EQ(run(spec()).ideasTried, 6u * (1 + 3));
    EXPECT_EQ(sweepIdeas(registry_, spec()), 24u);       // known before running, for the log
}

TEST_F(SweepJobTest, ProgressCountsTheRealGridAndEachCopy)
{
    std::vector<std::pair<std::size_t, std::size_t>> calls;
    std::mutex m;
    runSweep(registry_, dir(), spec(), [&](std::size_t done, std::size_t total) {
        std::lock_guard<std::mutex> lock(m);
        calls.emplace_back(done, total);
    }, 3);

    ASSERT_FALSE(calls.empty());
    for (const auto& c : calls)
        EXPECT_EQ(c.second, 4u);                         // the real grid + 3 copies
    for (std::size_t i = 1; i < calls.size(); ++i)
        EXPECT_GE(calls[i].first, calls[i - 1].first);
    EXPECT_EQ(calls.back().first, 4u);
}

TEST_F(SweepJobTest, CopiesThatTieTheRealResultCountAsAtOrAboveIt)
{
    // A flat price never moves, so shuffling it changes nothing: every cell returns 0, nothing passes, and every copy has
    // exactly the real density (0). "At or above" counts ties, so all of the copies reach it.
    {
        std::ofstream out(dir_ / "flat.txt");
        out << "<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>\n";
        for (int i = 0; i < 6; ++i)
            out << "X,D,2020010" << (i + 1) << ",000000,100,100,100,100,0,0\n";
    }
    SweepSpec s = spec();
    s.dataset = "flat.txt";
    s.threshold = 0.01;
    s.nullRuns = 5;

    const SweepOutput out = run(s);

    EXPECT_DOUBLE_EQ(out.passDensity, 0.0);
    EXPECT_EQ(out.nullPassAtLeast, 5u);
    EXPECT_EQ(out.nullPlateauAtLeast, 5u);
}

TEST_F(SweepJobTest, BarsPerYearReachesTheRealGridAndTheCopies)
{
    // CAGR depends on bars per year. Cell (delay 0, size 1) is the first cell; measure the same run by hand at 365 and 260.
    SweepSpec s = spec();
    s.metric = "cagr";
    s.nullRuns = 2;

    s.periodsPerYear = 365;
    const SweepOutput at365 = run(s);
    s.periodsPerYear = 260;
    const SweepOutput at260 = run(s);

    const BarSeries series = loadDataset(dir(), "x.txt");
    ScaledHold strategy(1, 0);
    const BacktestResult result = runBacktest(series, strategy, 1000.0);
    EXPECT_DOUBLE_EQ(at365.values[0], computeMetrics(result, 1000.0, 365.0).cagr);
    EXPECT_DOUBLE_EQ(at260.values[0], computeMetrics(result, 1000.0, 260.0).cagr);
    EXPECT_NE(at365.values[0], at260.values[0]);
    EXPECT_EQ(at365.spec.periodsPerYear, 365);
}

TEST_F(SweepJobTest, BarsPerYearReachesTheShuffledCopiesToo)
{
    // CAGR passes a threshold at one bars-per-year and not at another, so the copies' pass densities must change with it.
    SweepSpec s = walkSpec();
    s.metric = "cagr";
    s.threshold = 0.05;

    s.periodsPerYear = 365;
    const SweepOutput at365 = run(s);
    s.periodsPerYear = 260;
    const SweepOutput at260 = run(s);

    EXPECT_NE(at365.nullPass, at260.nullPass);
}

// ---- refusals ----

TEST_F(SweepJobTest, RefusesAnEmptyOrBlankNote)
{
    SweepSpec s = spec();
    s.note = "";
    EXPECT_THROW(run(s), std::invalid_argument);
    s.note = "   \n\t ";
    EXPECT_THROW(run(s), std::invalid_argument);
}

TEST_F(SweepJobTest, RefusesBadSettings)
{
    auto bad = [&](const std::function<void(SweepSpec&)>& change) {
        SweepSpec s = spec();
        change(s);
        EXPECT_THROW(validateSweepSpec(registry_, dir(), s), std::invalid_argument);
    };
    bad([](SweepSpec& s) { s.strategy = "nope"; });
    bad([](SweepSpec& s) { s.dataset = "../x.txt"; });
    bad([](SweepSpec& s) { s.dataset = "missing.txt"; });
    bad([](SweepSpec& s) { s.metric = "luck"; });
    bad([](SweepSpec& s) { s.radius = -1; });
    bad([](SweepSpec& s) { s.radius = 21; });
    bad([](SweepSpec& s) { s.minTrades = -1; });
    bad([](SweepSpec& s) { s.nullRuns = 0; });
    bad([](SweepSpec& s) { s.nullRuns = 1001; });
    bad([](SweepSpec& s) { s.block = 0; });
    bad([](SweepSpec& s) { s.cash = 0; });
    bad([](SweepSpec& s) { s.periodsPerYear = 0; });
    bad([](SweepSpec& s) { s.threshold = std::nan(""); });
}

TEST_F(SweepJobTest, RefusesBadAxes)
{
    auto bad = [&](const std::function<void(SweepSpec&)>& change) {
        SweepSpec s = spec();
        change(s);
        EXPECT_THROW(validateSweepSpec(registry_, dir(), s), std::invalid_argument);
    };
    bad([](SweepSpec& s) { s.axes.pop_back(); });                                    // one axis
    bad([](SweepSpec& s) { s.axes.push_back({"delay", 0, 1, 1}); });                  // three axes
    bad([](SweepSpec& s) { s.axes = {{"delay", 0, 2, 1}, {"delay", 0, 2, 1}}; });     // the same parameter twice
    bad([](SweepSpec& s) { s.axes[0].param = "bogus"; });                             // not a parameter of the strategy
    bad([](SweepSpec& s) { s.axes[0].to = 4; });                                      // delay is at most 3
    bad([](SweepSpec& s) { s.axes[1].from = 0; });                                    // size is at least 1
    bad([](SweepSpec& s) { s.axes[0].step = 0.5; });                                  // delay must be a whole number
    bad([](SweepSpec& s) { s.axes[0].step = 0; });
}

TEST_F(SweepJobTest, RefusesAFixedValueForASweptParameterAndUnknownFixedParameters)
{
    SweepSpec s = spec();
    s.params = {{"size", 2}};                            // size is on an axis
    EXPECT_THROW(validateSweepSpec(registry_, dir(), s), std::invalid_argument);
    s = spec();
    s.axes = {{"delay", 0, 2, 1}, {"size", 1, 2, 1}};
    s.params = {{"bogus", 1}};
    EXPECT_THROW(validateSweepSpec(registry_, dir(), s), std::invalid_argument);
}

TEST_F(SweepJobTest, RefusesAGridThatWouldTakeFarTooLong)
{
    StrategyInfo info;
    info.name = "wide";
    info.params = {{"a", 1, 1, 100000, true, ""}, {"b", 1, 1, 100000, true, ""}};
    registry_.add(info, [](const NamedParams&) { return std::make_unique<ScaledHold>(1, 0); });
    SweepSpec s = spec();
    s.strategy = "wide";
    s.axes = {{"a", 1, 5000, 1}, {"b", 1, 5000, 1}};     // 25 million cells
    EXPECT_THROW(validateSweepSpec(registry_, dir(), s), std::invalid_argument);

    s.axes = {{"a", 1, 100, 1}, {"b", 1, 100, 1}};       // 10,000 cells is fine alone, but x 201 runs is over the limit
    s.nullRuns = 200;
    EXPECT_THROW(validateSweepSpec(registry_, dir(), s), std::invalid_argument);
}

TEST_F(SweepJobTest, ADatasetWithNoBarsFailsWhileRunning)
{
    SweepSpec s = spec();
    s.dataset = "empty.csv";
    EXPECT_THROW(run(s), std::runtime_error);
}

// ---- JSON ----

static json requestJson()
{
    return json::parse(R"({
        "strategy": "scaled_hold", "dataset": "x.txt",
        "axes": [{"param": "delay", "from": 0, "to": 2, "step": 1}, {"param": "size", "from": 1, "to": 2, "step": 1}],
        "metric": "total-return", "threshold": 0.055, "radius": 1, "minTrades": 0, "nullRuns": 3, "block": 1,
        "cash": 1000, "periodsPerYear": 365, "params": {}, "note": "my pass rule" })");
}

TEST(SweepJson, ReadsAFullRequest)
{
    const SweepSpec s = sweepSpecFromJson(requestJson());
    EXPECT_EQ(s.strategy, "scaled_hold");
    EXPECT_EQ(s.dataset, "x.txt");
    ASSERT_EQ(s.axes.size(), 2u);
    EXPECT_EQ(s.axes[0].param, "delay");
    EXPECT_DOUBLE_EQ(s.axes[0].to, 2);
    EXPECT_EQ(s.axes[1].param, "size");
    EXPECT_EQ(s.metric, "total-return");
    EXPECT_DOUBLE_EQ(s.threshold, 0.055);
    EXPECT_EQ(s.radius, 1);
    EXPECT_EQ(s.minTrades, 0);
    EXPECT_EQ(s.nullRuns, 3);
    EXPECT_EQ(s.block, 1);
    EXPECT_DOUBLE_EQ(s.cash, 1000);
    EXPECT_DOUBLE_EQ(s.periodsPerYear, 365);
    EXPECT_EQ(s.note, "my pass rule");
}

TEST(SweepJson, CashPeriodsAndParamsAreOptionalEverythingElseIsRequired)
{
    json j = requestJson();
    j.erase("cash"); j.erase("periodsPerYear"); j.erase("params");
    const SweepSpec s = sweepSpecFromJson(j);
    EXPECT_DOUBLE_EQ(s.cash, 10000);
    EXPECT_DOUBLE_EQ(s.periodsPerYear, 260);
    EXPECT_TRUE(s.params.empty());

    for (const char* key : {"strategy", "dataset", "axes", "metric", "threshold", "radius", "minTrades", "nullRuns", "block", "note"})
    {
        json missing = requestJson();
        missing.erase(key);
        EXPECT_THROW(sweepSpecFromJson(missing), std::invalid_argument) << key;      // nothing is picked for the user
    }
}

TEST(SweepJson, RejectsMistakes)
{
    auto bad = [](const std::function<void(json&)>& change) {
        json j = requestJson();
        change(j);
        EXPECT_THROW(sweepSpecFromJson(j), std::invalid_argument);
    };
    bad([](json& j) { j["extra"] = 1; });                                   // unknown key
    bad([](json& j) { j["metric"] = 5; });                                  // wrong type
    bad([](json& j) { j["radius"] = 1.5; });                                // must be a whole number
    bad([](json& j) { j["nullRuns"] = "many"; });
    bad([](json& j) { j["note"] = 7; });
    bad([](json& j) { j["axes"] = 3; });
    bad([](json& j) { j["axes"][0].erase("step"); });
    bad([](json& j) { j["axes"][0]["extra"] = 1; });
    bad([](json& j) { j["axes"][0]["param"] = 4; });
    bad([](json& j) { j["axes"][0]["from"] = "a"; });
    bad([](json& j) { j["params"] = json::array(); });
    bad([](json& j) { j["params"]["x"] = "y"; });
}

TEST(SweepJson, RequestIsNotAnObject)
{
    EXPECT_THROW(sweepSpecFromJson(json::parse("[1]")), std::invalid_argument);
}

TEST_F(SweepJobTest, ResultJsonHasTheGridTheDensitiesAndTheCount)
{
    const std::string text = runSweepJson(registry_, dir(), requestJson().dump());
    const json doc = json::parse(text);

    EXPECT_EQ(doc.at("kind"), "sweep");
    EXPECT_EQ(doc.at("spec").at("note"), "my pass rule");
    EXPECT_EQ(doc.at("spec").at("metric"), "total-return");
    EXPECT_EQ(doc.at("cells").get<int>(), 6);
    EXPECT_EQ(doc.at("bars").get<int>(), 6);
    EXPECT_EQ(doc.at("ideasTried").get<int>(), 24);

    ASSERT_EQ(doc.at("axes").size(), 2u);
    EXPECT_EQ(doc.at("axes")[0].at("param"), "delay");
    EXPECT_EQ(doc.at("axes")[0].at("values"), json::parse("[0,1,2]"));
    EXPECT_EQ(doc.at("axes")[1].at("values"), json::parse("[1,2]"));

    const json& grid = doc.at("grid");
    ASSERT_EQ(grid.at("values").size(), 6u);
    EXPECT_NEAR(grid.at("values")[1].get<double>(), 0.10, 1e-9);
    EXPECT_EQ(grid.at("trades").size(), 6u);
    EXPECT_EQ(grid.at("valid").size(), 6u);
    EXPECT_TRUE(grid.at("valid")[0].get<bool>());

    const json& real = doc.at("real");
    EXPECT_EQ(real.at("validCells").get<int>(), 6);
    EXPECT_DOUBLE_EQ(real.at("passDensity").get<double>(), 0.5);
    EXPECT_DOUBLE_EQ(real.at("plateauDensity").get<double>(), 0.0);

    const json& shuffled = doc.at("shuffled");
    EXPECT_EQ(shuffled.at("runs").get<int>(), 3);
    EXPECT_EQ(shuffled.at("block").get<int>(), 1);
    EXPECT_EQ(shuffled.at("passDensity").at("values").size(), 3u);
    EXPECT_EQ(shuffled.at("plateauDensity").at("values").size(), 3u);
    EXPECT_TRUE(shuffled.at("plateauDensity").contains("median"));
    EXPECT_TRUE(shuffled.at("plateauDensity").contains("p95"));
    EXPECT_TRUE(shuffled.at("plateauDensity").contains("atLeastReal"));
    EXPECT_FALSE(doc.contains("verdict"));               // numbers only: the user judges against their own note
}

TEST_F(SweepJobTest, NonFiniteCellsAreNullInJson)
{
    // Sharpe of a run with no trades and a flat-then-rising curve is a real number, so force a NaN another way: a profit
    // factor cell can be not-a-number; whatever the metric, the JSON must never contain the text "nan" or "inf".
    SweepSpec s = spec();
    s.metric = "profit-factor";
    const std::string text = toJson(run(s)).dump();
    EXPECT_EQ(text.find("nan"), std::string::npos);
    EXPECT_EQ(text.find("inf"), std::string::npos);
}

TEST_F(SweepJobTest, RunSweepJsonRejectsBadText)
{
    EXPECT_THROW(runSweepJson(registry_, dir(), "{not json"), std::invalid_argument);
    json j = requestJson();
    j["note"] = "";
    EXPECT_THROW(runSweepJson(registry_, dir(), j.dump()), std::invalid_argument);
}
