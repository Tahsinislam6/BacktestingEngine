#include "null_series.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <vector>

namespace
{
struct Relative
{
    double open, high, low, close;   // each divided by the previous bar's close
};
}

BarSeries shuffleReturns(const BarSeries& series, std::uint64_t seed, int blockLength)
{
    if (blockLength < 1)
        throw std::invalid_argument("blockLength must be at least 1");

    const std::size_t n = series.bars.size();
    if (n < 2)
        return series;

    // Every bar but the last is the divisor of the next bar's ratios.
    std::vector<Relative> relative;
    relative.reserve(n - 1);
    for (std::size_t i = 1; i < n; ++i)
    {
        const double prev = series.bars[i - 1].close;
        if (!(prev > 0.0))
            throw std::invalid_argument("shuffleReturns needs positive closes");
        const Bar& b = series.bars[i];
        relative.push_back({b.open / prev, b.high / prev, b.low / prev, b.close / prev});
    }

    // Pick the relative bars for output positions 1..n-1, one block at a time.
    const std::size_t m = relative.size();
    const std::size_t block = std::min<std::size_t>(static_cast<std::size_t>(blockLength), m);
    std::mt19937_64 rng(seed);
    std::vector<Relative> picked;
    picked.reserve(m);
    while (picked.size() < m)
    {
        const std::size_t start = static_cast<std::size_t>(rng() % (m - block + 1));   // 0 .. m - block
        const std::size_t take = std::min(block, m - picked.size());
        for (std::size_t k = 0; k < take; ++k)
            picked.push_back(relative[start + k]);
    }

    // Rebuild: each bar's prices are its ratios times the previous rebuilt close.
    BarSeries out = series;
    double prevClose = series.bars[0].close;
    for (std::size_t i = 1; i < n; ++i)
    {
        const Relative& r = picked[i - 1];
        Bar& b = out.bars[i];
        b.open = r.open * prevClose;
        b.high = r.high * prevClose;
        b.low = r.low * prevClose;
        b.close = r.close * prevClose;
        prevClose = b.close;
    }
    return out;
}
