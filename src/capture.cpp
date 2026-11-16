#include "capture.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
    // Position of the bar with exactly this timestamp, or bars.size() if there is none.
    std::size_t indexOfTime(const std::vector<Bar>& bars, std::int64_t time)
    {
        auto it = std::lower_bound(bars.begin(), bars.end(), time,
            [](const Bar& b, std::int64_t t) { return b.timestamp < t; });
        if (it == bars.end() || it->timestamp != time)
            return bars.size();
        return static_cast<std::size_t>(it - bars.begin());
    }

    // Adds a trade's profit to profit[i] for every bar i it was held over.
    // lastIndex = the bar it ended on; exitPrice is used on that bar if given.
    void addTrade(std::vector<double>& profit, const std::vector<Bar>& bars, Direction dir,
                  std::size_t entryIndex, std::size_t lastIndex, const double* exitPrice)
    {
        const double s = sign(dir);
        for (std::size_t i = entryIndex + 1; i <= lastIndex; ++i)
        {
            const double price = (exitPrice && i == lastIndex) ? *exitPrice : bars[i].close;
            profit[i] += s * std::log(price / bars[i - 1].close);
        }
    }
}

CaptureResult captureEfficiency(const std::vector<Bar>& bars,
                                const std::vector<Pivot>& pivots,
                                const std::vector<ClosedTrade>& closed,
                                const std::vector<Trade>& open,
                                int asset)
{
    CaptureResult result;
    if (bars.empty())
        return result;

    // profit[i] = what all trades earned over bar i (from close i-1 to close i).
    std::vector<double> profit(bars.size(), 0.0);

    for (const ClosedTrade& c : closed)
    {
        if (c.trade.asset != asset)
            continue;
        const std::size_t e = indexOfTime(bars, c.trade.entryTime);
        const std::size_t x = indexOfTime(bars, c.exitTime);
        if (e >= bars.size() || x >= bars.size())
            continue;
        addTrade(profit, bars, c.trade.direction, e, x, &c.exitPrice);
    }

    for (const Trade& t : open)       // still open: valued at the last close
    {
        if (t.asset != asset)
            continue;
        const std::size_t e = indexOfTime(bars, t.entryTime);
        if (e >= bars.size())
            continue;
        addTrade(profit, bars, t.direction, e, bars.size() - 1, nullptr);
    }

    for (std::size_t k = 0; k + 1 < pivots.size(); ++k)
    {
        const Pivot& from = pivots[k];
        const Pivot& to = pivots[k + 1];

        double inLeg = 0.0;
        for (std::size_t i = from.index + 1; i <= to.index && i < profit.size(); ++i)
            inLeg += profit[i];

        result.available += std::abs(std::log(to.price / from.price));
        result.captured += inLeg;
        ++result.legs;
        if (inLeg > 0.0)
            ++result.legsRight;
    }

    if (result.available > 0.0)
        result.efficiency = result.captured / result.available;
    if (result.legs > 0)
        result.rightShare = static_cast<double>(result.legsRight) / result.legs;

    return result;
}

std::vector<Bar> barsOf(const AlignedData& data, int asset)
{
    std::vector<Bar> out;
    if (asset < 0)
        return out;

    const std::size_t a = static_cast<std::size_t>(asset);
    for (const Snapshot& s : data.snapshots)
        if (a < s.bars.size() && s.bars[a])
            out.push_back(*s.bars[a]);
    return out;
}

std::vector<std::vector<Pivot>> zigzagAll(const AlignedData& data, double atrMultiple, int atrPeriod)
{
    std::vector<std::vector<Pivot>> all;
    for (std::size_t a = 0; a < data.symbols.size(); ++a)
        all.push_back(zigzag(barsOf(data, static_cast<int>(a)), atrMultiple, atrPeriod));
    return all;
}

CaptureResult captureEfficiency(const AlignedData& data,
                                const std::vector<std::vector<Pivot>>& pivots,
                                const BacktestResult& result)
{
    if (pivots.size() != data.symbols.size())
        throw std::invalid_argument("need one pivot list per asset");

    CaptureResult total;
    for (std::size_t a = 0; a < data.symbols.size(); ++a)
    {
        const CaptureResult r = captureEfficiency(barsOf(data, static_cast<int>(a)), pivots[a],
                                                  result.closedTrades, result.openTrades, static_cast<int>(a));
        total.captured += r.captured;
        total.available += r.available;
        total.legs += r.legs;
        total.legsRight += r.legsRight;
    }

    if (total.available > 0.0)
        total.efficiency = total.captured / total.available;
    if (total.legs > 0)
        total.rightShare = static_cast<double>(total.legsRight) / total.legs;

    return total;
}
