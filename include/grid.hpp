#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "baseline_registry.hpp"
#include "components.hpp"
#include "control.hpp"

// The grid: every baseline x C1 pair tested in one batch (phase one of the plan, see README.md section 3).
//
// For each pair, with the baseline's parameters FIXED beforehand (nothing is searched or tuned):
//   1. the system (baseline + the C1, ATR stops and targets, no cross-back exit) is run on the in-sample folder and on the
//      out-of-sample folder: together the selection data
//   2. its out-of-sample expectancy in R is ranked among the random-direction runs of the same system (same signals, stops,
//      costs, C1 gating) and turned into a score z: standard deviations above those random runs
//   3. the multiple-testing correction: the best of M pairs looks good by luck far more often than one pair, so z is also
//      judged against the random runs of ALL pairs pooled, raised to the number of pairs tried: adjusted beats = F(z)^M
//   4. a pair is a FINALIST when its out-of-sample expectancy >= minExpectancy, it beats >= minBeats of its random runs,
//      its adjusted beats >= minBeats, and its in-sample expectancy is above 0 (a free replication on other pairs)
//   5. each finalist is run ONCE with the same parameters on the holdout (data nothing was chosen on); it is a CANDIDATE
//      when it passes there too: expectancy >= minExpectancy and beats >= minBeats, the bar being minBeats^(1/F) when
//      there are F > 2 finalists. Pairs that are not finalists never touch the holdout.

struct GridBaseline
{
    BaselineInfo info;
    Params params;                          // the fixed parameters of this baseline
};

struct GridC1
{
    std::string name;
    std::shared_ptr<const AnyState> prototype;      // a State component used as the first confirmation
};

struct GridOptions
{
    RunOptions run;                         // cash, costs, threads
    NnfxOptions nnfx;                       // the other nnfx switches; exitOnCross must be false and the confirmation unset
    int controlRuns{200};                   // random-direction runs per pair
    double minBeats{0.9};
    double minExpectancy{0.03};
    int priorTries{0};                      // pairs tried before on the same data: they count in the correction
};

struct GridRow
{
    std::string baseline, params, c1;
    std::string error;                      // not empty: this pair could not be run (the numbers are NaN)

    int inSampleTrades{0};
    double inSampleExpectancyR{std::nan("")};
    int oosTrades{0};
    double oosExpectancyR{std::nan("")};
    double oosBeatsExpectancy{std::nan("")};        // share of random runs beaten (by expectancy in R)
    double oosScore{std::nan("")};                  // the expectancy in standard deviations of its random runs
    double oosAdjustedBeats{std::nan("")};          // familyAdjustedBeats of that score
    int familySize{0};                              // pairs tried here + priorTries
    bool passedStageOne{false};                     // a finalist

    double holdoutExpectancyR{std::nan("")};        // the finalist, once, on the holdout (NaN: not run)
    int holdoutTrades{0};
    double holdoutBeatsExpectancy{std::nan("")};
    double holdoutBar{std::nan("")};                // the beats a finalist needs on the holdout
    bool candidate{false};
};

// (value - mean) / sd of the finite `samples` (sd with n - 1). NaN if value is NaN, fewer than two samples are finite,
// or they are all equal.
double standardScore(double value, const std::vector<double>& samples);

// Each finite sample as a standard score against the samples themselves. Empty if that is undefined (see standardScore).
std::vector<double> nullScores(const std::vector<double>& samples);

// "Beats after trying `familySize` pairs": F^familySize, where F = (n - number of `luck` scores at or above `score`)
// / (n + 1) is the share of luck the score is above (the +1 keeps a finite sample from proving "never"). The best of
// familySize pairs beats luck by chance with probability F^familySize when each one's chance is F.
// NaN if the score is NaN or `luck` is empty. Throws std::invalid_argument if familySize < 1.
double familyAdjustedBeats(double score, const std::vector<double>& luck, int familySize);

// Runs every baseline x C1 pair (one after another; each control uses the threads of options.run). A pair that fails (an
// exception) gets a row with `error` set, counts as a try, and the others carry on. Rows come back best first (see
// the order in gridTable). `holdout` may be null (then there are finalists but no candidates).
// Throws std::invalid_argument if there is no baseline or no C1, controlRuns < 1, priorTries < 0, or
// nnfx.exitOnCross is true or nnfx already has a confirmation.
std::vector<GridRow> runGrid(const std::vector<GridBaseline>& baselines, const std::vector<GridC1>& c1s,
                             const AlignedData& inSample, const AlignedData& outOfSample, const GridOptions& options,
                             const AlignedData* holdout = nullptr);

// The summary (pairs tried, finalists, candidates, the chance of luck at that number of tries) and the `top` best rows;
// every finalist and every failed pair is listed whatever `top` is. The full table is in the grid log.
std::string gridTable(const std::vector<GridRow>& rows, const GridOptions& options, std::size_t top = 15);

// The csv header line, one row's line (both end in "\n"), and a whole table. Undefined numbers are "nan"; fields with a
// comma are quoted.
std::string gridCsvHeader();
std::string gridCsvRow(const GridRow& row);
std::string gridCsv(const std::vector<GridRow>& rows);

// ---------- the plan file ----------
// The pre-registration of a grid run: which baselines with which parameters, and the C1 pool. Plain text, one item a line:
//   # comment
//   baseline kijun period=26
//   baseline kama period=15,slow=30
//   c1 rsi
//   c1 aroon
struct GridPlan
{
    std::vector<std::pair<std::string, std::string>> baselines;     // name, the parameter text as written
    std::vector<std::string> c1s;
};

// Throws std::invalid_argument naming the line for anything wrong: an unknown keyword, a missing name, a name twice, junk
// after a c1 name, or no baseline / no c1 at all.
GridPlan parseGridPlan(const std::string& text);

// Reads the file and parses it. Throws std::runtime_error if it cannot be read.
GridPlan loadGridPlan(const std::string& path);

struct ResolvedGrid
{
    std::vector<GridBaseline> baselines;
    std::vector<GridC1> c1s;
};

// Looks the names up in the baseline and component registries and reads the parameters (parseParams: every parameter,
// inside its range). A C1 must be a State component (default parameters). Throws std::invalid_argument naming the problem.
ResolvedGrid resolveGridPlan(const GridPlan& plan);
