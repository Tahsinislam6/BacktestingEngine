#include "single_asset.hpp"

#include <stdexcept>
#include <utility>

SingleAssetAdapter::SingleAssetAdapter(std::unique_ptr<Strategy> strategy) : strategy_(std::move(strategy))
{
    if (!strategy_)
        throw std::invalid_argument("SingleAssetAdapter needs a strategy");
}

void SingleAssetAdapter::setSymbols(const std::vector<std::string>& symbols)
{
    if (symbols.size() != 1)
        throw std::invalid_argument("a one-asset strategy needs exactly one asset, got " + std::to_string(symbols.size()));
}

std::vector<Order> SingleAssetAdapter::onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio)
{
    // No bar for the asset today: the strategy does not get called, as if the day were missing from its own series.
    if (snapshot.bars.empty() || !snapshot.bars[0])
        return {};

    return strategy_->onBar(*snapshot.bars[0], portfolio);
}
