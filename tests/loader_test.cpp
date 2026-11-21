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
