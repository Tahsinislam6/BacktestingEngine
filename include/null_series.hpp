#pragma once

#include <cstdint>

#include "bar.hpp"

// A "luck" copy of a price series, to measure how much density (see density.hpp) a grid shows on data with no real
// structure. Run the same sweep on many copies (different seeds) and compare the real density with e.g. the 95th
// percentile of the copies' densities (percentile() in density.hpp): a real result has to beat that.
//
// For every bar after the first, its "relative bar" is its open, high, low and close divided by the PREVIOUS bar's close.
// The relative bars are resampled in blocks of `blockLength` consecutive bars (each block starts at a random place; the
// last block is cut short to fit) and prices are rebuilt by chaining them from the first bar's close. This keeps the size
// of the moves, the gaps, the shape of each bar and short runs of bars, but destroys the real order. Use blockLength 1 to
// break every dependency, or a few days to keep short-term streaks.
//
// Timestamps, symbol, timeframe, volume and the first bar stay as they were. The same seed always gives the same series
// (on any machine: the random numbers come straight from std::mt19937_64, which the standard defines exactly).
// A blockLength above the number of relative bars is shortened to fit. Fewer than 2 bars are returned unchanged.
// Throws std::invalid_argument for blockLength < 1 or a non-positive close that would have to be a divisor.
BarSeries shuffleReturns(const BarSeries& series, std::uint64_t seed, int blockLength);
