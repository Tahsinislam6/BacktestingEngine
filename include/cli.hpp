#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "optimiser.hpp"

enum class Mode { Flip, Nnfx };

struct CliOptions
{
    std::string baseline{"sma"};
    Mode mode{Mode::Flip};              // flip = always in the market; nnfx = NNFX entries, stops and targets
    std::string inSampleDir;            // required (unless --help)
    std::string outSampleDir;           // optional
    std::string csvOut;                 // optional: write the ranked runs here
    Objective objective{Objective::Sharpe};
    int minTrades{30};
    int threads{0};
    double trainFraction{0.7};
    std::size_t walkTrain{0};           // 0 and 0 = no walk-forward
    std::size_t walkTest{0};
    double startingCash{100000.0};
    std::size_t top{15};
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
    bool continuation{false};           // nnfx: re-enter while price stays on the same side of the baseline
    std::optional<double> maxOpenRisk;      // nnfx: cap on the total risk of open signals, as a share of equity
    std::optional<int> maxPerCurrency;      // nnfx: cap on net signals per currency
    std::string signalKind;             // --signal kind:lookback: "breakout" or "momentum"; empty = the baseline study
    int signalLookback{0};
    int entryControl{0};                // nnfx: random stand-ins to compare each chosen confirmation / filter with, on the entry study; 0 = off
    int entryStudy{0};                  // nnfx: random-direction runs for the entry study (what price does after a signal); 0 = off
    int componentControl{0};            // nnfx: random stand-ins to compare each chosen component with; 0 = off
    bool screen{false};                 // screen every baseline (nnfx, no cross-back exit, with the control)
    std::string screenCsv;              // optional: write the screen table here
    double minBeats{0.9};               // screen: share of random runs a candidate must beat
    double minExpectancy{0.03};         // screen: expectancy in R a candidate must reach
    std::string logDir;                 // optional: append every screen / control result to the csv logs in this folder
    std::string note;                   // optional: why this test was run (goes into the log rows)
    bool noCosts{false};                // true = trade with no spread at all
    bool help{false};
};

// `args` are the command-line words after the program name. Throws std::invalid_argument with a
// message for the user on anything wrong.
CliOptions parseArgs(const std::vector<std::string>& args);

std::string usage();
