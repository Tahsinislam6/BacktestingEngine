#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "report.hpp"

namespace fs = std::filesystem;

namespace
{
const double kInf = std::numeric_limits<double>::infinity();

Ranked rankedRun(Params params, double score, int trades = 40)
{
    Ranked r;
    r.score = score;
    r.evaluation.params = params;
    r.evaluation.metrics.trades = trades;
    r.evaluation.metrics.sharpe = score;
    return r;
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
        out.push_back(line);
    return out;
}

std::vector<std::string> fields(const std::string& line)
{
    std::vector<std::string> out;
    std::istringstream in(line);
    for (std::string f; std::getline(in, f, ',');)
        out.push_back(f);
    return out;
}

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
}

// ---------- small helpers ----------

TEST(ParamsTextTest, ListsNamesInAlphabeticalOrder)
{
    EXPECT_EQ(paramsText(Params{{"period", 20}, {"atrMultiple", 1.5}}), "atrMultiple=1.5, period=20");
}

TEST(ParamsTextTest, NoParameters)
{
    EXPECT_EQ(paramsText(Params{}), "(no parameters)");
}

TEST(ObjectiveNameTest, EveryObjectiveHasItsCommandLineName)
{
    EXPECT_EQ(objectiveName(Objective::Sharpe), "sharpe");
    EXPECT_EQ(objectiveName(Objective::Sortino), "sortino");
    EXPECT_EQ(objectiveName(Objective::Calmar), "calmar");
    EXPECT_EQ(objectiveName(Objective::TotalReturn), "return");
    EXPECT_EQ(objectiveName(Objective::CaptureEfficiency), "capture");
}

// ---------- rankedCsv ----------

TEST(RankedCsvTest, NothingRankedGivesJustTheHeader)
{
    const std::vector<std::string> l = lines(rankedCsv({}));

    ASSERT_EQ(l.size(), 1u);
    EXPECT_EQ(l[0].rfind("rank,score,trades,total_return", 0), 0u);
}

TEST(RankedCsvTest, ParameterColumnsFollowTheScoreInAlphabeticalOrder)
{
    const std::vector<std::string> l = lines(rankedCsv({rankedRun({{"period", 20}, {"atrMultiple", 1.5}}, 1.0)}));

    ASSERT_EQ(l.size(), 2u);
    const std::vector<std::string> header = fields(l[0]);
    EXPECT_EQ(header[0], "rank");
    EXPECT_EQ(header[1], "score");
    EXPECT_EQ(header[2], "atrMultiple");
    EXPECT_EQ(header[3], "period");
    EXPECT_EQ(header[4], "trades");

    const std::vector<std::string> row = fields(l[1]);
    EXPECT_EQ(row[0], "1");
    EXPECT_EQ(row[2], "1.5");
    EXPECT_EQ(row[3], "20");
    EXPECT_EQ(row[4], "40");
}

TEST(RankedCsvTest, EveryRowHasAsManyFieldsAsTheHeader)
{
    const std::vector<std::string> l = lines(rankedCsv({rankedRun({{"p", 1}}, 2.0), rankedRun({{"p", 2}}, 1.0)}));

    ASSERT_EQ(l.size(), 3u);
    EXPECT_EQ(fields(l[1]).size(), fields(l[0]).size());
    EXPECT_EQ(fields(l[2]).size(), fields(l[0]).size());
}

TEST(RankedCsvTest, RowsAreNumberedFromOneInTheOrderGiven)
{
    const std::vector<std::string> l = lines(rankedCsv({rankedRun({{"p", 7}}, 3.0), rankedRun({{"p", 9}}, 2.0)}));

    EXPECT_EQ(fields(l[1])[0], "1");
    EXPECT_EQ(fields(l[1])[2], "7");
    EXPECT_EQ(fields(l[2])[0], "2");
    EXPECT_EQ(fields(l[2])[2], "9");
}

TEST(RankedCsvTest, WritesTheMetricsInTheirColumns)
{
    Ranked r = rankedRun({{"p", 1}}, 0.5);
    r.evaluation.metrics.totalReturn = 0.25;
    r.evaluation.metrics.maxDrawdown = 0.1;
    r.evaluation.metrics.winRate = 0.6;
    r.evaluation.metrics.avgHoldingSeconds = 2 * 86400.0;
    r.evaluation.capture.efficiency = 0.3;
    r.evaluation.capture.legs = 12;
    r.evaluation.capture.rightShare = 0.75;

    const std::vector<std::string> l = lines(rankedCsv({r}));
    const std::vector<std::string> header = fields(l[0]);
    const std::vector<std::string> row = fields(l[1]);

    auto at = [&](const std::string& column)
    {
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i] == column)
                return row[i];
        return std::string("MISSING");
    };
    EXPECT_EQ(at("total_return"), "0.25");
    EXPECT_EQ(at("max_drawdown"), "0.1");
    EXPECT_EQ(at("win_rate"), "0.6");
    EXPECT_EQ(at("avg_holding_days"), "2");
    EXPECT_EQ(at("capture_efficiency"), "0.3");
    EXPECT_EQ(at("capture_legs"), "12");
    EXPECT_EQ(at("right_share"), "0.75");
}

TEST(RankedCsvTest, InfinityAndNaNAreWrittenAsWords)
{
    Ranked r = rankedRun({{"p", 1}}, 1.0);
    r.evaluation.metrics.profitFactor = kInf;
    r.evaluation.metrics.calmar = std::nan("");

    const std::string csv = rankedCsv({r});

    EXPECT_NE(csv.find("inf"), std::string::npos);
    EXPECT_NE(csv.find("nan"), std::string::npos);
}

// ---------- writeTextFile ----------

TEST(WriteTextFileTest, WritesAndReplaces)
{
    const fs::path path = fs::temp_directory_path() / "report_test_write.txt";

    writeTextFile(path.string(), "first version, quite long\n");
    writeTextFile(path.string(), "second\n");

    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    EXPECT_EQ(text.str(), "second\n");
    fs::remove(path);
}

TEST(WriteTextFileTest, AnUnwritablePathThrows)
{
    EXPECT_THROW(writeTextFile((fs::temp_directory_path() / "no_such_folder_xyz" / "f.csv").string(), "x"),
                 std::runtime_error);
}

// ---------- rankedTable ----------

TEST(RankedTableTest, SaysSoWhenNothingRanked)
{
    EXPECT_NE(rankedTable({}, 10).find("no run reached the minimum"), std::string::npos);
}

TEST(RankedTableTest, ShowsOnlyTheTopRows)
{
    const std::vector<Ranked> ranked{rankedRun({{"period", 10}}, 3), rankedRun({{"period", 20}}, 2),
                                     rankedRun({{"period", 30}}, 1)};

    const std::string table = rankedTable(ranked, 2);

    EXPECT_EQ(lines(table).size(), 3u);                         // header + 2 rows
    EXPECT_NE(table.find("period=10"), std::string::npos);
    EXPECT_NE(table.find("period=20"), std::string::npos);
    EXPECT_EQ(table.find("period=30"), std::string::npos);
}

TEST(RankedTableTest, ShowsPercentagesAndTheTradeCount)
{
    Ranked r = rankedRun({{"period", 10}}, 1.25, 77);
    r.evaluation.metrics.totalReturn = 0.123;
    r.evaluation.metrics.maxDrawdown = 0.045;

    const std::string table = rankedTable({r}, 5);

    EXPECT_NE(table.find("12.3%"), std::string::npos);
    EXPECT_NE(table.find("4.5%"), std::string::npos);
    EXPECT_NE(table.find("77"), std::string::npos);
    EXPECT_NE(table.find("1.25"), std::string::npos);
}

TEST(RankedTableTest, AskingForMoreRowsThanThereAreIsFine)
{
    EXPECT_EQ(lines(rankedTable({rankedRun({{"p", 1}}, 1)}, 50)).size(), 2u);
}

// ---------- perAssetReport ----------

TEST(PerAssetReportTest, CountsTradesAndProfitPerAsset)
{
    const AlignedData d = dataOf({{100, 110, 120, 130}, {50, 50, 50, 50}});
    const std::vector<std::vector<Pivot>> pivots{{}, {}};

    BacktestResult result;
    ClosedTrade c;
    c.trade.asset = 0;
    c.pnl = 50;
    result.closedTrades.push_back(c);
    Trade open;                          // long 2 units from 100, last close 130
    open.asset = 0;
    open.direction = Direction::Long;
    open.quantity = 2;
    open.entryPrice = 100;
    result.openTrades.push_back(open);

    const std::vector<AssetReport> r = perAssetReport(d, result, pivots);

    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0].symbol, "A0");
    EXPECT_EQ(r[0].trades, 2);
    EXPECT_DOUBLE_EQ(r[0].pnl, 50 + 2 * 30);
    EXPECT_EQ(r[1].symbol, "A1");
    EXPECT_EQ(r[1].trades, 0);
    EXPECT_DOUBLE_EQ(r[1].pnl, 0);
}

TEST(PerAssetReportTest, ShortOpenTradesAreValuedAtTheLastClose)
{
    const AlignedData d = dataOf({{100, 90}});
    BacktestResult result;
    Trade open;
    open.asset = 0;
    open.direction = Direction::Short;
    open.quantity = 3;
    open.entryPrice = 100;
    result.openTrades.push_back(open);

    EXPECT_DOUBLE_EQ(perAssetReport(d, result, {{}})[0].pnl, 30);
}

TEST(PerAssetReportTest, CaptureIsWorkedOutPerAsset)
{
    const AlignedData d = dataOf({{100, 110, 120, 130, 120, 110, 100}});
    const std::vector<std::vector<Pivot>> pivots{{{0, 100, false}, {3, 130, true}, {6, 100, false}}};

    BacktestResult result;
    Trade open;                          // long from the start: up a leg, down a leg
    open.asset = 0;
    open.direction = Direction::Long;
    open.quantity = 1;
    open.entryTime = 0;
    open.entryPrice = 100;
    result.openTrades.push_back(open);

    const AssetReport r = perAssetReport(d, result, pivots)[0];

    EXPECT_EQ(r.capture.legs, 2);
    EXPECT_NEAR(r.capture.efficiency, 0.0, 1e-12);
    EXPECT_EQ(r.capture.legsRight, 1);
}

TEST(PerAssetReportTest, NeedsOnePivotListPerAsset)
{
    EXPECT_THROW(perAssetReport(dataOf({{1, 2}, {3, 4}}), BacktestResult{}, {{}}), std::invalid_argument);
}

TEST(AssetTableTest, ListsEachAsset)
{
    AssetReport a;
    a.symbol = "EUR_GBP";
    a.trades = 12;
    a.pnl = 345.5;
    AssetReport b;
    b.symbol = "USD_JPY";

    const std::string table = assetTable({a, b});

    EXPECT_NE(table.find("EUR_GBP"), std::string::npos);
    EXPECT_NE(table.find("USD_JPY"), std::string::npos);
    EXPECT_NE(table.find("345.50"), std::string::npos);
    EXPECT_EQ(lines(table).size(), 3u);
}

TEST(AssetTableTest, NoAssets)
{
    EXPECT_NE(assetTable({}).find("no assets"), std::string::npos);
}
