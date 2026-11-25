#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "buy_and_hold.hpp"
#include "job.hpp"
#include "job_json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

// The job runner turns a plain description of a run (strategy name, parameters, dataset name, cash) into a finished
// backtest. This is what the web app will send, so every mistake must come back as a clear exception, never a crash.
//
// Rules for datasets (a dataset is a file name inside one data folder):
//   1. listDatasets() gives the .csv and .txt file names in the folder, sorted; a missing folder is an error.
//   2. resolveDataset() gives the full path; a name with a slash, a backslash or ".." or a file that does not exist
//      is an error (a request must never reach outside the data folder).
//   3. loadDataset() reads a Stooq file (header with <DATE>) with the Stooq layout and anything else with the default
//      layout, so both kinds of file in the data folder just work.
//
// Rules for runJob():
//   4. It fills in the default parameters, loads the dataset, runs the backtest and measures it.
//   5. It throws std::invalid_argument for an unknown strategy or parameter, a bad dataset name, cash <= 0 or
//      periodsPerYear <= 0, and std::runtime_error when the dataset has no bars.
//
// Rules for JSON (job_json.hpp):
//   6. A request has "strategy" and "dataset" (required) and "params", "cash" (default 10000) and "periodsPerYear"
//      (default 260). Any other key, a wrong type or a missing required key is std::invalid_argument.
//   7. The result JSON has "spec" (with the resolved parameters), "metrics", "timestamps", "equity", "trades" and
//      "openTrades". Timestamps and equity have one entry per bar.
//   8. A number that is not finite (e.g. a profit factor with no losing trades) is written as null.

class JobTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("job_test_") + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        registerBuiltins(registry_);
    }

    void TearDown() override { fs::remove_all(dir_); }

    void write(const std::string& name, const std::string& content)
    {
        std::ofstream out(dir_ / name);
        out << content;
    }

    // Four daily bars with closes 100, 110, 121, 133.1 (stooq layout).
    void writeStooq(const std::string& name = "x.txt")
    {
        write(name,
              "<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>\n"
              "X,D,20200101,000000,100,100,100,100,0,0\n"
              "X,D,20200102,000000,110,110,110,110,0,0\n"
              "X,D,20200103,000000,121,121,121,121,0,0\n"
              "X,D,20200104,000000,133.1,133.1,133.1,133.1,0,0\n");
    }

    void writeSimple(const std::string& name = "s.csv")
    {
        write(name,
              "timestamp,open,high,low,close\n"
              "1577836800000,100,100,100,100\n"
              "1577923200000,110,110,110,110\n");
    }

    std::string dir() const { return dir_.string(); }

    StrategyRegistry registry_;
    fs::path dir_;
};

// ---- datasets ----

TEST_F(JobTest, ListDatasetsGivesCsvAndTxtFilesSorted)
{
    writeStooq("b.txt");
    writeSimple("a.csv");
    write("notes.md", "ignore me");

    const auto names = listDatasets(dir());

    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "a.csv");
    EXPECT_EQ(names[1], "b.txt");
}

TEST_F(JobTest, ListDatasetsOfMissingFolderThrows)
{
    EXPECT_THROW(listDatasets((dir_ / "nope").string()), std::runtime_error);
}

TEST_F(JobTest, ResolveDatasetGivesThePathInsideTheFolder)
{
    writeStooq();
    EXPECT_EQ(fs::path(resolveDataset(dir(), "x.txt")), dir_ / "x.txt");
}

TEST_F(JobTest, ResolveDatasetRejectsNamesThatLeaveTheFolder)
{
    writeStooq();
    EXPECT_THROW(resolveDataset(dir(), "../x.txt"), std::invalid_argument);
    EXPECT_THROW(resolveDataset(dir(), "sub/x.txt"), std::invalid_argument);
    EXPECT_THROW(resolveDataset(dir(), "sub\\x.txt"), std::invalid_argument);
    EXPECT_THROW(resolveDataset(dir(), "/etc/passwd"), std::invalid_argument);
    EXPECT_THROW(resolveDataset(dir(), ".."), std::invalid_argument);
    EXPECT_THROW(resolveDataset(dir(), ""), std::invalid_argument);
}

TEST_F(JobTest, ResolveDatasetRejectsMissingFile)
{
    EXPECT_THROW(resolveDataset(dir(), "missing.txt"), std::invalid_argument);
}

TEST_F(JobTest, LoadDatasetReadsStooqFiles)
{
    writeStooq();
    const BarSeries s = loadDataset(dir(), "x.txt");
    ASSERT_EQ(s.bars.size(), 4u);
    EXPECT_EQ(s.bars[0].timestamp, 1577836800000);   // 2020-01-01 UTC in milliseconds
    EXPECT_DOUBLE_EQ(s.bars[3].close, 133.1);
}

TEST_F(JobTest, LoadDatasetReadsSimpleFiles)
{
    writeSimple();
    const BarSeries s = loadDataset(dir(), "s.csv");
    ASSERT_EQ(s.bars.size(), 2u);
    EXPECT_EQ(s.bars[1].timestamp, 1577923200000);
}

// ---- runJob ----

TEST_F(JobTest, RunJobRunsBuyAndHold)
{
    writeStooq();
    RunSpec spec;
    spec.strategy = "buy_and_hold";
    spec.dataset = "x.txt";
    spec.cash = 1000.0;

    const RunOutput out = runJob(registry_, dir(), spec);

    ASSERT_EQ(out.timestamps.size(), 4u);
    ASSERT_EQ(out.result.equityCurve.size(), 4u);
    EXPECT_NEAR(out.result.finalEquity, 1331.0, 1e-9);        // 1000 x 133.1 / 100
    EXPECT_NEAR(out.metrics.totalReturn, 0.331, 1e-9);
}

TEST_F(JobTest, RunJobReportsTheResolvedParameters)
{
    writeStooq();
    RunSpec spec;
    spec.strategy = "buy_and_hold";
    spec.dataset = "x.txt";

    const RunOutput out = runJob(registry_, dir(), spec);

    EXPECT_DOUBLE_EQ(out.spec.params.at("fraction"), 1.0);    // the default was filled in
}

TEST_F(JobTest, RunJobUsesTheGivenParameters)
{
    writeStooq();
    RunSpec spec;
    spec.strategy = "buy_and_hold";
    spec.dataset = "x.txt";
    spec.cash = 1000.0;
    spec.params = {{"fraction", 0.5}};

    const RunOutput out = runJob(registry_, dir(), spec);

    EXPECT_NEAR(out.result.finalEquity, 1165.5, 1e-9);        // cash 1000 + 5 x 33.1
}

TEST_F(JobTest, RunJobRejectsBadRequests)
{
    writeStooq();
    RunSpec good;
    good.strategy = "buy_and_hold";
    good.dataset = "x.txt";

    RunSpec s = good; s.strategy = "nope";
    EXPECT_THROW(runJob(registry_, dir(), s), std::invalid_argument);

    s = good; s.params = {{"bogus", 1}};
    EXPECT_THROW(runJob(registry_, dir(), s), std::invalid_argument);

    s = good; s.dataset = "../x.txt";
    EXPECT_THROW(runJob(registry_, dir(), s), std::invalid_argument);

    s = good; s.cash = 0;
    EXPECT_THROW(runJob(registry_, dir(), s), std::invalid_argument);

    s = good; s.cash = -5;
    EXPECT_THROW(runJob(registry_, dir(), s), std::invalid_argument);

    s = good; s.periodsPerYear = 0;
    EXPECT_THROW(runJob(registry_, dir(), s), std::invalid_argument);
}

TEST_F(JobTest, RunJobRejectsDatasetWithNoBars)
{
    write("empty.csv", "timestamp,open,high,low,close\n");
    RunSpec spec;
    spec.strategy = "buy_and_hold";
    spec.dataset = "empty.csv";
    EXPECT_THROW(runJob(registry_, dir(), spec), std::runtime_error);
}

// ---- JSON ----

TEST_F(JobTest, RunSpecFromJsonReadsAllFields)
{
    const RunSpec spec = runSpecFromJson(json::parse(R"({
        "strategy": "buy_and_hold", "dataset": "x.txt", "params": {"fraction": 0.5},
        "cash": 2500, "periodsPerYear": 365 })"));

    EXPECT_EQ(spec.strategy, "buy_and_hold");
    EXPECT_EQ(spec.dataset, "x.txt");
    EXPECT_DOUBLE_EQ(spec.params.at("fraction"), 0.5);
    EXPECT_DOUBLE_EQ(spec.cash, 2500);
    EXPECT_DOUBLE_EQ(spec.periodsPerYear, 365);
}

TEST_F(JobTest, RunSpecFromJsonUsesDefaults)
{
    const RunSpec spec = runSpecFromJson(json::parse(R"({"strategy": "buy_and_hold", "dataset": "x.txt"})"));

    EXPECT_TRUE(spec.params.empty());
    EXPECT_DOUBLE_EQ(spec.cash, 10000);
    EXPECT_DOUBLE_EQ(spec.periodsPerYear, 260);
}

TEST_F(JobTest, RunSpecFromJsonRejectsMistakes)
{
    EXPECT_THROW(runSpecFromJson(json::parse(R"({"dataset": "x.txt"})")), std::invalid_argument);          // no strategy
    EXPECT_THROW(runSpecFromJson(json::parse(R"({"strategy": "a"})")), std::invalid_argument);             // no dataset
    EXPECT_THROW(runSpecFromJson(json::parse(R"({"strategy":"a","dataset":"b","cahs":5})")), std::invalid_argument);   // typo
    EXPECT_THROW(runSpecFromJson(json::parse(R"({"strategy":1,"dataset":"b"})")), std::invalid_argument);   // wrong type
    EXPECT_THROW(runSpecFromJson(json::parse(R"({"strategy":"a","dataset":"b","cash":"lots"})")), std::invalid_argument);
    EXPECT_THROW(runSpecFromJson(json::parse(R"({"strategy":"a","dataset":"b","params":{"x":"y"}})")), std::invalid_argument);
    EXPECT_THROW(runSpecFromJson(json::parse(R"({"strategy":"a","dataset":"b","params":[1]})")), std::invalid_argument);
    EXPECT_THROW(runSpecFromJson(json::parse("[1,2]")), std::invalid_argument);                             // not an object
}

TEST_F(JobTest, RunJobJsonReturnsTheResultDocument)
{
    writeStooq();
    const std::string text = runJobJson(registry_, dir(),
        R"({"strategy": "buy_and_hold", "dataset": "x.txt", "cash": 1000})");

    const json doc = json::parse(text);

    EXPECT_EQ(doc.at("spec").at("strategy"), "buy_and_hold");
    EXPECT_DOUBLE_EQ(doc.at("spec").at("params").at("fraction"), 1.0);
    EXPECT_EQ(doc.at("timestamps").size(), 4u);
    EXPECT_EQ(doc.at("equity").size(), 4u);
    EXPECT_NEAR(doc.at("equity").back().get<double>(), 1331.0, 1e-9);
    EXPECT_NEAR(doc.at("metrics").at("totalReturn").get<double>(), 0.331, 1e-9);
    EXPECT_EQ(doc.at("trades").size(), 0u);
    EXPECT_EQ(doc.at("openTrades").size(), 1u);
    EXPECT_DOUBLE_EQ(doc.at("openTrades")[0].at("quantity").get<double>(), 10.0);
}

TEST_F(JobTest, RunJobJsonWritesClosedTrades)
{
    // A strategy that closes its trade is needed; use a registry with a tiny strategy defined here.
    writeStooq();
    class OpenThenClose : public Strategy
    {
    public:
        std::vector<Order> onBar(const Bar&, const Portfolio& p) override
        {
            if (++n_ == 1) return { openOrder(Direction::Long, 1, 90.0) };
            if (n_ == 3) return { closeOrder(p.openTrades()[0].id) };
            return {};
        }
    private:
        int n_{0};
    };
    StrategyInfo info;
    info.name = "open_close";
    registry_.add(info, [](const NamedParams&) { return std::make_unique<OpenThenClose>(); });

    const json doc = json::parse(runJobJson(registry_, dir(), R"({"strategy":"open_close","dataset":"x.txt"})"));

    ASSERT_EQ(doc.at("trades").size(), 1u);
    const json& t = doc.at("trades")[0];
    EXPECT_EQ(t.at("direction"), "long");
    EXPECT_DOUBLE_EQ(t.at("entryPrice").get<double>(), 100.0);
    EXPECT_DOUBLE_EQ(t.at("exitPrice").get<double>(), 121.0);
    EXPECT_DOUBLE_EQ(t.at("pnl").get<double>(), 21.0);
    EXPECT_EQ(t.at("reason"), "signal");
    EXPECT_DOUBLE_EQ(t.at("initialStop").get<double>(), 90.0);
    EXPECT_EQ(t.at("entryTime").get<long long>(), 1577836800000LL);
    EXPECT_EQ(t.at("exitTime").get<long long>(), 1577836800000LL + 2 * 86'400'000LL);
}

TEST_F(JobTest, NonFiniteNumbersBecomeNull)
{
    writeStooq();   // buy and hold never closes a trade, so the profit factor has nothing to divide
    const json doc = json::parse(runJobJson(registry_, dir(), R"({"strategy":"buy_and_hold","dataset":"x.txt"})"));
    for (const auto& [key, value] : doc.at("metrics").items())
        EXPECT_TRUE(value.is_number() || value.is_null()) << key;   // never the text "nan" / "inf"
}

TEST_F(JobTest, RunJobJsonRejectsBadText)
{
    EXPECT_THROW(runJobJson(registry_, dir(), "{not json"), std::invalid_argument);
    EXPECT_THROW(runJobJson(registry_, dir(), R"({"strategy":"nope","dataset":"x.txt"})"), std::invalid_argument);
}

TEST_F(JobTest, StrategiesToJsonListsSchemas)
{
    const json doc = strategiesToJson(registry_);

    ASSERT_EQ(doc.size(), 1u);
    EXPECT_EQ(doc[0].at("name"), "buy_and_hold");
    ASSERT_EQ(doc[0].at("params").size(), 1u);
    const json& p = doc[0].at("params")[0];
    EXPECT_EQ(p.at("name"), "fraction");
    EXPECT_DOUBLE_EQ(p.at("default").get<double>(), 1.0);
    EXPECT_DOUBLE_EQ(p.at("min").get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(p.at("max").get<double>(), 1.0);
    EXPECT_EQ(p.at("integer"), false);
}
