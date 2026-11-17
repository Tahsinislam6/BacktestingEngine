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
#include "system_run.hpp"

// evaluate runs one strategy once and scores it; parallelFor spreads work over threads; RunOptions carries the cash, costs and threads.
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

RunOptions oneThread()
{
    RunOptions o;
    o.threads = 1;
    return o;
}
}

// ---------- evaluate ----------

TEST(EvaluateTest, ScoresOneRunWithTheMetricsAndCapture)
{
    const AlignedData d = randomData();
    const Params p{{"period", 10}};
    const RunOptions opt = oneThread();
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
    RunOptions opt = oneThread();
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

TEST(EvaluateTest, TheZigZagSettingsReachTheCaptureNumbers)
{
    const AlignedData d = randomData();
    const Params p{{"period", 10}};
    RunOptions loose = oneThread();   loose.zigzagMultiple = 1.0;
    RunOptions tight = oneThread();   tight.zigzagMultiple = 4.0;

    const int looseLegs = evaluate(d, p, smaFactory, zigzagAll(d, loose.zigzagMultiple, loose.atrPeriod), loose).capture.legs;
    const int tightLegs = evaluate(d, p, smaFactory, zigzagAll(d, tight.zigzagMultiple, tight.atrPeriod), tight).capture.legs;

    EXPECT_GT(looseLegs, tightLegs);
}

TEST(RunOptionsTest, DefaultOptions)
{
    const RunOptions o;

    EXPECT_DOUBLE_EQ(o.startingCash, 100000.0);
    EXPECT_DOUBLE_EQ(o.periodsPerYear, 260.0);
    EXPECT_DOUBLE_EQ(o.zigzagMultiple, 3.0);
    EXPECT_EQ(o.atrPeriod, 14);
    EXPECT_EQ(o.threads, 0);
}

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

// ---------- trading costs ----------
//
// RunOptions::costs (a CostModel, default = no cost) is turned into one spread per asset from the data's
// symbols and given to every backtest. A spread of 0.1 on A0 and A1 needs defaultPips = 1000 (pip 0.0001).

namespace
{
RunOptions costly(double multiplier = 1.0)
{
    RunOptions o;
    o.threads = 1;
    o.costs.defaultPips = 1000.0;
    o.costs.multiplier = multiplier;
    return o;
}

Evaluation runLevel50(const RunOptions& options)
{
    const AlignedData d = dataFromSeries({{100, 102, 104, 106, 108, 110}, {50, 51, 52, 53, 54, 55}});
    Params p;
    p.set("level", 20);
    return evaluate(d, p, fixedLineFactory, zigzagAll(d, options.zigzagMultiple, options.atrPeriod), options);
}
}

TEST(CostsInRunTest, NoCostsIsTheDefaultAndChangesNothing)
{
    RunOptions plain;
    plain.threads = 1;
    RunOptions zeroModel = plain;
    zeroModel.costs = CostModel{};

    EXPECT_DOUBLE_EQ(runLevel50(plain).metrics.totalReturn, runLevel50(zeroModel).metrics.totalReturn);
    EXPECT_GT(runLevel50(plain).metrics.totalReturn, 0.0);
}

TEST(CostsInRunTest, SpreadsLowerTheReturn)
{
    RunOptions plain;
    plain.threads = 1;

    EXPECT_LT(runLevel50(costly()).metrics.totalReturn, runLevel50(plain).metrics.totalReturn);
}

TEST(CostsInRunTest, TheLossIsHalfTheSpreadTimesQuantityOnEntryForEachOpenLong)
{
    // both assets: long from the first bar, still open. cost = (0.1/2) * (10000/close) per asset
    RunOptions plain;
    plain.threads = 1;
    const double lost = (runLevel50(plain).metrics.totalReturn - runLevel50(costly()).metrics.totalReturn) * 100000.0;

    EXPECT_NEAR(lost, 0.05 * 10000.0 / 100.0 + 0.05 * 10000.0 / 50.0, 1e-6);
}

TEST(CostsInRunTest, TheMultiplierScalesTheCost)
{
    RunOptions plain;
    plain.threads = 1;
    const double base = runLevel50(plain).metrics.totalReturn;
    const double one = base - runLevel50(costly(1.0)).metrics.totalReturn;
    const double two = base - runLevel50(costly(2.0)).metrics.totalReturn;

    EXPECT_NEAR(two, 2 * one, 1e-12);
}

// ---------- swap in a run ----------

TEST(SwapInRunTest, EvaluateCarriesTheSwapOfTheOptions)
{
    // a long held to the end of a rising line on EURUSD with days one apart: a big rate difference changes the return
    AlignedData d = risingLine();
    d.symbols = {"EURUSD"};
    for (std::size_t i = 0; i < d.snapshots.size(); ++i)
    {
        const std::int64_t t = static_cast<std::int64_t>(i) * kTimestampUnitsPerDay;
        d.snapshots[i].timestamp = t;
        d.snapshots[i].bars[0]->timestamp = t;
    }
    RunOptions plain = oneThread();
    RunOptions swapped = oneThread();
    swapped.swap.rates["EUR"] = {{0, 6.0}};
    swapped.swap.rates["USD"] = {{0, 0.0}};
    const Params p{{"period", 5}};

    const Evaluation a = evaluate(d, p, smaFactory, zigzagAll(d), plain);
    const Evaluation b = evaluate(d, p, smaFactory, zigzagAll(d), swapped);

    EXPECT_GT(b.metrics.totalReturn, a.metrics.totalReturn);          // the long earns 6 points a year
}
