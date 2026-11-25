#include "buy_and_hold.hpp"

#include <memory>
#include <stdexcept>

BuyAndHold::BuyAndHold(double fraction) : fraction_(fraction)
{
    if (!(fraction >= 0.0 && fraction <= 1.0))     // written so that NaN is rejected too
        throw std::invalid_argument("fraction must be between 0 and 1");
}

std::vector<Order> BuyAndHold::onBar(const Bar& bar, const Portfolio& portfolio)
{
    if (bought_)
        return {};
    bought_ = true;
    if (bar.close <= 0.0 || fraction_ == 0.0)
        return {};
    return { openOrder(Direction::Long, fraction_ * portfolio.equity() / bar.close) };
}

void registerBuiltins(StrategyRegistry& registry)
{
    StrategyInfo info;
    info.name = "buy_and_hold";
    info.description = "Buys on the first bar and holds to the end. The baseline other strategies are compared with.";
    info.params = {{"fraction", 1.0, 0.0, 1.0, false, "Share of the equity to invest on the first bar"}};
    registry.add(info, [](const NamedParams& p) { return std::make_unique<BuyAndHold>(p.at("fraction")); });
}
