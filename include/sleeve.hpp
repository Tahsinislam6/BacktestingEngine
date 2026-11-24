#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "aligned.hpp"
#include "swap.hpp"

// Helpers for splitting one AlignedData into smaller ones, one per sleeve (a sleeve = a few assets with their own cash).
// A slice keeps ALL the snapshots of the original, even those where its assets have no bar, so every slice has the same
// length and timestamps and their equity curves can be added up later.

// The position in data.symbols of each of `symbols`, in the order given.
// Throws std::invalid_argument for an empty list, a symbol that is not in the data, or a symbol given twice.
std::vector<std::size_t> assetIndices(const AlignedData& data, const std::vector<std::string>& symbols);

// A copy of the data with only the assets at `indices`, renumbered 0..n-1 in the order given (new asset k = old asset
// indices[k]). Same snapshots, same timestamps; each snapshot's bars are the chosen assets' bars.
// Throws std::invalid_argument for an empty list, an index past the last asset, or an index given twice.
AlignedData selectAssets(const AlignedData& data, const std::vector<std::size_t>& indices);

// The spreads of the chosen assets, renumbered the same way. An empty list stays empty; an asset past the end of a
// shorter list gets 0 (no cost), as in runMultiBacktest.
std::vector<double> selectSpreads(const std::vector<double>& spreads, const std::vector<std::size_t>& indices);

// The swap schedule of the chosen assets, renumbered the same way. The markup is kept; an inactive schedule stays inactive.
// An asset without a row of rates gets an empty row (unknown rates, which the engine counts as no interest).
SwapSchedule selectSwap(const SwapSchedule& swap, const std::vector<std::size_t>& indices);
