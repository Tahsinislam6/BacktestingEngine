#include <gtest/gtest.h>

#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <vector>

#include "flip_strategy.hpp"
#include "optimiser.hpp"

// The optimiser runs a strategy for every parameter set and ranks the results.
// These tests use two stand-in baselines instead of real indicators:
//   FixedLine  a flat line at params["level"]
//   SmaStub    a plain moving average of params["period"] closes

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
    const std::size_t assets = closesPerAsset.size();
    const std::size_t days = closesPerAsset[0].size();
    for (std::size_t a = 0; a < assets; ++a)
        d.symbols.push_back("A" + std::to_string(a));
    for (std::size_t i = 0; i < days; ++i)
    {
        Snapshot s;
        s.timestamp = static_cast<std::int64_t>(i);
        for (std::size_t a = 0; a < assets; ++a)
        {
            Bar b;
            b.timestamp = s.timestamp;
            const double c = closesPerAsset[a][i];
            b.open = b.close = c;
            b.high = c * 1.002;
            b.low = c * 0.998;
            s.bars.push_back(b);
        }
        d.snapshots.push_back(s);
    }
    return d;
}

// Trending data: the drift flips sign about every 40 bars.
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

AlignedData randomData(std::size_t assets = 2, std::size_t days = 300)
{
    std::vector<std::vector<double>> series;
    for (std::size_t a = 0; a < assets; ++a)
        series.push_back(trendingSeries(static_cast<unsigned>(10 + a), days));
    return dataFromSeries(series);
}

// 100, 102, ..., 200 on one asset.
AlignedData risingLine()
{
    std::vector<double> closes;
    for (int i = 0; i <= 50; ++i)
        closes.push_back(100.0 + 2 * i);
    return dataFromSeries({closes});
}

void expectSame(const Evaluation& a, const Evaluation& b)
{
    EXPECT_EQ(a.params, b.params);
    EXPECT_EQ(a.metrics.totalReturn, b.metrics.totalReturn);
    EXPECT_EQ(a.metrics.cagr, b.metrics.cagr);
    EXPECT_EQ(a.metrics.maxDrawdown, b.metrics.maxDrawdown);
    EXPECT_EQ(a.metrics.sharpe, b.metrics.sharpe);
    EXPECT_EQ(a.metrics.sortino, b.metrics.sortino);
    EXPECT_EQ(a.metrics.calmar, b.metrics.calmar);
    EXPECT_EQ(a.metrics.trades, b.metrics.trades);
    EXPECT_EQ(a.metrics.winRate, b.metrics.winRate);
    EXPECT_EQ(a.metrics.profitFactor, b.metrics.profitFactor);
    EXPECT_EQ(a.metrics.expectancy, b.metrics.expectancy);
    EXPECT_EQ(a.capture.captured, b.capture.captured);
    EXPECT_EQ(a.capture.available, b.capture.available);
    EXPECT_EQ(a.capture.efficiency, b.capture.efficiency);
    EXPECT_EQ(a.capture.legs, b.capture.legs);
}

Evaluation evalWith(double sharpe, int trades, double totalReturn = 0.0, double efficiency = 0.0,
                    Params params = {})
{
    Evaluation e;
    e.params = params;
    e.metrics.sharpe = sharpe;
    e.metrics.trades = trades;
    e.metrics.totalReturn = totalReturn;
    e.capture.efficiency = efficiency;
    return e;
}

GridOptions oneThread()
{
    GridOptions o;
    o.threads = 1;
    return o;
}
}

// ---------- evaluate ----------

TEST(EvaluateTest, ScoresOneRunWithTheMetricsAndCapture)
{
    const AlignedData d = randomData();
    const Params p{{"period", 10}};
    const GridOptions opt = oneThread();
    const auto pivots = zigzagAll(d, opt.zigzagMultiple, opt.atrPeriod);

    const Evaluation e = evaluate(d, p, smaFactory, pivots, opt);

    // the same thing done by hand
    FlipStrategy<SmaStub> s(2, SmaStub(10));
    const BacktestResult run = runMultiBacktest(d, s, opt.startingCash);
    const Metrics m = computeMetrics(run, opt.startingCash, opt.periodsPerYear);
    const CaptureResult c = captureEfficiency(d, pivots, run);

    EXPECT_EQ(e.params, p);
    EXPECT_EQ(e.metrics.sharpe, m.sharpe);
    EXPECT_EQ(e.metrics.totalReturn, m.totalReturn);
    EXPECT_EQ(e.metrics.trades, m.trades);
    EXPECT_EQ(e.capture.efficiency, c.efficiency);
    EXPECT_EQ(e.capture.legs, c.legs);
    EXPECT_GT(e.metrics.trades, 0);
    EXPECT_GT(e.capture.legs, 0);
}

TEST(EvaluateTest, TheFactoryGetsTheParamsAndTheNumberOfAssets)
{
    const AlignedData d = randomData(3, 50);
    Params seenParams;
    std::size_t seenAssets = 0;
    StrategyFactory spy = [&](const Params& p, std::size_t assets) -> std::unique_ptr<MultiStrategy>
    {
        seenParams = p;
        seenAssets = assets;
        return fixedLineFactory(Params{{"level", 100}}, assets);
    };

    evaluate(d, Params{{"x", 5}}, spy, zigzagAll(d), oneThread());

    EXPECT_EQ(seenParams, (Params{{"x", 5}}));
    EXPECT_EQ(seenAssets, 3u);
}

TEST(EvaluateTest, BuildsAFreshStrategyEveryTime)
{
    const AlignedData d = randomData();
    const auto pivots = zigzagAll(d);

    const Evaluation first = evaluate(d, Params{{"period", 10}}, smaFactory, pivots, oneThread());
    const Evaluation second = evaluate(d, Params{{"period", 10}}, smaFactory, pivots, oneThread());

    expectSame(first, second);     // no state carried over from the first run
}

TEST(EvaluateTest, UsesTheStartingCashItIsGiven)
{
    const AlignedData d = risingLine();
    GridOptions opt = oneThread();
    opt.startingCash = 10000.0;

    const Evaluation e = evaluate(d, Params{{"level", 50}}, fixedLineFactory, zigzagAll(d), opt);

    // always long 100 units from 100 to 200 = +10000 on 10000 of cash
    EXPECT_NEAR(e.metrics.totalReturn, 1.0, 1e-9);
}

TEST(EvaluateTest, AFactoryThatReturnsNothingThrows)
{
    const AlignedData d = risingLine();
    StrategyFactory none = [](const Params&, std::size_t) -> std::unique_ptr<MultiStrategy> { return nullptr; };

    EXPECT_THROW(evaluate(d, Params{}, none, zigzagAll(d), oneThread()), std::invalid_argument);
}

// ---------- runGrid ----------

TEST(RunGridTest, ResultsComeBackInGridOrder)
{
    const AlignedData d = risingLine();
    const std::vector<Params> grid = expandGrid({{"level", 50, 190, 20}});

    GridOptions opt;
    opt.threads = 4;
    const std::vector<Evaluation> results = runGrid(d, grid, fixedLineFactory, opt);

    ASSERT_EQ(results.size(), grid.size());
    for (std::size_t i = 0; i < grid.size(); ++i)
        EXPECT_EQ(results[i].params, grid[i]);
}

TEST(RunGridTest, ManyThreadsGiveExactlyTheSameNumbersAsOne)
{
    const AlignedData d = randomData();
    const std::vector<Params> grid = expandGrid({{"period", 3, 60, 3}});

    GridOptions one;   one.threads = 1;
    GridOptions four;  four.threads = 4;
    GridOptions autoT; autoT.threads = 0;

    const std::vector<Evaluation> a = runGrid(d, grid, smaFactory, one);
    const std::vector<Evaluation> b = runGrid(d, grid, smaFactory, four);
    const std::vector<Evaluation> c = runGrid(d, grid, smaFactory, autoT);

    ASSERT_EQ(a.size(), grid.size());
    ASSERT_EQ(b.size(), grid.size());
    ASSERT_EQ(c.size(), grid.size());
    for (std::size_t i = 0; i < grid.size(); ++i)
    {
        expectSame(a[i], b[i]);
        expectSame(a[i], c[i]);
    }
}

TEST(RunGridTest, EachResultMatchesAStandaloneEvaluate)
{
    const AlignedData d = randomData();
    const std::vector<Params> grid = expandGrid({{"period", 5, 25, 5}});
    GridOptions opt;
    opt.threads = 3;

    const std::vector<Evaluation> results = runGrid(d, grid, smaFactory, opt);
    const auto pivots = zigzagAll(d, opt.zigzagMultiple, opt.atrPeriod);

    for (std::size_t i = 0; i < grid.size(); ++i)
        expectSame(results[i], evaluate(d, grid[i], smaFactory, pivots, opt));
}

TEST(RunGridTest, AnEmptyGridGivesNoResults)
{
    EXPECT_TRUE(runGrid(risingLine(), {}, fixedLineFactory).empty());
}

TEST(RunGridTest, MoreThreadsThanRunsIsFine)
{
    const AlignedData d = risingLine();
    GridOptions opt;
    opt.threads = 16;

    const std::vector<Evaluation> r = runGrid(d, {Params{{"level", 50}}, Params{{"level", 150}}},
                                              fixedLineFactory, opt);

    EXPECT_EQ(r.size(), 2u);
}

TEST(RunGridTest, AnExceptionInARunIsRethrownNotLost)
{
    const AlignedData d = risingLine();
    StrategyFactory failing = [](const Params& p, std::size_t assets) -> std::unique_ptr<MultiStrategy>
    {
        if (p.get("level") == 110)
            throw std::runtime_error("boom");
        return fixedLineFactory(p, assets);
    };
    const std::vector<Params> grid = expandGrid({{"level", 50, 190, 20}});

    GridOptions many;  many.threads = 4;

    EXPECT_THROW(runGrid(d, grid, failing, oneThread()), std::runtime_error);
    EXPECT_THROW(runGrid(d, grid, failing, many), std::runtime_error);
}

TEST(RunGridTest, ANegativeThreadCountIsRejected)
{
    GridOptions opt;
    opt.threads = -1;

    EXPECT_THROW(runGrid(risingLine(), {Params{{"level", 50}}}, fixedLineFactory, opt), std::invalid_argument);
}

TEST(RunGridTest, TheZigZagSettingsReachTheCaptureNumbers)
{
    const AlignedData d = randomData();
    const std::vector<Params> grid{Params{{"period", 10}}};
    GridOptions loose = oneThread();   loose.zigzagMultiple = 1.0;
    GridOptions tight = oneThread();   tight.zigzagMultiple = 4.0;

    const int looseLegs = runGrid(d, grid, smaFactory, loose)[0].capture.legs;
    const int tightLegs = runGrid(d, grid, smaFactory, tight)[0].capture.legs;

    EXPECT_GT(looseLegs, tightLegs);
}

TEST(RunGridTest, DefaultOptions)
{
    const GridOptions o;

    EXPECT_DOUBLE_EQ(o.startingCash, 100000.0);
    EXPECT_DOUBLE_EQ(o.periodsPerYear, 252.0);
    EXPECT_DOUBLE_EQ(o.zigzagMultiple, 3.0);
    EXPECT_EQ(o.atrPeriod, 14);
    EXPECT_EQ(o.threads, 0);
}

// ---------- scoreBy ----------

TEST(ScoreByTest, EachObjectiveReadsItsOwnNumber)
{
    Evaluation e;
    e.metrics.sharpe = 1;
    e.metrics.sortino = 2;
    e.metrics.calmar = 3;
    e.metrics.totalReturn = 4;
    e.capture.efficiency = 5;

    EXPECT_DOUBLE_EQ(scoreBy(Objective::Sharpe)(e), 1);
    EXPECT_DOUBLE_EQ(scoreBy(Objective::Sortino)(e), 2);
    EXPECT_DOUBLE_EQ(scoreBy(Objective::Calmar)(e), 3);
    EXPECT_DOUBLE_EQ(scoreBy(Objective::TotalReturn)(e), 4);
    EXPECT_DOUBLE_EQ(scoreBy(Objective::CaptureEfficiency)(e), 5);
}

// ---------- rankResults ----------

TEST(RankResultsTest, BestScoreFirst)
{
    const std::vector<Evaluation> results{evalWith(0.5, 50), evalWith(2.0, 50), evalWith(1.0, 50)};

    const std::vector<Ranked> r = rankResults(results, scoreBy(Objective::Sharpe), 0);

    ASSERT_EQ(r.size(), 3u);
    EXPECT_DOUBLE_EQ(r[0].score, 2.0);
    EXPECT_DOUBLE_EQ(r[1].score, 1.0);
    EXPECT_DOUBLE_EQ(r[2].score, 0.5);
    EXPECT_DOUBLE_EQ(r[0].evaluation.metrics.sharpe, 2.0);
}

TEST(RankResultsTest, RunsWithTooFewTradesAreDropped)
{
    const std::vector<Evaluation> results{evalWith(9.0, 3), evalWith(1.0, 30), evalWith(0.5, 29)};

    const std::vector<Ranked> r = rankResults(results, scoreBy(Objective::Sharpe), 30);

    ASSERT_EQ(r.size(), 1u);              // exactly minTrades is allowed; one fewer is not
    EXPECT_DOUBLE_EQ(r[0].score, 1.0);
}

TEST(RankResultsTest, EqualScoresKeepTheirGridOrder)
{
    const std::vector<Evaluation> results{
        evalWith(1.0, 50, 0, 0, Params{{"id", 1}}),
        evalWith(1.0, 50, 0, 0, Params{{"id", 2}}),
        evalWith(1.0, 50, 0, 0, Params{{"id", 3}})};

    const std::vector<Ranked> r = rankResults(results, scoreBy(Objective::Sharpe), 0);

    ASSERT_EQ(r.size(), 3u);
    EXPECT_DOUBLE_EQ(r[0].evaluation.params.get("id"), 1);
    EXPECT_DOUBLE_EQ(r[1].evaluation.params.get("id"), 2);
    EXPECT_DOUBLE_EQ(r[2].evaluation.params.get("id"), 3);
}

TEST(RankResultsTest, EqualScoresKeepTheirGridOrderEvenInALongList)
{
    std::vector<Evaluation> results;
    for (int i = 0; i < 200; ++i)
        results.push_back(evalWith(1.0, 50, 0, 0, Params{{"id", static_cast<double>(i)}}));

    const std::vector<Ranked> r = rankResults(results, scoreBy(Objective::Sharpe), 0);

    ASSERT_EQ(r.size(), 200u);
    for (int i = 0; i < 200; ++i)
        EXPECT_DOUBLE_EQ(r[i].evaluation.params.get("id"), i);
}

TEST(RankResultsTest, ScoresThatAreNaNAreDropped)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<Evaluation> results{evalWith(nan, 50), evalWith(1.0, 50)};

    const std::vector<Ranked> r = rankResults(results, scoreBy(Objective::Sharpe), 0);

    ASSERT_EQ(r.size(), 1u);
    EXPECT_DOUBLE_EQ(r[0].score, 1.0);
}

TEST(RankResultsTest, NegativeScoresRankBelowPositiveOnes)
{
    const std::vector<Evaluation> results{evalWith(-1.0, 50), evalWith(0.0, 50), evalWith(-0.2, 50)};

    const std::vector<Ranked> r = rankResults(results, scoreBy(Objective::Sharpe), 0);

    EXPECT_DOUBLE_EQ(r[0].score, 0.0);
    EXPECT_DOUBLE_EQ(r[1].score, -0.2);
    EXPECT_DOUBLE_EQ(r[2].score, -1.0);
}

TEST(RankResultsTest, AnInfiniteScoreRanksFirst)
{
    const double inf = std::numeric_limits<double>::infinity();
    const std::vector<Evaluation> results{evalWith(5.0, 50), evalWith(inf, 50)};

    const std::vector<Ranked> r = rankResults(results, scoreBy(Objective::Sharpe), 0);

    EXPECT_EQ(r[0].score, inf);
}

TEST(RankResultsTest, RanksByTheChosenObjective)
{
    const std::vector<Evaluation> results{
        evalWith(3.0, 50, 0.10, 0.2),     // best Sharpe
        evalWith(1.0, 50, 0.50, 0.9)};    // best return and capture

    EXPECT_DOUBLE_EQ(rankResults(results, scoreBy(Objective::Sharpe), 0)[0].score, 3.0);
    EXPECT_DOUBLE_EQ(rankResults(results, scoreBy(Objective::TotalReturn), 0)[0].score, 0.50);
    EXPECT_DOUBLE_EQ(rankResults(results, scoreBy(Objective::CaptureEfficiency), 0)[0].score, 0.9);
}

TEST(RankResultsTest, ACustomScoreCanMixSeveralNumbers)
{
    const std::vector<Evaluation> results{
        evalWith(1.0, 50, 0, 0.1),
        evalWith(0.5, 50, 0, 0.9)};
    ScoreFn mix = [](const Evaluation& e) { return e.metrics.sharpe + 2 * e.capture.efficiency; };

    const std::vector<Ranked> r = rankResults(results, mix, 0);

    EXPECT_DOUBLE_EQ(r[0].score, 2.3);     // 0.5 + 1.8
    EXPECT_DOUBLE_EQ(r[1].score, 1.2);
}

TEST(RankResultsTest, NothingInNothingOut)
{
    EXPECT_TRUE(rankResults({}, scoreBy(Objective::Sharpe), 0).empty());
}

// ---------- optimise (everything together) ----------

TEST(OptimiseTest, RanksTheGridBestFirst)
{
    // Rising line 100..200. A line far below the start is long the whole way: the best you can do.
    // Higher lines start short, lose on the way up, and flip late.
    const AlignedData d = risingLine();
    OptimiseOptions opt;
    opt.grid.threads = 2;
    opt.objective = Objective::TotalReturn;
    opt.minTrades = 0;

    const std::vector<Ranked> r = optimise(d, {{"level", 50, 150, 20}}, fixedLineFactory, opt);

    // levels 50, 70, 90 (long all the way) tie and keep grid order; then 110, 130, 150 get worse
    ASSERT_EQ(r.size(), 6u);
    EXPECT_DOUBLE_EQ(r[0].evaluation.params.get("level"), 50);
    EXPECT_DOUBLE_EQ(r[1].evaluation.params.get("level"), 70);
    EXPECT_DOUBLE_EQ(r[2].evaluation.params.get("level"), 90);
    EXPECT_DOUBLE_EQ(r[3].evaluation.params.get("level"), 110);
    EXPECT_DOUBLE_EQ(r[4].evaluation.params.get("level"), 130);
    EXPECT_DOUBLE_EQ(r[5].evaluation.params.get("level"), 150);
    EXPECT_NEAR(r[0].score, 0.10, 1e-9);        // 100 units gaining 100 on 100000
}

TEST(OptimiseTest, TheMinimumTradeCountIsApplied)
{
    const AlignedData d = risingLine();
    OptimiseOptions opt;
    opt.grid.threads = 1;
    opt.objective = Objective::TotalReturn;
    opt.minTrades = 1;           // the always-long runs have no CLOSED trade

    const std::vector<Ranked> r = optimise(d, {{"level", 50, 150, 20}}, fixedLineFactory, opt);

    ASSERT_EQ(r.size(), 3u);     // levels 110, 130, 150 each closed one trade
    EXPECT_DOUBLE_EQ(r[0].evaluation.params.get("level"), 110);
}

TEST(OptimiseTest, ScoresAreSortedAndEveryRunIsAccountedFor)
{
    const AlignedData d = randomData();
    OptimiseOptions opt;
    opt.objective = Objective::Sharpe;
    opt.minTrades = 0;

    const std::vector<Ranked> r = optimise(d, {{"period", 3, 60, 3}}, smaFactory, opt);

    ASSERT_EQ(r.size(), 20u);
    for (std::size_t i = 1; i < r.size(); ++i)
        EXPECT_GE(r[i - 1].score, r[i].score);
}

TEST(OptimiseTest, TheDefaultMinimumIsThirtyTrades)
{
    const OptimiseOptions o;

    EXPECT_EQ(o.minTrades, 30);
    EXPECT_EQ(o.objective, Objective::Sharpe);
}

TEST(OptimiseTest, BadRangesAreReportedBeforeAnythingRuns)
{
    EXPECT_THROW(optimise(risingLine(), {{"level", 5, 1, 1}}, fixedLineFactory), std::invalid_argument);
}

// ---------- parallelFor ----------

#include <atomic>
#include <thread>

TEST(ParallelForTest, CallsEveryIndexExactlyOnce)
{
    std::vector<std::atomic<int>> counts(1000);

    parallelFor(1000, 4, [&](std::size_t i) { ++counts[i]; });

    for (const auto& c : counts)
        EXPECT_EQ(c.load(), 1);
}

TEST(ParallelForTest, ZeroCountCallsNothing)
{
    int calls = 0;

    parallelFor(0, 4, [&](std::size_t) { ++calls; });

    EXPECT_EQ(calls, 0);
}

TEST(ParallelForTest, OneThreadRunsInTheCallingThread)
{
    const std::thread::id caller = std::this_thread::get_id();
    bool allHere = true;

    parallelFor(10, 1, [&](std::size_t) { allHere = allHere && std::this_thread::get_id() == caller; });

    EXPECT_TRUE(allHere);
}

TEST(ParallelForTest, MoreThreadsThanWorkIsFine)
{
    std::vector<std::atomic<int>> counts(3);

    parallelFor(3, 32, [&](std::size_t i) { ++counts[i]; });

    for (const auto& c : counts)
        EXPECT_EQ(c.load(), 1);
}

TEST(ParallelForTest, AnExceptionIsRethrown)
{
    EXPECT_THROW(parallelFor(50, 1, [](std::size_t i) { if (i == 7) throw std::runtime_error("boom"); }),
                 std::runtime_error);
    EXPECT_THROW(parallelFor(50, 4, [](std::size_t i) { if (i == 7) throw std::runtime_error("boom"); }),
                 std::runtime_error);
}

TEST(ParallelForTest, AfterAFailureTheRemainingWorkIsSkipped)
{
    int calls = 0;

    EXPECT_THROW(parallelFor(100, 1, [&](std::size_t i) { ++calls; if (i == 3) throw std::runtime_error("x"); }),
                 std::runtime_error);

    EXPECT_EQ(calls, 4);        // indexes 0, 1, 2 and the failing 3
}

TEST(ParallelForTest, ANegativeThreadCountIsRejected)
{
    EXPECT_THROW(parallelFor(5, -1, [](std::size_t) {}), std::invalid_argument);
}
