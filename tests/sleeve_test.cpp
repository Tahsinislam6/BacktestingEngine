#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "aligned.hpp"
#include "single_asset.hpp"
#include "sleeve.hpp"

// These tests define how the sleeve helpers must behave (see include/sleeve.hpp).
//
// Rules:
//   1. assetIndices finds each symbol's position, in the order asked; it throws on an empty list, an unknown symbol
//      or a symbol given twice.
//   2. selectAssets keeps every snapshot and timestamp, keeps only the chosen assets' bars, renumbers the assets in the
//      order given, and throws on an empty list, an index past the end or an index given twice.
//   3. selectSpreads / selectSwap slice per-asset lists the same way, so asset k of the slice has asset indices[k]'s costs.
//   4. runSleeves runs each sleeve alone on its slice and adds the results up: the combined equity curve is the sum of the
//      sleeves' curves, each sleeve only sees its own assets and cash, combined trades are tagged with their sleeve and
//      carry the asset's position in the full data, and every configuration error throws before any strategy is called.
//   5. computeSleeveMetrics gives one Metrics per sleeve (against its own cash) and one for the portfolio (against the sum
//      of the cash), in the sleeves' order.

namespace
{
Bar barAt(std::int64_t time, double price)
{
    Bar b;
    b.timestamp = time;
    b.open = b.high = b.low = b.close = price;
    return b;
}

// Three assets (EURUSD, GBPUSD, XAUUSD) over three snapshots. Price = asset * 100 + day, so a bar tells where it came from.
// GBPUSD has no bar on day 1.
AlignedData sampleData()
{
    AlignedData data;
    data.symbols = {"EURUSD", "GBPUSD", "XAUUSD"};
    for (int day = 0; day < 3; ++day)
    {
        Snapshot s;
        s.timestamp = 1000 * (day + 1);
        for (int asset = 0; asset < 3; ++asset)
        {
            if (asset == 1 && day == 1)
                s.bars.push_back(std::nullopt);
            else
                s.bars.push_back(barAt(s.timestamp, asset * 100 + day));
        }
        data.snapshots.push_back(s);
    }
    return data;
}
}

// ---------- assetIndices ----------

TEST(AssetIndices, FindsEachSymbolInTheOrderAsked)
{
    const AlignedData data = sampleData();
    EXPECT_EQ(assetIndices(data, {"XAUUSD", "EURUSD"}), (std::vector<std::size_t>{2, 0}));
}

TEST(AssetIndices, OneSymbol)
{
    EXPECT_EQ(assetIndices(sampleData(), {"GBPUSD"}), (std::vector<std::size_t>{1}));
}

TEST(AssetIndices, UnknownSymbolThrows)
{
    EXPECT_THROW(assetIndices(sampleData(), {"EURUSD", "USDJPY"}), std::invalid_argument);
}

TEST(AssetIndices, EmptyListThrows)
{
    EXPECT_THROW(assetIndices(sampleData(), {}), std::invalid_argument);
}

TEST(AssetIndices, SymbolGivenTwiceThrows)
{
    EXPECT_THROW(assetIndices(sampleData(), {"EURUSD", "EURUSD"}), std::invalid_argument);
}

// ---------- selectAssets ----------

TEST(SelectAssets, KeepsOnlyTheChosenSymbolsInTheOrderGiven)
{
    const AlignedData out = selectAssets(sampleData(), {2, 0});
    EXPECT_EQ(out.symbols, (std::vector<std::string>{"XAUUSD", "EURUSD"}));
}

TEST(SelectAssets, KeepsEverySnapshotAndTimestamp)
{
    const AlignedData data = sampleData();
    const AlignedData out = selectAssets(data, {1});   // GBPUSD, which has a gap on day 1

    ASSERT_EQ(out.snapshots.size(), data.snapshots.size());
    for (std::size_t i = 0; i < data.snapshots.size(); ++i)
        EXPECT_EQ(out.snapshots[i].timestamp, data.snapshots[i].timestamp);
}

TEST(SelectAssets, EachSnapshotHasOneSlotPerChosenAsset)
{
    const AlignedData out = selectAssets(sampleData(), {0, 2});
    for (const Snapshot& s : out.snapshots)
        EXPECT_EQ(s.bars.size(), 2u);
}

TEST(SelectAssets, BarsAreTheChosenAssetsOwnBarsRenumbered)
{
    const AlignedData out = selectAssets(sampleData(), {2, 0});

    // new asset 0 = XAUUSD (prices 200..202), new asset 1 = EURUSD (prices 0..2)
    for (int day = 0; day < 3; ++day)
    {
        ASSERT_TRUE(out.snapshots[day].bars[0].has_value());
        ASSERT_TRUE(out.snapshots[day].bars[1].has_value());
        EXPECT_DOUBLE_EQ(out.snapshots[day].bars[0]->close, 200 + day);
        EXPECT_DOUBLE_EQ(out.snapshots[day].bars[1]->close, 0 + day);
    }
}

TEST(SelectAssets, AMissingBarStaysMissing)
{
    const AlignedData out = selectAssets(sampleData(), {1});
    EXPECT_TRUE(out.snapshots[0].bars[0].has_value());
    EXPECT_FALSE(out.snapshots[1].bars[0].has_value());
    EXPECT_TRUE(out.snapshots[2].bars[0].has_value());
}

TEST(SelectAssets, DoesNotChangeTheOriginal)
{
    const AlignedData data = sampleData();
    selectAssets(data, {1});
    EXPECT_EQ(data.symbols.size(), 3u);
    EXPECT_EQ(data.snapshots[0].bars.size(), 3u);
}

TEST(SelectAssets, EmptyListThrows)
{
    EXPECT_THROW(selectAssets(sampleData(), {}), std::invalid_argument);
}

TEST(SelectAssets, IndexPastTheEndThrows)
{
    EXPECT_THROW(selectAssets(sampleData(), {0, 3}), std::invalid_argument);
}

TEST(SelectAssets, IndexGivenTwiceThrows)
{
    EXPECT_THROW(selectAssets(sampleData(), {1, 1}), std::invalid_argument);
}

// ---------- selectSpreads ----------

TEST(SelectSpreads, PicksTheChosenAssetsSpreadsInOrder)
{
    const std::vector<double> out = selectSpreads({0.1, 0.2, 0.3}, {2, 0});
    EXPECT_EQ(out, (std::vector<double>{0.3, 0.1}));
}

TEST(SelectSpreads, EmptyListStaysEmpty)
{
    EXPECT_TRUE(selectSpreads({}, {0, 1}).empty());
}

TEST(SelectSpreads, AssetPastTheEndOfAShortListGetsZero)
{
    const std::vector<double> out = selectSpreads({0.1}, {0, 2});
    EXPECT_EQ(out, (std::vector<double>{0.1, 0.0}));
}

// ---------- selectSwap ----------

TEST(SelectSwap, PicksTheChosenAssetsRowsInOrder)
{
    SwapSchedule swap;
    swap.differentialPct = {{1, 1, 1}, {2, 2, 2}, {3, 3, 3}};
    swap.markupPct = 0.5;

    const SwapSchedule out = selectSwap(swap, {2, 0});

    ASSERT_EQ(out.differentialPct.size(), 2u);
    EXPECT_EQ(out.differentialPct[0], (std::vector<double>{3, 3, 3}));
    EXPECT_EQ(out.differentialPct[1], (std::vector<double>{1, 1, 1}));
}

TEST(SelectSwap, KeepsTheMarkup)
{
    SwapSchedule swap;
    swap.differentialPct = {{1}, {2}};
    swap.markupPct = 0.75;

    EXPECT_DOUBLE_EQ(selectSwap(swap, {1}).markupPct, 0.75);
}

TEST(SelectSwap, MarkupOnlyScheduleStaysMarkupOnly)
{
    SwapSchedule swap;
    swap.markupPct = 0.5;   // no differentials

    const SwapSchedule out = selectSwap(swap, {0, 1});

    EXPECT_TRUE(out.differentialPct.empty());
    EXPECT_DOUBLE_EQ(out.markupPct, 0.5);
    EXPECT_TRUE(out.active());
}

TEST(SelectSwap, InactiveScheduleStaysInactive)
{
    const SwapSchedule out = selectSwap(SwapSchedule{}, {0});
    EXPECT_FALSE(out.active());
}

TEST(SelectSwap, AssetWithoutARowGetsAnEmptyRow)
{
    SwapSchedule swap;
    swap.differentialPct = {{1, 1}};   // only asset 0 has rates

    const SwapSchedule out = selectSwap(swap, {0, 1});

    ASSERT_EQ(out.differentialPct.size(), 2u);
    EXPECT_EQ(out.differentialPct[0], (std::vector<double>{1, 1}));
    EXPECT_TRUE(out.differentialPct[1].empty());
}

// ======================= runSleeves =======================

namespace
{
constexpr double kDay = static_cast<double>(kTimestampUnitsPerDay);

// closes[asset][day]; open = high = low = close, one snapshot per day, every asset has a bar every day.
AlignedData makeData(const std::vector<std::string>& symbols, const std::vector<std::vector<double>>& closes)
{
    AlignedData data;
    data.symbols = symbols;
    for (std::size_t day = 0; day < closes[0].size(); ++day)
    {
        Snapshot s;
        s.timestamp = static_cast<std::int64_t>((day + 1) * kDay);
        for (std::size_t a = 0; a < symbols.size(); ++a)
            s.bars.push_back(barAt(s.timestamp, closes[a][day]));
        data.snapshots.push_back(s);
    }
    return data;
}

// EURUSD, GBPUSD, XAUUSD over 6 days.
AlignedData threeAssets()
{
    return makeData({"EURUSD", "GBPUSD", "XAUUSD"},
                    {{100, 101, 103, 102, 104, 105},
                     {200, 198, 199, 195, 193, 194},
                     {50, 50, 51, 52, 51, 50}});
}

// Does what its script says: on the n-th snapshot it returns the orders listed for n. Trade ids start at 1 in every portfolio.
class ScriptedMulti : public MultiStrategy
{
public:
    struct Step { int call; Order order; };

    explicit ScriptedMulti(std::vector<Step> steps) : steps_(std::move(steps)) {}

    std::vector<Order> onSnapshot(const Snapshot&, const Portfolio&) override
    {
        ++call_;
        std::vector<Order> out;
        for (const Step& step : steps_)
            if (step.call == call_)
                out.push_back(step.order);
        return out;
    }

private:
    std::vector<Step> steps_;
    int call_{0};
};

// Records what it is shown and never trades.
struct Spy
{
    std::vector<std::string> symbols;
    std::vector<std::size_t> barCounts;   // bars.size() of every snapshot it saw
    int setSymbolsCalls{0};
};

class SpyMulti : public MultiStrategy
{
public:
    explicit SpyMulti(Spy& spy) : spy_(spy) {}

    void setSymbols(const std::vector<std::string>& symbols) override
    {
        spy_.symbols = symbols;
        ++spy_.setSymbolsCalls;
    }

    std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio&) override
    {
        spy_.barCounts.push_back(snapshot.bars.size());
        return {};
    }

private:
    Spy& spy_;
};

// A one-asset strategy: buys 10 on its 2nd bar, closes on its 5th.
class PlainScripted : public Strategy
{
public:
    std::vector<Order> onBar(const Bar&, const Portfolio& portfolio) override
    {
        ++seen_;
        if (seen_ == 2)
            return {openOrder(Direction::Long, 10.0)};
        if (seen_ == 5 && !portfolio.openTrades().empty())
            return {closeOrder(portfolio.openTrades().front().id)};
        return {};
    }

private:
    int seen_{0};
};

Order buy(double qty, int asset = 0) { return openOrder(Direction::Long, qty, std::nullopt, std::nullopt, asset); }
Order sell(double qty, int asset = 0) { return openOrder(Direction::Short, qty, std::nullopt, std::nullopt, asset); }

Sleeve makeSleeve(std::string name, std::vector<std::string> symbols, std::unique_ptr<MultiStrategy> strategy, double cash)
{
    Sleeve s;
    s.name = std::move(name);
    s.symbols = std::move(symbols);
    s.strategy = std::move(strategy);
    s.cash = cash;
    return s;
}

std::unique_ptr<MultiStrategy> scripted(std::vector<ScriptedMulti::Step> steps)
{
    return std::make_unique<ScriptedMulti>(std::move(steps));
}

SwapSchedule sampleSwap()
{
    SwapSchedule swap;
    swap.differentialPct = {{1, 1, 1, 1, 1, 1}, {-2, -2, -2, -2, -2, -2}, {0.5, 0.5, 0.5, 0.5, 0.5, 0.5}};
    swap.markupPct = 0.5;
    return swap;
}
}

// ---------- one sleeve is a plain run ----------

TEST(RunSleeves, OneSleeveWithAllAssetsEqualsAPlainRun)
{
    const AlignedData data = threeAssets();
    const std::vector<double> spreads = {0.2, 0.4, 0.1};
    const SwapSchedule swap = sampleSwap();
    const std::vector<ScriptedMulti::Step> script = {{1, buy(100, 0)}, {1, sell(50, 1)}, {4, closeOrder(1)}};

    ScriptedMulti plain(script);
    const BacktestResult expected = runMultiBacktest(data, plain, 10000.0, spreads, swap);

    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("all", {"EURUSD", "GBPUSD", "XAUUSD"}, scripted(script), 10000.0));
    const SleeveResult actual = runSleeves(data, sleeves, spreads, swap);

    ASSERT_EQ(actual.combined.equityCurve.size(), expected.equityCurve.size());
    for (std::size_t i = 0; i < expected.equityCurve.size(); ++i)
        EXPECT_DOUBLE_EQ(actual.combined.equityCurve[i], expected.equityCurve[i]) << "snapshot " << i;
    EXPECT_DOUBLE_EQ(actual.combined.finalEquity, expected.finalEquity);
    EXPECT_EQ(actual.combined.closedTrades.size(), expected.closedTrades.size());
    EXPECT_EQ(actual.combined.openTrades.size(), expected.openTrades.size());
}

// ---------- the sum of the sleeves ----------

TEST(RunSleeves, CombinedCurveIsTheSumOfTheSleeveCurves)
{
    const AlignedData data = threeAssets();

    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("eur", {"EURUSD"}, scripted({{1, buy(100)}, {4, closeOrder(1)}}), 6000.0));
    sleeves.push_back(makeSleeve("gbp", {"GBPUSD"}, scripted({{2, sell(30)}}), 4000.0));
    const SleeveResult r = runSleeves(data, sleeves);

    ASSERT_EQ(r.sleeves.size(), 2u);
    const std::vector<double>& a = r.sleeves[0].result.equityCurve;
    const std::vector<double>& b = r.sleeves[1].result.equityCurve;
    ASSERT_EQ(a.size(), data.snapshots.size());
    ASSERT_EQ(b.size(), data.snapshots.size());
    ASSERT_EQ(r.combined.equityCurve.size(), data.snapshots.size());
    for (std::size_t i = 0; i < a.size(); ++i)
        EXPECT_DOUBLE_EQ(r.combined.equityCurve[i], a[i] + b[i]) << "snapshot " << i;
    EXPECT_DOUBLE_EQ(r.combined.finalEquity, r.sleeves[0].result.finalEquity + r.sleeves[1].result.finalEquity);
}

TEST(RunSleeves, EachSleeveEqualsAStandaloneRunOnItsSlice)
{
    const AlignedData data = threeAssets();
    const std::vector<double> spreads = {0.2, 0.4, 0.1};
    const SwapSchedule swap = sampleSwap();
    const std::vector<ScriptedMulti::Step> script = {{1, buy(20)}, {5, closeOrder(1)}};

    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("gbp", {"GBPUSD"}, scripted(script), 5000.0));
    sleeves.push_back(makeSleeve("xau", {"XAUUSD"}, scripted(script), 3000.0));
    const SleeveResult r = runSleeves(data, sleeves, spreads, swap);

    const std::vector<std::vector<std::size_t>> picks = {{1}, {2}};
    const std::vector<double> cash = {5000.0, 3000.0};
    for (std::size_t k = 0; k < 2; ++k)
    {
        ScriptedMulti alone(script);
        const BacktestResult expected = runMultiBacktest(selectAssets(data, picks[k]), alone, cash[k],
                                                         selectSpreads(spreads, picks[k]), selectSwap(swap, picks[k]));
        ASSERT_EQ(r.sleeves[k].result.equityCurve.size(), expected.equityCurve.size());
        for (std::size_t i = 0; i < expected.equityCurve.size(); ++i)
            EXPECT_DOUBLE_EQ(r.sleeves[k].result.equityCurve[i], expected.equityCurve[i]) << "sleeve " << k << " snapshot " << i;
    }
}

// Trades in different assets never touch each other, so two sleeves must add up to ONE strategy trading both assets
// with the sum of the cash. This checks the slicing of data, spreads and swap against the full-data run.
TEST(RunSleeves, TwoSleevesAddUpToOneRunTradingBothAssets)
{
    const AlignedData data = threeAssets();
    const std::vector<double> spreads = {0.2, 0.4, 0.1};
    const SwapSchedule swap = sampleSwap();

    ScriptedMulti both({{1, buy(100, 0)}, {1, sell(50, 1)}, {4, closeOrder(1)}});
    const BacktestResult expected = runMultiBacktest(data, both, 10000.0, spreads, swap);

    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("eur", {"EURUSD"}, scripted({{1, buy(100)}, {4, closeOrder(1)}}), 6000.0));
    sleeves.push_back(makeSleeve("gbp", {"GBPUSD"}, scripted({{1, sell(50)}}), 4000.0));
    const SleeveResult r = runSleeves(data, sleeves, spreads, swap);

    ASSERT_EQ(r.combined.equityCurve.size(), expected.equityCurve.size());
    for (std::size_t i = 0; i < expected.equityCurve.size(); ++i)
        EXPECT_NEAR(r.combined.equityCurve[i], expected.equityCurve[i], 1e-9) << "snapshot " << i;
    EXPECT_NEAR(r.combined.finalEquity, expected.finalEquity, 1e-9);
}

// ---------- isolation ----------

TEST(RunSleeves, ALosingSleeveDoesNotTouchTheOthersCash)
{
    // GBPUSD falls from 200 to 194: buying 10 loses 60. The XAUUSD sleeve never trades.
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("gbp", {"GBPUSD"}, scripted({{1, buy(10)}}), 6000.0));
    sleeves.push_back(makeSleeve("idle", {"XAUUSD"}, scripted({}), 4000.0));
    const SleeveResult r = runSleeves(threeAssets(), sleeves);

    EXPECT_DOUBLE_EQ(r.sleeves[0].result.finalEquity, 5940.0);
    for (double equity : r.sleeves[1].result.equityCurve)
        EXPECT_DOUBLE_EQ(equity, 4000.0);
    EXPECT_DOUBLE_EQ(r.combined.finalEquity, 9940.0);
}

TEST(RunSleeves, EachSleeveStartsWithItsOwnCash)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("a", {"EURUSD"}, scripted({}), 1500.0));
    sleeves.push_back(makeSleeve("b", {"GBPUSD"}, scripted({}), 2500.0));
    const SleeveResult r = runSleeves(threeAssets(), sleeves);

    EXPECT_DOUBLE_EQ(r.sleeves[0].result.equityCurve.front(), 1500.0);
    EXPECT_DOUBLE_EQ(r.sleeves[1].result.equityCurve.front(), 2500.0);
    EXPECT_DOUBLE_EQ(r.combined.equityCurve.front(), 4000.0);
}

TEST(RunSleeves, AStrategySeesOnlyItsOwnAssets)
{
    Spy spyA;
    Spy spyB;
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("a", {"XAUUSD", "EURUSD"}, std::make_unique<SpyMulti>(spyA), 1000.0));
    sleeves.push_back(makeSleeve("b", {"GBPUSD"}, std::make_unique<SpyMulti>(spyB), 1000.0));
    runSleeves(threeAssets(), sleeves);

    EXPECT_EQ(spyA.symbols, (std::vector<std::string>{"XAUUSD", "EURUSD"}));   // in the order the sleeve lists them
    EXPECT_EQ(spyB.symbols, (std::vector<std::string>{"GBPUSD"}));
    ASSERT_EQ(spyA.barCounts.size(), 6u);
    for (std::size_t n : spyA.barCounts)
        EXPECT_EQ(n, 2u);
    for (std::size_t n : spyB.barCounts)
        EXPECT_EQ(n, 1u);
}

TEST(RunSleeves, AssetsInNoSleeveAreSimplyNotTraded)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("only", {"XAUUSD"}, scripted({}), 1000.0));
    const SleeveResult r = runSleeves(threeAssets(), sleeves);

    ASSERT_EQ(r.sleeves.size(), 1u);
    EXPECT_DOUBLE_EQ(r.combined.finalEquity, 1000.0);
}

// ---------- a plain strategy in a one-asset sleeve ----------

TEST(RunSleeves, APlainStrategyInAOneAssetSleeveEqualsRunBacktest)
{
    const AlignedData data = threeAssets();

    BarSeries series;   // the XAUUSD bars alone
    series.symbol = "XAUUSD";
    for (const Snapshot& s : data.snapshots)
        series.bars.push_back(*s.bars[2]);

    PlainScripted forSingle;
    const BacktestResult expected = runBacktest(series, forSingle, 3000.0);

    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("xau", {"XAUUSD"}, std::make_unique<SingleAssetAdapter>(std::make_unique<PlainScripted>()), 3000.0));
    const SleeveResult r = runSleeves(data, sleeves);

    ASSERT_EQ(r.combined.equityCurve.size(), expected.equityCurve.size());
    for (std::size_t i = 0; i < expected.equityCurve.size(); ++i)
        EXPECT_DOUBLE_EQ(r.combined.equityCurve[i], expected.equityCurve[i]) << "bar " << i;
    EXPECT_GE(r.combined.closedTrades.size(), 1u);   // the scenario really trades
}

// ---------- trades ----------

TEST(RunSleeves, CombinedTradesAreTaggedWithTheirSleeveAndTheFullDataAssetNumber)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("gbp", {"GBPUSD"}, scripted({{1, buy(5)}, {3, closeOrder(1)}}), 1000.0));
    sleeves.push_back(makeSleeve("xau", {"XAUUSD"}, scripted({{1, sell(5)}}), 1000.0));
    const SleeveResult r = runSleeves(threeAssets(), sleeves);

    ASSERT_EQ(r.combined.closedTrades.size(), 1u);
    EXPECT_EQ(r.combined.closedTrades[0].trade.asset, 1);   // GBPUSD is asset 1 in the full data (it is asset 0 in its sleeve)
    ASSERT_EQ(r.closedTradeSleeve.size(), 1u);
    EXPECT_EQ(r.closedTradeSleeve[0], "gbp");

    ASSERT_EQ(r.combined.openTrades.size(), 1u);
    EXPECT_EQ(r.combined.openTrades[0].asset, 2);           // XAUUSD
    ASSERT_EQ(r.openTradeSleeve.size(), 1u);
    EXPECT_EQ(r.openTradeSleeve[0], "xau");

    // The sleeve's own result keeps the sleeve's own numbering.
    EXPECT_EQ(r.sleeves[0].result.closedTrades[0].trade.asset, 0);
}

TEST(RunSleeves, SleeveResultsKeepTheOrderGivenWithTheirNameSymbolsAndCash)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("second-asset-first", {"GBPUSD"}, scripted({}), 700.0));
    sleeves.push_back(makeSleeve("first-asset-last", {"EURUSD"}, scripted({}), 300.0));
    const SleeveResult r = runSleeves(threeAssets(), sleeves);

    ASSERT_EQ(r.sleeves.size(), 2u);
    EXPECT_EQ(r.sleeves[0].name, "second-asset-first");
    EXPECT_EQ(r.sleeves[0].symbols, (std::vector<std::string>{"GBPUSD"}));
    EXPECT_DOUBLE_EQ(r.sleeves[0].cash, 700.0);
    EXPECT_EQ(r.sleeves[1].name, "first-asset-last");
    EXPECT_DOUBLE_EQ(r.sleeves[1].cash, 300.0);
}

TEST(RunSleeves, NoSnapshotsGivesEmptyCurvesAndTheSumOfTheCash)
{
    AlignedData data;
    data.symbols = {"EURUSD", "GBPUSD"};

    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("a", {"EURUSD"}, scripted({}), 1000.0));
    sleeves.push_back(makeSleeve("b", {"GBPUSD"}, scripted({}), 500.0));
    const SleeveResult r = runSleeves(data, sleeves);

    EXPECT_TRUE(r.combined.equityCurve.empty());
    EXPECT_DOUBLE_EQ(r.combined.finalEquity, 1500.0);
}

// ---------- errors: all checked before anything runs ----------

TEST(RunSleeves, NoSleevesThrows)
{
    std::vector<Sleeve> sleeves;
    EXPECT_THROW(runSleeves(threeAssets(), sleeves), std::invalid_argument);
}

TEST(RunSleeves, NullStrategyThrows)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("a", {"EURUSD"}, nullptr, 1000.0));
    EXPECT_THROW(runSleeves(threeAssets(), sleeves), std::invalid_argument);
}

TEST(RunSleeves, CashThatIsNotPositiveThrows)
{
    for (double cash : {0.0, -1.0})
    {
        std::vector<Sleeve> sleeves;
        sleeves.push_back(makeSleeve("a", {"EURUSD"}, scripted({}), cash));
        EXPECT_THROW(runSleeves(threeAssets(), sleeves), std::invalid_argument) << "cash " << cash;
    }
}

TEST(RunSleeves, DuplicateSleeveNameThrows)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("same", {"EURUSD"}, scripted({}), 1000.0));
    sleeves.push_back(makeSleeve("same", {"GBPUSD"}, scripted({}), 1000.0));
    EXPECT_THROW(runSleeves(threeAssets(), sleeves), std::invalid_argument);
}

TEST(RunSleeves, SameAssetInTwoSleevesThrows)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("a", {"EURUSD", "GBPUSD"}, scripted({}), 1000.0));
    sleeves.push_back(makeSleeve("b", {"GBPUSD"}, scripted({}), 1000.0));
    EXPECT_THROW(runSleeves(threeAssets(), sleeves), std::invalid_argument);
}

TEST(RunSleeves, UnknownSymbolThrows)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("a", {"USDJPY"}, scripted({}), 1000.0));
    EXPECT_THROW(runSleeves(threeAssets(), sleeves), std::invalid_argument);
}

TEST(RunSleeves, EmptySymbolListThrows)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("a", {}, scripted({}), 1000.0));
    EXPECT_THROW(runSleeves(threeAssets(), sleeves), std::invalid_argument);
}

TEST(RunSleeves, AnErrorInALaterSleeveMeansNoStrategyWasCalled)
{
    Spy spy;
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("good", {"EURUSD"}, std::make_unique<SpyMulti>(spy), 1000.0));
    sleeves.push_back(makeSleeve("bad", {"USDJPY"}, scripted({}), 1000.0));

    EXPECT_THROW(runSleeves(threeAssets(), sleeves), std::invalid_argument);
    EXPECT_EQ(spy.setSymbolsCalls, 0);
    EXPECT_TRUE(spy.barCounts.empty());
}

// ======================= computeSleeveMetrics =======================

namespace
{
// GBPUSD (200,198,199,195,193,194): buying 10 on day 1 gives the equity 6000, 5980, 5990, 5950, 5930, 5940 in a 6000 sleeve.
// The XAUUSD sleeve (4000) never trades. Together: 10000 at the start, 9930 at the lowest, 9940 at the end.
SleeveResult losingAndIdle()
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("gbp", {"GBPUSD"}, scripted({{1, buy(10)}}), 6000.0));
    sleeves.push_back(makeSleeve("idle", {"XAUUSD"}, scripted({}), 4000.0));
    return runSleeves(threeAssets(), sleeves);
}
}

TEST(SleeveMetrics, OneEntryPerSleeveInOrderWithItsName)
{
    const PortfolioMetrics m = computeSleeveMetrics(losingAndIdle());

    ASSERT_EQ(m.sleeves.size(), 2u);
    EXPECT_EQ(m.sleeves[0].name, "gbp");
    EXPECT_EQ(m.sleeves[1].name, "idle");
}

TEST(SleeveMetrics, EachSleeveIsMeasuredAgainstItsOwnCash)
{
    const PortfolioMetrics m = computeSleeveMetrics(losingAndIdle());

    EXPECT_NEAR(m.sleeves[0].metrics.totalReturn, 5940.0 / 6000.0 - 1.0, 1e-12);   // -1%
    EXPECT_NEAR(m.sleeves[0].metrics.maxDrawdown, 70.0 / 6000.0, 1e-12);
    EXPECT_DOUBLE_EQ(m.sleeves[1].metrics.totalReturn, 0.0);
    EXPECT_DOUBLE_EQ(m.sleeves[1].metrics.maxDrawdown, 0.0);
}

TEST(SleeveMetrics, ThePortfolioIsMeasuredAgainstTheSumOfTheCash)
{
    const PortfolioMetrics m = computeSleeveMetrics(losingAndIdle());

    EXPECT_NEAR(m.combined.totalReturn, 9940.0 / 10000.0 - 1.0, 1e-12);   // -0.6%, not -1% and not 9940/6000-1
    EXPECT_NEAR(m.combined.maxDrawdown, 70.0 / 10000.0, 1e-12);           // the idle sleeve dilutes the drawdown
}

TEST(SleeveMetrics, OneSleeveGivesTheSameMetricsAsThePortfolio)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("only", {"EURUSD"}, scripted({{1, buy(100)}, {4, closeOrder(1)}}), 5000.0));
    const PortfolioMetrics m = computeSleeveMetrics(runSleeves(threeAssets(), sleeves));

    const Metrics& a = m.sleeves[0].metrics;
    const Metrics& b = m.combined;
    EXPECT_DOUBLE_EQ(a.totalReturn, b.totalReturn);
    EXPECT_DOUBLE_EQ(a.cagr, b.cagr);
    EXPECT_DOUBLE_EQ(a.maxDrawdown, b.maxDrawdown);
    EXPECT_DOUBLE_EQ(a.sharpe, b.sharpe);
    EXPECT_DOUBLE_EQ(a.sortino, b.sortino);
    EXPECT_EQ(a.trades, b.trades);
    EXPECT_DOUBLE_EQ(a.expectancy, b.expectancy);
    EXPECT_EQ(b.trades, 1);   // the scenario really closes a trade
}

TEST(SleeveMetrics, PortfolioTradeCountIsTheSumOfTheSleevesTrades)
{
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("eur", {"EURUSD"}, scripted({{1, buy(10)}, {3, closeOrder(1)}, {4, buy(10)}, {6, closeOrder(2)}}), 5000.0));
    sleeves.push_back(makeSleeve("gbp", {"GBPUSD"}, scripted({{2, sell(10)}, {5, closeOrder(1)}}), 5000.0));
    const PortfolioMetrics m = computeSleeveMetrics(runSleeves(threeAssets(), sleeves));

    EXPECT_EQ(m.sleeves[0].metrics.trades, 2);
    EXPECT_EQ(m.sleeves[1].metrics.trades, 1);
    EXPECT_EQ(m.combined.trades, 3);
}

TEST(SleeveMetrics, PeriodsPerYearIsPassedOn)
{
    const SleeveResult r = losingAndIdle();

    const PortfolioMetrics m = computeSleeveMetrics(r, 365.0);

    const Metrics expectedSleeve = computeMetrics(r.sleeves[0].result, 6000.0, 365.0);
    const Metrics expectedAll = computeMetrics(r.combined, 10000.0, 365.0);
    EXPECT_DOUBLE_EQ(m.sleeves[0].metrics.cagr, expectedSleeve.cagr);
    EXPECT_DOUBLE_EQ(m.combined.cagr, expectedAll.cagr);
    EXPECT_NE(m.combined.cagr, computeSleeveMetrics(r, 260.0).combined.cagr);   // and it makes a difference
}

TEST(SleeveMetrics, NoSnapshotsGivesZeroMetricsWithoutCrashing)
{
    AlignedData data;
    data.symbols = {"EURUSD"};
    std::vector<Sleeve> sleeves;
    sleeves.push_back(makeSleeve("a", {"EURUSD"}, scripted({}), 1000.0));

    const PortfolioMetrics m = computeSleeveMetrics(runSleeves(data, sleeves));

    EXPECT_EQ(m.combined.trades, 0);
    EXPECT_DOUBLE_EQ(m.combined.totalReturn, 0.0);
}
