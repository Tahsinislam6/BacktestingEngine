#include "screen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace
{
    template <typename... Args>
    std::string fmt(const char* format, Args... args)
    {
        char buffer[1024];
        std::snprintf(buffer, sizeof buffer, format, args...);
        return buffer;
    }

    std::string num(double v, const char* format = "%.2f") { return std::isnan(v) ? "nan" : fmt(format, v); }
    std::string pct(double f) { return std::isnan(f) ? "n/a" : fmt("%.0f%%", 100.0 * f); }

    std::string csvField(const std::string& s)
    {
        if (s.find_first_of(",\"\n") == std::string::npos)
            return s;
        std::string out = "\"";
        for (char c : s)
            out += c == '"' ? "\"\"" : std::string(1, c);
        return out + "\"";
    }

    ScreenRow screenOne(const BaselineInfo& baseline, const AlignedData& inSample, const AlignedData& outOfSample,
                        const ScreenOptions& options)
    {
        ScreenRow row;
        row.name = baseline.name;
        try
        {
            StudyOptions study = options.study;
            study.validation.warmupBars = nnfxWarmupBars(baseline, options.nnfx);
            const GridOptions& grid = study.validation.optimise.grid;

            const StudyResult s = runStudy(inSample, &outOfSample, baseline.ranges, baseline.nnfx(options.nnfx), study);
            if (s.ranking.empty())
            {
                row.error = "no parameter set reached the minimum number of trades";
                return row;
            }

            const Evaluation& winner = s.ranking[0].evaluation;
            row.params = paramsText(winner.params);
            row.inSampleSharpe = winner.metrics.sharpe;
            if (s.stability && s.stability->neighbours > 0 && s.stability->score > 0)
                row.neighbourShare = s.stability->meanNeighbour / s.stability->score;
            if (s.timeSplit.test)
                row.timeSplitScore = s.timeSplit.testScore;

            if (s.outOfSample)
                row.oosSharpe = s.outOfSample->metrics.sharpe;
            if (s.outOfSampleR)
            {
                row.oosExpectancyR = s.outOfSampleR->all.expectancyR;
                row.oosTrades = s.outOfSampleR->all.trades;
            }

            const ControlResult out = runDirectionControl(outOfSample, winner.params, baseline.nnfx, options.nnfx,
                                                          options.controlRuns, grid);
            row.oosBeatsExpectancy = out.expectancyRank;
            row.oosBeatsSharpe = out.sharpeRank;
            row.inSampleBeatsExpectancy = runDirectionControl(inSample, winner.params, baseline.nnfx, options.nnfx,
                                                              options.controlRuns, grid).expectancyRank;

            row.candidate = row.oosBeatsExpectancy >= options.minBeats && row.oosExpectancyR >= options.minExpectancy;
        }
        catch (const std::exception& e)
        {
            row.error = e.what();
            row.candidate = false;
        }
        return row;
    }
}

std::vector<ScreenRow> screenBaselines(const std::vector<BaselineInfo>& baselines, const AlignedData& inSample,
                                       const AlignedData& outOfSample, const ScreenOptions& options)
{
    if (options.controlRuns < 1)
        throw std::invalid_argument("the screen needs at least one random-direction run");
    if (options.nnfx.exitOnCross)
        throw std::invalid_argument("the screen cannot use the cross-back exit (the control needs it off)");

    std::vector<ScreenRow> rows;
    for (const BaselineInfo& b : baselines)
        rows.push_back(screenOne(b, inSample, outOfSample, options));
    sortScreenRows(rows);
    return rows;
}

void sortScreenRows(std::vector<ScreenRow>& rows)
{
    std::stable_sort(rows.begin(), rows.end(), [](const ScreenRow& a, const ScreenRow& b)
    {
        const bool aNan = std::isnan(a.oosBeatsExpectancy), bNan = std::isnan(b.oosBeatsExpectancy);
        if (aNan || bNan)
            return !aNan && bNan;
        return a.oosBeatsExpectancy > b.oosBeatsExpectancy;
    });
}

std::string screenTable(const std::vector<ScreenRow>& rows, const ScreenOptions& options)
{
    std::string out = fmt("SCREEN: %zu baselines, NNFX trading without the cross-back exit, %d random-direction runs each\n",
                          rows.size(), options.controlRuns);
    out += fmt("candidate = out-of-sample expectancy >= %sR and the baseline beats >= %s of the random runs out-of-sample\n",
               num(options.minExpectancy).c_str(), pct(options.minBeats).c_str());
    out += "(the in-sample columns are for orientation only: the winner was tuned on that data)\n\n";
    out += fmt("  %-14s %-16s %9s %10s %10s | %10s %9s %7s %10s %13s | %s\n", "baseline", "winner", "IS sharpe",
               "neighbours", "split test", "OOS sharpe", "OOS exp R", "trades", "beats(exp)", "beats(sharpe)", "verdict");
    for (const ScreenRow& r : rows)
    {
        if (!r.error.empty())
        {
            out += fmt("  %-14s ERROR: %s\n", r.name.c_str(), r.error.c_str());
            continue;
        }
        out += fmt("  %-14s %-16s %9s %10s %10s | %10s %9s %7d %10s %13s | %s\n", r.name.c_str(), r.params.c_str(),
                   num(r.inSampleSharpe).c_str(), pct(r.neighbourShare).c_str(), num(r.timeSplitScore).c_str(),
                   num(r.oosSharpe).c_str(), num(r.oosExpectancyR, "%.3f").c_str(), r.oosTrades,
                   pct(r.oosBeatsExpectancy).c_str(), pct(r.oosBeatsSharpe).c_str(),
                   r.candidate ? "candidate" : "-");
    }
    return out;
}

std::string screenCsv(const std::vector<ScreenRow>& rows)
{
    std::string out = "baseline,params,in_sample_sharpe,neighbour_share,time_split_score,oos_sharpe,oos_expectancy_r,"
                      "oos_trades,oos_beats_expectancy,oos_beats_sharpe,in_sample_beats_expectancy,candidate,error\n";
    for (const ScreenRow& r : rows)
    {
        out += csvField(r.name) + "," + csvField(r.params);
        for (double v : {r.inSampleSharpe, r.neighbourShare, r.timeSplitScore, r.oosSharpe, r.oosExpectancyR})
            out += "," + num(v, "%.6g");
        out += "," + std::to_string(r.oosTrades);
        for (double v : {r.oosBeatsExpectancy, r.oosBeatsSharpe, r.inSampleBeatsExpectancy})
            out += "," + num(v, "%.6g");
        out += std::string(",") + (r.candidate ? "yes" : "no") + "," + csvField(r.error) + "\n";
    }
    return out;
}
