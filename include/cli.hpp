#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct CliOptions
{
    std::string baseline;               // required unless --signal: the entry signal of the system
    std::string params;                 // the baseline's parameters, "period=26,mult=2" (checked against the baseline in main)
    std::string inSampleDir;            // required (unless --help)
    std::string outSampleDir;           // optional
    int threads{0};
    double startingCash{100000.0};
    bool keepSundays{false};            // true = do not fold Sunday bars into Monday
    std::string costsFile;              // optional: "symbol,spread_pips" rows replacing the typical spreads
    double spreadMultiplier{1.0};       // scales every spread
    bool noCrossExit{false};            // nnfx: do not close on the baseline crossing back (ATR stop/target/breakeven only)
    int control{0};                     // nnfx: random-direction runs to compare the winner with; 0 = off
    std::string confirm;                // nnfx: a State component that must agree with the direction at entry
    std::string exitIndicator;          // nnfx: a State component that closes a trade when it turns against it
    std::string filter;                 // nnfx: a Gate component that must allow the entry
    int confirmWindow{7};               // bars a signal waits for its confirmation
    bool oneCandle{false};              // nnfx: a signal that cannot be taken on the cross bar gets one more bar
    std::optional<double> stopAtr;      // nnfx: stop distance in ATR (default 1.5)
    std::optional<int> atrPeriod;       // nnfx: ATR period for the stop, the target and the chase check (default 14)
    std::optional<double> risk;         // nnfx: share of the account lost if the stop is hit, for the whole signal (default 0.02)
    bool noTrail{false};                // nnfx: no trailing stop and no breakeven stop
    std::optional<double> maxDistance;  // nnfx: skip an entry whose close is further than this many ATR from the line (default 1)
    bool noChaseLimit{false};           // nnfx: take every signal however far the close is from the line (a breakout channel's line is far away)
    bool c1Exit{false};                 // grid: each pair's C1 also closes a trade when its state turns against it
    bool crossExit{false};              // grid: close on the next baseline cross (the grid otherwise runs without that exit)
    std::optional<double> trailAfter;   // nnfx: one trade; its stop trails only after a close this many entry-ATR in profit
    bool noScaling{false};              // nnfx: one trade per signal with the whole risk, no take-profit half, trail from entry
    bool continuation{false};           // nnfx: re-enter while price stays on the same side of the baseline
    std::optional<double> maxOpenRisk;      // nnfx: cap on the total risk of open signals, as a share of equity
    std::optional<int> maxPerCurrency;      // nnfx: cap on net signals per currency
    std::string signalKind;             // --signal kind:lookback: "breakout" or "momentum"; empty = the baseline study
    int signalLookback{0};
    int entryControl{0};                // nnfx: random stand-ins to compare each chosen confirmation / filter with, on the entry study; 0 = off
    int entryStudy{0};                  // nnfx: random-direction runs for the entry study (what price does after a signal); 0 = off
    int componentControl{0};            // nnfx: random stand-ins to compare each chosen component with; 0 = off
    std::string gridFile;               // --grid <plan>: test every baseline x C1 pair listed in the plan file (see grid.hpp)
    std::string holdoutDir;             // grid: data the finalists are run on once, never chosen on
    std::string ledgerFile;             // grid: the tries ledger to append one row to
    int priorTries{0};                  // grid: pairs tried before on the same data (counted in the correction)
    double minBeats{0.9};               // grid: share of random runs a pair must beat (also after the correction)
    double minExpectancy{0.03};         // grid: expectancy in R a pair must reach
    std::string logDir;                 // optional: append every control / entry-study result to the csv logs in this folder
    std::string note;                   // optional: why this test was run (goes into the log rows)
    bool noCosts{false};                // true = trade with no spread at all
    std::string ratesFile;              // optional: "currency,date,rate" interest rates; turns swap (financing) on
    double swapMarkup{0.0};             // broker markup on swap, percentage points a year, against the trader
    bool help{false};
};

// `args` are the command-line words after the program name. Throws std::invalid_argument with a
// message for the user on anything wrong.
CliOptions parseArgs(const std::vector<std::string>& args);

std::string usage();
