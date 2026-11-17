#pragma once

#include <optional>
#include <string>
#include <vector>

#include "report.hpp"
#include "rstats.hpp"
#include "validation.hpp"

// The whole workflow for one baseline:
//
//   1. run every parameter set over the IN-SAMPLE assets (all years) and rank them
//   2. check how stable the winner is against its neighbours
//   3. time split and walk-forward on the in-sample assets: does the way of choosing parameters
//      still work on later years?
//   4. run the winner ONCE on the out-of-sample assets, which were never used to choose anything
//
// The parameters are shared by all assets: one set is chosen for the whole group.

struct StudyOptions
{
    ValidationOptions validation;       // objective, minimum trades, grid options, warm-up
    double trainFraction{0.7};          // for the time split
    std::size_t walkTrainBars{0};       // walk-forward window sizes in snapshots; 0 = skip walk-forward
    std::size_t walkTestBars{0};
};

struct StudyResult
{
    std::vector<Evaluation> allRuns;                    // every parameter set, in grid order
    std::vector<Ranked> ranking;                        // best first
    std::optional<StabilityResult> stability;           // of the winner (empty if nothing ranked)
    TrainTestResult timeSplit;
    std::optional<WalkForwardResult> walkForward;       // empty if skipped
    std::vector<AssetReport> inSampleAssets;            // the winner, per in-sample asset
    std::optional<RReport> inSampleR;                   // the winner's R-multiples; empty if its trades have no stops
    std::optional<Evaluation> outOfSample;              // the winner on the out-of-sample assets
    std::vector<AssetReport> outOfSampleAssets;
    std::optional<RReport> outOfSampleR;
};

// `outOfSample` may be null (then step 4 is skipped). Both datasets must be non-copied references that
// outlive the call. If no run reaches the minimum trade count there is no winner and steps 2 and 4 are skipped.
StudyResult runStudy(const AlignedData& inSample, const AlignedData* outOfSample,
                     const std::vector<ParamRange>& ranges, const StrategyFactory& factory,
                     const StudyOptions& options);

// The study as readable text. `top` = how many ranked rows to show.
std::string formatStudy(const StudyResult& study, const StudyOptions& options, std::size_t top);
