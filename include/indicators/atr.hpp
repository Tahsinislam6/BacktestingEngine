#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "indicators/core.hpp"

// Average True Range
class Atr
{
public:
    explicit Atr(int period = 14) : period_(period), warm_(period)
    {
        if (period <= 0)
            throw std::invalid_argument("ATR period must be positive");
    }

    void update(const Bar& bar)
    {
        // True range: the biggest of today's range and the gaps from yesterday's close.
        double tr = bar.high - bar.low;
        if (hasPrev_)
            tr = std::max({tr, std::abs(bar.high - prevClose_), std::abs(bar.low - prevClose_)});
        prevClose_ = bar.close;
        hasPrev_ = true;

        if (!warm_.ready())
        {
            sum_ += tr;                      
            warm_.tick();
            if (warm_.ready())
                atr_ = sum_ / period_;         
        }
        else
        {
            atr_ = (atr_ * (period_ - 1) + tr) / period_;   
        }
    }

    bool ready() const { return warm_.ready(); }

    double value() const
    {
        if (!ready())
            throw std::logic_error("ATR is not ready yet");
        return atr_;
    }

private:
    int period_;
    WarmUp warm_;
    double sum_{0};
    double atr_{0};
    double prevClose_{0};
    bool hasPrev_{false};
};

static_assert(BarIndicator<Atr>);
