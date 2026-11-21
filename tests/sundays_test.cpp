#include <gtest/gtest.h>

#include <vector>

#include "sundays.hpp"

// FX data cut at midnight UTC has a tiny Sunday bar before each Monday. mergeSundayBars folds it into the
// Monday bar: Sunday's open, the highest high, the lowest low, Monday's close, Monday's timestamp.
//
// Real dates (UTC), in milliseconds:
//   2016-01-03 Sunday = 1451779200000      then Monday, Tuesday ... each + 86400000

namespace
{
const std::int64_t kDay = 86400000;
const std::int64_t kSunday = 1451779200000LL;          // 2016-01-03
const std::int64_t kMonday = kSunday + kDay;
const std::int64_t kTuesday = kSunday + 2 * kDay;
const std::int64_t kFriday = kSunday + 5 * kDay;
const std::int64_t kNextSunday = kSunday + 7 * kDay;

Bar bar(std::int64_t time, double open, double high, double low, double close)
{
    Bar b;
    b.timestamp = time;
    b.open = open;
    b.high = high;
    b.low = low;
    b.close = close;
    return b;
}

BarSeries seriesOf(std::vector<Bar> bars)
{
    BarSeries s;
    s.symbol = "EURUSD";
    s.timeframe = "1d";
    s.bars = std::move(bars);
    return s;
}
}

// ---------- isSunday ----------

TEST(IsSundayTest, RecognisesASunday)
{
    EXPECT_TRUE(isSunday(kSunday));
    EXPECT_TRUE(isSunday(kNextSunday));
}

TEST(IsSundayTest, OtherDaysAreNot)
{
    for (int d = 1; d <= 6; ++d)
        EXPECT_FALSE(isSunday(kSunday + d * kDay)) << d;
}

TEST(IsSundayTest, TheWholeUtcDayCounts)
{
    EXPECT_TRUE(isSunday(kSunday + kDay - 1));              // 23:59:59.999
    EXPECT_FALSE(isSunday(kSunday + kDay));                 // Monday 00:00
    EXPECT_FALSE(isSunday(kSunday - 1));                    // Saturday 23:59:59.999
}

TEST(IsSundayTest, WorksBefore1970)
{
    // 1969-12-28 was a Sunday: 4 days before the epoch (a Thursday)
    EXPECT_TRUE(isSunday(-4 * kDay));
    EXPECT_FALSE(isSunday(-3 * kDay));
    EXPECT_TRUE(isSunday(-4 * kDay + kDay - 1));
    EXPECT_FALSE(isSunday(-5 * kDay + kDay - 1));
}

TEST(IsSundayTest, TheEpochItselfIsAThursday)
{
    EXPECT_FALSE(isSunday(0));
}

// ---------- mergeSundayBars ----------

TEST(MergeSundayBarsTest, FoldsASundayIntoTheMonday)
{
    const BarSeries merged = mergeSundayBars(seriesOf({
        bar(kSunday, 1.00, 1.10, 0.95, 1.05),
        bar(kMonday, 1.05, 1.20, 1.00, 1.15)}));

    ASSERT_EQ(merged.bars.size(), 1u);
    const Bar& b = merged.bars[0];
    EXPECT_EQ(b.timestamp, kMonday);
    EXPECT_DOUBLE_EQ(b.open, 1.00);        // Sunday's open
    EXPECT_DOUBLE_EQ(b.high, 1.20);        // higher of 1.10 and 1.20
    EXPECT_DOUBLE_EQ(b.low, 0.95);         // lower of 0.95 and 1.00
    EXPECT_DOUBLE_EQ(b.close, 1.15);       // Monday's close
}

TEST(MergeSundayBarsTest, TheHighAndLowCanComeFromSunday)
{
    const BarSeries merged = mergeSundayBars(seriesOf({
        bar(kSunday, 1.00, 1.50, 0.50, 1.05),
        bar(kMonday, 1.05, 1.20, 1.00, 1.15)}));

    EXPECT_DOUBLE_EQ(merged.bars[0].high, 1.50);
    EXPECT_DOUBLE_EQ(merged.bars[0].low, 0.50);
}

TEST(MergeSundayBarsTest, TheLowAndHighCanComeFromMonday)
{
    const BarSeries merged = mergeSundayBars(seriesOf({
        bar(kSunday, 1.00, 1.10, 1.00, 1.05),
        bar(kMonday, 1.05, 1.30, 0.90, 1.15)}));

    EXPECT_DOUBLE_EQ(merged.bars[0].low, 0.90);
    EXPECT_DOUBLE_EQ(merged.bars[0].high, 1.30);
}

TEST(MergeSundayBarsTest, AFullWeekBecomesFiveBars)
{
    std::vector<Bar> week{bar(kSunday, 1, 1, 1, 1)};
    for (int d = 1; d <= 5; ++d)
        week.push_back(bar(kSunday + d * kDay, 1, 2, 0.5, 1.5));

    const BarSeries merged = mergeSundayBars(seriesOf(week));

    ASSERT_EQ(merged.bars.size(), 5u);
    EXPECT_EQ(merged.bars.front().timestamp, kMonday);
    EXPECT_EQ(merged.bars.back().timestamp, kFriday);
}

TEST(MergeSundayBarsTest, SeveralWeeksEachGetTheirOwnMerge)
{
    const BarSeries merged = mergeSundayBars(seriesOf({
        bar(kSunday, 10, 10, 10, 10),     bar(kMonday, 11, 12, 11, 12),
        bar(kTuesday, 12, 13, 12, 13),
        bar(kNextSunday, 20, 20, 20, 20), bar(kNextSunday + kDay, 21, 22, 21, 22)}));

    ASSERT_EQ(merged.bars.size(), 3u);
    EXPECT_DOUBLE_EQ(merged.bars[0].open, 10);
    EXPECT_DOUBLE_EQ(merged.bars[1].open, 12);          // Tuesday untouched
    EXPECT_DOUBLE_EQ(merged.bars[2].open, 20);
    EXPECT_DOUBLE_EQ(merged.bars[2].close, 22);
}

TEST(MergeSundayBarsTest, WeekdayBarsAreLeftExactlyAsTheyAre)
{
    const std::vector<Bar> bars{bar(kMonday, 1, 2, 0.5, 1.5), bar(kTuesday, 1.5, 2.5, 1, 2)};

    const BarSeries merged = mergeSundayBars(seriesOf(bars));

    ASSERT_EQ(merged.bars.size(), 2u);
    for (std::size_t i = 0; i < 2; ++i)
    {
        EXPECT_EQ(merged.bars[i].timestamp, bars[i].timestamp);
        EXPECT_DOUBLE_EQ(merged.bars[i].open, bars[i].open);
        EXPECT_DOUBLE_EQ(merged.bars[i].high, bars[i].high);
        EXPECT_DOUBLE_EQ(merged.bars[i].low, bars[i].low);
        EXPECT_DOUBLE_EQ(merged.bars[i].close, bars[i].close);
    }
}

TEST(MergeSundayBarsTest, ASundayWithoutAMondayBarIsDropped)
{
    // the next bar is Tuesday: a Monday is missing, so there is nothing to join the Sunday to
    const BarSeries merged = mergeSundayBars(seriesOf({
        bar(kSunday, 1, 1, 1, 1),
        bar(kTuesday, 2, 2, 2, 2)}));

    ASSERT_EQ(merged.bars.size(), 1u);
    EXPECT_EQ(merged.bars[0].timestamp, kTuesday);
    EXPECT_DOUBLE_EQ(merged.bars[0].open, 2);           // not changed by the dropped Sunday
}

TEST(MergeSundayBarsTest, ATrailingSundayIsDropped)
{
    const BarSeries merged = mergeSundayBars(seriesOf({
        bar(kMonday, 1, 1, 1, 1),
        bar(kNextSunday, 2, 2, 2, 2)}));

    ASSERT_EQ(merged.bars.size(), 1u);
    EXPECT_EQ(merged.bars[0].timestamp, kMonday);
}

TEST(MergeSundayBarsTest, ALeadingSundayIsMergedNormally)
{
    const BarSeries merged = mergeSundayBars(seriesOf({
        bar(kSunday, 5, 5, 5, 5),
        bar(kMonday, 6, 7, 6, 7)}));

    ASSERT_EQ(merged.bars.size(), 1u);
    EXPECT_DOUBLE_EQ(merged.bars[0].open, 5);
}

TEST(MergeSundayBarsTest, SymbolAndTimeframeAreKept)
{
    const BarSeries merged = mergeSundayBars(seriesOf({bar(kMonday, 1, 1, 1, 1)}));

    EXPECT_EQ(merged.symbol, "EURUSD");
    EXPECT_EQ(merged.timeframe, "1d");
}

TEST(MergeSundayBarsTest, AnEmptySeriesStaysEmpty)
{
    EXPECT_TRUE(mergeSundayBars(seriesOf({})).bars.empty());
}

TEST(MergeSundayBarsTest, DataWithoutSundaysComesBackUnchanged)
{
    const BarSeries in = seriesOf({bar(kMonday, 1, 2, 0.5, 1.5), bar(kTuesday, 1, 2, 0.5, 1.5),
                                   bar(kFriday, 1, 2, 0.5, 1.5)});

    EXPECT_EQ(mergeSundayBars(in).bars.size(), in.bars.size());
}

TEST(MergeSundayBarsTest, TheInputIsNotChanged)
{
    const BarSeries in = seriesOf({bar(kSunday, 1, 1, 1, 1), bar(kMonday, 2, 2, 2, 2)});

    mergeSundayBars(in);

    EXPECT_EQ(in.bars.size(), 2u);
    EXPECT_DOUBLE_EQ(in.bars[1].open, 2);
}

TEST(MergeSundayBarsTest, TheResultIsStillSortedAndHasNoSundays)
{
    std::vector<Bar> bars;
    for (int d = 0; d < 70; ++d)                       // ten weeks of 7 days, every day present
        bars.push_back(bar(kSunday + d * kDay, 1, 2, 0.5, 1.5));

    const BarSeries merged = mergeSundayBars(seriesOf(bars));

    for (std::size_t i = 0; i < merged.bars.size(); ++i)
    {
        EXPECT_FALSE(isSunday(merged.bars[i].timestamp));
        if (i > 0)
            EXPECT_GT(merged.bars[i].timestamp, merged.bars[i - 1].timestamp);
    }
    EXPECT_EQ(merged.bars.size(), 60u);                // Saturdays stay, Sundays go: 6 per week
}

TEST(MergeSundayBarsTest, TheVolumesAreAdded)
{
    Bar sunday = bar(kSunday, 1.0, 1.2, 0.9, 1.1);
    sunday.volume = 150.0;
    Bar monday = bar(kMonday, 1.1, 1.5, 1.0, 1.4);
    monday.volume = 9000.0;
    Bar tuesday = bar(kTuesday, 1.4, 1.6, 1.3, 1.5);
    tuesday.volume = 7000.0;

    const BarSeries out = mergeSundayBars(seriesOf({sunday, monday, tuesday}));

    ASSERT_EQ(out.bars.size(), 2u);
    EXPECT_DOUBLE_EQ(out.bars[0].volume, 9150.0);          // Sunday + Monday
    EXPECT_DOUBLE_EQ(out.bars[1].volume, 7000.0);          // an ordinary day is untouched
}
