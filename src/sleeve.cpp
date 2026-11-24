#include "sleeve.hpp"

#include <algorithm>
#include <stdexcept>

namespace
{
    // Throws if `indices` is empty, has an index past `count` assets, or has the same index twice.
    void checkIndices(const std::vector<std::size_t>& indices, std::size_t count)
    {
        if (indices.empty())
            throw std::invalid_argument("a sleeve needs at least one asset");

        for (std::size_t k = 0; k < indices.size(); ++k)
        {
            if (indices[k] >= count)
                throw std::invalid_argument("asset index past the last asset");

            // Compare with every earlier entry: any match means the asset was listed twice.
            if (std::find(indices.begin(), indices.begin() + k, indices[k]) != indices.begin() + k)
                throw std::invalid_argument("asset listed twice");
        }
    }
}

std::vector<std::size_t> assetIndices(const AlignedData& data, const std::vector<std::string>& symbols)
{
    if (symbols.empty())
        throw std::invalid_argument("a sleeve needs at least one asset");

    std::vector<std::size_t> indices;
    for (std::size_t k = 0; k < symbols.size(); ++k)
    {
        // The same symbol twice would be the same asset twice.
        if (std::find(symbols.begin(), symbols.begin() + k, symbols[k]) != symbols.begin() + k)
            throw std::invalid_argument("symbol listed twice: " + symbols[k]);

        auto it = std::find(data.symbols.begin(), data.symbols.end(), symbols[k]);
        if (it == data.symbols.end())
            throw std::invalid_argument("symbol not in the data: " + symbols[k]);

        indices.push_back(static_cast<std::size_t>(it - data.symbols.begin()));
    }
    return indices;
}

AlignedData selectAssets(const AlignedData& data, const std::vector<std::size_t>& indices)
{
    checkIndices(indices, data.symbols.size());

    AlignedData out;
    for (std::size_t index : indices)
        out.symbols.push_back(data.symbols[index]);

    // Every snapshot is kept; only its list of bars is rebuilt with the chosen assets.
    for (const Snapshot& snapshot : data.snapshots)
    {
        Snapshot slice;
        slice.timestamp = snapshot.timestamp;
        for (std::size_t index : indices)
            slice.bars.push_back(snapshot.bars[index]);
        out.snapshots.push_back(std::move(slice));
    }
    return out;
}

std::vector<double> selectSpreads(const std::vector<double>& spreads, const std::vector<std::size_t>& indices)
{
    std::vector<double> out;
    if (spreads.empty())
        return out;   // an empty list means no cost for anyone

    for (std::size_t index : indices)
        out.push_back(index < spreads.size() ? spreads[index] : 0.0);
    return out;
}

SwapSchedule selectSwap(const SwapSchedule& swap, const std::vector<std::size_t>& indices)
{
    SwapSchedule out;
    out.markupPct = swap.markupPct;

    if (swap.differentialPct.empty())
        return out;   // markup only (or nothing at all): no per-asset rows to pick

    for (std::size_t index : indices)
    {
        if (index < swap.differentialPct.size())
            out.differentialPct.push_back(swap.differentialPct[index]);
        else
            out.differentialPct.push_back({});   // no rates known for this asset
    }
    return out;
}
