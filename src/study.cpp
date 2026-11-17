#include "study.hpp"

#include <cmath>
#include <cstdio>

namespace
{
    template <typename... Args>
    std::string fmt(const char* format, Args... args)
    {
        char buffer[1024];
        std::snprintf(buffer, sizeof buffer, format, args...);
        return buffer;
    }

    std::string pct(double f)  { return std::isnan(f) ? "nan" : fmt("%.1f%%", 100.0 * f); }
    std::string num(double v)  { return std::isnan(v) ? "nan" : fmt("%.2f", v); }

    // Run one parameter set over a whole dataset and report it, asset by asset.
    void runWhole(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                  const GridOptions& grid, Evaluation& evaluation, std::vector<AssetReport>& assets,
                  std::optional<RReport>& rReport)
    {
        const WindowRun run = runWindow(data, params, factory, {0, data.snapshots.size(), 0}, grid);

        evaluation.params = params;
        evaluation.metrics = computeMetrics(run.result, grid.startingCash, grid.periodsPerYear);
        evaluation.capture = run.capture;

        assets = perAssetReport(data, run.result, zigzagAll(data, grid.zigzagMultiple, grid.atrPeriod),
                                spreadsFor(grid.costs, data.symbols));

        RReport r = computeRReport(run.result.closedTrades, static_cast<int>(run.result.openTrades.size()));
        rReport = r.all.trades > 0 ? std::optional<RReport>(r) : std::nullopt;
    }

    std::string rRow(const char* label, const RStats& s)
    {
        return fmt("  %-12s %6d %7s %9s %9s %11s %9s\n", label, s.trades, pct(s.winRate).c_str(),
                   num(s.avgWinR).c_str(), num(s.avgLossR).c_str(), num(s.expectancyR).c_str(),
                   num(s.totalR).c_str());
    }

    std::string exitText(const char* label, const ExitStats& e)
    {
        return fmt("%s %d (%s R avg)", label, e.count, e.count > 0 ? num(e.avgR()).c_str() : "-");
    }

    // 1R = the amount a trade risked to its stop.
    std::string rTable(const char* title, const RReport& r)
    {
        std::string out = fmt("  R-MULTIPLES, %s (1R = the amount risked to the stop):\n", title);
        out += fmt("  %-12s %6s %7s %9s %9s %11s %9s\n", "", "trades", "win", "avg win", "avg loss", "expectancy",
                   "total R");
        out += rRow("all", r.all) + rRow("target half", r.targetHalf) + rRow("runner", r.runner);
        out += "  exits: " + exitText("stop", r.all.stop) + " | " + exitText("target", r.all.target) + " | " +
               exitText("breakeven", r.all.breakeven) + " | " + exitText("trailing", r.all.trailing) + " | " +
               exitText("indicator", r.all.indicator) + " | " +
               exitText("cross-back", r.all.signal) + "\n";
        out += fmt("  %d trades were still open when the data ended and are not counted above.\n", r.openAtEnd);
        return out;
    }
}

StudyResult runStudy(const AlignedData& inSample, const AlignedData* outOfSample,
                     const std::vector<ParamRange>& ranges, const StrategyFactory& factory,
                     const StudyOptions& options)
{
    const OptimiseOptions& optimise = options.validation.optimise;
    const ScoreFn score = scoreBy(optimise.objective);

    StudyResult out;

    // 1. every parameter set over the in-sample assets
    out.allRuns = runGrid(inSample, expandGrid(ranges), factory, optimise.grid);
    out.ranking = rankResults(out.allRuns, score, optimise.minTrades);

    // 2. stability of the winner
    if (!out.ranking.empty())
        out.stability = neighbourStability(out.allRuns, ranges, out.ranking[0].evaluation.params, score,
                                           optimise.minTrades);

    // 3. does the way of choosing hold up on later years?
    out.timeSplit = trainTestSplit(inSample, ranges, factory, options.trainFraction, options.validation);
    if (options.walkTrainBars > 0 && options.walkTestBars > 0)
        out.walkForward = walkForward(inSample, ranges, factory, options.walkTrainBars, options.walkTestBars,
                                      options.validation);

    // 4. the winner on assets it has never seen
    if (!out.ranking.empty())
    {
        const Params& winner = out.ranking[0].evaluation.params;

        Evaluation unused;
        runWhole(inSample, winner, factory, optimise.grid, unused, out.inSampleAssets, out.inSampleR);

        if (outOfSample)
        {
            Evaluation e;
            runWhole(*outOfSample, winner, factory, optimise.grid, e, out.outOfSampleAssets, out.outOfSampleR);
            out.outOfSample = e;
        }
    }
    return out;
}

std::string formatStudy(const StudyResult& s, const StudyOptions& options, std::size_t top)
{
    const Objective objective = options.validation.optimise.objective;
    const int minTrades = options.validation.optimise.minTrades;

    std::string out;
    out += fmt("IN-SAMPLE: %zu parameter sets run; ranked by %s, minimum %d trades\n", s.allRuns.size(),
               objectiveName(objective).c_str(), minTrades);
    out += rankedTable(s.ranking, top);

    if (s.ranking.empty())
        return out + "\nNo winner, so there is nothing more to check.\n";

    const Ranked& best = s.ranking[0];
    out += "\nWINNER: " + paramsText(best.evaluation.params) + "\n";

    if (s.stability)
    {
        if (s.stability->neighbours == 0)
            out += "  Stability: no neighbouring parameter sets to compare with.\n";
        else
            out += fmt("  Stability: neighbours score %s on average (%s of the winner's %s), worst %s, over %zu neighbours.\n",
                       num(s.stability->meanNeighbour).c_str(),
                       s.stability->score > 0 ? pct(s.stability->meanNeighbour / s.stability->score).c_str() : "n/a",
                       num(s.stability->score).c_str(), num(s.stability->worstNeighbour).c_str(),
                       s.stability->neighbours);
    }
    out += "  Per asset (in-sample):\n" + assetTable(s.inSampleAssets);
    if (s.inSampleR)
        out += rTable("winner, in-sample", *s.inSampleR);

    out += fmt("\nTIME SPLIT (train on the first %.0f%% of the years, test on the rest):\n",
               100.0 * options.trainFraction);
    if (s.timeSplit.test)
        out += fmt("  Chosen on the training years: %s, %s %s -> on the test years %s %s\n",
                   paramsText(s.timeSplit.ranking[0].evaluation.params).c_str(), objectiveName(objective).c_str(),
                   num(s.timeSplit.ranking[0].score).c_str(), objectiveName(objective).c_str(),
                   num(s.timeSplit.testScore).c_str());
    else
        out += "  No parameter set reached the minimum number of trades on the training years.\n";

    if (s.walkForward)
    {
        const WalkForwardResult& w = *s.walkForward;
        out += fmt("\nWALK-FORWARD (%zu folds, train %zu / test %zu snapshots), out-of-sample results chained:\n",
                   w.folds.size(), options.walkTrainBars, options.walkTestBars);
        out += fmt("  return %s, sharpe %s, max drawdown %s, %d trades, capture %s\n",
                   pct(w.metrics.totalReturn).c_str(), num(w.metrics.sharpe).c_str(),
                   pct(w.metrics.maxDrawdown).c_str(), w.metrics.trades, num(w.capture.efficiency).c_str());
        for (std::size_t i = 0; i < w.folds.size(); ++i)
            out += fmt("  fold %zu: %s\n", i + 1,
                       w.folds[i].best ? paramsText(w.folds[i].best->evaluation.params).c_str() : "(no winner, stayed in cash)");
    }

    if (s.outOfSample)
    {
        const Evaluation& e = *s.outOfSample;
        out += "\nOUT-OF-SAMPLE ASSETS (winner run once, nothing tuned on them):\n";
        out += fmt("  return %s, sharpe %s, max drawdown %s, %d trades, capture %s\n",
                   pct(e.metrics.totalReturn).c_str(), num(e.metrics.sharpe).c_str(),
                   pct(e.metrics.maxDrawdown).c_str(), e.metrics.trades, num(e.capture.efficiency).c_str());
        out += assetTable(s.outOfSampleAssets);
        if (s.outOfSampleR)
            out += rTable("winner, out-of-sample", *s.outOfSampleR);
    }
    return out;
}
