#pragma once

#include <cstddef>
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
    bool screen{false};                 // screen every baseline (nnfx, no cross-back exit, with the control)
    std::string screenCsv;              // optional: write the screen table here
    double minBeats{0.9};               // screen: share of random runs a candidate must beat
    double minExpectancy{0.03};         // screen: expectancy in R a candidate must reach
    bool noCosts{false};                // true = trade with no spread at all
    bool help{false};
};

// `args` are the command-line words after the program name. Throws std::invalid_argument with a
// message for the user on anything wrong.
CliOptions parseArgs(const std::vector<std::string>& args);

std::string usage();
