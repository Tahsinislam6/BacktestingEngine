#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nnfx_strategy.hpp"
#include "report.hpp"

namespace fs = std::filesystem;

namespace
{
const double kInf = std::numeric_limits<double>::infinity();

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

// ---------- cost paid ----------

TEST(PerAssetReportTest, CostPaidIsTheFullSpreadForClosedTradesAndHalfForOpenOnes)
{
    const AlignedData d = dataOf({{100, 110}, {50, 50}});
    BacktestResult result;
    ClosedTrade c;
    c.trade.asset = 0;
    c.trade.quantity = 3;
    result.closedTrades.push_back(c);            // 0.2 * 3 = 0.6
    Trade open;
    open.asset = 0;
    open.quantity = 5;
    result.openTrades.push_back(open);           // 0.2 / 2 * 5 = 0.5

    const std::vector<AssetReport> r = perAssetReport(d, result, {{}, {}}, {0.2, 0.7});

    EXPECT_NEAR(r[0].costPaid, 1.1, 1e-12);
    EXPECT_DOUBLE_EQ(r[1].costPaid, 0.0);
}

TEST(PerAssetReportTest, CostPaidIsZeroWithoutSpreads)
{
    const AlignedData d = dataOf({{100, 110}});
    BacktestResult result;
    ClosedTrade c;
    c.trade.asset = 0;
    c.trade.quantity = 3;
    result.closedTrades.push_back(c);

    EXPECT_DOUBLE_EQ(perAssetReport(d, result, {{}})[0].costPaid, 0.0);
}

TEST(AssetTableTest, ShowsTheCostPaid)
{
    AssetReport a;
    a.symbol = "EURUSD";
    a.costPaid = 12.5;

    const std::string t = assetTable({a});

    EXPECT_NE(t.find("cost"), std::string::npos);
    EXPECT_NE(t.find("12.50"), std::string::npos);
}

// ---------- one run of a whole system ----------
// runSystem runs the strategy once with the given parameters and gives everything a run is judged on:
// the metrics, the per-asset table and (for strategies that place stops) the R-multiples.

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

const StrategyFactory nnfxFixedLine = [](const Params& p, std::size_t assets) -> std::unique_ptr<MultiStrategy>
{
    NnfxOptions o;
    o.atrPeriod = 3;
    o.maxDistanceAtr = 2.0;
    return std::make_unique<NnfxStrategy<FixedLine>>(assets, FixedLine(p.get("level")), o);
};

std::vector<double> trendingSeries(int legs)
{
    std::vector<double> c{90};
    for (int leg = 0; leg < legs; ++leg)
        for (int i = 0; i < 10; ++i)
            c.push_back(c.back() + (leg % 2 == 0 ? 2.0 : -2.0));
    return c;
}

RunOptions oneThread()
{
    RunOptions o;
    o.threads = 1;
    return o;
}
}

TEST(RunSystemTest, GivesTheSameNumbersAsEvaluate)
{
    const AlignedData d = dataOf({trendingSeries(20), trendingSeries(20)});
    const Params p{{"level", 100}};

    const SystemRun run = runSystem(d, p, nnfxFixedLine, oneThread());
    const Evaluation direct = evaluate(d, p, nnfxFixedLine, zigzagAll(d), oneThread());

    EXPECT_EQ(run.evaluation.params, p);
    EXPECT_DOUBLE_EQ(run.evaluation.metrics.totalReturn, direct.metrics.totalReturn);
    EXPECT_DOUBLE_EQ(run.evaluation.metrics.sharpe, direct.metrics.sharpe);
    EXPECT_EQ(run.evaluation.metrics.trades, direct.metrics.trades);
    EXPECT_DOUBLE_EQ(run.evaluation.capture.efficiency, direct.capture.efficiency);
    EXPECT_GT(run.evaluation.metrics.trades, 0);
}

TEST(RunSystemTest, HasOneReportPerAssetInTheDataOrder)
{
    const AlignedData d = dataOf({trendingSeries(20), trendingSeries(16), trendingSeries(12)});
    const SystemRun run = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, oneThread());

    ASSERT_EQ(run.assets.size(), 3u);
    EXPECT_EQ(run.assets[0].symbol, "A0");
    EXPECT_EQ(run.assets[2].symbol, "A2");
    int trades = 0;
    for (const AssetReport& a : run.assets)
        trades += a.trades;
    EXPECT_GE(trades, run.evaluation.metrics.trades);          // closed ones plus any still open
}

TEST(RunSystemTest, TheRMultiplesComeFromTheTradesStops)
{
    const AlignedData d = dataOf({trendingSeries(20)});
    const SystemRun run = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, oneThread());

    ASSERT_TRUE(run.r.has_value());
    EXPECT_GT(run.r->all.trades, 0);
    EXPECT_EQ(run.r->all.trades, run.r->targetHalf.trades + run.r->runner.trades);
}

TEST(RunSystemTest, ADataSetWithNoTradesHasNoRMultiples)
{
    const AlignedData d = dataOf({std::vector<double>(40, 100.0)});            // flat: the price never crosses the line
    const SystemRun run = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, oneThread());

    EXPECT_FALSE(run.r.has_value());
    EXPECT_EQ(run.evaluation.metrics.trades, 0);
}

TEST(RunSystemTest, TheSpreadIsPaidAndShownInTheAssetTable)
{
    const AlignedData d = dataOf({trendingSeries(20)});
    RunOptions costly = oneThread();
    costly.costs.defaultPips = 10.0;

    const SystemRun free = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, oneThread());
    const SystemRun paid = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, costly);

    EXPECT_LT(paid.evaluation.metrics.totalReturn, free.evaluation.metrics.totalReturn);
    EXPECT_DOUBLE_EQ(free.assets[0].costPaid, 0.0);
    EXPECT_GT(paid.assets[0].costPaid, 0.0);
}

TEST(RunSystemTest, AFactoryThatReturnsNothingThrows)
{
    const AlignedData d = dataOf({trendingSeries(4)});
    const StrategyFactory none = [](const Params&, std::size_t) -> std::unique_ptr<MultiStrategy> { return nullptr; };
    EXPECT_THROW(runSystem(d, Params{}, none, oneThread()), std::invalid_argument);
}

TEST(FormatSystemRunTest, ShowsTheLabelTheNumbersTheAssetsAndTheRTable)
{
    const AlignedData d = dataOf({trendingSeries(20), trendingSeries(20)});
    const SystemRun run = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, oneThread());

    const std::string text = formatSystemRun("out-of-sample", run);

    EXPECT_NE(text.find("OUT-OF-SAMPLE"), std::string::npos);
    EXPECT_NE(text.find("level=100"), std::string::npos);
    for (const char* word : {"return", "sharpe", "max drawdown", "trades", "capture", "A0", "A1", "R-MULTIPLES",
                             "expectancy", "target half", "runner", "exits:"})
        EXPECT_NE(text.find(word), std::string::npos) << word;
}

TEST(FormatSystemRunTest, WithoutRMultiplesThereIsNoRTable)
{
    const AlignedData d = dataOf({std::vector<double>(40, 100.0)});
    const std::string text = formatSystemRun("in-sample", runSystem(d, Params{{"level", 100}}, nnfxFixedLine, oneThread()));

    EXPECT_NE(text.find("IN-SAMPLE"), std::string::npos);
    EXPECT_EQ(text.find("R-MULTIPLES"), std::string::npos);
}

// ---------- swap in the reports ----------

namespace
{
// the closes of dataOf() are stamped 0, 1, 2 ...: spread them one day apart and name the pair so swap can apply
AlignedData daily(AlignedData d, const std::string& symbol)
{
    d.symbols = {symbol};
    for (std::size_t i = 0; i < d.snapshots.size(); ++i)
    {
        const std::int64_t t = static_cast<std::int64_t>(i) * kTimestampUnitsPerDay;
        d.snapshots[i].timestamp = t;
        d.snapshots[i].bars[0]->timestamp = t;
    }
    return d;
}

RunOptions withSwap(double baseRate, double quoteRate)
{
    RunOptions o = oneThread();
    o.swap.rates["EUR"] = {{0, baseRate}};
    o.swap.rates["USD"] = {{0, quoteRate}};
    return o;
}
}

TEST(PerAssetSwapTest, SumsTheSwapOfClosedAndOpenTrades)
{
    AlignedData d = dataOf({{100, 101, 102}});
    BacktestResult result;
    ClosedTrade c;
    c.trade.asset = 0;
    c.trade.quantity = 1;
    c.trade.swap = 1.5;
    c.pnl = 11.5;
    result.closedTrades.push_back(c);
    Trade open;
    open.asset = 0;
    open.quantity = 1;
    open.entryPrice = 100;
    open.swap = -0.25;
    result.openTrades.push_back(open);

    const auto r = perAssetReport(d, result, {{}});

    EXPECT_DOUBLE_EQ(r[0].swap, 1.25);
    EXPECT_DOUBLE_EQ(r[0].pnl, 11.5 + (102 - 100) - 0.25);      // closed pnl already holds its swap; an open one adds its own
}

TEST(AssetTableTest, ShowsTheSwap)
{
    AssetReport a;
    a.symbol = "EURUSD";
    a.swap = -7.25;

    const std::string t = assetTable({a});

    EXPECT_NE(t.find("swap"), std::string::npos);
    EXPECT_NE(t.find("-7.25"), std::string::npos);
}

TEST(RunSystemTest, TheSwapIsPaidAndShowsInTheAssetTableAndTheReturn)
{
    const AlignedData d = daily(dataOf({trendingSeries(20)}), "EURUSD");

    const SystemRun none = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, oneThread());
    const SystemRun swapped = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, withSwap(8.0, 0.0));

    EXPECT_DOUBLE_EQ(none.assets[0].swap, 0.0);
    EXPECT_NE(swapped.assets[0].swap, 0.0);
    EXPECT_NE(swapped.evaluation.metrics.totalReturn, none.evaluation.metrics.totalReturn);
    EXPECT_EQ(swapped.evaluation.metrics.trades, none.evaluation.metrics.trades);      // swap changes results, not trading
}

TEST(RunSystemTest, TheMarkupAloneLowersTheReturn)
{
    const AlignedData d = daily(dataOf({trendingSeries(20)}), "EURUSD");
    RunOptions markup = oneThread();
    markup.swap.markupPct = 5.0;

    const SystemRun none = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, oneThread());
    const SystemRun paid = runSystem(d, Params{{"level", 100}}, nnfxFixedLine, markup);

    EXPECT_LT(paid.assets[0].swap, 0.0);
    EXPECT_LT(paid.evaluation.metrics.totalReturn, none.evaluation.metrics.totalReturn);
}
