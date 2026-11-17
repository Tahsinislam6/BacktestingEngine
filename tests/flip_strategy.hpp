#pragma once

#include <stdexcept>
#include <vector>

#include "baseline.hpp"
#include "multi_backtester.hpp"

template <BaselineLine B>
class FlipStrategy : public MultiStrategy
{
    public:
        FlipStrategy(std::size_t assets, const B& prototype, double notional = 10000.0) : notional_(notional), lines_(assets, prototype), assets_(assets)
        {
            if (notional <=  0.0)
                throw std::invalid_argument("notional  must be positive");
        }

        std::vector<Order> onSnapshot(const Snapshot& snapshot, const Portfolio& portfolio) override
        {
            lines_.update(snapshot);

            std::vector<Order> orders;
            for (std::size_t a = 0; a < assets_ && a < snapshot.bars.size(); ++a)
            {
                if (!snapshot.bars[a] || !lines_[a].ready())
                    continue;

                const double close = snapshot.bars[a]->close;
                const double line = lines_[a].value();
                if (close == line)
                    continue;
                const Direction want = close > line ? Direction::Long : Direction::Short;

                const Trade* open = nullptr;
                for (const Trade& t : portfolio.openTrades())
                {
                    if (t.asset == static_cast<int>(a))
                        open = &t;
                }

                if (open && open->direction == want)
                    continue;
                if (open)
                    orders.push_back(closeOrder(open->id));
                orders.push_back(openOrder(want,notional_ / close, std::nullopt, std::nullopt, static_cast<int>(a)));
            }
            return orders;       
        }

        


    private:
        double notional_;
        PerAsset<B> lines_;
        std::size_t assets_;
};