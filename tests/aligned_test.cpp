#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "aligned.hpp"
#include "timestamp.hpp"

// These tests define how align() must behave.
//
// Expected API (include/aligned.hpp):
//
//   struct Snapshot {
//       std::int64_t timestamp{};                  // the timestamp shared by the bars in this snapshot
//       std::vector<std::optional<Bar>> bars;      // one slot per series; empty = no bar at that timestamp
//   };
//   struct AlignedData {
//       std::vector<std::string> symbols;          // symbols[i] is series[i].symbol
//       std::vector<Snapshot>    snapshots;        // ascending by timestamp
//   };
//   AlignedData align(const std::vector<BarSeries>& series);
//
// Rules (simplest version: all series come from one source, so the same day has the same timestamp):
//   1. Bars are matched on their exact timestamp.
//   2. The result has one snapshot for every timestamp that appears in ANY series (union),
//      ascending. Timestamps no series has are not invented (no weekend snapshots).
//   3. snapshot.bars has exactly one slot per input series, in input order. The slot holds that
//      series' bar for that timestamp, unchanged, or is empty when the series has no such bar.
//   4. Throws std::invalid_argument if a series' timestamps are not strictly increasing
//      (an unsorted series or a repeated timestamp).

namespace
{
// A bar stamped at `when` (parsed as UTC) with all four prices equal to `price`.
Bar barAt(const char* when, double price)
{
    Bar b;
    b.timestamp = parseTimestamp(when);
    b.open = b.high = b.low = b.close = price;
    return b;
}

BarSeries makeSeries(const std::string& symbol, std::vector<Bar> bars)
{
    BarSeries s;
    s.symbol = symbol;
    s.bars = std::move(bars);
    return s;
}

std::int64_t ts(const char* when)
{
    return parseTimestamp(when);
}
}

// ---------- shape of the result ----------

TEST(AlignTest, NoSeriesGivesEmptyResult)
{
    AlignedData a = align({});

    EXPECT_TRUE(a.symbols.empty());
    EXPECT_TRUE(a.snapshots.empty());
}

TEST(AlignTest, SymbolsFollowInputOrder)
{
    AlignedData a = align({makeSeries("EURUSD", {}), makeSeries("GBPUSD", {}), makeSeries("EURGBP", {})});

    ASSERT_EQ(a.symbols.size(), 3u);
    EXPECT_EQ(a.symbols[0], "EURUSD");
    EXPECT_EQ(a.symbols[1], "GBPUSD");
    EXPECT_EQ(a.symbols[2], "EURGBP");
}

TEST(AlignTest, SingleSeriesGivesOneSnapshotPerBar)
{
    AlignedData a = align({makeSeries("EURGBP", {
        barAt("2025-01-01 22:04:00", 1.0),
        barAt("2025-01-02 22:04:00", 2.0),
        barAt("2025-01-03 22:04:00", 3.0),
    })});

    ASSERT_EQ(a.snapshots.size(), 3u);
    for (const Snapshot& s : a.snapshots)
        EXPECT_EQ(s.bars.size(), 1u);
}

TEST(AlignTest, SnapshotTimestampIsTheBarTimestamp)
{
    AlignedData a = align({makeSeries("EURGBP", {barAt("2025-01-01 22:04:00", 1.0)})});

    ASSERT_EQ(a.snapshots.size(), 1u);
    EXPECT_EQ(a.snapshots[0].timestamp, ts("2025-01-01 22:04:00"));
}

TEST(AlignTest, BarsAreStoredUnchanged)
{
    Bar original;
    original.timestamp = ts("2025-01-01 22:04:00");
    original.open = 1.1;
    original.high = 1.4;
    original.low = 1.0;
    original.close = 1.3;

    AlignedData a = align({makeSeries("EURGBP", {original})});

    ASSERT_TRUE(a.snapshots[0].bars[0].has_value());
    const Bar& stored = *a.snapshots[0].bars[0];
    EXPECT_EQ(stored.timestamp, original.timestamp);
    EXPECT_DOUBLE_EQ(stored.open, 1.1);
    EXPECT_DOUBLE_EQ(stored.high, 1.4);
    EXPECT_DOUBLE_EQ(stored.low, 1.0);
    EXPECT_DOUBLE_EQ(stored.close, 1.3);
}

// ---------- matching ----------

TEST(AlignTest, SeriesWithTheSameTimestampsShareSnapshots)
{
    AlignedData a = align({
        makeSeries("A", {barAt("2025-01-01 22:04:00", 1), barAt("2025-01-02 22:04:00", 2)}),
        makeSeries("B", {barAt("2025-01-01 22:04:00", 10), barAt("2025-01-02 22:04:00", 20)}),
    });

    ASSERT_EQ(a.snapshots.size(), 2u);
    for (const Snapshot& s : a.snapshots)
    {
        ASSERT_EQ(s.bars.size(), 2u);
        EXPECT_TRUE(s.bars[0].has_value());
        EXPECT_TRUE(s.bars[1].has_value());
    }
    EXPECT_DOUBLE_EQ(a.snapshots[0].bars[0]->close, 1);
    EXPECT_DOUBLE_EQ(a.snapshots[0].bars[1]->close, 10);
    EXPECT_DOUBLE_EQ(a.snapshots[1].bars[0]->close, 2);
    EXPECT_DOUBLE_EQ(a.snapshots[1].bars[1]->close, 20);
}

TEST(AlignTest, TwentySeriesGetTwentySlots)
{
    std::vector<BarSeries> all;
    for (int i = 0; i < 20; ++i)
        all.push_back(makeSeries("S" + std::to_string(i), {
            barAt("2025-01-01 22:04:00", i),
            barAt("2025-01-02 22:04:00", i + 100),
        }));

    AlignedData a = align(all);

    ASSERT_EQ(a.symbols.size(), 20u);
    ASSERT_EQ(a.snapshots.size(), 2u);
    for (int i = 0; i < 20; ++i)
    {
        ASSERT_TRUE(a.snapshots[0].bars[i].has_value());
        EXPECT_DOUBLE_EQ(a.snapshots[0].bars[i]->close, i);
        EXPECT_DOUBLE_EQ(a.snapshots[1].bars[i]->close, i + 100);
    }
}

TEST(AlignTest, DifferentTimestampsOnTheSameDateDoNotMatch)
{
    // Exact matching is the deliberate simple rule: 21:04 and 22:04 are different timestamps.
    AlignedData a = align({
        makeSeries("A", {barAt("2025-03-10 22:04:00", 1)}),
        makeSeries("B", {barAt("2025-03-10 21:04:00", 2)}),
    });

    ASSERT_EQ(a.snapshots.size(), 2u);
    EXPECT_EQ(a.snapshots[0].timestamp, ts("2025-03-10 21:04:00"));
    EXPECT_FALSE(a.snapshots[0].bars[0].has_value());
    EXPECT_TRUE(a.snapshots[0].bars[1].has_value());
    EXPECT_TRUE(a.snapshots[1].bars[0].has_value());
    EXPECT_FALSE(a.snapshots[1].bars[1].has_value());
}

TEST(AlignTest, ClockChangeInsideBothSeriesStillAligns)
{
    // Both series switch from 22:04 to 21:04 on the same days, like the real data does.
    AlignedData a = align({
        makeSeries("A", {barAt("2025-03-07 22:04:00", 1), barAt("2025-03-10 21:04:00", 2)}),
        makeSeries("B", {barAt("2025-03-07 22:04:00", 3), barAt("2025-03-10 21:04:00", 4)}),
    });

    ASSERT_EQ(a.snapshots.size(), 2u);
    for (const Snapshot& s : a.snapshots)
    {
        EXPECT_TRUE(s.bars[0].has_value());
        EXPECT_TRUE(s.bars[1].has_value());
    }
}

// ---------- gaps ----------

TEST(AlignTest, MissingBarLeavesAnEmptySlot)
{
    AlignedData a = align({
        makeSeries("A", {barAt("2025-01-01 22:04:00", 1), barAt("2025-01-02 22:04:00", 2)}),
        makeSeries("B", {barAt("2025-01-02 22:04:00", 20)}),
    });

    ASSERT_EQ(a.snapshots.size(), 2u);
    EXPECT_TRUE(a.snapshots[0].bars[0].has_value());
    EXPECT_FALSE(a.snapshots[0].bars[1].has_value());
    EXPECT_TRUE(a.snapshots[1].bars[1].has_value());
}

TEST(AlignTest, ResultCoversTheUnionOfTimestamps)
{
    AlignedData a = align({
        makeSeries("A", {barAt("2025-01-01 22:04:00", 1), barAt("2025-01-03 22:04:00", 3)}),
        makeSeries("B", {barAt("2025-01-02 22:04:00", 20), barAt("2025-01-04 22:04:00", 40)}),
    });

    ASSERT_EQ(a.snapshots.size(), 4u);
    EXPECT_EQ(a.snapshots[0].timestamp, ts("2025-01-01 22:04:00"));
    EXPECT_EQ(a.snapshots[1].timestamp, ts("2025-01-02 22:04:00"));
    EXPECT_EQ(a.snapshots[2].timestamp, ts("2025-01-03 22:04:00"));
    EXPECT_EQ(a.snapshots[3].timestamp, ts("2025-01-04 22:04:00"));
}

TEST(AlignTest, EmptySeriesGivesEmptySlotsEverywhere)
{
    AlignedData a = align({
        makeSeries("A", {barAt("2025-01-01 22:04:00", 1), barAt("2025-01-02 22:04:00", 2)}),
        makeSeries("EMPTY", {}),
    });

    ASSERT_EQ(a.snapshots.size(), 2u);
    for (const Snapshot& s : a.snapshots)
    {
        ASSERT_EQ(s.bars.size(), 2u);
        EXPECT_TRUE(s.bars[0].has_value());
        EXPECT_FALSE(s.bars[1].has_value());
    }
}

TEST(AlignTest, EverySeriesEmptyGivesNoSnapshots)
{
    AlignedData a = align({makeSeries("A", {}), makeSeries("B", {})});

    EXPECT_EQ(a.symbols.size(), 2u);
    EXPECT_TRUE(a.snapshots.empty());
}

TEST(AlignTest, DifferentSeriesCanBeMissingDifferentDays)
{
    AlignedData a = align({
        makeSeries("A", {barAt("2025-01-01 22:04:00", 1), barAt("2025-01-03 22:04:00", 3)}),
        makeSeries("B", {barAt("2025-01-01 22:04:00", 10), barAt("2025-01-02 22:04:00", 20)}),
        makeSeries("C", {barAt("2025-01-02 22:04:00", 200), barAt("2025-01-03 22:04:00", 300)}),
    });

    ASSERT_EQ(a.snapshots.size(), 3u);

    // day 1: A, B present; C missing
    EXPECT_TRUE(a.snapshots[0].bars[0].has_value());
    EXPECT_TRUE(a.snapshots[0].bars[1].has_value());
    EXPECT_FALSE(a.snapshots[0].bars[2].has_value());
    // day 2: B, C present; A missing
    EXPECT_FALSE(a.snapshots[1].bars[0].has_value());
    EXPECT_TRUE(a.snapshots[1].bars[1].has_value());
    EXPECT_TRUE(a.snapshots[1].bars[2].has_value());
    // day 3: A, C present; B missing
    EXPECT_TRUE(a.snapshots[2].bars[0].has_value());
    EXPECT_FALSE(a.snapshots[2].bars[1].has_value());
    EXPECT_TRUE(a.snapshots[2].bars[2].has_value());
}

// ---------- ordering ----------

TEST(AlignTest, SnapshotsAreInAscendingOrderEvenWhenSeriesArePassedInAnyOrder)
{
    AlignedData a = align({
        makeSeries("LATE", {barAt("2025-01-05 22:04:00", 5), barAt("2025-01-06 22:04:00", 6)}),
        makeSeries("EARLY", {barAt("2025-01-01 22:04:00", 1), barAt("2025-01-06 22:04:00", 60)}),
    });

    ASSERT_EQ(a.snapshots.size(), 3u);
    for (std::size_t i = 1; i < a.snapshots.size(); ++i)
        EXPECT_LT(a.snapshots[i - 1].timestamp, a.snapshots[i].timestamp);
}

TEST(AlignTest, TimestampsNoSeriesHasAreNotInvented)
{
    // Friday and Monday: no Saturday or Sunday snapshots.
    AlignedData a = align({makeSeries("A", {
        barAt("2025-01-03 22:04:00", 1),   // Friday
        barAt("2025-01-06 22:04:00", 2),   // Monday
    })});

    EXPECT_EQ(a.snapshots.size(), 2u);
}

// ---------- bad input ----------

TEST(AlignTest, RepeatedTimestampInOneSeriesThrows)
{
    std::vector<BarSeries> input = {
        makeSeries("A", {barAt("2025-01-01 22:04:00", 1), barAt("2025-01-01 22:04:00", 2)}),
    };

    EXPECT_THROW(align(input), std::invalid_argument);
}

TEST(AlignTest, UnsortedSeriesThrows)
{
    std::vector<BarSeries> input = {
        makeSeries("A", {barAt("2025-01-02 22:04:00", 2), barAt("2025-01-01 22:04:00", 1)}),
    };

    EXPECT_THROW(align(input), std::invalid_argument);
}

TEST(AlignTest, ABadSeriesThrowsEvenWhenOthersAreFine)
{
    std::vector<BarSeries> input = {
        makeSeries("GOOD", {barAt("2025-01-01 22:04:00", 1), barAt("2025-01-02 22:04:00", 2)}),
        makeSeries("BAD", {barAt("2025-01-01 22:04:00", 1), barAt("2025-01-01 22:04:00", 1)}),
    };

    EXPECT_THROW(align(input), std::invalid_argument);
}
