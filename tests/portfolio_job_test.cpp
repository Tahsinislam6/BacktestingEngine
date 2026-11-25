#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "backtester.hpp"
#include "job.hpp"
#include "portfolio_job.hpp"
#include "portfolio_job_json.hpp"
#include "test_strategies.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

// A portfolio job: several sleeves side by side, each one asset (a dataset) with its own strategy, settings and bucket of
// cash; nothing moves between sleeves. It is the job version of the research program `portfolio_run`.
//
//   1. A sleeve is named; its name is also its asset's symbol, so two sleeves can use the same dataset under different names.
//   2. Each sleeve is judged against its own cash; the portfolio against the sum of the cash, on the sum of the curves.
//   3. commonPeriodOnly starts everything on the first day every asset has data (otherwise a late asset sits in cash).
//   4. Everything is checked before anything runs.
//
// Worked example (cash 1000 and 500):
//   sleeve "up":   scaled_hold size 2, delay 0 on closes 100,110,...,150 -> equity 1000 + 2 * (close - 100)
//                  = 1000, 1020, 1040, 1060, 1080, 1100                    final 1100, return 0.10
//   sleeve "down": scaled_hold size 1, delay 1 on closes 200,190,...,150  -> buys at 190 on bar 1: equity 500 + (close - 190)
//                  = 500, 500, 490, 480, 470, 460                          final 460, return -0.08
//   portfolio:     1500, 1520, 1530, 1540, 1550, 1560                      final 1560 on 1500 cash, return 0.04

class PortfolioJobTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("portfolio_job_test_") + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        writeSixBars(dir_ / "up.txt");                        // 100 .. 150
        writeSixBars(dir_ / "down.txt", 200, -10);            // 200 .. 150
        writeSixBars(dir_ / "late.txt", 130, 10, 4, 3);       // 3 bars from day 4: 130, 140, 150
        std::ofstream(dir_ / "empty.csv") << "timestamp,open,high,low,close\n";
        registerScaledHold(registry_);
        registerRoundTrip(registry_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    static PortfolioSpec spec()
    {
        PortfolioSpec p;
        p.sleeves = {
            {"up", "up.txt", "scaled_hold", {{"size", 2}, {"delay", 0}}, 1000},
            {"down", "down.txt", "scaled_hold", {{"size", 1}, {"delay", 1}}, 500},
        };
        return p;
    }

    PortfolioOutput run(const PortfolioSpec& p) const { return runPortfolio(registry_, dir_.string(), p); }
    std::string dir() const { return dir_.string(); }

    StrategyRegistry registry_;
    fs::path dir_;
};

TEST_F(PortfolioJobTest, EquityCurvesMatchTheHandWorkedValues)
{
    const PortfolioOutput out = run(spec());

    ASSERT_EQ(out.result.sleeves.size(), 2u);
    EXPECT_EQ(out.result.sleeves[0].name, "up");
    EXPECT_EQ(out.result.sleeves[1].name, "down");
    const double up[] = {1000, 1020, 1040, 1060, 1080, 1100};
    const double down[] = {500, 500, 490, 480, 470, 460};
    const double all[] = {1500, 1520, 1530, 1540, 1550, 1560};
    ASSERT_EQ(out.result.combined.equityCurve.size(), 6u);
    for (int i = 0; i < 6; ++i)
    {
        EXPECT_NEAR(out.result.sleeves[0].result.equityCurve[i], up[i], 1e-9) << i;
        EXPECT_NEAR(out.result.sleeves[1].result.equityCurve[i], down[i], 1e-9) << i;
        EXPECT_NEAR(out.result.combined.equityCurve[i], all[i], 1e-9) << i;
    }
    EXPECT_NEAR(out.result.combined.finalEquity, 1560, 1e-9);
    EXPECT_DOUBLE_EQ(out.totalCash, 1500);
    EXPECT_EQ(out.bars, 6u);
    EXPECT_EQ(out.timestamps.size(), 6u);
}

TEST_F(PortfolioJobTest, EachSleeveIsJudgedAgainstItsOwnCashAndThePortfolioAgainstTheSum)
{
    const PortfolioOutput out = run(spec());

    EXPECT_NEAR(out.metrics.sleeves[0].metrics.totalReturn, 0.10, 1e-9);
    EXPECT_NEAR(out.metrics.sleeves[1].metrics.totalReturn, -0.08, 1e-9);
    EXPECT_NEAR(out.metrics.combined.totalReturn, 0.04, 1e-9);        // 1560 / 1500 - 1
    EXPECT_EQ(out.metrics.sleeves[0].name, "up");
}

TEST_F(PortfolioJobTest, OpenTradesAreTaggedWithTheirSleeve)
{
    const PortfolioOutput out = run(spec());

    EXPECT_TRUE(out.result.combined.closedTrades.empty());
    ASSERT_EQ(out.result.combined.openTrades.size(), 2u);
    ASSERT_EQ(out.result.openTradeSleeve.size(), 2u);
    EXPECT_EQ(out.result.openTradeSleeve[0], "up");
    EXPECT_EQ(out.result.openTradeSleeve[1], "down");
    EXPECT_DOUBLE_EQ(out.result.combined.openTrades[0].quantity, 2.0);
    EXPECT_DOUBLE_EQ(out.result.combined.openTrades[1].entryPrice, 190.0);
}

TEST_F(PortfolioJobTest, ClosedTradesAreTaggedWithTheirSleeveOneSleeveAfterTheOther)
{
    PortfolioSpec p = spec();
    p.sleeves = {{"a", "up.txt", "round_trip", {{"size", 1}}, 1000}, {"b", "up.txt", "round_trip", {{"size", 2}}, 1000}};

    const PortfolioOutput out = run(p);

    ASSERT_EQ(out.result.combined.closedTrades.size(), 2u);
    EXPECT_EQ(out.result.closedTradeSleeve[0], "a");
    EXPECT_EQ(out.result.closedTradeSleeve[1], "b");
    EXPECT_NEAR(out.result.combined.closedTrades[0].pnl, 30, 1e-9);        // 1 unit x (130 - 100)
    EXPECT_NEAR(out.result.combined.closedTrades[1].pnl, 60, 1e-9);        // 2 units x 30

    // and in the JSON, each trade names its sleeve
    const json doc = toJson(out);
    ASSERT_EQ(doc.at("trades").size(), 2u);
    EXPECT_EQ(doc.at("trades")[0].at("sleeve"), "a");
    EXPECT_NEAR(doc.at("trades")[0].at("pnl").get<double>(), 30, 1e-9);
    EXPECT_EQ(doc.at("trades")[1].at("sleeve"), "b");
    EXPECT_NEAR(doc.at("trades")[1].at("pnl").get<double>(), 60, 1e-9);
    EXPECT_EQ(doc.at("trades")[1].at("reason"), "signal");
    EXPECT_EQ(doc.at("openTrades").size(), 0u);
}

TEST_F(PortfolioJobTest, TheSameDatasetCanBeInTwoSleevesUnderDifferentNames)
{
    PortfolioSpec p = spec();
    p.sleeves = {{"first", "up.txt", "scaled_hold", {{"size", 1}}, 1000}, {"second", "up.txt", "scaled_hold", {{"size", 3}}, 1000}};

    const PortfolioOutput out = run(p);

    EXPECT_NEAR(out.result.sleeves[0].result.finalEquity, 1050, 1e-9);     // 1 unit x 50
    EXPECT_NEAR(out.result.sleeves[1].result.finalEquity, 1150, 1e-9);     // 3 units x 50
    EXPECT_NEAR(out.result.combined.finalEquity, 2200, 1e-9);
}

TEST_F(PortfolioJobTest, ParametersAreResolvedPerSleeveInTheResult)
{
    PortfolioSpec p = spec();
    p.sleeves[0].params = {{"size", 4}};                    // delay left out -> its default
    const PortfolioOutput out = run(p);

    EXPECT_DOUBLE_EQ(out.spec.sleeves[0].params.at("size"), 4);
    EXPECT_DOUBLE_EQ(out.spec.sleeves[0].params.at("delay"), 0);
    EXPECT_DOUBLE_EQ(out.spec.sleeves[1].params.at("size"), 1);
}

TEST_F(PortfolioJobTest, WithoutCommonPeriodOnlyALateAssetSitsInCash)
{
    PortfolioSpec p = spec();
    p.sleeves.push_back({"late", "late.txt", "scaled_hold", {{"size", 1}, {"delay", 0}}, 300});

    const PortfolioOutput out = run(p);

    ASSERT_EQ(out.bars, 6u);
    const auto& late = out.result.sleeves[2].result.equityCurve;
    EXPECT_NEAR(late[0], 300, 1e-9);                        // no data yet: just cash
    EXPECT_NEAR(late[2], 300, 1e-9);
    EXPECT_NEAR(late[3], 300, 1e-9);                        // buys at 130 on its first bar (day 4)
    EXPECT_NEAR(late[5], 320, 1e-9);                        // 150 - 130
}

TEST_F(PortfolioJobTest, CommonPeriodOnlyStartsWhenEveryAssetHasData)
{
    PortfolioSpec p = spec();
    p.sleeves.push_back({"late", "late.txt", "scaled_hold", {{"size", 1}, {"delay", 0}}, 300});
    p.commonPeriodOnly = true;

    const PortfolioOutput out = run(p);

    ASSERT_EQ(out.bars, 3u);                                // days 4, 5 and 6
    ASSERT_EQ(out.timestamps.size(), 3u);
    EXPECT_EQ(out.timestamps[0], 1578096000000LL);          // 2020-01-04 00:00 UTC in milliseconds
    EXPECT_EQ(out.result.combined.equityCurve.size(), 3u);
    EXPECT_NEAR(out.result.sleeves[2].result.finalEquity, 320, 1e-9);
}

TEST_F(PortfolioJobTest, BarsPerYearReachesTheMetrics)
{
    PortfolioSpec p = spec();
    p.periodsPerYear = 365;
    const PortfolioOutput at365 = run(p);
    p.periodsPerYear = 260;
    const PortfolioOutput at260 = run(p);

    EXPECT_DOUBLE_EQ(at365.metrics.combined.cagr, computeMetrics(at365.result.combined, 1500, 365).cagr);
    EXPECT_DOUBLE_EQ(at260.metrics.combined.cagr, computeMetrics(at260.result.combined, 1500, 260).cagr);
    EXPECT_NE(at365.metrics.combined.cagr, at260.metrics.combined.cagr);
    EXPECT_NEAR(at365.metrics.sleeves[0].metrics.cagr, computeMetrics(at365.result.sleeves[0].result, 1000, 365).cagr, 1e-12);
}

TEST_F(PortfolioJobTest, RefusesBadPortfolios)
{
    auto bad = [&](const std::function<void(PortfolioSpec&)>& change) {
        PortfolioSpec p = spec();
        change(p);
        EXPECT_THROW(validatePortfolioSpec(registry_, dir(), p), std::invalid_argument);
        EXPECT_THROW(run(p), std::invalid_argument);
    };
    bad([](PortfolioSpec& p) { p.sleeves.clear(); });
    bad([](PortfolioSpec& p) { p.sleeves[1].name = "up"; });                    // a name used twice
    bad([](PortfolioSpec& p) { p.sleeves[0].name = ""; });
    bad([](PortfolioSpec& p) { p.sleeves[0].name = "   "; });
    bad([](PortfolioSpec& p) { p.sleeves[0].name = std::string(41, 'x'); });
    bad([](PortfolioSpec& p) { p.sleeves[0].strategy = "nope"; });
    bad([](PortfolioSpec& p) { p.sleeves[0].params = {{"bogus", 1}}; });
    bad([](PortfolioSpec& p) { p.sleeves[0].params = {{"size", 9}}; });         // above the limit
    bad([](PortfolioSpec& p) { p.sleeves[0].params = {{"size", 1.5}}; });       // must be whole
    bad([](PortfolioSpec& p) { p.sleeves[0].dataset = "../up.txt"; });
    bad([](PortfolioSpec& p) { p.sleeves[0].dataset = "missing.txt"; });
    bad([](PortfolioSpec& p) { p.sleeves[0].cash = 0; });
    bad([](PortfolioSpec& p) { p.sleeves[1].cash = -1; });
    bad([](PortfolioSpec& p) { p.sleeves[0].cash = std::nan(""); });
    bad([](PortfolioSpec& p) { p.periodsPerYear = 0; });
    bad([](PortfolioSpec& p) { const PortfolioSleeve first = p.sleeves[0]; p.sleeves.assign(51, first); });   // repeated names
    bad([](PortfolioSpec& p) {                                                                                  // too many
        const PortfolioSleeve first = p.sleeves[0];
        p.sleeves.clear();
        for (int i = 0; i < 51; ++i)
        {
            PortfolioSleeve s = first;
            s.name = "sleeve" + std::to_string(i);
            p.sleeves.push_back(s);
        }
    });
}

TEST_F(PortfolioJobTest, ASleeveWhoseDatasetHasNoBarsFailsWhileRunning)
{
    PortfolioSpec p = spec();
    p.sleeves[1].dataset = "empty.csv";
    EXPECT_THROW(run(p), std::runtime_error);
}

// ---- JSON ----

static json requestJson()
{
    return json::parse(R"({
        "sleeves": [
            {"name": "up", "dataset": "up.txt", "strategy": "scaled_hold", "params": {"size": 2, "delay": 0}, "cash": 1000},
            {"name": "down", "dataset": "down.txt", "strategy": "scaled_hold", "params": {"size": 1, "delay": 1}, "cash": 500}
        ],
        "commonPeriodOnly": false, "periodsPerYear": 365 })");
}

TEST(PortfolioJson, ReadsARequest)
{
    const PortfolioSpec p = portfolioSpecFromJson(requestJson());
    ASSERT_EQ(p.sleeves.size(), 2u);
    EXPECT_EQ(p.sleeves[0].name, "up");
    EXPECT_EQ(p.sleeves[0].dataset, "up.txt");
    EXPECT_EQ(p.sleeves[0].strategy, "scaled_hold");
    EXPECT_DOUBLE_EQ(p.sleeves[0].params.at("size"), 2);
    EXPECT_DOUBLE_EQ(p.sleeves[0].cash, 1000);
    EXPECT_FALSE(p.commonPeriodOnly);
    EXPECT_DOUBLE_EQ(p.periodsPerYear, 365);
}

TEST(PortfolioJson, OptionsAndParamsAreOptional)
{
    const PortfolioSpec p = portfolioSpecFromJson(json::parse(
        R"({"sleeves": [{"name": "a", "dataset": "b", "strategy": "c", "cash": 5}]})"));
    EXPECT_FALSE(p.commonPeriodOnly);
    EXPECT_DOUBLE_EQ(p.periodsPerYear, 260);
    EXPECT_TRUE(p.sleeves[0].params.empty());
}

TEST(PortfolioJson, RejectsMistakes)
{
    auto bad = [](const std::function<void(json&)>& change) {
        json j = requestJson();
        change(j);
        EXPECT_THROW(portfolioSpecFromJson(j), std::invalid_argument);
    };
    bad([](json& j) { j.erase("sleeves"); });
    bad([](json& j) { j["extra"] = 1; });
    bad([](json& j) { j["sleeves"] = 3; });
    bad([](json& j) { j["sleeves"][0] = 3; });
    bad([](json& j) { j["sleeves"][0].erase("name"); });
    bad([](json& j) { j["sleeves"][0].erase("dataset"); });
    bad([](json& j) { j["sleeves"][0].erase("strategy"); });
    bad([](json& j) { j["sleeves"][0].erase("cash"); });
    bad([](json& j) { j["sleeves"][0]["extra"] = 1; });
    bad([](json& j) { j["sleeves"][0]["cash"] = "lots"; });
    bad([](json& j) { j["sleeves"][0]["name"] = 5; });
    bad([](json& j) { j["sleeves"][0]["params"] = json::array(); });
    bad([](json& j) { j["sleeves"][0]["params"]["size"] = "x"; });
    bad([](json& j) { j["commonPeriodOnly"] = "yes"; });
    bad([](json& j) { j["periodsPerYear"] = "daily"; });
}

TEST(PortfolioJson, RequestIsNotAnObject)
{
    EXPECT_THROW(portfolioSpecFromJson(json::parse("[1]")), std::invalid_argument);
}

TEST_F(PortfolioJobTest, ResultJsonHasEverythingTheScreenNeeds)
{
    const json doc = json::parse(runPortfolioJson(registry_, dir(), requestJson().dump()));

    EXPECT_EQ(doc.at("kind"), "portfolio");
    EXPECT_EQ(doc.at("bars").get<int>(), 6);
    EXPECT_DOUBLE_EQ(doc.at("totalCash").get<double>(), 1500);
    EXPECT_EQ(doc.at("timestamps").size(), 6u);
    EXPECT_EQ(doc.at("spec").at("sleeves").size(), 2u);
    EXPECT_DOUBLE_EQ(doc.at("spec").at("sleeves")[0].at("params").at("delay").get<double>(), 0);

    const json& combined = doc.at("combined");
    EXPECT_NEAR(combined.at("finalEquity").get<double>(), 1560, 1e-9);
    EXPECT_DOUBLE_EQ(combined.at("cash").get<double>(), 1500);
    ASSERT_EQ(combined.at("equity").size(), 6u);
    EXPECT_NEAR(combined.at("equity")[2].get<double>(), 1530, 1e-9);
    EXPECT_NEAR(combined.at("metrics").at("totalReturn").get<double>(), 0.04, 1e-9);

    ASSERT_EQ(doc.at("sleeves").size(), 2u);
    const json& down = doc.at("sleeves")[1];
    EXPECT_EQ(down.at("name"), "down");
    EXPECT_EQ(down.at("dataset"), "down.txt");
    EXPECT_EQ(down.at("strategy"), "scaled_hold");
    EXPECT_DOUBLE_EQ(down.at("cash").get<double>(), 500);
    EXPECT_NEAR(down.at("finalEquity").get<double>(), 460, 1e-9);
    EXPECT_NEAR(down.at("metrics").at("totalReturn").get<double>(), -0.08, 1e-9);
    ASSERT_EQ(down.at("equity").size(), 6u);
    EXPECT_NEAR(down.at("equity")[5].get<double>(), 460, 1e-9);

    EXPECT_EQ(doc.at("trades").size(), 0u);
    ASSERT_EQ(doc.at("openTrades").size(), 2u);
    EXPECT_EQ(doc.at("openTrades")[0].at("sleeve"), "up");
    EXPECT_EQ(doc.at("openTrades")[1].at("sleeve"), "down");
}

TEST_F(PortfolioJobTest, RunPortfolioJsonRejectsBadText)
{
    EXPECT_THROW(runPortfolioJson(registry_, dir(), "{not json"), std::invalid_argument);
    json j = requestJson();
    j["sleeves"][0]["cash"] = 0;
    EXPECT_THROW(runPortfolioJson(registry_, dir(), j.dump()), std::invalid_argument);
}

// ---- layouts: each sleeve names how its own file is read ----

TEST_F(PortfolioJobTest, ASleeveLayoutDefaultsToAutoAndIsReadFromJson)
{
    EXPECT_EQ(PortfolioSleeve{}.layout, "auto");
    EXPECT_EQ(portfolioSpecFromJson(requestJson()).sleeves[0].layout, "auto");
    json j = requestJson();
    j["sleeves"][1]["layout"] = "stooq";
    const PortfolioSpec p = portfolioSpecFromJson(j);
    EXPECT_EQ(p.sleeves[0].layout, "auto");
    EXPECT_EQ(p.sleeves[1].layout, "stooq");
    EXPECT_EQ(toJson(p).at("sleeves")[1].at("layout"), "stooq");
    EXPECT_EQ(toJson(p).at("sleeves")[0].at("layout"), "auto");
    j["sleeves"][1]["layout"] = 3;
    EXPECT_THROW(portfolioSpecFromJson(j), std::invalid_argument);
}

TEST_F(PortfolioJobTest, AnUnknownSleeveLayoutIsRefusedAndNamesTheSleeve)
{
    PortfolioSpec p = spec();
    p.sleeves[1].layout = "nope";
    try
    {
        validatePortfolioSpec(registry_, dir(), p);
        FAIL();
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("down"), std::string::npos) << e.what();
    }
}

TEST_F(PortfolioJobTest, EachSleeveUsesItsOwnLayout)
{
    std::ofstream(dir_ / "simple.csv") << "timestamp,open,high,low,close\n1577836800000,100,100,100,100\n1577923200000,110,110,110,110\n";
    PortfolioSpec p = spec();                            // up.txt and down.txt are Stooq files
    p.sleeves[0].layout = "stooq";
    p.sleeves[1].layout = "stooq";
    EXPECT_NO_THROW(run(p));
    p.sleeves[1].layout = "simple";                      // wrong for down.txt: only that sleeve is read differently
    EXPECT_THROW(run(p), std::exception);
    p.sleeves[1] = {"flat", "simple.csv", "scaled_hold", {{"size", 1}, {"delay", 0}}, 500, "simple"};
    EXPECT_NO_THROW(run(p));                             // a Stooq sleeve and a simple sleeve in one portfolio
}
