#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "aligned.hpp"
#include "baseline_registry.hpp"
#include "component_control.hpp"
#include "components.hpp"
#include "param_space.hpp"

// The entry study: does the baseline's signal carry any information, before stops, targets, sizing or costs get
// in the way? For every signal NnfxStrategy would see - the close crosses the baseline line (the first bar after
// warm-up never signals), the ATR is ready and positive, and the don't-chase filter lets it through - it measures
// what price does next, in ATR units of the signal bar, and compares that with the same signals given a coin-flip
// direction (seeds 1, 2, ...). Overlapping signals are all counted: the study does not simulate positions.
//
//   components  a confirmation and a filter turn crosses into entries exactly as in NnfxStrategy: a cross starts a
//             pending signal that lasts confirmationWindow bars (1 without a confirmation); on each of those bars the
//             entry is taken if the ATR is positive, the close is within the chase distance, the confirmation agrees
//             with the direction and the filter allows. Everything below is then measured from the ENTRY bar (its
//             close and its ATR). A component that is not ready hides the bar, so a cross before that is not seen.
//             Exit indicators, the one-candle rule and continuation entries are not part of the study.
//   forward   (close h bars later - signal close) / ATR, times the direction, for h in entryHorizons().
//             Only bars the asset has count as bars. An event without a bar h later is left out of that horizon.
//   net       the same minus the asset's spread (a full round trip) in ATR
//   race      from the signal close, does the target (targetAtr * ATR) or the stop (stopAtr * ATR) come first over
//             the next kRaceBars bars? The stop wins if one bar reaches both. Events that reach neither count as such.
//   excursion the best (MFE) and worst (MAE) move in the signal's favour / against it over the next kExcursionBars
//             bars, never below 0; events without that many bars after them are left out

// What counts as a signal. Cross is the baseline line (runEntryStudy); the others need no baseline (runSignalStudy):
//   Breakout  the close is above the highest high (below the lowest low) of the previous `lookback` bars; a break counts
//             only if the last break the same way was more than `lookback` bars ago, so a run of breaks is one signal
//   Momentum  the sign of (close - close `lookback` bars ago) flips; the first sign, and a return of exactly zero, are
//             not flips
// The don't-chase filter is a distance from the baseline line, so it applies to Cross only.
enum class SignalKind { Cross, Breakout, Momentum };
const char* signalKindName(SignalKind kind);       // "cross", "breakout", "momentum"

inline constexpr int kExcursionBars = 20;
inline constexpr int kRaceBars = 40;

const std::vector<int>& entryHorizons();           // 1, 2, 3, 5, 10, 20, 40

struct EntryStudyOptions
{
    int atrPeriod{14};
    std::optional<double> maxDistanceAtr{1.0};     // skip a cross whose close is further than this many ATR from the line
    double stopAtr{1.5};
    double targetAtr{1.0};
    NnfxComponents components;                     // confirmation and filter are used; the exit indicator is ignored
    int confirmationWindow{7};                     // bars a signal waits for its confirmation, the signal bar included
    SignalKind kind{SignalKind::Cross};
    int lookback{20};                              // Breakout and Momentum
};

struct HorizonResult
{
    int horizon{};
    int events{};                                  // events with a bar `horizon` bars later
    double meanAtr{std::nan("")};                  // mean move in the signal's direction, in ATR
    double netMeanAtr{std::nan("")};               // minus the spread
    double shareUp{std::nan("")};                  // share of events that moved in the signal's direction
    double randomMeanAtr{std::nan("")};            // the same with coin-flip directions, averaged over the runs
    double beats{std::nan("")};                    // share of random runs whose mean is below the real one (ties half)
};

struct EntryStudyResult
{
    int signals{};                                 // signals seen (before any filter or component); events are the entries taken
    int events{};
    std::vector<HorizonResult> horizons;           // one per entryHorizons(), in order

    double stopAtr{};                              // the options the race used
    double targetAtr{};
    double targetFirst{std::nan("")};              // shares of all events
    double stopFirst{std::nan("")};
    double neither{std::nan("")};
    double raceR{std::nan("")};                    // (targetFirst * targetAtr - stopFirst * stopAtr) / stopAtr, no costs
    double randomTargetFirst{std::nan("")};
    double randomRaceR{std::nan("")};
    double targetBeats{std::nan("")};              // share of random runs with a lower target-first share

    int excursionEvents{};
    double mfeAtr{std::nan("")};
    double maeAtr{std::nan("")};
    double randomMfeAtr{std::nan("")};
    double randomMaeAtr{std::nan("")};
    double excursionBeats{std::nan("")};           // share of random runs with a lower (MFE - MAE)
};

// `spreads[a]` is asset a's spread in price units (spreadsFor in costs.hpp); missing entries cost nothing.
// Throws std::invalid_argument if randomRuns < 1, atrPeriod < 1, stopAtr or targetAtr <= 0, maxDistanceAtr <= 0,
// confirmationWindow < 1, or options.kind is not Cross.
EntryStudyResult runEntryStudy(const AlignedData& data, const BaselineInfo& baseline, const Params& params,
                               const EntryStudyOptions& options, int randomRuns,
                               const std::vector<double>& spreads = {});

// ---- the component control on the entry study ----
// Does a real confirmation / filter pick better entries than a random stand-in that is just as often on and just as
// persistent (see random_component.hpp)? Three kinds of arm, each the study with the REAL signal directions (no coin
// flips): the options as given, the options without this role's component, and one arm per seed with the component
// replaced by a stand-in. The real arm's numbers are ranked among the stand-ins'.
struct EntryArm
{
    int events{};
    std::vector<double> meanAtr;                   // per entryHorizons(), gross; NaN where there are no events
    double raceR{std::nan("")};
    double balance{std::nan("")};                  // mean MFE - mean MAE over the excursion bars
};

struct EntryControlResult
{
    EntryArm real, without;
    std::vector<EntryArm> random;                  // seed 1, 2, ... in order
    std::vector<double> meanBeats;                 // per horizon: share of random arms the real mean is above (ties half)
    double raceBeats{std::nan("")};
    double balanceBeats{std::nan("")};
    std::string behaviour;                         // what the stand-ins matched
};

// Throws std::invalid_argument if seeds < 1, the role is the exit indicator (an exit does not pick entries), the
// options have no component in `role`, or the options are not valid for runEntryStudy.
EntryControlResult runEntryComponentControl(const AlignedData& data, const BaselineInfo& baseline, const Params& params,
                                            const EntryStudyOptions& options, ComponentRole role, int seeds,
                                            const std::vector<double>& spreads = {});

std::string formatEntryControl(const std::string& label, const std::string& componentName, ComponentRole role,
                               const EntryControlResult& result);

// The study for a signal that does not need a baseline (options.kind must be Breakout or Momentum, with lookback >= 1).
// Same result, same text, same random-direction comparison; throws std::invalid_argument as runEntryStudy does, and
// when the kind is Cross.
EntryStudyResult runSignalStudy(const AlignedData& data, const EntryStudyOptions& options, int randomRuns,
                                const std::vector<double>& spreads = {});

// A readable table. `label` names the data (e.g. "out-of-sample"). Undefined numbers print as "n/a".
std::string formatEntryStudy(const std::string& label, const EntryStudyResult& result);
