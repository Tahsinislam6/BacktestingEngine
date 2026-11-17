#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bar.hpp"
#include "swap.hpp"
#include "timestamp.hpp"

// Swap (financing): holding a position over a night earns the interest rate of the currency you hold and pays the rate of
// the currency you owe. For a pair BASEQUOTE a long holds the base and owes the quote, so per night (in quote currency)
//     long  = notional * (rate base - rate quote) / 100 / 365
//     short = the opposite sign
// minus an optional broker markup, always against the trader. Rates are annual percentages by currency and date.

namespace
{
const std::int64_t kDay = kTimestampUnitsPerDay;

std::int64_t at(int day) { return static_cast<std::int64_t>(day) * kDay; }

SwapModel modelOf(std::initializer_list<std::pair<std::string, std::vector<std::pair<std::int64_t, double>>>> rates)
{
    SwapModel m;
    for (const auto& [currency, obs] : rates)
        m.rates[currency] = obs;
    return m;
}

AlignedData dataOfDays(const std::vector<std::string>& symbols, const std::vector<int>& days)
{
    AlignedData d;
    d.symbols = symbols;
    for (int day : days)
    {
        Snapshot s;
        s.timestamp = at(day);
        for (std::size_t a = 0; a < symbols.size(); ++a)
        {
            Bar b;
            b.timestamp = s.timestamp;
            b.open = b.high = b.low = b.close = 1.0;
            s.bars.push_back(b);
        }
        d.snapshots.push_back(s);
    }
    return d;
}
}

// ---------- the currencies of a pair ----------

TEST(SwapPairTest, SplitsASixLetterSymbol)
{
    const auto c = pairCurrencies("EURUSD");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->first, "EUR");
    EXPECT_EQ(c->second, "USD");
    EXPECT_EQ(pairCurrencies("USDJPY")->second, "JPY");
}

TEST(SwapPairTest, AnythingElseHasNoCurrencies)
{
    for (const char* bad : {"A0", "EURUS", "EURUSDX", "eurusd", "EUR_USD", "", "EUR123"})
        EXPECT_FALSE(pairCurrencies(bad).has_value()) << bad;
}

// ---------- the rate at a date ----------

TEST(SwapRateTest, IsTheLastObservationAtOrBeforeTheDate)
{
    const SwapModel m = modelOf({{"USD", {{at(10), 1.0}, {at(20), 2.0}, {at(30), 3.0}}}});

    EXPECT_DOUBLE_EQ(rateAt(m, "USD", at(10)), 1.0);               // on the day
    EXPECT_DOUBLE_EQ(rateAt(m, "USD", at(19) + 5), 1.0);           // between two observations: the earlier one holds
    EXPECT_DOUBLE_EQ(rateAt(m, "USD", at(20)), 2.0);
    EXPECT_DOUBLE_EQ(rateAt(m, "USD", at(25)), 2.0);
    EXPECT_DOUBLE_EQ(rateAt(m, "USD", at(999)), 3.0);              // after the last one: it holds
}

TEST(SwapRateTest, IsUnknownBeforeTheFirstObservationAndForAnUnlistedCurrency)
{
    const SwapModel m = modelOf({{"USD", {{at(10), 1.0}}}});
    EXPECT_TRUE(std::isnan(rateAt(m, "USD", at(9))));
    EXPECT_TRUE(std::isnan(rateAt(m, "SGD", at(10))));
}

TEST(SwapRateTest, NegativeRatesAreFine)
{
    const SwapModel m = modelOf({{"CHF", {{at(0), -0.75}}}});
    EXPECT_DOUBLE_EQ(rateAt(m, "CHF", at(5)), -0.75);
}

// ---------- the amount of one night ----------

TEST(SwapAmountTest, ALongEarnsTheDifferenceAndAShortPaysIt)
{
    // 1000 units at 1.2 = 1200 notional; 2 percentage points a year: 1200 * 0.02 / 365 per night
    const double perNight = 1200.0 * 0.02 / 365.0;
    EXPECT_NEAR(swapAmount(Direction::Long, 1000, 1.2, 2.0, 0.0, 1.0), perNight, 1e-12);
    EXPECT_NEAR(swapAmount(Direction::Short, 1000, 1.2, 2.0, 0.0, 1.0), -perNight, 1e-12);
}

TEST(SwapAmountTest, ANegativeDifferenceReversesIt)
{
    const double perNight = 1200.0 * 0.02 / 365.0;
    EXPECT_NEAR(swapAmount(Direction::Long, 1000, 1.2, -2.0, 0.0, 1.0), -perNight, 1e-12);
    EXPECT_NEAR(swapAmount(Direction::Short, 1000, 1.2, -2.0, 0.0, 1.0), perNight, 1e-12);
}

TEST(SwapAmountTest, ItScalesWithQuantityPriceAndNights)
{
    const double one = swapAmount(Direction::Long, 1000, 1.2, 2.0, 0.0, 1.0);
    EXPECT_NEAR(swapAmount(Direction::Long, 3000, 1.2, 2.0, 0.0, 1.0), 3 * one, 1e-12);
    EXPECT_NEAR(swapAmount(Direction::Long, 1000, 2.4, 2.0, 0.0, 1.0), 2 * one, 1e-12);
    EXPECT_NEAR(swapAmount(Direction::Long, 1000, 1.2, 2.0, 0.0, 3.0), 3 * one, 1e-12);
    EXPECT_DOUBLE_EQ(swapAmount(Direction::Long, 1000, 1.2, 2.0, 0.0, 0.0), 0.0);
}

TEST(SwapAmountTest, TheMarkupIsAlwaysAgainstTheTrader)
{
    // markup 0.5 points a year: a long gets (2 - 0.5), a short pays (2 + 0.5)
    EXPECT_NEAR(swapAmount(Direction::Long, 1000, 1.2, 2.0, 0.5, 1.0), 1200.0 * 0.015 / 365.0, 1e-12);
    EXPECT_NEAR(swapAmount(Direction::Short, 1000, 1.2, 2.0, 0.5, 1.0), -1200.0 * 0.025 / 365.0, 1e-12);
    // with no rate difference both pay the markup
    EXPECT_NEAR(swapAmount(Direction::Long, 1000, 1.2, 0.0, 0.5, 1.0), -1200.0 * 0.005 / 365.0, 1e-12);
    EXPECT_NEAR(swapAmount(Direction::Short, 1000, 1.2, 0.0, 0.5, 1.0), -1200.0 * 0.005 / 365.0, 1e-12);
}

TEST(SwapAmountTest, AnUnknownDifferenceMeansNoInterestButTheMarkupStillApplies)
{
    EXPECT_DOUBLE_EQ(swapAmount(Direction::Long, 1000, 1.2, std::nan(""), 0.0, 1.0), 0.0);
    EXPECT_NEAR(swapAmount(Direction::Long, 1000, 1.2, std::nan(""), 0.5, 1.0), -1200.0 * 0.005 / 365.0, 1e-12);
}

// ---------- the rates file ----------

TEST(SwapFileTest, ReadsCurrencyDateAndRate)
{
    const SwapModel m = parseRatesCsv("currency,date,rate\nUSD,2016-01-01,0.5\nUSD,2016-02-01,0.75\nJPY,2016-01-01,-0.1\n");

    ASSERT_EQ(m.rates.at("USD").size(), 2u);
    EXPECT_EQ(m.rates.at("USD")[0].first, parseTimestamp("2016-01-01") * 1000);
    EXPECT_DOUBLE_EQ(m.rates.at("USD")[0].second, 0.5);
    EXPECT_DOUBLE_EQ(m.rates.at("USD")[1].second, 0.75);
    ASSERT_EQ(m.rates.at("JPY").size(), 1u);
    EXPECT_DOUBLE_EQ(m.rates.at("JPY")[0].second, -0.1);
}

TEST(SwapFileTest, SortsObservationsByDate)
{
    const SwapModel m = parseRatesCsv("currency,date,rate\nUSD,2016-03-01,3\nUSD,2016-01-01,1\nUSD,2016-02-01,2\n");
    ASSERT_EQ(m.rates.at("USD").size(), 3u);
    EXPECT_DOUBLE_EQ(m.rates.at("USD")[0].second, 1.0);
    EXPECT_DOUBLE_EQ(m.rates.at("USD")[2].second, 3.0);
}

TEST(SwapFileTest, ToleratesBlankLinesCommentsSpacesAndWindowsLineEndings)
{
    const SwapModel m = parseRatesCsv("# from FRED\r\ncurrency,date,rate\r\n\r\n USD , 2016-01-01 , 0.5 \r\n# note\r\nEUR,2016-01-01,0\r\n");
    EXPECT_EQ(m.rates.size(), 2u);
    EXPECT_DOUBLE_EQ(m.rates.at("USD")[0].second, 0.5);
}

TEST(SwapFileTest, RejectsBadFilesAndNamesTheLine)
{
    for (const char* bad : {"",                                                      // no header
                            "currency,date,rate\n",                                  // no rows
                            "date,currency,rate\nUSD,2016-01-01,1\n",                // wrong header
                            "currency,date,rate\nUSD,2016-01-01\n",                  // missing field
                            "currency,date,rate\nUSD,2016-01-01,1,9\n",              // extra field
                            "currency,date,rate\nUSD,not-a-date,1\n",
                            "currency,date,rate\nUSD,2016-13-45,1\n",
                            "currency,date,rate\nUSD,2016-01-01,abc\n",
                            "currency,date,rate\nUSD,2016-01-01,1x\n",
                            "currency,date,rate\nUSD,2016-01-01,nan\n",
                            "currency,date,rate\nUSD,2016-01-01,inf\n",
                            "currency,date,rate\nusd,2016-01-01,1\n",                // currency must be 3 capital letters
                            "currency,date,rate\nUS,2016-01-01,1\n",
                            "currency,date,rate\nUSDX,2016-01-01,1\n",
                            "currency,date,rate\nUSD,2016-01-01,1\nUSD,2016-01-01,2\n"})   // a date twice
        EXPECT_THROW(parseRatesCsv(bad), std::invalid_argument) << bad;

    try
    {
        parseRatesCsv("currency,date,rate\nUSD,2016-01-01,1\nUSD,oops,2\n");
        FAIL() << "should have thrown";
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("line 3"), std::string::npos) << e.what();
    }
}

TEST(SwapFileTest, LoadsAFileAndReportsAMissingOne)
{
    const std::string path = (std::filesystem::temp_directory_path() / "swap_test_rates.csv").string();
    {
        std::ofstream out(path);
        out << "currency,date,rate\nUSD,2016-01-01,0.5\n";
    }
    EXPECT_EQ(loadRatesCsv(path).rates.size(), 1u);
    EXPECT_THROW(loadRatesCsv(path + ".missing"), std::runtime_error);
}

// ---------- the schedule over a data set ----------

TEST(SwapScheduleTest, HasTheBaseMinusQuoteDifferenceForEveryAssetAndSnapshot)
{
    // EURUSD: base EUR 3, quote USD 1 until day 20, then USD 2; USDJPY: base USD, quote JPY 0
    const SwapModel m = modelOf({{"EUR", {{at(0), 3.0}}}, {"USD", {{at(0), 1.0}, {at(20), 2.0}}}, {"JPY", {{at(0), 0.0}}}});
    const AlignedData d = dataOfDays({"EURUSD", "USDJPY"}, {5, 15, 25});

    const SwapSchedule s = makeSwapSchedule(m, d.symbols, d);

    ASSERT_EQ(s.differentialPct.size(), 2u);
    ASSERT_EQ(s.differentialPct[0].size(), 3u);
    EXPECT_DOUBLE_EQ(s.differentialPct[0][0], 2.0);       // 3 - 1
    EXPECT_DOUBLE_EQ(s.differentialPct[0][1], 2.0);
    EXPECT_DOUBLE_EQ(s.differentialPct[0][2], 1.0);       // 3 - 2 after day 20
    EXPECT_DOUBLE_EQ(s.differentialPct[1][0], 1.0);       // 1 - 0
    EXPECT_DOUBLE_EQ(s.differentialPct[1][2], 2.0);       // 2 - 0
    EXPECT_DOUBLE_EQ(s.markupPct, 0.0);
    EXPECT_TRUE(s.active());
}

TEST(SwapScheduleTest, ACurrencyWithoutARateGivesAnUnknownDifference)
{
    const SwapModel m = modelOf({{"USD", {{at(0), 1.0}}}});
    const AlignedData d = dataOfDays({"USDSGD", "A0"}, {5, 15});
    const SwapSchedule s = makeSwapSchedule(m, d.symbols, d);
    EXPECT_TRUE(std::isnan(s.differentialPct[0][0]));     // SGD unknown
    EXPECT_TRUE(std::isnan(s.differentialPct[1][1]));     // not a pair symbol at all
}

TEST(SwapScheduleTest, TheMarkupIsCarried)
{
    SwapModel m = modelOf({{"USD", {{at(0), 1.0}}}, {"EUR", {{at(0), 1.0}}}});
    m.markupPct = 0.7;
    const AlignedData d = dataOfDays({"EURUSD"}, {5});
    EXPECT_DOUBLE_EQ(makeSwapSchedule(m, d.symbols, d).markupPct, 0.7);
}

TEST(SwapScheduleTest, NothingToChargeMeansAnInactiveSchedule)
{
    const AlignedData d = dataOfDays({"EURUSD"}, {5, 6});
    const SwapSchedule s = makeSwapSchedule(SwapModel{}, d.symbols, d);
    EXPECT_FALSE(s.active());
    EXPECT_TRUE(s.differentialPct.empty());
}

TEST(SwapScheduleTest, AMarkupAloneIsActive)
{
    SwapModel m;
    m.markupPct = 1.0;
    const AlignedData d = dataOfDays({"EURUSD"}, {5, 6});
    EXPECT_TRUE(makeSwapSchedule(m, d.symbols, d).active());
}

TEST(SwapScheduleTest, ListsThePairsThatHaveNoRate)
{
    const SwapModel m = modelOf({{"USD", {{at(0), 1.0}}}, {"EUR", {{at(0), 1.0}}}});
    const std::vector<std::string> missing = pairsWithoutRates(m, {"EURUSD", "USDSGD", "AUDSGD", "A0"});
    ASSERT_EQ(missing.size(), 2u);
    EXPECT_EQ(missing[0], "USDSGD");
    EXPECT_EQ(missing[1], "AUDSGD");
}
