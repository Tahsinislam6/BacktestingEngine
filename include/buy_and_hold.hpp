#pragma once

#include <vector>

#include "registry.hpp"
#include "strategy.hpp"

// The one generic strategy in the public engine, and the baseline every real strategy is compared with: on the first bar
// it opens one long trade worth `fraction` of the equity, and never trades again.
// Throws std::invalid_argument if fraction is not in 0..1.
class BuyAndHold : public Strategy
{
    public:
        explicit BuyAndHold(double fraction = 1.0);

        std::vector<Order> onBar(const Bar& bar, const Portfolio& portfolio) override;

    private:
        double fraction_;
        bool bought_{false};
};

// Adds "buy_and_hold" (parameter "fraction", 0..1, default 1) to the registry. Throws if it is already there.
void registerBuiltins(StrategyRegistry& registry);
