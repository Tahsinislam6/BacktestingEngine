#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>

#include "null_series.hpp"

// These tests define shuffleReturns(): a "luck" copy of a price series, used as a baseline for density numbers.
//
// For every bar after the first, its "relative bar" is its open, high, low and close divided by the PREVIOUS bar's close.
// shuffleReturns resamples those relative bars in blocks of `blockLength` consecutive bars (blocks start at random places;
// the last block is cut short to fit) and rebuilds prices by chaining them from the first bar's close. That keeps the
// size of the moves, the gaps and the shape of each bar, and keeps short runs of bars together, but destroys the real
// order. Timestamps, symbol, timeframe, volume and the first bar stay as they were. The same seed always gives the same
// series.

namespace
{
Bar makeBar(std::int64_t t, double o, double h, double l, double c, double v = 0.0)
{
    Bar b;
    b.timestamp = t;
    b.open = o;
    b.high = h;
    b.low = l;
    b.close = c;
    b.volume = v;
    return b;
}

// A longer series with different relative bars, so a reshuffle shows.
BarSeries wavySeries(int n)
{
    BarSeries s;
    s.symbol = "WAVE";
    s.timeframe = "D1";
    double prevClose = 100.0;
    for (int i = 0; i < n; ++i)
    {
        const double gap = 1.0 + 0.002 * ((i * 7) % 5 - 2);
        const double move = 1.0 + 0.01 * ((i * 3) % 7 - 3);
        const double open = prevClose * gap;
        const double close = prevClose * move;
        s.bars.push_back(makeBar(i * kTimestampUnitsPerDay, open, std::max(open, close) * 1.003,
                                 std::min(open, close) * 0.997, close, 10.0 + i));
        prevClose = close;
    }
    return s;
}

struct Relative
{
    double o, h, l, c;
};

std::vector<Relative> relativeBars(const BarSeries& s)
{
    std::vector<Relative> r;
    for (std::size_t i = 1; i < s.bars.size(); ++i)
    {
        const double p = s.bars[i - 1].close;
        r.push_back({s.bars[i].open / p, s.bars[i].high / p, s.bars[i].low / p, s.bars[i].close / p});
    }
    return r;
}

bool sameRelative(const Relative& a, const Relative& b)
{
    return std::abs(a.o - b.o) < 1e-9 && std::abs(a.h - b.h) < 1e-9 && std::abs(a.l - b.l) < 1e-9 &&
           std::abs(a.c - b.c) < 1e-9;
}
}

TEST(ShuffleReturns, KeepsEverythingExceptThePrices)
{
    const BarSeries in = wavySeries(40);
    const BarSeries out = shuffleReturns(in, 1, 5);

    EXPECT_EQ(out.symbol, "WAVE");
    EXPECT_EQ(out.timeframe, "D1");
    ASSERT_EQ(out.bars.size(), in.bars.size());
    for (std::size_t i = 0; i < in.bars.size(); ++i)
    {
        EXPECT_EQ(out.bars[i].timestamp, in.bars[i].timestamp);
        EXPECT_DOUBLE_EQ(out.bars[i].volume, in.bars[i].volume);
    }
}

TEST(ShuffleReturns, TheFirstBarIsUnchanged)
{
    const BarSeries in = wavySeries(40);
    const BarSeries out = shuffleReturns(in, 1, 5);
    EXPECT_DOUBLE_EQ(out.bars[0].open, in.bars[0].open);
    EXPECT_DOUBLE_EQ(out.bars[0].high, in.bars[0].high);
    EXPECT_DOUBLE_EQ(out.bars[0].low, in.bars[0].low);
    EXPECT_DOUBLE_EQ(out.bars[0].close, in.bars[0].close);
}

TEST(ShuffleReturns, TheSameSeedGivesTheSameSeries)
{
    const BarSeries in = wavySeries(40);
    const BarSeries a = shuffleReturns(in, 42, 5);
    const BarSeries b = shuffleReturns(in, 42, 5);
    for (std::size_t i = 0; i < in.bars.size(); ++i)
        EXPECT_DOUBLE_EQ(a.bars[i].close, b.bars[i].close);
}

TEST(ShuffleReturns, DifferentSeedsGiveDifferentSeries)
{
    const BarSeries in = wavySeries(60);
    const BarSeries a = shuffleReturns(in, 1, 3);
    const BarSeries b = shuffleReturns(in, 2, 3);
    bool differsFromOriginal = false, differsBetweenSeeds = false;
    for (std::size_t i = 0; i < in.bars.size(); ++i)
    {
        differsFromOriginal = differsFromOriginal || a.bars[i].close != in.bars[i].close;
        differsBetweenSeeds = differsBetweenSeeds || a.bars[i].close != b.bars[i].close;
    }
    EXPECT_TRUE(differsFromOriginal);
    EXPECT_TRUE(differsBetweenSeeds);
}

TEST(ShuffleReturns, EveryRelativeBarComesFromTheOriginal)
{
    // With blockLength 1 each output bar is one original relative bar, so its ratios to the previous close must match
    // one of the originals' ratios.
    const BarSeries in = wavySeries(50);
    const BarSeries out = shuffleReturns(in, 7, 1);
    const std::vector<Relative> original = relativeBars(in);
    for (const Relative& r : relativeBars(out))
    {
        bool found = false;
        for (const Relative& o : original)
            found = found || sameRelative(r, o);
        EXPECT_TRUE(found);
    }
}

TEST(ShuffleReturns, ABlockKeepsItsBarsTogetherInOrder)
{
    // With blockLength 3, every group of 3 consecutive output bars (after the first) is 3 consecutive original relative
    // bars. Check this on the first block.
    const BarSeries in = wavySeries(50);
    const BarSeries out = shuffleReturns(in, 5, 3);
    const std::vector<Relative> original = relativeBars(in);
    const std::vector<Relative> shuffled = relativeBars(out);

    bool found = false;
    for (std::size_t start = 0; start + 3 <= original.size(); ++start)
        found = found || (sameRelative(shuffled[0], original[start]) && sameRelative(shuffled[1], original[start + 1]) &&
                          sameRelative(shuffled[2], original[start + 2]));
    EXPECT_TRUE(found);
}

TEST(ShuffleReturns, OneBlockCoveringEverythingGivesTheOriginalBack)
{
    // 4 bars -> 3 relative bars. A block of 3 can only start at the first relative bar, so nothing changes.
    BarSeries in;
    in.bars = {makeBar(0, 100, 101, 99, 100), makeBar(1, 102, 112, 100, 110), makeBar(2, 108, 111, 98, 99),
               makeBar(3, 100, 105, 97, 104)};
    const BarSeries out = shuffleReturns(in, 123, 3);
    for (std::size_t i = 0; i < in.bars.size(); ++i)
    {
        EXPECT_NEAR(out.bars[i].open, in.bars[i].open, 1e-9);
        EXPECT_NEAR(out.bars[i].high, in.bars[i].high, 1e-9);
        EXPECT_NEAR(out.bars[i].low, in.bars[i].low, 1e-9);
        EXPECT_NEAR(out.bars[i].close, in.bars[i].close, 1e-9);
    }
}

TEST(ShuffleReturns, ABlockLongerThanTheDataIsShortenedToFit)
{
    BarSeries in;
    in.bars = {makeBar(0, 100, 101, 99, 100), makeBar(1, 102, 112, 100, 110), makeBar(2, 108, 111, 98, 99)};
    const BarSeries out = shuffleReturns(in, 9, 1000);
    ASSERT_EQ(out.bars.size(), 3u);
    EXPECT_NEAR(out.bars[2].close, 99.0, 1e-9);
}

TEST(ShuffleReturns, BarsStayWellFormed)
{
    const BarSeries out = shuffleReturns(wavySeries(80), 11, 4);
    for (const Bar& b : out.bars)
    {
        EXPECT_GT(b.low, 0.0);
        EXPECT_GE(b.high, std::max(b.open, b.close) - 1e-9);
        EXPECT_LE(b.low, std::min(b.open, b.close) + 1e-9);
    }
}

TEST(ShuffleReturns, TinySeriesAreReturnedAsTheyAre)
{
    BarSeries empty;
    EXPECT_TRUE(shuffleReturns(empty, 1, 5).bars.empty());

    BarSeries one;
    one.bars = {makeBar(0, 100, 101, 99, 100)};
    const BarSeries out = shuffleReturns(one, 1, 5);
    ASSERT_EQ(out.bars.size(), 1u);
    EXPECT_DOUBLE_EQ(out.bars[0].close, 100.0);
}

TEST(ShuffleReturns, BadInputIsRejected)
{
    EXPECT_THROW(shuffleReturns(wavySeries(10), 1, 0), std::invalid_argument);
    EXPECT_THROW(shuffleReturns(wavySeries(10), 1, -2), std::invalid_argument);

    BarSeries bad;
    bad.bars = {makeBar(0, 100, 101, 99, 0.0), makeBar(1, 100, 101, 99, 100)};   // a zero close cannot be a divisor
    EXPECT_THROW(shuffleReturns(bad, 1, 1), std::invalid_argument);
}
