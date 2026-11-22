#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "loader.hpp"

namespace fs = std::filesystem;

// Each test writes a small CSV into the system temp folder,
// loads it, and the fixture deletes it afterwards.
class LoaderTest : public ::testing::Test
{
protected:
    std::string writeCsv(const std::string& content)
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        fs::path path = fs::temp_directory_path() /
                        (std::string("loader_test_") + info->name() + ".csv");
        std::ofstream out(path);
        out << content;
        out.close();
        created_.push_back(path);
        return path.string();
    }

    void TearDown() override
    {
        for (const auto& p : created_)
            fs::remove(p);
    }

private:
    std::vector<fs::path> created_;
};

TEST_F(LoaderTest, LoadsValidFile)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "1704067200,100.0,102.5,99.5,101.0\n"
        "1704153600,101.0,103.0,100.5,102.8\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 2u);
    EXPECT_EQ(series.bars[0].timestamp, 1704067200);
    EXPECT_DOUBLE_EQ(series.bars[0].open, 100.0);
    EXPECT_DOUBLE_EQ(series.bars[0].high, 102.5);
    EXPECT_DOUBLE_EQ(series.bars[0].low, 99.5);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 101.0);
    EXPECT_EQ(series.bars[1].timestamp, 1704153600);
    EXPECT_DOUBLE_EQ(series.bars[1].close, 102.8);
}

TEST_F(LoaderTest, IgnoresExtraColumns)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close,volume,adj_close,note\n"
        "1000,1.0,2.0,0.5,1.5,99999,1.4,hello\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_EQ(series.bars[0].timestamp, 1000);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 1.5);
}

// ---------- the volume column ----------
// The file's header decides: a column named "volume" (any capitals) is read into Bar::volume; without one the volume is 0.

TEST_F(LoaderTest, ReadsTheVolumeColumnByItsHeaderName)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close,volume\n"
        "1000,1.0,2.0,0.5,1.5,5557.58\n"
        "2000,1.5,2.5,1.0,2.0,193941.5\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 2u);
    EXPECT_DOUBLE_EQ(series.bars[0].volume, 5557.58);
    EXPECT_DOUBLE_EQ(series.bars[1].volume, 193941.5);
}

TEST_F(LoaderTest, TheVolumeColumnCanBeAnywhereAfterTheFirstFive)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close,adj_close,Volume,note\n"
        "1000,1.0,2.0,0.5,1.5,1.4,777,hello\n");

    BarSeries series = loadCsv(path);

    EXPECT_DOUBLE_EQ(series.bars[0].volume, 777.0);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 1.5);
}

TEST_F(LoaderTest, WithoutAVolumeColumnTheVolumeIsZero)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "1000,1.0,2.0,0.5,1.5\n");

    EXPECT_DOUBLE_EQ(loadCsv(path).bars[0].volume, 0.0);
}

TEST_F(LoaderTest, AColumnThatOnlyContainsVolumeInItsNameIsNotTheVolume)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close,volume_usd\n"
        "1000,1.0,2.0,0.5,1.5,42\n");

    EXPECT_DOUBLE_EQ(loadCsv(path).bars[0].volume, 0.0);
}

TEST_F(LoaderTest, AColumnNamedVolumeAmongTheFirstFiveIsNotTheVolume)
{
    // the first five columns are always timestamp, open, high, low, close; a name in one of those places does not move the volume
    std::string path = writeCsv(
        "timestamp,open,high,low,volume\n"
        "1000,1.0,2.0,0.5,1.5\n");

    const BarSeries series = loadCsv(path);
    EXPECT_DOUBLE_EQ(series.bars[0].volume, 0.0);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 1.5);
}

TEST_F(LoaderTest, ABadVolumeValueIsAnError)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close,volume\n"
        "1000,1.0,2.0,0.5,1.5,lots\n");

    EXPECT_THROW(loadCsv(path), std::exception);
}

TEST_F(LoaderTest, ARowTooShortToHoldTheVolumeIsAnError)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close,volume\n"
        "1000,1.0,2.0,0.5,1.5\n");

    EXPECT_THROW(loadCsv(path), std::runtime_error);
}

TEST_F(LoaderTest, SpacesAroundTheVolumeHeaderAreIgnored)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close, volume \n"
        "1000,1.0,2.0,0.5,1.5,12\n");

    EXPECT_DOUBLE_EQ(loadCsv(path).bars[0].volume, 12.0);
}

TEST_F(LoaderTest, SortsUnorderedRowsByTimestamp)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "300,3,3,3,3\n"
        "100,1,1,1,1\n"
        "200,2,2,2,2\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 3u);
    EXPECT_EQ(series.bars[0].timestamp, 100);
    EXPECT_EQ(series.bars[1].timestamp, 200);
    EXPECT_EQ(series.bars[2].timestamp, 300);
}

TEST_F(LoaderTest, SkipsBlankLines)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "100,1,1,1,1\n"
        "\n"
        "200,2,2,2,2\n"
        "\n");

    BarSeries series = loadCsv(path);

    EXPECT_EQ(series.bars.size(), 2u);
}

TEST_F(LoaderTest, HeaderOnlyGivesNoBars)
{
    std::string path = writeCsv("timestamp,open,high,low,close\n");

    BarSeries series = loadCsv(path);

    EXPECT_TRUE(series.bars.empty());
}

TEST_F(LoaderTest, EmptyFileGivesNoBars)
{
    std::string path = writeCsv("");

    BarSeries series = loadCsv(path);

    EXPECT_TRUE(series.bars.empty());
}

TEST_F(LoaderTest, HandlesNegativeAndDecimalPrices)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "100,-1.5,0.25,-2.75,0.0\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_DOUBLE_EQ(series.bars[0].open, -1.5);
    EXPECT_DOUBLE_EQ(series.bars[0].high, 0.25);
    EXPECT_DOUBLE_EQ(series.bars[0].low, -2.75);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 0.0);
}

TEST_F(LoaderTest, ThrowsWhenFileDoesNotExist)
{
    EXPECT_THROW(loadCsv("/definitely/not/a/real/file.csv"), std::runtime_error);
}

TEST_F(LoaderTest, ThrowsWhenRowHasTooFewColumns)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "100,1,1,1\n");

    EXPECT_THROW(loadCsv(path), std::runtime_error);
}

TEST_F(LoaderTest, ThrowsWhenValueIsNotANumber)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "100,abc,1,1,1\n");

    // std::stod throws std::invalid_argument, which is a std::logic_error
    EXPECT_THROW(loadCsv(path), std::invalid_argument);
}

// ---------- timestamps are read as they are ----------

TEST_F(LoaderTest, ReadsMillisecondTimestampsWithoutConvertingThem)
{
    // the layout of the current data files
    std::string path = writeCsv(
        "timestamp,open,high,low,close,volume\n"
        "1451779200000,1.0873,1.0873,1.08451,1.08516,5557.58\n"
        "1451865600000,1.08515,1.09463,1.07812,1.08237,193941.52\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 2u);
    EXPECT_EQ(series.bars[0].timestamp, 1451779200000LL);
    EXPECT_EQ(series.bars[1].timestamp, 1451865600000LL);
    EXPECT_DOUBLE_EQ(series.bars[0].open, 1.0873);
    EXPECT_DOUBLE_EQ(series.bars[0].high, 1.0873);
    EXPECT_DOUBLE_EQ(series.bars[0].low, 1.08451);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 1.08516);
    EXPECT_DOUBLE_EQ(series.bars[1].close, 1.08237);
}

TEST_F(LoaderTest, TimestampsBeyondThirtyTwoBitsAreFine)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "9000000000000,1,1,1,1\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_EQ(series.bars[0].timestamp, 9000000000000LL);
}

TEST_F(LoaderTest, SecondsAreAlsoKeptAsTheyAre)
{
    // the loader does not know or care about the unit
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "1735689600,1,1,1,1\n");

    EXPECT_EQ(loadCsv(path).bars[0].timestamp, 1735689600);
}

TEST_F(LoaderTest, DatesAreNoLongerAccepted)
{
    EXPECT_THROW(loadCsv(writeCsv("timestamp,open,high,low,close\n2025-01-02,1,1,1,1\n")), std::invalid_argument);
}

TEST_F(LoaderTest, DatetimesAreNoLongerAccepted)
{
    EXPECT_THROW(loadCsv(writeCsv("timestamp,open,high,low,close\n\"2025-01-01 22:04:00\",1,1,1,1\n")),
                 std::invalid_argument);
}

TEST_F(LoaderTest, ThrowsOnGarbageTimestamp)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "yesterday,1,1,1,1\n");

    EXPECT_THROW(loadCsv(path), std::invalid_argument);
}

TEST_F(LoaderTest, ThrowsWhenTheTimestampHasTrailingJunk)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "1451779200000abc,1,1,1,1\n");

    EXPECT_THROW(loadCsv(path), std::invalid_argument);
}

TEST_F(LoaderTest, ThrowsWhenTheTimestampIsADecimal)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "1451779200.5,1,1,1,1\n");

    EXPECT_THROW(loadCsv(path), std::invalid_argument);
}

TEST_F(LoaderTest, ThrowsWhenTheTimestampIsEmpty)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        ",1,1,1,1\n");

    EXPECT_THROW(loadCsv(path), std::invalid_argument);
}

// ---------- symbol ----------

TEST_F(LoaderTest, SymbolIsStoredInTheSeries)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "100,1,1,1,1\n");

    BarSeries series = loadCsv(path, "EURGBP");

    EXPECT_EQ(series.symbol, "EURGBP");
}

TEST_F(LoaderTest, SymbolDefaultsToEmpty)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "100,1,1,1,1\n");

    BarSeries series = loadCsv(path);

    EXPECT_TRUE(series.symbol.empty());
}

// ======================= CsvLayout: column mapping and the date type =======================
//
// A CsvLayout says where each value is and how the time is written. A column is named by its header text
// (capitals, spaces and angle brackets ignored: "<DATE>", "Date" and " date " are the same) or, when the text is
// only digits, by its 0-based position ("3"). `volume` left empty means the old rule (a header named "volume").
// TimeFormat: Raw = whole number kept as written (the default), EpochSeconds = whole number of seconds, stored in
// milliseconds, CompactDate = 20100717, IsoDate = 2010-07-17 or 2010-07-17 13:45:00. The converted formats give
// milliseconds since 1970 UTC. `timeOfDay` is an optional second column (093000, 0930 or 09:30[:15]) added to the date.

namespace
{
CsvLayout namedLayout()
{
    CsvLayout l;
    l.time = "date";
    l.open = "open";
    l.high = "high";
    l.low = "low";
    l.close = "close";
    return l;
}
}

TEST_F(LoaderTest, ColumnsCanBeFoundByHeaderNameInAnyOrder)
{
    std::string path = writeCsv(
        "close,low,date,high,open\n"
        "4.5,1.5,1000,3.5,2.5\n");

    const BarSeries series = loadCsv(path, "X", namedLayout());

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_EQ(series.bars[0].timestamp, 1000);
    EXPECT_DOUBLE_EQ(series.bars[0].open, 2.5);
    EXPECT_DOUBLE_EQ(series.bars[0].high, 3.5);
    EXPECT_DOUBLE_EQ(series.bars[0].low, 1.5);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 4.5);
    EXPECT_EQ(series.symbol, "X");
}

TEST_F(LoaderTest, HeaderNamesIgnoreCapitalsSpacesAndAngleBrackets)
{
    std::string path = writeCsv(
        "<DATE>, Open ,<High>,LOW,<Close>\n"
        "1000,1,2,0.5,1.5\n");

    const BarSeries series = loadCsv(path, "", namedLayout());

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_DOUBLE_EQ(series.bars[0].open, 1.0);
    EXPECT_DOUBLE_EQ(series.bars[0].high, 2.0);
    EXPECT_DOUBLE_EQ(series.bars[0].low, 0.5);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 1.5);
}

TEST_F(LoaderTest, QuotedHeaderNamesAreFound)
{
    std::string path = writeCsv(
        "\"date\",\"open\",\"high\",\"low\",\"close\"\n"
        "1000,1,2,0.5,1.5\n");

    const BarSeries series = loadCsv(path, "", namedLayout());

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 1.5);
}

TEST_F(LoaderTest, TheNamesInTheLayoutAreMatchedTheSameWayAsTheHeader)
{
    std::string path = writeCsv(
        "date,open,high,low,close\n"
        "1000,1,2,0.5,1.5\n");

    CsvLayout l;
    l.time = "<DATE>";
    l.open = " Open ";
    l.high = "HIGH";
    l.low = "<low>";
    l.close = "Close";
    const BarSeries series = loadCsv(path, "", l);

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_EQ(series.bars[0].timestamp, 1000);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 1.5);
}

TEST_F(LoaderTest, ColumnsCanBeGivenByPosition)
{
    std::string path = writeCsv(
        "a,b,c,d,e,f\n"
        "x,4.5,1000,2.5,3.5,1.5\n");

    CsvLayout l;
    l.time = "2";
    l.open = "3";
    l.high = "4";
    l.low = "5";
    l.close = "1";
    const BarSeries series = loadCsv(path, "", l);

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_EQ(series.bars[0].timestamp, 1000);
    EXPECT_DOUBLE_EQ(series.bars[0].open, 2.5);
    EXPECT_DOUBLE_EQ(series.bars[0].high, 3.5);
    EXPECT_DOUBLE_EQ(series.bars[0].low, 1.5);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 4.5);
}

TEST_F(LoaderTest, WithoutAHeaderTheFirstLineIsData)
{
    std::string path = writeCsv(
        "1000,1,2,0.5,1.5\n"
        "2000,1.5,2.5,1,2\n");

    CsvLayout l;
    l.hasHeader = false;
    const BarSeries series = loadCsv(path, "", l);

    ASSERT_EQ(series.bars.size(), 2u);
    EXPECT_EQ(series.bars[0].timestamp, 1000);
    EXPECT_EQ(series.bars[1].timestamp, 2000);
}

TEST_F(LoaderTest, WithoutAHeaderANameCannotBeResolved)
{
    std::string path = writeCsv("1000,1,2,0.5,1.5\n");

    CsvLayout l = namedLayout();
    l.hasHeader = false;

    EXPECT_THROW(loadCsv(path, "", l), std::runtime_error);
}

TEST_F(LoaderTest, ANamedColumnThatIsNotInTheHeaderIsAnError)
{
    std::string path = writeCsv(
        "date,open,high,low\n"
        "1000,1,2,0.5\n");

    EXPECT_THROW(loadCsv(path, "", namedLayout()), std::runtime_error);
}

TEST_F(LoaderTest, TheDelimiterCanBeChanged)
{
    std::string path = writeCsv(
        "timestamp;open;high;low;close\n"
        "1000;1.0;2;0.5;1.5\n");

    CsvLayout l;
    l.delimiter = ';';
    const BarSeries series = loadCsv(path, "", l);

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_EQ(series.bars[0].timestamp, 1000);
    EXPECT_DOUBLE_EQ(series.bars[0].high, 2.0);
}

TEST_F(LoaderTest, ARowTooShortForTheHighestMappedColumnIsAnError)
{
    std::string path = writeCsv(
        "date,open,high,low,close,extra\n"
        "1000,1,2,0.5,1.5\n");

    CsvLayout l = namedLayout();
    l.volume = "extra";

    EXPECT_THROW(loadCsv(path, "", l), std::runtime_error);
}

TEST_F(LoaderTest, TheVolumeColumnCanBeNamedExplicitly)
{
    std::string path = writeCsv(
        "date,open,high,low,close,vol\n"
        "1000,1,2,0.5,1.5,321\n");

    CsvLayout l = namedLayout();
    l.volume = "vol";

    EXPECT_DOUBLE_EQ(loadCsv(path, "", l).bars[0].volume, 321.0);
}

TEST_F(LoaderTest, AnExplicitVolumeColumnCanBeAmongTheFirstFive)
{
    std::string path = writeCsv(
        "vol,date,open,high,low,close\n"
        "77,1000,1,2,0.5,1.5\n");

    CsvLayout l = namedLayout();
    l.volume = "vol";
    const BarSeries series = loadCsv(path, "", l);

    EXPECT_DOUBLE_EQ(series.bars[0].volume, 77.0);
    EXPECT_EQ(series.bars[0].timestamp, 1000);
}

TEST_F(LoaderTest, WithAnEmptyVolumeSettingTheHeaderNamedVolumeIsStillUsed)
{
    std::string path = writeCsv(
        "date,open,high,low,close,volume\n"
        "1000,1,2,0.5,1.5,55\n");

    EXPECT_DOUBLE_EQ(loadCsv(path, "", namedLayout()).bars[0].volume, 55.0);
}

TEST_F(LoaderTest, AVolumeNameThatIsNotInTheHeaderIsAnError)
{
    std::string path = writeCsv(
        "date,open,high,low,close\n"
        "1000,1,2,0.5,1.5\n");

    CsvLayout l = namedLayout();
    l.volume = "vol";

    EXPECT_THROW(loadCsv(path, "", l), std::runtime_error);
}

TEST_F(LoaderTest, AByteOrderMarkBeforeTheFirstHeaderIsIgnored)
{
    std::string path = writeCsv(
        "\xEF\xBB\xBF" "date,open,high,low,close\n"
        "1000,1,2,0.5,1.5\n");

    const BarSeries series = loadCsv(path, "", namedLayout());

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_EQ(series.bars[0].timestamp, 1000);
}

TEST_F(LoaderTest, MappedRowsAreStillSortedByTime)
{
    std::string path = writeCsv(
        "close,open,high,low,date\n"
        "2,2,2,2,2000\n"
        "1,1,1,1,1000\n");

    const BarSeries series = loadCsv(path, "", namedLayout());

    ASSERT_EQ(series.bars.size(), 2u);
    EXPECT_EQ(series.bars[0].timestamp, 1000);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 1.0);
}

// ---------- the time type ----------

TEST_F(LoaderTest, EpochSecondsAreStoredAsMilliseconds)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "1735689600,1,1,1,1\n");

    CsvLayout l;
    l.timeFormat = TimeFormat::EpochSeconds;

    EXPECT_EQ(loadCsv(path, "", l).bars[0].timestamp, 1735689600000LL);
}

TEST_F(LoaderTest, RawTimestampsAreKeptAsWrittenByDefault)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "1735689600,1,1,1,1\n");

    CsvLayout l;
    EXPECT_EQ(l.timeFormat, TimeFormat::Raw);
    EXPECT_EQ(loadCsv(path, "", l).bars[0].timestamp, 1735689600);
}

TEST_F(LoaderTest, CompactDatesBecomeMillisecondsAtMidnightUtc)
{
    std::string path = writeCsv(
        "date,open,high,low,close\n"
        "20100717,1,1,1,1\n"
        "20240229,2,2,2,2\n");

    CsvLayout l = namedLayout();
    l.timeFormat = TimeFormat::CompactDate;
    const BarSeries series = loadCsv(path, "", l);

    ASSERT_EQ(series.bars.size(), 2u);
    EXPECT_EQ(series.bars[0].timestamp, 1279324800000LL);
    EXPECT_EQ(series.bars[1].timestamp, 1709164800000LL);
}

TEST_F(LoaderTest, ATimeOfDayColumnIsAddedToTheDate)
{
    std::string path = writeCsv(
        "date,time,open,high,low,close\n"
        "20100717,093000,1,1,1,1\n"
        "20100717,000000,2,2,2,2\n");

    CsvLayout l = namedLayout();
    l.timeFormat = TimeFormat::CompactDate;
    l.timeOfDay = "time";
    const BarSeries series = loadCsv(path, "", l);

    ASSERT_EQ(series.bars.size(), 2u);
    EXPECT_EQ(series.bars[0].timestamp, 1279324800000LL);                 // sorted: midnight first
    EXPECT_EQ(series.bars[1].timestamp, 1279324800000LL + 9 * 3600000LL + 30 * 60000LL);
}

TEST_F(LoaderTest, TheTimeOfDayColumnCanBeTheLastColumn)
{
    std::string path = writeCsv(
        "date,open,high,low,close,time\n"
        "20100717,1,1,1,1,093000\n");

    CsvLayout l = namedLayout();
    l.timeFormat = TimeFormat::CompactDate;
    l.timeOfDay = "time";

    EXPECT_EQ(loadCsv(path, "", l).bars[0].timestamp, 1279324800000LL + 9 * 3600000LL + 30 * 60000LL);
}

TEST_F(LoaderTest, ATimeOfDayColumnWorksWithColonTimesAndIsoDates)
{
    std::string path = writeCsv(
        "date,time,open,high,low,close\n"
        "2010-07-17,13:45:00,1,1,1,1\n");

    CsvLayout l = namedLayout();
    l.timeFormat = TimeFormat::IsoDate;
    l.timeOfDay = "time";

    EXPECT_EQ(loadCsv(path, "", l).bars[0].timestamp, 1279374300000LL);
}

TEST_F(LoaderTest, IsoDatesAndDatetimesAreAccepted)
{
    std::string path = writeCsv(
        "date,open,high,low,close\n"
        "2010-07-17,1,1,1,1\n"
        "2010-07-17 13:45:00,2,2,2,2\n"
        "2010-07-17T13:46:00,3,3,3,3\n");

    CsvLayout l = namedLayout();
    l.timeFormat = TimeFormat::IsoDate;
    const BarSeries series = loadCsv(path, "", l);

    ASSERT_EQ(series.bars.size(), 3u);
    EXPECT_EQ(series.bars[0].timestamp, 1279324800000LL);
    EXPECT_EQ(series.bars[1].timestamp, 1279374300000LL);
    EXPECT_EQ(series.bars[2].timestamp, 1279374360000LL);
}

TEST_F(LoaderTest, IsoDatesAreNotAcceptedWithoutTheIsoFormat)
{
    std::string path = writeCsv(
        "date,open,high,low,close\n"
        "2010-07-17,1,1,1,1\n");

    EXPECT_THROW(loadCsv(path, "", namedLayout()), std::invalid_argument);
}

TEST_F(LoaderTest, APlainNumberIsNotAnIsoDate)
{
    // parseTimestamp would read 1279324800 as epoch seconds; with the IsoDate format it must be refused
    std::string path = writeCsv(
        "date,open,high,low,close\n"
        "1279324800,1,1,1,1\n");

    CsvLayout l = namedLayout();
    l.timeFormat = TimeFormat::IsoDate;

    EXPECT_THROW(loadCsv(path, "", l), std::invalid_argument);
}

TEST_F(LoaderTest, ABadCompactDateIsAnError)
{
    std::string path = writeCsv(
        "date,open,high,low,close\n"
        "20101340,1,1,1,1\n");

    CsvLayout l = namedLayout();
    l.timeFormat = TimeFormat::CompactDate;

    EXPECT_THROW(loadCsv(path, "", l), std::invalid_argument);
}

TEST_F(LoaderTest, ABadTimeOfDayIsAnError)
{
    std::string path = writeCsv(
        "date,time,open,high,low,close\n"
        "20100717,250000,1,1,1,1\n");

    CsvLayout l = namedLayout();
    l.timeFormat = TimeFormat::CompactDate;
    l.timeOfDay = "time";

    EXPECT_THROW(loadCsv(path, "", l), std::invalid_argument);
}

TEST_F(LoaderTest, ATimeOfDayNameThatIsNotInTheHeaderIsAnError)
{
    std::string path = writeCsv(
        "date,open,high,low,close\n"
        "20100717,1,1,1,1\n");

    CsvLayout l = namedLayout();
    l.timeFormat = TimeFormat::CompactDate;
    l.timeOfDay = "time";

    EXPECT_THROW(loadCsv(path, "", l), std::runtime_error);
}

// ---------- a file with extra columns, a separate date and time, and a volume ----------

TEST_F(LoaderTest, ADateAndTimeInSeparateColumnsWithOtherColumnsAroundThem)
{
    std::string path = writeCsv(
        "<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>\n"
        "AAA,D,20100717,000000,0.04951,0.04951,0.04951,0.04951,0,0\n"
        "AAA,D,20100718,000000,0.04951,0.08585,0.04951,0.08584,12.5,0\n"
        "AAA,60,20100719,093000,0.1,0.2,0.05,0.15,3,0\n");

    CsvLayout l;
    l.timeFormat = TimeFormat::CompactDate;
    l.time = "date";
    l.timeOfDay = "time";
    l.open = "open";
    l.high = "high";
    l.low = "low";
    l.close = "close";
    l.volume = "vol";
    const BarSeries series = loadCsv(path, "AAA", l);

    ASSERT_EQ(series.bars.size(), 3u);
    EXPECT_EQ(series.symbol, "AAA");
    EXPECT_EQ(series.bars[0].timestamp, 1279324800000LL);
    EXPECT_EQ(series.bars[1].timestamp, 1279324800000LL + 86400000LL);
    EXPECT_EQ(series.bars[2].timestamp, 1279324800000LL + 2 * 86400000LL + 9 * 3600000LL + 30 * 60000LL);
    EXPECT_DOUBLE_EQ(series.bars[1].open, 0.04951);
    EXPECT_DOUBLE_EQ(series.bars[1].high, 0.08585);
    EXPECT_DOUBLE_EQ(series.bars[1].low, 0.04951);
    EXPECT_DOUBLE_EQ(series.bars[1].close, 0.08584);
    EXPECT_DOUBLE_EQ(series.bars[1].volume, 12.5);
}

TEST_F(LoaderTest, ABadTimestampErrorNamesTheValue)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "BTC.V,1,1,1,1\n");

    try
    {
        loadCsv(path);
        FAIL() << "expected an exception";
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("BTC.V"), std::string::npos) << e.what();
    }
}
