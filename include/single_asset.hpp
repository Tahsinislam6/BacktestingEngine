#pragma once

#include <memory>
#include <string>
#include <vector>

#include "multi_backtester.hpp"
#include "strategy.hpp"

// Lets a one-asset Strategy (onBar) run where a MultiStrategy (onSnapshot) is expected, e.g. in a sleeve with one asset.
//
// Each snapshot, the bar of asset 0 is handed to the strategy's onBar and its orders are returned unchanged (a Strategy's
// orders already name asset 0). A snapshot where the asset has no bar is skipped: the strategy is not called, as if that
// day were missing from its own price series.
//
// It owns the strategy. It accepts exactly one asset: setSymbols (which the runner calls before the first snapshot)
// throws std::invalid_argument for any other number, because a one-asset strategy given two assets would size and trade
// them as if they were one. The constructor throws std::invalid_argument for a null strategy.
class SingleAssetAdapter : public MultiStrategy
{
    public:
        explicit SingleAssetAdapter(std::unique_ptr<Strategy> strategy);

        void setSymbols(const std::vector<std::string>& symbols) override;
        std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio) override;

    private:
        std::unique_ptr<Strategy> strategy_;
};
