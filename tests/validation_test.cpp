#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include "flip_strategy.hpp"
#include "validation.hpp"

// Validation = checking that a winning parameter set was not just lucky on the data it was tuned on.
// Stand-in baselines (no real indicators yet):  FixedLine = a flat line at params["level"],
// SmaStub = a plain moving average of params["period"] closes.
//
// Most tests use a rising line, close = 100 + 2*i for i = 0..50 (51 snapshots, timestamp = i), because the
// right answers can then be worked out by hand: a baseline below the price is long the whole way.

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

class SmaStub
{
public:
    explicit SmaStub(int period) : period_(period) {}
    void update(const Bar& bar)
    {
        window_.push_back(bar.close);
        sum_ += bar.close;
        if (static_cast<int>(window_.size()) > period_)
        {
            sum_ -= window_.front();
            window_.pop_front();
        }
    }
    bool ready() const { return static_cast<int>(window_.size()) >= period_; }
    double value() const { return sum_ / static_cast<double>(window_.size()); }
private:
    int period_;
    std::deque<double> window_;
    double sum_{0};
};

const StrategyFactory fixedLineFactory = [](const Params& p, std::size_t assets) -> std::unique_ptr<MultiStrategy>
{
    return std::make_unique<FlipStrategy<FixedLine>>(assets, FixedLine(p.get("level")));
};

const StrategyFactory smaFactory = [](const Params& p, std::size_t assets) -> std::unique_ptr<MultiStrategy>
{
    return std::make_unique<FlipStrategy<SmaStub>>(assets, SmaStub(p.getInt("period")));
};

AlignedData dataFromSeries(const std::vector<std::vector<double>>& closesPerAsset)
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
            b.open = b.close = series[i];
            b.high = series[i] * 1.002;
            b.low = series[i] * 0.998;
            s.bars.push_back(b);
        }
        d.snapshots.push_back(s);
    }
    return d;
}

AlignedData risingLine()
{
    std::vector<double> closes;
    for (int i = 0; i <= 50; ++i)
        closes.push_back(100.0 + 2 * i);
    return dataFromSeries({closes});
}

std::vector<double> trendingSeries(unsigned seed, std::size_t n)
{
    std::mt19937 gen(seed);
    std::normal_distribution<double> noise(0.0, 0.006);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::vector<double> closes{100.0};
    double drift = 0.002;
    for (std::size_t i = 1; i < n; ++i)
    {
        if (uni(gen) < 1.0 / 40)
            drift = -drift;
        closes.push_back(closes.back() * std::exp(drift + noise(gen)));
    }
    return closes;
}

AlignedData randomData(std::size_t days = 400)
{
    return dataFromSeries({trendingSeries(10, days), trendingSeries(11, days)});
}

const double kCash = 100000.0;

GridOptions oneThread()
{
    GridOptions o;
    o.threads = 1;
    return o;
}

ValidationOptions returnOptions(int minTrades = 0)
{
    ValidationOptions v;
    v.optimise.grid.threads = 1;
    v.optimise.objective = Objective::TotalReturn;
    v.optimise.minTrades = minTrades;
    return v;
}

Evaluation evalWith(Params params, double sharpe, int trades = 50)
{
    Evaluation e;
    e.params = params;
    e.metrics.sharpe = sharpe;
    e.metrics.trades = trades;
    return e;
}
}

// ---------- sliceSnapshots ----------

TEST(SliceSnapshotsTest, KeepsTheRequestedRangeAndTheSymbols)
{
    const AlignedData d = risingLine();

    const AlignedData s = sliceSnapshots(d, 10, 15);

    ASSERT_EQ(s.snapshots.size(), 5u);
    EXPECT_EQ(s.snapshots[0].timestamp, 10);
    EXPECT_EQ(s.snapshots[4].timestamp, 14);
    EXPECT_EQ(s.symbols, d.symbols);
}

TEST(SliceSnapshotsTest, AnEmptyRangeIsAllowed)
{
    EXPECT_TRUE(sliceSnapshots(risingLine(), 5, 5).snapshots.empty());
    EXPECT_TRUE(sliceSnapshots(risingLine(), 51, 51).snapshots.empty());
}

TEST(SliceSnapshotsTest, TheWholeRangeIsTheWholeData)
{
    EXPECT_EQ(sliceSnapshots(risingLine(), 0, 51).snapshots.size(), 51u);
}

TEST(SliceSnapshotsTest, BadRangesThrow)
{
    EXPECT_THROW(sliceSnapshots(risingLine(), 10, 5), std::invalid_argument);
    EXPECT_THROW(sliceSnapshots(risingLine(), 0, 52), std::invalid_argument);
}

// ---------- runWindow ----------

TEST(RunWindowTest, TheEquityCurveCoversOnlyTheWindowAndStartsAtTheStartingCash)
{
    const WindowRun run = runWindow(risingLine(), Params{{"level", 50}}, fixedLineFactory, {20, 30, 0}, oneThread());

    ASSERT_EQ(run.result.equityCurve.size(), 10u);
    EXPECT_DOUBLE_EQ(run.result.equityCurve[0], kCash);      // the trade opens at the window's first close
    EXPECT_DOUBLE_EQ(run.result.finalEquity, run.result.equityCurve.back());
}

TEST(RunWindowTest, TheWindowResultIsTheProfitInsideTheWindowOnly)
{
    // long from the close at bar 20 (140) to the close at bar 29 (158): 71.43 units * 18
    const WindowRun run = runWindow(risingLine(), Params{{"level", 50}}, fixedLineFactory, {20, 30, 0}, oneThread());

    EXPECT_NEAR(run.result.finalEquity, kCash + (10000.0 / 140.0) * 18.0, 1e-9);
}

TEST(RunWindowTest, WarmUpLetsTheIndicatorBeReadyWhenTheWindowStarts)
{
    // SmaStub(5) is ready after 5 bars. It starts long at the close of bar 14 (128) when warm-up starts at 10.
    const WindowRun warm = runWindow(risingLine(), Params{{"period", 5}}, smaFactory, {20, 51, 10}, oneThread());
    const WindowRun cold = runWindow(risingLine(), Params{{"period", 5}}, smaFactory, {20, 51, 0}, oneThread());

    // warm: already long, so bar 20's move (138 -> 140) is the first thing it earns
    EXPECT_NEAR(warm.result.equityCurve[0], kCash + 2.0 * 10000.0 / 128.0, 1e-9);

    // cold: not ready for the first four bars of the window, so equity stays at the starting cash
    for (int i = 0; i <= 4; ++i)
        EXPECT_DOUBLE_EQ(cold.result.equityCurve[i], kCash);
    EXPECT_GT(cold.result.equityCurve[5], kCash);
}

TEST(RunWindowTest, TheWarmUpCannotGoBeforeTheStartOfTheData)
{
    const WindowRun run = runWindow(risingLine(), Params{{"level", 50}}, fixedLineFactory, {3, 10, 100}, oneThread());

    EXPECT_EQ(run.result.equityCurve.size(), 7u);
}

TEST(RunWindowTest, OnlyTradesThatClosedInsideTheWindowAreCounted)
{
    // line 100; the close alternates 101, 99, ...: a trade closes on every bar from bar 1 on
    const AlignedData d = dataFromSeries({{101, 99, 101, 99, 101, 99, 101, 99}});

    const WindowRun all = runWindow(d, Params{{"level", 100}}, fixedLineFactory, {0, 8, 0}, oneThread());
    const WindowRun late = runWindow(d, Params{{"level", 100}}, fixedLineFactory, {4, 8, 4}, oneThread());

    EXPECT_EQ(all.result.closedTrades.size(), 7u);
    ASSERT_EQ(late.result.closedTrades.size(), 4u);          // exits at bars 4, 5, 6, 7
    EXPECT_EQ(late.result.closedTrades[0].exitTime, 4);
}

TEST(RunWindowTest, ATradeOpenedInWarmUpAndClosedInTheWindowKeepsItsWholeProfit)
{
    const AlignedData d = dataFromSeries({{101, 99, 101, 99, 101, 99, 101, 99}});

    const WindowRun late = runWindow(d, Params{{"level", 100}}, fixedLineFactory, {4, 8, 4}, oneThread());

    EXPECT_EQ(late.result.closedTrades[0].trade.entryTime, 3);     // opened in warm-up
}

TEST(RunWindowTest, TradesStillOpenAtTheEndAreKept)
{
    const WindowRun run = runWindow(risingLine(), Params{{"level", 50}}, fixedLineFactory, {20, 30, 5}, oneThread());

    EXPECT_EQ(run.result.openTrades.size(), 1u);
    EXPECT_TRUE(run.result.closedTrades.empty());
}

TEST(RunWindowTest, AnEmptyWindowIsAllowedAndEmpty)
{
    const WindowRun run = runWindow(risingLine(), Params{{"level", 50}}, fixedLineFactory, {20, 20, 5}, oneThread());

    EXPECT_TRUE(run.result.equityCurve.empty());
    EXPECT_DOUBLE_EQ(run.result.finalEquity, kCash);
    EXPECT_EQ(run.capture.legs, 0);
}

TEST(RunWindowTest, BadWindowsThrow)
{
    EXPECT_THROW(runWindow(risingLine(), Params{{"level", 50}}, fixedLineFactory, {30, 20, 0}), std::invalid_argument);
    EXPECT_THROW(runWindow(risingLine(), Params{{"level", 50}}, fixedLineFactory, {0, 52, 0}), std::invalid_argument);
}

TEST(RunWindowTest, AFactoryThatReturnsNothingThrows)
{
    StrategyFactory none = [](const Params&, std::size_t) -> std::unique_ptr<MultiStrategy> { return nullptr; };

    EXPECT_THROW(runWindow(risingLine(), Params{}, none, {0, 10, 0}), std::invalid_argument);
}

TEST(RunWindowTest, TheWholeDataAsAWindowGivesTheSameNumbersAsEvaluate)
{
    const AlignedData d = randomData();
    const Params p{{"period", 12}};
    const GridOptions opt = oneThread();

    const Evaluation whole = evaluate(d, p, smaFactory, zigzagAll(d, opt.zigzagMultiple, opt.atrPeriod), opt);
    const Evaluation window = evaluateWindow(d, p, smaFactory, {0, d.snapshots.size(), 0}, opt);

    EXPECT_NEAR(window.metrics.totalReturn, whole.metrics.totalReturn, 1e-9);
    EXPECT_NEAR(window.metrics.sharpe, whole.metrics.sharpe, 1e-9);
    EXPECT_NEAR(window.metrics.maxDrawdown, whole.metrics.maxDrawdown, 1e-9);
    EXPECT_EQ(window.metrics.trades, whole.metrics.trades);
    EXPECT_EQ(window.capture.legs, whole.capture.legs);
    EXPECT_NEAR(window.capture.efficiency, whole.capture.efficiency, 1e-9);
}

TEST(RunWindowTest, CaptureCountsOnlyLegsFromTheWindowStart)
{
    const AlignedData d = randomData();
    const GridOptions opt = oneThread();
    const std::size_t start = 150, end = 400, warmup = 50;

    const WindowRun run = runWindow(d, Params{{"period", 10}}, smaFactory, {start, end, warmup}, opt);

    // The same legs worked out by hand: ZigZag over warm-up + window, keeping pivots from the window start on.
    const AlignedData slice = sliceSnapshots(d, start - warmup, end);
    double available = 0;
    int legs = 0;
    for (std::size_t a = 0; a < slice.symbols.size(); ++a)
    {
        const std::vector<Bar> bars = barsOf(slice, static_cast<int>(a));
        std::vector<Pivot> kept;
        for (const Pivot& p : zigzag(bars, opt.zigzagMultiple, opt.atrPeriod))
            if (bars[p.index].timestamp >= static_cast<std::int64_t>(start))
                kept.push_back(p);
        for (std::size_t k = 0; k + 1 < kept.size(); ++k, ++legs)
            available += std::abs(std::log(kept[k + 1].price / kept[k].price));
    }

    EXPECT_GT(legs, 0);
    EXPECT_EQ(run.capture.legs, legs);
    EXPECT_NEAR(run.capture.available, available, 1e-12);
}

// ---------- runGridWindow ----------

TEST(RunGridWindowTest, EachResultMatchesAStandaloneWindowRun)
{
    const AlignedData d = randomData();
    const std::vector<Params> grid = expandGrid({{"period", 5, 30, 5}});
    GridOptions opt;
    opt.threads = 3;

    const std::vector<Evaluation> results = runGridWindow(d, grid, smaFactory, {100, 300, 30}, opt);

    ASSERT_EQ(results.size(), grid.size());
    for (std::size_t i = 0; i < grid.size(); ++i)
    {
        const Evaluation alone = evaluateWindow(d, grid[i], smaFactory, {100, 300, 30}, opt);
        EXPECT_EQ(results[i].params, grid[i]);
        EXPECT_EQ(results[i].metrics.totalReturn, alone.metrics.totalReturn);
        EXPECT_EQ(results[i].metrics.trades, alone.metrics.trades);
        EXPECT_EQ(results[i].capture.efficiency, alone.capture.efficiency);
    }
}

TEST(RunGridWindowTest, TheThreadCountDoesNotChangeTheNumbers)
{
    const AlignedData d = randomData();
    const std::vector<Params> grid = expandGrid({{"period", 3, 45, 3}});
    GridOptions one;  one.threads = 1;
    GridOptions four; four.threads = 4;

    const std::vector<Evaluation> a = runGridWindow(d, grid, smaFactory, {50, 350, 20}, one);
    const std::vector<Evaluation> b = runGridWindow(d, grid, smaFactory, {50, 350, 20}, four);

    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(a[i].metrics.sharpe, b[i].metrics.sharpe);
        EXPECT_EQ(a[i].metrics.totalReturn, b[i].metrics.totalReturn);
        EXPECT_EQ(a[i].capture.captured, b[i].capture.captured);
    }
}

TEST(RunGridWindowTest, AnEmptyGridGivesNoResultsAndABadWindowStillThrows)
{
    EXPECT_TRUE(runGridWindow(risingLine(), {}, fixedLineFactory, {0, 10, 0}).empty());
    EXPECT_THROW(runGridWindow(risingLine(), {}, fixedLineFactory, {10, 5, 0}), std::invalid_argument);
}

// ---------- trainTestSplit ----------

TEST(TrainTestSplitTest, TunesOnTheFirstPartAndScoresTheWinnerOnTheRest)
{
    // 51 snapshots, 60% => train on bars 0..29, test on bars 30..50.
    // Levels 50, 70 and 90 are long the whole way and tie; the first (50) wins.
    const TrainTestResult r = trainTestSplit(risingLine(), {{"level", 50, 150, 20}}, fixedLineFactory, 0.6,
                                             returnOptions());

    EXPECT_EQ(r.splitIndex, 30u);
    ASSERT_EQ(r.ranking.size(), 6u);
    EXPECT_DOUBLE_EQ(r.ranking[0].evaluation.params.get("level"), 50);
    EXPECT_NEAR(r.ranking[0].score, 0.058, 1e-9);                  // 100 units: 100 -> 158

    ASSERT_TRUE(r.test.has_value());
    EXPECT_DOUBLE_EQ(r.test->params.get("level"), 50);
    EXPECT_NEAR(r.testScore, 0.025, 1e-9);                          // 62.5 units: 160 -> 200
    EXPECT_NEAR(r.test->metrics.totalReturn, 0.025, 1e-9);
}

TEST(TrainTestSplitTest, TheMinimumTradeCountAppliesToTheTrainingRuns)
{
    // the always-long runs have no closed trade, so minTrades = 1 leaves only levels 110 to 150
    const TrainTestResult r = trainTestSplit(risingLine(), {{"level", 50, 150, 20}}, fixedLineFactory, 0.6,
                                             returnOptions(1));

    ASSERT_EQ(r.ranking.size(), 3u);
    EXPECT_DOUBLE_EQ(r.ranking[0].evaluation.params.get("level"), 110);
    ASSERT_TRUE(r.test.has_value());
    EXPECT_DOUBLE_EQ(r.test->params.get("level"), 110);
}

TEST(TrainTestSplitTest, WhenNothingRanksThereIsNoTest)
{
    const TrainTestResult r = trainTestSplit(risingLine(), {{"level", 50, 150, 20}}, fixedLineFactory, 0.6,
                                             returnOptions(100));

    EXPECT_TRUE(r.ranking.empty());
    EXPECT_FALSE(r.test.has_value());
    EXPECT_TRUE(std::isnan(r.testScore));
    EXPECT_EQ(r.splitIndex, 30u);
}

TEST(TrainTestSplitTest, TheTestWindowGetsTheWarmUpTheCallerAsksFor)
{
    // SmaStub(5), train on bars 0..24, test on 25..50.
    //   cold:  ready only at bar 29 (close 158): 10000/158 units * (200 - 158)
    //   warm:  5 bars of history, so long since bar 24 (close 148): 10000/148 units * (200 - 148)
    ValidationOptions cold = returnOptions();
    ValidationOptions warm = returnOptions();
    warm.warmupBars = [](const Params& p) { return static_cast<std::size_t>(p.getInt("period")); };

    const TrainTestResult a = trainTestSplit(risingLine(), {{"period", 5, 5, 1}}, smaFactory, 0.5, cold);
    const TrainTestResult b = trainTestSplit(risingLine(), {{"period", 5, 5, 1}}, smaFactory, 0.5, warm);

    EXPECT_EQ(a.splitIndex, 25u);
    EXPECT_NEAR(a.testScore, (10000.0 / 158.0) * 42.0 / kCash, 1e-9);
    EXPECT_NEAR(b.testScore, (10000.0 / 148.0) * 52.0 / kCash, 1e-9);
}

TEST(TrainTestSplitTest, TheScoreUsesTheChosenObjective)
{
    ValidationOptions opt = returnOptions();
    opt.optimise.objective = Objective::Sharpe;

    const TrainTestResult r = trainTestSplit(randomData(), {{"period", 5, 25, 5}}, smaFactory, 0.7, opt);

    ASSERT_TRUE(r.test.has_value());
    EXPECT_DOUBLE_EQ(r.testScore, r.test->metrics.sharpe);
    EXPECT_DOUBLE_EQ(r.ranking[0].score, r.ranking[0].evaluation.metrics.sharpe);
}

TEST(TrainTestSplitTest, TheFractionMustBeBetweenZeroAndOne)
{
    for (double f : {0.0, 1.0, -0.5, 1.5, std::nan("")})
        EXPECT_THROW(trainTestSplit(risingLine(), {{"level", 50, 50, 1}}, fixedLineFactory, f, returnOptions()),
                     std::invalid_argument);
}

// ---------- walkForwardWindows ----------

TEST(WalkForwardWindowsTest, RollsForwardByTheTestLength)
{
    const std::vector<FoldWindows> w = walkForwardWindows(100, 40, 20);

    ASSERT_EQ(w.size(), 3u);
    EXPECT_EQ(w[0].trainStart, 0u);   EXPECT_EQ(w[0].trainEnd, 40u);
    EXPECT_EQ(w[0].testStart, 40u);   EXPECT_EQ(w[0].testEnd, 60u);
    EXPECT_EQ(w[1].trainStart, 20u);  EXPECT_EQ(w[1].trainEnd, 60u);
    EXPECT_EQ(w[1].testStart, 60u);   EXPECT_EQ(w[1].testEnd, 80u);
    EXPECT_EQ(w[2].trainStart, 40u);  EXPECT_EQ(w[2].trainEnd, 80u);
    EXPECT_EQ(w[2].testStart, 80u);   EXPECT_EQ(w[2].testEnd, 100u);
}

TEST(WalkForwardWindowsTest, TestWindowsFollowOneAnotherWithoutOverlap)
{
    const std::vector<FoldWindows> w = walkForwardWindows(1000, 100, 30);

    ASSERT_GT(w.size(), 2u);
    for (std::size_t k = 1; k < w.size(); ++k)
        EXPECT_EQ(w[k].testStart, w[k - 1].testEnd);
}

TEST(WalkForwardWindowsTest, ACustomStep)
{
    const std::vector<FoldWindows> w = walkForwardWindows(100, 40, 20, 10);

    ASSERT_EQ(w.size(), 5u);                // starts at 0, 10, 20, 30, 40
    EXPECT_EQ(w[4].testEnd, 100u);
    EXPECT_EQ(w[1].trainStart, 10u);
}

TEST(WalkForwardWindowsTest, OnlyWholeWindowsAreReturned)
{
    EXPECT_EQ(walkForwardWindows(60, 40, 20).size(), 1u);     // exactly fits
    EXPECT_TRUE(walkForwardWindows(59, 40, 20).empty());      // one short
    EXPECT_TRUE(walkForwardWindows(0, 40, 20).empty());
}

TEST(WalkForwardWindowsTest, ZeroLengthsAreRejected)
{
    EXPECT_THROW(walkForwardWindows(100, 0, 20), std::invalid_argument);
    EXPECT_THROW(walkForwardWindows(100, 40, 0), std::invalid_argument);
}

// ---------- walkForward ----------

TEST(WalkForwardTest, TunesOnEachTrainWindowAndChainsTheTestWindows)
{
    // train 20, test 10 on 51 bars: tests are bars 20-29, 30-39, 40-49.
    // Every fold picks level 50 (long all the way): it opens at the window's first close and earns 18 points.
    const WalkForwardResult r = walkForward(risingLine(), {{"level", 50, 150, 20}}, fixedLineFactory, 20, 10,
                                            returnOptions());

    ASSERT_EQ(r.folds.size(), 3u);
    for (const Fold& f : r.folds)
    {
        ASSERT_TRUE(f.best.has_value());
        ASSERT_TRUE(f.test.has_value());
        EXPECT_DOUBLE_EQ(f.best->evaluation.params.get("level"), 50);
    }
    EXPECT_EQ(r.folds[1].windows.testStart, 30u);

    ASSERT_EQ(r.stitched.equityCurve.size(), 30u);
    const double gain = (10000.0 / 140.0) * 18 + (10000.0 / 160.0) * 18 + (10000.0 / 180.0) * 18;
    EXPECT_NEAR(r.stitched.finalEquity, kCash + gain, 1e-9);
    EXPECT_NEAR(r.metrics.totalReturn, gain / kCash, 1e-9);
}

TEST(WalkForwardTest, EachWindowPicksUpWhereThePreviousOneEnded)
{
    const WalkForwardResult r = walkForward(risingLine(), {{"level", 50, 150, 20}}, fixedLineFactory, 20, 10,
                                            returnOptions());

    // a new window opens its trade at its first close, so it adds nothing on its first point
    EXPECT_DOUBLE_EQ(r.stitched.equityCurve[10], r.stitched.equityCurve[9]);
    EXPECT_DOUBLE_EQ(r.stitched.equityCurve[20], r.stitched.equityCurve[19]);
    EXPECT_GT(r.stitched.equityCurve[11], r.stitched.equityCurve[10]);
}

TEST(WalkForwardTest, AFoldWithNoWinnerStaysInCash)
{
    // no run can have 100 closed trades
    const WalkForwardResult r = walkForward(risingLine(), {{"level", 50, 150, 20}}, fixedLineFactory, 20, 10,
                                            returnOptions(100));

    ASSERT_EQ(r.folds.size(), 3u);
    for (const Fold& f : r.folds)
    {
        EXPECT_FALSE(f.best.has_value());
        EXPECT_FALSE(f.test.has_value());
    }
    ASSERT_EQ(r.stitched.equityCurve.size(), 30u);
    for (double e : r.stitched.equityCurve)
        EXPECT_DOUBLE_EQ(e, kCash);
    EXPECT_DOUBLE_EQ(r.metrics.totalReturn, 0.0);
    EXPECT_EQ(r.metrics.trades, 0);
}

TEST(WalkForwardTest, TooLittleDataGivesNoFolds)
{
    const WalkForwardResult r = walkForward(risingLine(), {{"level", 50, 150, 20}}, fixedLineFactory, 40, 20,
                                            returnOptions());

    EXPECT_TRUE(r.folds.empty());
    EXPECT_TRUE(r.stitched.equityCurve.empty());
    EXPECT_DOUBLE_EQ(r.stitched.finalEquity, kCash);
}

TEST(WalkForwardTest, TheStitchedRunIsTheSumOfTheFolds)
{
    const AlignedData d = randomData(600);

    const WalkForwardResult r = walkForward(d, {{"period", 3, 30, 3}}, smaFactory, 150, 50, returnOptions());

    ASSERT_GT(r.folds.size(), 4u);
    int trades = 0;
    double totalReturns = 0;
    std::size_t bars = 0;
    for (const Fold& f : r.folds)
    {
        ASSERT_TRUE(f.test.has_value());
        trades += f.test->metrics.trades;
        totalReturns += f.test->metrics.totalReturn;
        bars += f.windows.testEnd - f.windows.testStart;
    }

    EXPECT_EQ(r.stitched.equityCurve.size(), bars);
    EXPECT_EQ(r.metrics.trades, trades);
    EXPECT_NEAR(r.stitched.finalEquity, kCash * (1 + totalReturns), 1e-6);
}

TEST(WalkForwardTest, TheThreadCountDoesNotChangeTheResult)
{
    const AlignedData d = randomData(600);
    ValidationOptions one = returnOptions();
    ValidationOptions four = returnOptions();
    four.optimise.grid.threads = 4;

    const WalkForwardResult a = walkForward(d, {{"period", 3, 30, 3}}, smaFactory, 150, 50, one);
    const WalkForwardResult b = walkForward(d, {{"period", 3, 30, 3}}, smaFactory, 150, 50, four);

    ASSERT_EQ(a.stitched.equityCurve.size(), b.stitched.equityCurve.size());
    for (std::size_t i = 0; i < a.stitched.equityCurve.size(); ++i)
        EXPECT_EQ(a.stitched.equityCurve[i], b.stitched.equityCurve[i]);
    for (std::size_t k = 0; k < a.folds.size(); ++k)
        EXPECT_EQ(a.folds[k].best->evaluation.params, b.folds[k].best->evaluation.params);
}

TEST(WalkForwardTest, WarmUpIsAppliedToEachTestWindow)
{
    ValidationOptions cold = returnOptions();
    ValidationOptions warm = returnOptions();
    warm.warmupBars = [](const Params& p) { return static_cast<std::size_t>(p.getInt("period")); };

    const WalkForwardResult a = walkForward(risingLine(), {{"period", 5, 5, 1}}, smaFactory, 20, 10, cold);
    const WalkForwardResult b = walkForward(risingLine(), {{"period", 5, 5, 1}}, smaFactory, 20, 10, warm);

    // warm windows are long from their first bar; cold ones lose their first few bars
    EXPECT_GT(b.metrics.totalReturn, a.metrics.totalReturn);
}

TEST(WalkForwardTest, CaptureIsSummedOverTheTestWindows)
{
    const AlignedData d = randomData(600);

    const WalkForwardResult r = walkForward(d, {{"period", 3, 30, 3}}, smaFactory, 150, 50, returnOptions());

    double available = 0;
    int legs = 0;
    for (const Fold& f : r.folds)
    {
        available += f.test->capture.available;
        legs += f.test->capture.legs;
    }
    EXPECT_GT(legs, 0);
    EXPECT_EQ(r.capture.legs, legs);
    EXPECT_NEAR(r.capture.available, available, 1e-12);
    EXPECT_NEAR(r.capture.efficiency, r.capture.captured / r.capture.available, 1e-12);
}

// ---------- neighbourStability ----------

TEST(NeighbourStabilityTest, AveragesTheTwoNeighboursOfAOneParameterGrid)
{
    const std::vector<ParamRange> ranges{{"p", 1, 5, 1}};
    const std::vector<Evaluation> results{
        evalWith({{"p", 1}}, 1), evalWith({{"p", 2}}, 2), evalWith({{"p", 3}}, 5),
        evalWith({{"p", 4}}, 3), evalWith({{"p", 5}}, 2)};

    const StabilityResult s = neighbourStability(results, ranges, Params{{"p", 3}}, scoreBy(Objective::Sharpe));

    EXPECT_DOUBLE_EQ(s.score, 5);
    EXPECT_EQ(s.neighbours, 2u);
    EXPECT_DOUBLE_EQ(s.meanNeighbour, 2.5);
    EXPECT_DOUBLE_EQ(s.worstNeighbour, 2);
}

TEST(NeighbourStabilityTest, AtTheEdgeOfTheGridThereIsOnlyOneNeighbour)
{
    const std::vector<ParamRange> ranges{{"p", 1, 5, 1}};
    const std::vector<Evaluation> results{
        evalWith({{"p", 1}}, 4), evalWith({{"p", 2}}, 2), evalWith({{"p", 3}}, 9)};

    const StabilityResult s = neighbourStability(results, ranges, Params{{"p", 1}}, scoreBy(Objective::Sharpe));

    EXPECT_EQ(s.neighbours, 1u);
    EXPECT_DOUBLE_EQ(s.meanNeighbour, 2);
}

TEST(NeighbourStabilityTest, NeighboursAreOneStepInOneParameterNotDiagonal)
{
    const std::vector<ParamRange> ranges{{"a", 1, 3, 1}, {"b", 1, 3, 1}};
    std::vector<Evaluation> results;
    for (int a = 1; a <= 3; ++a)
        for (int b = 1; b <= 3; ++b)
            results.push_back(evalWith({{"a", double(a)}, {"b", double(b)}}, a * 10 + b));

    const StabilityResult s =
        neighbourStability(results, ranges, Params{{"a", 2}, {"b", 2}}, scoreBy(Objective::Sharpe));

    EXPECT_DOUBLE_EQ(s.score, 22);
    EXPECT_EQ(s.neighbours, 4u);                         // (1,2) (3,2) (2,1) (2,3)
    EXPECT_DOUBLE_EQ(s.meanNeighbour, (12 + 32 + 21 + 23) / 4.0);
    EXPECT_DOUBLE_EQ(s.worstNeighbour, 12);              // the diagonal (1,1) = 11 is not a neighbour
}

TEST(NeighbourStabilityTest, ALoneSpikeHasPoorNeighbours)
{
    const std::vector<ParamRange> ranges{{"p", 1, 3, 1}};
    const std::vector<Evaluation> spike{evalWith({{"p", 1}}, 0.1), evalWith({{"p", 2}}, 3.0), evalWith({{"p", 3}}, 0.2)};
    const std::vector<Evaluation> plateau{evalWith({{"p", 1}}, 2.8), evalWith({{"p", 2}}, 3.0), evalWith({{"p", 3}}, 2.9)};

    const StabilityResult a = neighbourStability(spike, ranges, Params{{"p", 2}}, scoreBy(Objective::Sharpe));
    const StabilityResult b = neighbourStability(plateau, ranges, Params{{"p", 2}}, scoreBy(Objective::Sharpe));

    EXPECT_LT(a.meanNeighbour / a.score, 0.1);
    EXPECT_GT(b.meanNeighbour / b.score, 0.9);
}

TEST(NeighbourStabilityTest, NeighboursWithTooFewTradesAreSkipped)
{
    const std::vector<ParamRange> ranges{{"p", 1, 3, 1}};
    const std::vector<Evaluation> results{
        evalWith({{"p", 1}}, 100, 5), evalWith({{"p", 2}}, 3), evalWith({{"p", 3}}, 2, 50)};

    const StabilityResult s = neighbourStability(results, ranges, Params{{"p", 2}}, scoreBy(Objective::Sharpe), 10);

    EXPECT_EQ(s.neighbours, 1u);
    EXPECT_DOUBLE_EQ(s.meanNeighbour, 2);
}

TEST(NeighbourStabilityTest, NeighboursWithANaNScoreAreSkipped)
{
    const std::vector<ParamRange> ranges{{"p", 1, 3, 1}};
    const std::vector<Evaluation> results{
        evalWith({{"p", 1}}, std::nan("")), evalWith({{"p", 2}}, 3), evalWith({{"p", 3}}, 2)};

    const StabilityResult s = neighbourStability(results, ranges, Params{{"p", 2}}, scoreBy(Objective::Sharpe));

    EXPECT_EQ(s.neighbours, 1u);
    EXPECT_DOUBLE_EQ(s.worstNeighbour, 2);
}

TEST(NeighbourStabilityTest, NoNeighboursGivesNaN)
{
    const std::vector<ParamRange> ranges{{"p", 1, 1, 1}};

    const StabilityResult s = neighbourStability({evalWith({{"p", 1}}, 4)}, ranges, Params{{"p", 1}},
                                                 scoreBy(Objective::Sharpe));

    EXPECT_EQ(s.neighbours, 0u);
    EXPECT_DOUBLE_EQ(s.score, 4);
    EXPECT_TRUE(std::isnan(s.meanNeighbour));
    EXPECT_TRUE(std::isnan(s.worstNeighbour));
}

TEST(NeighbourStabilityTest, FractionalStepsStillFindTheirNeighbours)
{
    const std::vector<ParamRange> ranges{{"x", 0, 0.3, 0.1}};
    std::vector<Evaluation> results;
    double score = 1;
    for (const Params& p : expandGrid(ranges))
        results.push_back(evalWith(p, score++));        // 1, 2, 3, 4 for x = 0, 0.1, 0.2, 0.3

    const StabilityResult s = neighbourStability(results, ranges, expandGrid(ranges)[2], scoreBy(Objective::Sharpe));

    EXPECT_EQ(s.neighbours, 2u);
    EXPECT_DOUBLE_EQ(s.meanNeighbour, 3.0);             // (2 + 4) / 2
}

TEST(NeighbourStabilityTest, ParametersOutsideTheRangesMustMatchToo)
{
    const std::vector<ParamRange> ranges{{"p", 1, 3, 1}};
    const std::vector<Evaluation> results{
        evalWith({{"p", 2}, {"fixed", 7}}, 5),
        evalWith({{"p", 3}, {"fixed", 7}}, 1),
        evalWith({{"p", 3}, {"fixed", 8}}, 100)};       // a different run altogether

    const StabilityResult s =
        neighbourStability(results, ranges, Params{{"p", 2}, {"fixed", 7}}, scoreBy(Objective::Sharpe));

    EXPECT_EQ(s.neighbours, 1u);
    EXPECT_DOUBLE_EQ(s.meanNeighbour, 1);
}

TEST(NeighbourStabilityTest, ARunMissingAParameterTheChosenSetHasIsNotANeighbour)
{
    const std::vector<ParamRange> ranges{{"p", 1, 3, 1}};
    const std::vector<Evaluation> results{
        evalWith({{"p", 2}, {"fixed", 7}}, 5),
        evalWith({{"p", 3}}, 100)};                      // same p, but no "fixed" at all

    const StabilityResult s =
        neighbourStability(results, ranges, Params{{"p", 2}, {"fixed", 7}}, scoreBy(Objective::Sharpe));

    EXPECT_EQ(s.neighbours, 0u);
}

TEST(NeighbourStabilityTest, TheOrderOfTheResultsDoesNotMatter)
{
    const std::vector<ParamRange> ranges{{"p", 1, 5, 1}};
    std::vector<Evaluation> results{
        evalWith({{"p", 4}}, 3), evalWith({{"p", 1}}, 1), evalWith({{"p", 3}}, 5),
        evalWith({{"p", 5}}, 2), evalWith({{"p", 2}}, 2)};

    const StabilityResult s = neighbourStability(results, ranges, Params{{"p", 3}}, scoreBy(Objective::Sharpe));

    EXPECT_DOUBLE_EQ(s.meanNeighbour, 2.5);
}

TEST(NeighbourStabilityTest, AChosenSetThatIsNotInTheResultsThrows)
{
    EXPECT_THROW(neighbourStability({evalWith({{"p", 1}}, 1)}, {{"p", 1, 3, 1}}, Params{{"p", 9}},
                                    scoreBy(Objective::Sharpe)),
                 std::invalid_argument);
}

TEST(NeighbourStabilityTest, WorksOnRealGridResults)
{
    const AlignedData d = randomData();
    const std::vector<ParamRange> ranges{{"period", 3, 45, 3}};
    const std::vector<Evaluation> results = runGrid(d, expandGrid(ranges), smaFactory, oneThread());
    const std::vector<Ranked> ranked = rankResults(results, scoreBy(Objective::Sharpe), 0);

    const StabilityResult s = neighbourStability(results, ranges, ranked[0].evaluation.params,
                                                 scoreBy(Objective::Sharpe));

    EXPECT_DOUBLE_EQ(s.score, ranked[0].score);
    EXPECT_GE(s.neighbours, 1u);
    EXPECT_LE(s.neighbours, 2u);
    EXPECT_LE(s.worstNeighbour, s.meanNeighbour);
    EXPECT_LE(s.meanNeighbour, s.score);                 // the winner is at least as good as its neighbours
}
