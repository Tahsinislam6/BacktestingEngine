#pragma once

#include <string>
#include <vector>

#include "baseline_registry.hpp"
#include "control.hpp"
#include "study.hpp"

// The NNFX screen for many baselines in one run. For each baseline:
//   1. the study on the in-sample assets, trading the NNFX way WITHOUT the cross-back exit
//   2. the winner run once on the out-of-sample assets
//   3. the direction control (the baseline's side vs `controlRuns` coin-flip runs) on both
// and one row of results. The out-of-sample numbers are the fair ones: the winner was tuned on the in-sample data.

struct ScreenOptions
{
    StudyOptions study;                 // objective, minimum trades, grid options (costs, threads, cash), windows
    NnfxOptions nnfx;                   // must have exitOnCross == false
    int controlRuns{200};
    double minBeats{0.9};               // a candidate beats at least this share of the random runs out-of-sample ...
    double minExpectancy{0.03};         // ... and has at least this expectancy in R out-of-sample
};

struct ScreenRow
{
    std::string name;
    std::string error;                  // not empty: the baseline could not be screened (the numbers are NaN)
    std::string params;                 // the winning parameters, "period=133"

    double inSampleSharpe{std::nan("")};
    double neighbourShare{std::nan("")};            // neighbours' mean score / the winner's (winner above 0 only)
    double timeSplitScore{std::nan("")};            // the objective on the later years, chosen on the earlier ones
    double oosSharpe{std::nan("")};
    double oosExpectancyR{std::nan("")};
    int oosTrades{0};
    double oosBeatsExpectancy{std::nan("")};        // share of random runs beaten, by expectancy in R
    double oosBeatsSharpe{std::nan("")};
    double inSampleBeatsExpectancy{std::nan("")};   // biased: tuned on this data
    bool candidate{false};
};

// Screens each baseline (one after another; each study and control uses the threads of options.study).
// A baseline that fails (an exception, or no parameter set reaching the minimum trades) gets a row with `error`
// set and the others carry on. Rows come back best first (see sortScreenRows).
// Throws std::invalid_argument if controlRuns < 1 or nnfx.exitOnCross is true.
std::vector<ScreenRow> screenBaselines(const std::vector<BaselineInfo>& baselines, const AlignedData& inSample,
                                       const AlignedData& outOfSample, const ScreenOptions& options);

// Best first: by oosBeatsExpectancy, highest first; rows without a number go last; ties keep their order.
void sortScreenRows(std::vector<ScreenRow>& rows);

std::string screenTable(const std::vector<ScreenRow>& rows, const ScreenOptions& options);

// One header line and one line per row. Numbers undefined are "nan"; fields with a comma are quoted.
std::string screenCsv(const std::vector<ScreenRow>& rows);
