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

// ---------- timestamps in date/datetime form ----------

TEST_F(LoaderTest, ReadsQuotedDatetimeTimestamps)
{
    std::string path = writeCsv(
        "\"time\",\"open\",\"high\",\"low\",\"close\",\"volume\",\"trades\",\"date\"\n"
        "\"2025-01-01 22:04:00\",0.82759,0.83254,0.82668,0.82928,0,72455,\"2025-01-02\"\n"
        "\"2025-01-02 22:04:00\",0.82928,0.83034,0.8285,0.82988,0,55090,\"2025-01-03\"\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 2u);
    EXPECT_EQ(series.bars[0].timestamp, 1735769040);
    EXPECT_EQ(series.bars[1].timestamp, 1735769040 + 86400);
    EXPECT_DOUBLE_EQ(series.bars[0].open, 0.82759);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 0.82928);
}

TEST_F(LoaderTest, ReadsDateOnlyTimestamps)
{
    std::string path = writeCsv(
        "date,open,high,low,close\n"
        "2025-01-02,1,2,0.5,1.5\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 1u);
    EXPECT_EQ(series.bars[0].timestamp, 1735776000);
}

TEST_F(LoaderTest, MixedEpochAndDateRowsAreSorted)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "2025-01-03,3,3,3,3\n"
        "1735776000,1,1,1,1\n"       // 2025-01-02
        "2025-01-04,4,4,4,4\n");

    BarSeries series = loadCsv(path);

    ASSERT_EQ(series.bars.size(), 3u);
    EXPECT_DOUBLE_EQ(series.bars[0].close, 1.0);
    EXPECT_DOUBLE_EQ(series.bars[1].close, 3.0);
    EXPECT_DOUBLE_EQ(series.bars[2].close, 4.0);
}

TEST_F(LoaderTest, ThrowsOnImpossibleDate)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "2025-02-30,1,1,1,1\n");

    EXPECT_THROW(loadCsv(path), std::invalid_argument);
}

TEST_F(LoaderTest, ThrowsOnGarbageTimestamp)
{
    std::string path = writeCsv(
        "timestamp,open,high,low,close\n"
        "yesterday,1,1,1,1\n");

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
