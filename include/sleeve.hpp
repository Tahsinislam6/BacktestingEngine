#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "aligned.hpp"
#include "backtester.hpp"
#include "multi_backtester.hpp"
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

// A sleeve: a bucket of money with its own assets and its own strategy. Nothing moves between sleeves.
// To run a one-asset Strategy in a sleeve, wrap it in a SingleAssetAdapter (single_asset.hpp).
struct Sleeve
{
    std::string name;                           // labels the results; unique among the sleeves of a run
    std::vector<std::string> symbols;           // its assets, as named in the data (no asset may be in two sleeves)
    std::unique_ptr<MultiStrategy> strategy;    // sees only its own assets, numbered 0..n-1 in the order of `symbols`
    double cash{};                              // its starting cash, positive
};

struct SleeveRun
{
    std::string name;
    std::vector<std::string> symbols;
    double cash{};
    BacktestResult result;                      // this sleeve alone; its trades' asset numbers are the sleeve's own (0..n-1)
};

struct SleeveResult
{
    std::vector<SleeveRun> sleeves;             // in the order the sleeves were given

    // The whole portfolio: equityCurve[i] is the sum of the sleeves' curves at snapshot i, finalEquity the sum of their
    // final equities, and the trades are all sleeves' trades one sleeve after the other, with `asset` renumbered to the
    // asset's position in the full data (so trades of different sleeves can't be mistaken for each other).
    BacktestResult combined;
    std::vector<std::string> closedTradeSleeve; // closedTradeSleeve[i] = name of the sleeve of combined.closedTrades[i]
    std::vector<std::string> openTradeSleeve;   // the same for combined.openTrades
};

// Runs every sleeve as its own backtest (runMultiBacktest) on its own slice of the data, with the matching slice of the
// spreads and the swap, then adds the results up. All checks happen before anything runs, so on an error no strategy was
// called. Throws std::invalid_argument for: no sleeves; a sleeve with a null strategy or cash that is not positive;
// two sleeves with the same name; a symbol that is not in the data or is listed twice in a sleeve; an asset in two
// sleeves. Assets that are in no sleeve are simply not traded.
SleeveResult runSleeves(const AlignedData& data, std::vector<Sleeve>& sleeves,
                        const std::vector<double>& spreads = {}, const SwapSchedule& swap = {});
