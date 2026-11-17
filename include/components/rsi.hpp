#pragma once

#include <stdexcept>

#include "../components.hpp"

namespace components::rsi
{
// Wilder's RSI of the close, as a direction state: above 50 bullish (+1), below 50 bearish (-1), 50 neutral (0).
class Rsi
{
public:
    explicit Rsi(int period = 14) : period_(period)
    {
        if (period <= 0)
            throw std::invalid_argument("RSI period must be positive");
    }

    void update(const Bar& bar)
    {
        if (hasPrev_)
        {
            const double change = bar.close - prevClose_;
            const double gain = change > 0 ? change : 0.0;
            const double loss = change < 0 ? -change : 0.0;
            if (changes_ < period_)
            {
                gainSum_ += gain;
                lossSum_ += loss;
                if (++changes_ == period_)
                {
                    avgGain_ = gainSum_ / period_;
                    avgLoss_ = lossSum_ / period_;
                }
            }
            else
            {
                avgGain_ = (avgGain_ * (period_ - 1) + gain) / period_;
                avgLoss_ = (avgLoss_ * (period_ - 1) + loss) / period_;
            }
        }
        prevClose_ = bar.close;
        hasPrev_ = true;
    }

    // `period` price changes are needed, so period + 1 bars
    bool ready() const { return changes_ >= period_; }

    int state() const
    {
        if (!ready())
            throw std::logic_error("RSI is not ready yet");
        if (avgGain_ == avgLoss_)
            return 0;                                   // includes a flat market: no gains and no losses
        return avgGain_ > avgLoss_ ? 1 : -1;            // RSI > 50 exactly when the average gain exceeds the loss
    }

private:
    int period_;
    bool hasPrev_{false};
    double prevClose_{0};
    int changes_{0};
    double gainSum_{0}, lossSum_{0}, avgGain_{0}, avgLoss_{0};
};

struct Spec
{
    using Line = Rsi;
    static Params defaults() { return {{"period", 14}}; }
    static Line make(const Params& p) { return Line(p.getInt("period")); }
    static std::size_t warmup(const Params& p) { return static_cast<std::size_t>(p.getInt("period")) + 1; }
};
}
