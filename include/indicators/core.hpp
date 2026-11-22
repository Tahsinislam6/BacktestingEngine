#pragma once

#include <concepts>
#include <cstddef>
#include <vector>

#include "../aligned.hpp"
#include "../bar.hpp"

template <typename T>
concept BarIndicator = requires(T t, const Bar& bar) 
{
    t.update(bar);
    {t.ready()} -> std::convertible_to<bool>;
};

class WarmUp
{
    public:
        explicit WarmUp(int barsNeeded) : needed_(barsNeeded) {}

        void tick() 
        {
            if (seen_ < needed_)
                ++seen_;
        }

        bool ready() const
        {
            return seen_ >= needed_;
        }

    private:
        int needed_;
        int seen_{};
};

template <BarIndicator Ind>
class PerAsset
{
    public:
        PerAsset(std::size_t assets, const Ind& prototype) : items_(assets, prototype) {}

        void update(const Snapshot& snap)
        {
            for (std::size_t a = 0; a < items_.size() && a < snap.bars.size(); ++a)
            {
                if (snap.bars[a])
                    items_[a].update(*snap.bars[a]);
            }
        }

        Ind& operator[](std::size_t a)
        {
            return items_[a];
        }
        const Ind& operator[](std::size_t a) const
        {
            return items_[a];
        }
    private:
        std::vector<Ind> items_;
};