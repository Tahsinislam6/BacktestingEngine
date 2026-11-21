#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "dataset.hpp"

namespace fs = std::filesystem;

// loadFolder reads every .csv in a folder into one aligned dataset, one asset per file.

class DatasetTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("dataset_test_") + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }

    void TearDown() override { fs::remove_all(dir_); }

    // 2025-01-01 00:00 UTC in milliseconds, plus whole days
    static long long day(int n) { return 1735689600000LL + n * 86400000LL; }

    // rows: {timestamp, close}; open/high/low are filled in around the close
    void writeCsv(const std::string& filename, const std::vector<std::pair<long long, double>>& rows)
    {
        std::ofstream out(dir_ / filename);
        out << "timestamp,open,high,low,close,volume\n";
        for (const auto& [time, close] : rows)
            out << time << "," << close << "," << close + 1 << "," << close - 1 << "," << close << ",0\n";
    }

    void writeText(const std::string& filename, const std::string& text)
    {
        std::ofstream(dir_ / filename) << text;
    }

    std::string dir() const { return dir_.string(); }

    fs::path dir_;
};

// ---------- symbolFromFilename ----------

TEST(SymbolFromFilenameTest, CutsAtForex)
{
    EXPECT_EQ(symbolFromFilename("EUR_GBP_forex_1d_2025-01-01_2025-12-31.csv"), "EUR_GBP");
}

TEST(SymbolFromFilenameTest, CutsAt1d)
{
    EXPECT_EQ(symbolFromFilename("EURUSD_1d_2016-01-01_2025-12-31.csv"), "EURUSD");
}

TEST(SymbolFromFilenameTest, DropsTheFolder)
{
    EXPECT_EQ(symbolFromFilename("/data/in/USD_JPY_forex_1d.csv"), "USD_JPY");
}

TEST(SymbolFromFilenameTest, WithoutForexKeepsTheWholeStem)
{
    EXPECT_EQ(symbolFromFilename("gold.csv"), "gold");
    EXPECT_EQ(symbolFromFilename("EUR_GBP.CSV"), "EUR_GBP");
    EXPECT_EQ(symbolFromFilename("no_extension"), "no_extension");
}

// ---------- csvFilesIn ----------

TEST_F(DatasetTest, ListsOnlyCsvFilesSortedByName)
{
    writeText("b.csv", "x");
    writeText("a.csv", "x");
    writeText("notes.txt", "x");
    writeText("C.CSV", "x");
    fs::create_directories(dir_ / "sub.csv");          // a folder, not a file

    const std::vector<std::string> files = csvFilesIn(dir());

    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(fs::path(files[0]).filename(), "C.CSV");      // uppercase sorts before lowercase
    EXPECT_EQ(fs::path(files[1]).filename(), "a.csv");
    EXPECT_EQ(fs::path(files[2]).filename(), "b.csv");
}

TEST_F(DatasetTest, AnEmptyFolderHasNoFiles)
{
    EXPECT_TRUE(csvFilesIn(dir()).empty());
}

TEST_F(DatasetTest, AFolderThatDoesNotExistThrows)
{
    EXPECT_THROW(csvFilesIn((dir_ / "nope").string()), std::runtime_error);
    EXPECT_THROW(loadFolder((dir_ / "nope").string()), std::runtime_error);
}

TEST_F(DatasetTest, AFileIsNotAFolder)
{
    writeText("a.csv", "x");

    EXPECT_THROW(csvFilesIn((dir_ / "a.csv").string()), std::runtime_error);
}

// ---------- loadFolder ----------

TEST_F(DatasetTest, LoadsOneAssetPerFileInNameOrder)
{
    writeCsv("USD_JPY_forex_1d_x.csv", {{day(0), 150}, {day(1), 151}});
    writeCsv("EUR_GBP_forex_1d_x.csv", {{day(0), 0.8}, {day(1), 0.9}});

    const AlignedData d = loadFolder(dir());

    ASSERT_EQ(d.symbols.size(), 2u);
    EXPECT_EQ(d.symbols[0], "EUR_GBP");
    EXPECT_EQ(d.symbols[1], "USD_JPY");
    ASSERT_EQ(d.snapshots.size(), 2u);
    EXPECT_DOUBLE_EQ(d.snapshots[0].bars[0]->close, 0.8);
    EXPECT_DOUBLE_EQ(d.snapshots[1].bars[1]->close, 151);
}

TEST_F(DatasetTest, DaysOneAssetLacksBecomeGaps)
{
    writeCsv("A_forex.csv", {{day(0), 1}, {day(1), 2}, {day(2), 3}});
    writeCsv("B_forex.csv", {{day(0), 10}, {day(2), 30}});

    const AlignedData d = loadFolder(dir());

    ASSERT_EQ(d.snapshots.size(), 3u);
    EXPECT_TRUE(d.snapshots[1].bars[0].has_value());
    EXPECT_FALSE(d.snapshots[1].bars[1].has_value());
}

// 2016-01-03 was a Sunday: a Sunday bar followed by a Monday bar
static const long long kSundayMs = 1451779200000LL;
static const long long kMondayMs = kSundayMs + 86400000LL;

TEST_F(DatasetTest, SundayBarsAreFoldedIntoMondayByDefault)
{
    writeCsv("A_1d.csv", {{kSundayMs, 5}, {kMondayMs, 9}});       // writeCsv puts high = close+1, low = close-1

    const AlignedData d = loadFolder(dir());

    ASSERT_EQ(d.snapshots.size(), 1u);
    EXPECT_EQ(d.snapshots[0].timestamp, kMondayMs);
    EXPECT_DOUBLE_EQ(d.snapshots[0].bars[0]->open, 5);            // Sunday's open
    EXPECT_DOUBLE_EQ(d.snapshots[0].bars[0]->close, 9);           // Monday's close
    EXPECT_DOUBLE_EQ(d.snapshots[0].bars[0]->high, 10);
    EXPECT_DOUBLE_EQ(d.snapshots[0].bars[0]->low, 4);
}

TEST_F(DatasetTest, SundayBarsCanBeKept)
{
    writeCsv("A_1d.csv", {{kSundayMs, 5}, {kMondayMs, 9}});

    const AlignedData d = loadFolder(dir(), false);

    EXPECT_EQ(d.snapshots.size(), 2u);
}

TEST_F(DatasetTest, EveryFileIsMergedSoTheyStillLineUp)
{
    writeCsv("A_1d.csv", {{kSundayMs, 1}, {kMondayMs, 2}, {kMondayMs + 86400000LL, 3}});
    writeCsv("B_1d.csv", {{kSundayMs, 10}, {kMondayMs, 20}, {kMondayMs + 86400000LL, 30}});

    const AlignedData d = loadFolder(dir());

    ASSERT_EQ(d.snapshots.size(), 2u);
    EXPECT_TRUE(d.snapshots[0].bars[0].has_value());
    EXPECT_TRUE(d.snapshots[0].bars[1].has_value());
}

TEST_F(DatasetTest, ASingleFileWorks)
{
    writeCsv("A_forex.csv", {{day(0), 1}});

    const AlignedData d = loadFolder(dir());

    EXPECT_EQ(d.symbols.size(), 1u);
    EXPECT_EQ(d.snapshots.size(), 1u);
}

TEST_F(DatasetTest, OtherFilesAreIgnored)
{
    writeCsv("A_forex.csv", {{day(0), 1}});
    writeText("readme.txt", "not data");

    EXPECT_EQ(loadFolder(dir()).symbols.size(), 1u);
}

TEST_F(DatasetTest, AFolderWithoutCsvFilesThrows)
{
    writeText("readme.txt", "not data");

    EXPECT_THROW(loadFolder(dir()), std::runtime_error);
}

TEST_F(DatasetTest, TwoFilesWithTheSameSymbolThrow)
{
    writeCsv("EUR_GBP_forex_1d_2024.csv", {{day(-366), 1}});
    writeCsv("EUR_GBP_forex_1d_2025.csv", {{day(0), 1}});

    EXPECT_THROW(loadFolder(dir()), std::runtime_error);
}

TEST_F(DatasetTest, ABadFileIsReportedNotSkipped)
{
    writeCsv("A_forex.csv", {{day(0), 1}});
    writeText("B_forex.csv", "header\n1,2\n");

    EXPECT_THROW(loadFolder(dir()), std::runtime_error);
}

TEST_F(DatasetTest, TheVolumeOfEveryBarReachesTheSnapshots)
{
    writeText("EURUSD_1d_x.csv",
              "timestamp,open,high,low,close,volume\n" +
              std::to_string(day(0)) + ",1.0,1.1,0.9,1.05,1500.5\n" +
              std::to_string(day(1)) + ",1.05,1.2,1.0,1.1,2500.25\n");
    writeText("GBPUSD_1d_x.csv",
              "timestamp,open,high,low,close,volume\n" +
              std::to_string(day(0)) + ",1.3,1.4,1.2,1.35,10\n" +
              std::to_string(day(1)) + ",1.35,1.5,1.3,1.4,20\n");

    const AlignedData d = loadFolder(dir());

    ASSERT_EQ(d.snapshots.size(), 2u);
    EXPECT_DOUBLE_EQ(d.snapshots[0].bars[0]->volume, 1500.5);
    EXPECT_DOUBLE_EQ(d.snapshots[1].bars[0]->volume, 2500.25);
    EXPECT_DOUBLE_EQ(d.snapshots[0].bars[1]->volume, 10.0);
    EXPECT_DOUBLE_EQ(d.snapshots[1].bars[1]->volume, 20.0);
}
