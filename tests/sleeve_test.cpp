#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "aligned.hpp"
#include "sleeve.hpp"

// These tests define how the sleeve helpers must behave (see include/sleeve.hpp).
//
// Rules:
//   1. assetIndices finds each symbol's position, in the order asked; it throws on an empty list, an unknown symbol
//      or a symbol given twice.
//   2. selectAssets keeps every snapshot and timestamp, keeps only the chosen assets' bars, renumbers the assets in the
//      order given, and throws on an empty list, an index past the end or an index given twice.
//   3. selectSpreads / selectSwap slice per-asset lists the same way, so asset k of the slice has asset indices[k]'s costs.

namespace
{
Bar barAt(std::int64_t time, double price)
{
    Bar b;
    b.timestamp = time;
    b.open = b.high = b.low = b.close = price;
    return b;
}

// Three assets (EURUSD, GBPUSD, XAUUSD) over three snapshots. Price = asset * 100 + day, so a bar tells where it came from.
// GBPUSD has no bar on day 1.
AlignedData sampleData()
{
    AlignedData data;
    data.symbols = {"EURUSD", "GBPUSD", "XAUUSD"};
    for (int day = 0; day < 3; ++day)
    {
        Snapshot s;
        s.timestamp = 1000 * (day + 1);
        for (int asset = 0; asset < 3; ++asset)
        {
            if (asset == 1 && day == 1)
                s.bars.push_back(std::nullopt);
            else
                s.bars.push_back(barAt(s.timestamp, asset * 100 + day));
        }
        data.snapshots.push_back(s);
    }
    return data;
}
}

// ---------- assetIndices ----------

TEST(AssetIndices, FindsEachSymbolInTheOrderAsked)
{
    const AlignedData data = sampleData();
    EXPECT_EQ(assetIndices(data, {"XAUUSD", "EURUSD"}), (std::vector<std::size_t>{2, 0}));
}

TEST(AssetIndices, OneSymbol)
{
    EXPECT_EQ(assetIndices(sampleData(), {"GBPUSD"}), (std::vector<std::size_t>{1}));
}

TEST(AssetIndices, UnknownSymbolThrows)
{
    EXPECT_THROW(assetIndices(sampleData(), {"EURUSD", "USDJPY"}), std::invalid_argument);
}

TEST(AssetIndices, EmptyListThrows)
{
    EXPECT_THROW(assetIndices(sampleData(), {}), std::invalid_argument);
}

TEST(AssetIndices, SymbolGivenTwiceThrows)
{
    EXPECT_THROW(assetIndices(sampleData(), {"EURUSD", "EURUSD"}), std::invalid_argument);
}

// ---------- selectAssets ----------

TEST(SelectAssets, KeepsOnlyTheChosenSymbolsInTheOrderGiven)
{
    const AlignedData out = selectAssets(sampleData(), {2, 0});
    EXPECT_EQ(out.symbols, (std::vector<std::string>{"XAUUSD", "EURUSD"}));
}

TEST(SelectAssets, KeepsEverySnapshotAndTimestamp)
{
    const AlignedData data = sampleData();
    const AlignedData out = selectAssets(data, {1});   // GBPUSD, which has a gap on day 1

    ASSERT_EQ(out.snapshots.size(), data.snapshots.size());
    for (std::size_t i = 0; i < data.snapshots.size(); ++i)
        EXPECT_EQ(out.snapshots[i].timestamp, data.snapshots[i].timestamp);
}

TEST(SelectAssets, EachSnapshotHasOneSlotPerChosenAsset)
{
    const AlignedData out = selectAssets(sampleData(), {0, 2});
    for (const Snapshot& s : out.snapshots)
        EXPECT_EQ(s.bars.size(), 2u);
}

TEST(SelectAssets, BarsAreTheChosenAssetsOwnBarsRenumbered)
{
    const AlignedData out = selectAssets(sampleData(), {2, 0});

    // new asset 0 = XAUUSD (prices 200..202), new asset 1 = EURUSD (prices 0..2)
    for (int day = 0; day < 3; ++day)
    {
        ASSERT_TRUE(out.snapshots[day].bars[0].has_value());
        ASSERT_TRUE(out.snapshots[day].bars[1].has_value());
        EXPECT_DOUBLE_EQ(out.snapshots[day].bars[0]->close, 200 + day);
        EXPECT_DOUBLE_EQ(out.snapshots[day].bars[1]->close, 0 + day);
    }
}

TEST(SelectAssets, AMissingBarStaysMissing)
{
    const AlignedData out = selectAssets(sampleData(), {1});
    EXPECT_TRUE(out.snapshots[0].bars[0].has_value());
    EXPECT_FALSE(out.snapshots[1].bars[0].has_value());
    EXPECT_TRUE(out.snapshots[2].bars[0].has_value());
}

TEST(SelectAssets, DoesNotChangeTheOriginal)
{
    const AlignedData data = sampleData();
    selectAssets(data, {1});
    EXPECT_EQ(data.symbols.size(), 3u);
    EXPECT_EQ(data.snapshots[0].bars.size(), 3u);
}

TEST(SelectAssets, EmptyListThrows)
{
    EXPECT_THROW(selectAssets(sampleData(), {}), std::invalid_argument);
}

TEST(SelectAssets, IndexPastTheEndThrows)
{
    EXPECT_THROW(selectAssets(sampleData(), {0, 3}), std::invalid_argument);
}

TEST(SelectAssets, IndexGivenTwiceThrows)
{
    EXPECT_THROW(selectAssets(sampleData(), {1, 1}), std::invalid_argument);
}

// ---------- selectSpreads ----------

TEST(SelectSpreads, PicksTheChosenAssetsSpreadsInOrder)
{
    const std::vector<double> out = selectSpreads({0.1, 0.2, 0.3}, {2, 0});
    EXPECT_EQ(out, (std::vector<double>{0.3, 0.1}));
}

TEST(SelectSpreads, EmptyListStaysEmpty)
{
    EXPECT_TRUE(selectSpreads({}, {0, 1}).empty());
}

TEST(SelectSpreads, AssetPastTheEndOfAShortListGetsZero)
{
    const std::vector<double> out = selectSpreads({0.1}, {0, 2});
    EXPECT_EQ(out, (std::vector<double>{0.1, 0.0}));
}

// ---------- selectSwap ----------

TEST(SelectSwap, PicksTheChosenAssetsRowsInOrder)
{
    SwapSchedule swap;
    swap.differentialPct = {{1, 1, 1}, {2, 2, 2}, {3, 3, 3}};
    swap.markupPct = 0.5;

    const SwapSchedule out = selectSwap(swap, {2, 0});

    ASSERT_EQ(out.differentialPct.size(), 2u);
    EXPECT_EQ(out.differentialPct[0], (std::vector<double>{3, 3, 3}));
    EXPECT_EQ(out.differentialPct[1], (std::vector<double>{1, 1, 1}));
}

TEST(SelectSwap, KeepsTheMarkup)
{
    SwapSchedule swap;
    swap.differentialPct = {{1}, {2}};
    swap.markupPct = 0.75;

    EXPECT_DOUBLE_EQ(selectSwap(swap, {1}).markupPct, 0.75);
}

TEST(SelectSwap, MarkupOnlyScheduleStaysMarkupOnly)
{
    SwapSchedule swap;
    swap.markupPct = 0.5;   // no differentials

    const SwapSchedule out = selectSwap(swap, {0, 1});

    EXPECT_TRUE(out.differentialPct.empty());
    EXPECT_DOUBLE_EQ(out.markupPct, 0.5);
    EXPECT_TRUE(out.active());
}

TEST(SelectSwap, InactiveScheduleStaysInactive)
{
    const SwapSchedule out = selectSwap(SwapSchedule{}, {0});
    EXPECT_FALSE(out.active());
}

TEST(SelectSwap, AssetWithoutARowGetsAnEmptyRow)
{
    SwapSchedule swap;
    swap.differentialPct = {{1, 1}};   // only asset 0 has rates

    const SwapSchedule out = selectSwap(swap, {0, 1});

    ASSERT_EQ(out.differentialPct.size(), 2u);
    EXPECT_EQ(out.differentialPct[0], (std::vector<double>{1, 1}));
    EXPECT_TRUE(out.differentialPct[1].empty());
}
