#include "zigzag.hpp"

#include <stdexcept>

#include "indicators/atr.hpp"

namespace
{
    // Index of the highest value in v[from..to] (the earliest one if there is a tie).
    std::size_t argMax(const std::vector<double>& v, std::size_t from, std::size_t to)
    {
        std::size_t best = from;
        for (std::size_t i = from + 1; i <= to; ++i)
            if (v[i] > v[best])
                best = i;
        return best;
    }

    std::size_t argMin(const std::vector<double>& v, std::size_t from, std::size_t to)
    {
        std::size_t best = from;
        for (std::size_t i = from + 1; i <= to; ++i)
            if (v[i] < v[best])
                best = i;
        return best;
    }

    enum class Dir { Unknown, Up, Down };
}

std::vector<Pivot> zigzagPivots(const std::vector<double>& closes, const std::vector<double>& thresholds)
{
    if (closes.size() != thresholds.size())
        throw std::invalid_argument("closes and thresholds must have the same length");

    std::vector<Pivot> pivots;
    Dir dir = Dir::Unknown;
    std::size_t hi = 0;   // candidate swing high (highest close since the last pivot)
    std::size_t lo = 0;   // candidate swing low

    for (std::size_t i = 0; i < closes.size(); ++i)
    {
        const double c = closes[i];
        const double t = thresholds[i];
        const bool usable = t > 0.0;     // false for 0, negatives and NaN

        // No pivot yet: track the extremes so far and wait for the first big enough move.
        if (dir == Dir::Unknown)
        {
            if (c > closes[hi]) hi = i;
            if (c < closes[lo]) lo = i;
            if (!usable)
                continue;

            const bool upOk = c - closes[lo] >= t;     // rose far enough from the low
            const bool downOk = closes[hi] - c >= t;   // fell far enough from the high
            if (!upOk && !downOk)
                continue;

            if (upOk && (!downOk || lo < hi))          // if both, the earlier extreme is the first pivot
            {
                pivots.push_back({lo, closes[lo], false});
                dir = Dir::Up;
                hi = argMax(closes, lo, i);
            }
            else
            {
                pivots.push_back({hi, closes[hi], true});
                dir = Dir::Down;
                lo = argMin(closes, hi, i);
            }
            // fall through: this same bar may already confirm the next pivot
        }

        if (dir == Dir::Up)
        {
            if (c > closes[hi])
            {
                hi = i;                                         // still climbing
            }
            else if (usable && closes[hi] - c >= t)             // reversed far enough: the high is real
            {
                pivots.push_back({hi, closes[hi], true});
                dir = Dir::Down;
                lo = argMin(closes, hi, i);
            }
        }
        else if (dir == Dir::Down)
        {
            if (c < closes[lo])
            {
                lo = i;                                         // still falling
            }
            else if (usable && c - closes[lo] >= t)
            {
                pivots.push_back({lo, closes[lo], false});
                dir = Dir::Up;
                hi = argMax(closes, lo, i);
            }
        }
    }

    return pivots;
}

std::vector<Pivot> zigzag(const std::vector<Bar>& bars, double atrMultiple, int atrPeriod)
{
    if (atrMultiple <= 0.0)
        throw std::invalid_argument("atrMultiple must be positive");

    Atr atr(atrPeriod);     // throws if atrPeriod <= 0

    std::vector<double> closes;
    std::vector<double> thresholds;
    closes.reserve(bars.size());
    thresholds.reserve(bars.size());

    for (const Bar& bar : bars)
    {
        atr.update(bar);
        closes.push_back(bar.close);
        thresholds.push_back(atr.ready() ? atrMultiple * atr.value() : 0.0);
    }

    return zigzagPivots(closes, thresholds);
}
