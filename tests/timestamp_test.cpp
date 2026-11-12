#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>

#include "timestamp.hpp"

// ---------- parseTimestamp: accepted formats ----------

TEST(ParseTimestampTest, EpochSeconds)
{
    EXPECT_EQ(parseTimestamp("1735689600"), 1735689600);
    EXPECT_EQ(parseTimestamp("0"), 0);
}

TEST(ParseTimestampTest, NegativeEpochSeconds)
{
    EXPECT_EQ(parseTimestamp("-5"), -5);
}

TEST(ParseTimestampTest, DateAndTime)
{
    EXPECT_EQ(parseTimestamp("2025-01-01 22:04:00"), 1735769040);
}

TEST(ParseTimestampTest, DateOnlyIsMidnightUtc)
{
    EXPECT_EQ(parseTimestamp("2025-01-02"), 1735776000);
    EXPECT_EQ(parseTimestamp("1970-01-01"), 0);
}

TEST(ParseTimestampTest, TSeparatorIsAccepted)
{
    EXPECT_EQ(parseTimestamp("2025-01-01T22:04:00"), 1735769040);
}

TEST(ParseTimestampTest, SurroundingQuotesAreIgnored)
{
    EXPECT_EQ(parseTimestamp("\"2025-01-01 22:04:00\""), 1735769040);
    EXPECT_EQ(parseTimestamp("\"1735689600\""), 1735689600);
}

TEST(ParseTimestampTest, LastSecondOfTheDay)
{
    EXPECT_EQ(parseTimestamp("2025-01-01 23:59:59"), 1735689600 + 86399);
}

// ---------- parseTimestamp: calendar rules ----------

TEST(ParseTimestampTest, LeapDayIsValidInLeapYears)
{
    EXPECT_EQ(parseTimestamp("2024-02-29"), 1709164800);
    EXPECT_NO_THROW(parseTimestamp("2000-02-29"));   // divisible by 400
}

TEST(ParseTimestampTest, LeapDayIsInvalidInOtherYears)
{
    EXPECT_THROW(parseTimestamp("2023-02-29"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("1900-02-29"), std::invalid_argument);   // divisible by 100, not 400
}

TEST(ParseTimestampTest, ConsecutiveDaysDifferBy86400)
{
    EXPECT_EQ(parseTimestamp("2024-02-29") - parseTimestamp("2024-02-28"), 86400);
    EXPECT_EQ(parseTimestamp("2024-03-01") - parseTimestamp("2024-02-29"), 86400);
    EXPECT_EQ(parseTimestamp("2025-01-01") - parseTimestamp("2024-12-31"), 86400);
}

// ---------- parseTimestamp: rejected input ----------

TEST(ParseTimestampTest, ImpossibleMonthOrDayThrows)
{
    EXPECT_THROW(parseTimestamp("2025-13-01"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("2025-00-10"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("2025-01-00"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("2025-04-31"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("2025-02-30"), std::invalid_argument);
}

TEST(ParseTimestampTest, ImpossibleTimeThrows)
{
    EXPECT_THROW(parseTimestamp("2025-01-01 24:00:00"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("2025-01-01 12:60:00"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("2025-01-01 12:00:60"), std::invalid_argument);
}

TEST(ParseTimestampTest, MalformedTextThrows)
{
    EXPECT_THROW(parseTimestamp(""), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("abc"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("12abc"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("1.5"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("2025/01/02"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("2025-1-1"), std::invalid_argument);
    EXPECT_THROW(parseTimestamp("2025-01-01 12:00"), std::invalid_argument);       // no seconds
    EXPECT_THROW(parseTimestamp("2025-01-01X12:00:00"), std::invalid_argument);    // bad separator
    EXPECT_THROW(parseTimestamp("2025-01-02 22:04:00 "), std::invalid_argument);   // trailing space
    EXPECT_THROW(parseTimestamp("\"2025-01-02"), std::invalid_argument);           // unclosed quote
}

TEST(ParseTimestampTest, EpochOutOfRangeThrows)
{
    EXPECT_THROW(parseTimestamp("9999999999999999999999"), std::invalid_argument);
}

// ---------- tradingDay ----------
//
// 2025-01-02 is day 20090 since 1970-01-01, and 2025-07-02 is day 20271.

TEST(TradingDayTest, WinterCloseStampMapsToTheNextCalendarDay)
{
    // NY 5pm close in winter: 22:04 UTC on Jan 1 is the trading day of Jan 2.
    EXPECT_EQ(tradingDay(parseTimestamp("2025-01-01 22:04:00"), kFxDailyCloseOffsetSeconds), 20090);
}

TEST(TradingDayTest, SummerCloseStampMapsToTheNextCalendarDay)
{
    // NY 5pm close in summer: 21:04 UTC on Jul 1 is the trading day of Jul 2.
    EXPECT_EQ(tradingDay(parseTimestamp("2025-07-01 21:04:00"), kFxDailyCloseOffsetSeconds), 20271);
}

TEST(TradingDayTest, WinterAndSummerCloseTimesOfTheSameDayGetTheSameDay)
{
    const std::int64_t a = parseTimestamp("2025-03-10 21:04:00");   // after the US clocks change
    const std::int64_t b = parseTimestamp("2025-03-10 22:04:00");   // before it, same calendar date

    EXPECT_NE(a, b);   // the raw timestamps differ ...
    EXPECT_EQ(tradingDay(a, kFxDailyCloseOffsetSeconds),
              tradingDay(b, kFxDailyCloseOffsetSeconds));   // ... but the day does not
}

TEST(TradingDayTest, MidnightStampOfTheTradingDateGetsTheSameDay)
{
    // A source that stamps the trading date at 00:00 UTC lines up with the close stamp the evening before.
    EXPECT_EQ(tradingDay(parseTimestamp("2025-01-02"), kFxDailyCloseOffsetSeconds),
              tradingDay(parseTimestamp("2025-01-01 22:04:00"), kFxDailyCloseOffsetSeconds));
}

TEST(TradingDayTest, ConsecutiveDaysAreOneApart)
{
    const auto d1 = tradingDay(parseTimestamp("2025-01-02 22:04:00"), kFxDailyCloseOffsetSeconds);
    const auto d2 = tradingDay(parseTimestamp("2025-01-03 22:04:00"), kFxDailyCloseOffsetSeconds);

    EXPECT_EQ(d2 - d1, 1);
}

TEST(TradingDayTest, ZeroOffsetIsThePlainUtcDay)
{
    EXPECT_EQ(tradingDay(0, 0), 0);
    EXPECT_EQ(tradingDay(86399, 0), 0);
    EXPECT_EQ(tradingDay(86400, 0), 1);
}

TEST(TradingDayTest, NegativeTimestampsRoundDown)
{
    EXPECT_EQ(tradingDay(-1, 0), -1);        // one second before 1970-01-01 is the day before
    EXPECT_EQ(tradingDay(-86400, 0), -1);
    EXPECT_EQ(tradingDay(-86401, 0), -2);
}

TEST(TradingDayTest, OffsetShiftsTheDayBoundary)
{
    EXPECT_EQ(tradingDay(86400 - 1, 1), 1);   // one second of offset tips it over midnight
    EXPECT_EQ(tradingDay(86400 - 2, 1), 0);
}
