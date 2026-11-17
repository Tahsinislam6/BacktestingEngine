#pragma once

#include <stdexcept>

#include "../components.hpp"
#include "../indicators/atr.hpp"

namespace components::atr_expansion
{
// A volatility gate: trade only when the short ATR is at least `ratio` times the long ATR (volatility is
// expanding, not dying).
class AtrExpansion
{
public:
    AtrExpansion(int fast, int slow, double ratio) : fast_(fast), slow_(slow), ratio_(ratio)
    {
        if (fast <= 0 || slow <= fast)
            throw std::invalid_argument("need 0 < fast < slow");
        if (!(ratio > 0.0))
            throw std::invalid_argument("ratio must be positive");
    }

    void update(const Bar& bar)
    {
        fast_.update(bar);
        slow_.update(bar);
    }

    bool ready() const { return slow_.ready(); }

    bool allows() const
    {
        if (!ready())
            throw std::logic_error("the gate is not ready yet");
        return fast_.value() >= ratio_ * slow_.value();
    }

private:
    Atr fast_, slow_;
    double ratio_;
};

struct Spec
{
    using Line = AtrExpansion;
    static Params defaults() { return {{"fast", 14}, {"slow", 50}, {"ratio", 1.0}}; }
    static Line make(const Params& p) { return Line(p.getInt("fast"), p.getInt("slow"), p.get("ratio")); }
    static std::size_t warmup(const Params& p) { return static_cast<std::size_t>(p.getInt("slow")); }
};
}
