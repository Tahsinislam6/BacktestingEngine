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

SleeveResult runSleeves(const AlignedData& data, std::vector<Sleeve>& sleeves,
                        const std::vector<double>& spreads, const SwapSchedule& swap)
{
    if (sleeves.empty())
        throw std::invalid_argument("there are no sleeves");

    // First check everything and work out each sleeve's asset positions, so that a mistake in any sleeve is found
    // before the first strategy is called.
    std::vector<std::vector<std::size_t>> picks;
    std::vector<bool> taken(data.symbols.size(), false);   // taken[a]: an earlier sleeve already has asset a
    for (std::size_t k = 0; k < sleeves.size(); ++k)
    {
        const Sleeve& sleeve = sleeves[k];

        if (!sleeve.strategy)
            throw std::invalid_argument("sleeve " + sleeve.name + " has no strategy");
        if (!(sleeve.cash > 0.0))   // written this way so that NaN is refused too
            throw std::invalid_argument("sleeve " + sleeve.name + " needs positive cash");
        for (std::size_t j = 0; j < k; ++j)
            if (sleeves[j].name == sleeve.name)
                throw std::invalid_argument("two sleeves are named " + sleeve.name);

        std::vector<std::size_t> indices = assetIndices(data, sleeve.symbols);   // throws for unknown / repeated / no symbols
        for (std::size_t index : indices)
        {
            if (taken[index])
                throw std::invalid_argument("asset " + data.symbols[index] + " is in two sleeves");
            taken[index] = true;
        }
        picks.push_back(std::move(indices));
    }

    SleeveResult out;
    out.combined.equityCurve.assign(data.snapshots.size(), 0.0);

    for (std::size_t k = 0; k < sleeves.size(); ++k)
    {
        Sleeve& sleeve = sleeves[k];
        const std::vector<std::size_t>& indices = picks[k];

        BacktestResult result = runMultiBacktest(selectAssets(data, indices), *sleeve.strategy, sleeve.cash,
                                                 selectSpreads(spreads, indices), selectSwap(swap, indices));

        // Every run has one equity value per snapshot, so the curves line up and can simply be added.
        for (std::size_t i = 0; i < result.equityCurve.size(); ++i)
            out.combined.equityCurve[i] += result.equityCurve[i];
        out.combined.finalEquity += result.finalEquity;

        // Trades go into the combined lists with the asset's position in the full data and the sleeve's name.
        for (ClosedTrade trade : result.closedTrades)
        {
            trade.trade.asset = static_cast<int>(indices[static_cast<std::size_t>(trade.trade.asset)]);
            out.combined.closedTrades.push_back(trade);
            out.closedTradeSleeve.push_back(sleeve.name);
        }
        for (Trade trade : result.openTrades)
        {
            trade.asset = static_cast<int>(indices[static_cast<std::size_t>(trade.asset)]);
            out.combined.openTrades.push_back(trade);
            out.openTradeSleeve.push_back(sleeve.name);
        }

        out.sleeves.push_back(SleeveRun{sleeve.name, sleeve.symbols, sleeve.cash, std::move(result)});
    }

    return out;
}

PortfolioMetrics computeSleeveMetrics(const SleeveResult& result, double periodsPerYear)
{
    PortfolioMetrics out;

    // The portfolio started with all the sleeves' money together.
    double totalCash = 0.0;
    for (const SleeveRun& sleeve : result.sleeves)
    {
        out.sleeves.push_back(SleeveMetrics{sleeve.name, computeMetrics(sleeve.result, sleeve.cash, periodsPerYear)});
        totalCash += sleeve.cash;
    }

    out.combined = computeMetrics(result.combined, totalCash, periodsPerYear);
    return out;
}
