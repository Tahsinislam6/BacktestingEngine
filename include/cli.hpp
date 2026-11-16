#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "optimiser.hpp"

struct CliOptions
{
    std::string baseline{"sma"};
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
    bool help{false};
};

// `args` are the command-line words after the program name. Throws std::invalid_argument with a
// message for the user on anything wrong.
CliOptions parseArgs(const std::vector<std::string>& args);

std::string usage();
