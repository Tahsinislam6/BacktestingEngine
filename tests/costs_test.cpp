#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "costs.hpp"

namespace fs = std::filesystem;

// ---------- pip size ----------

TEST(PipSizeTest, JpyQuotedPairsUseZeroPointZeroOne)
{
    EXPECT_DOUBLE_EQ(pipSize("USDJPY"), 0.01);
    EXPECT_DOUBLE_EQ(pipSize("GBPJPY"), 0.01);
    EXPECT_DOUBLE_EQ(pipSize("CHFJPY"), 0.01);
}

TEST(PipSizeTest, OtherPairsUseZeroPointZeroZeroZeroOne)
{
    EXPECT_DOUBLE_EQ(pipSize("EURUSD"), 0.0001);
    EXPECT_DOUBLE_EQ(pipSize("JPYUSD"), 0.0001);   // JPY is the base here, not the quote
    EXPECT_DOUBLE_EQ(pipSize("USDSGD"), 0.0001);
}

TEST(PipSizeTest, UnusualSymbolsFallBackToTheCommonPip)
{
    EXPECT_DOUBLE_EQ(pipSize(""), 0.0001);
    EXPECT_DOUBLE_EQ(pipSize("A0"), 0.0001);
    EXPECT_DOUBLE_EQ(pipSize("JPY"), 0.0001);      // not a pair
}

// ---------- CostModel ----------

TEST(CostModelTest, SpreadIsPipsTimesPipSize)
{
    CostModel m;
    m.spreadPips["EURUSD"] = 1.5;
    m.spreadPips["USDJPY"] = 2.0;

    EXPECT_NEAR(m.spreadPrice("EURUSD"), 0.00015, 1e-12);
    EXPECT_NEAR(m.spreadPrice("USDJPY"), 0.02, 1e-12);
}

TEST(CostModelTest, UnlistedSymbolsUseTheDefault)
{
    CostModel m;
    m.defaultPips = 3.0;
    m.spreadPips["EURUSD"] = 1.0;

    EXPECT_NEAR(m.spreadPrice("GBPAUD"), 0.0003, 1e-12);
    EXPECT_NEAR(m.spreadPrice("EURUSD"), 0.0001, 1e-12);
}

TEST(CostModelTest, ADefaultModelCostsNothing)
{
    EXPECT_DOUBLE_EQ(CostModel{}.spreadPrice("EURUSD"), 0.0);
}

TEST(CostModelTest, MultiplierScalesEverySpreadIncludingTheDefault)
{
    CostModel m;
    m.defaultPips = 3.0;
    m.spreadPips["EURUSD"] = 1.0;
    m.multiplier = 2.0;

    EXPECT_NEAR(m.spreadPrice("EURUSD"), 0.0002, 1e-12);
    EXPECT_NEAR(m.spreadPrice("GBPAUD"), 0.0006, 1e-12);
}

TEST(CostModelTest, SpreadsForFollowsTheSymbolOrder)
{
    CostModel m;
    m.spreadPips["EURUSD"] = 1.0;
    m.spreadPips["USDJPY"] = 2.0;

    const std::vector<double> s = spreadsFor(m, {"USDJPY", "EURUSD", "XXXXXX"});

    ASSERT_EQ(s.size(), 3u);
    EXPECT_NEAR(s[0], 0.02, 1e-12);
    EXPECT_NEAR(s[1], 0.0001, 1e-12);
    EXPECT_DOUBLE_EQ(s[2], 0.0);
}

// ---------- typical values ----------

TEST(TypicalCostsTest, EveryPairOfTheStudyIsListed)
{
    const CostModel m = typicalFxCosts();
    for (const char* s : {"EURUSD", "USDJPY", "GBPAUD", "GBPCAD", "CHFJPY", "NZDCAD", "AUDCHF", "AUDNZD", "EURGBP",
                          "USDSGD", "NZDUSD", "USDCAD", "USDCHF", "GBPJPY", "CADJPY", "EURCAD", "EURNZD", "GBPCHF",
                          "EURCHF", "AUDJPY"})
        EXPECT_TRUE(m.spreadPips.count(s)) << s;
}

TEST(TypicalCostsTest, MajorsAreCheaperThanExoticCrosses)
{
    const CostModel m = typicalFxCosts();
    EXPECT_LT(m.spreadPips.at("EURUSD"), m.spreadPips.at("GBPAUD"));
    EXPECT_LT(m.spreadPips.at("EURUSD"), m.spreadPips.at("EURNZD"));
}

TEST(TypicalCostsTest, EverySpreadIsPositiveAndPlausible)
{
    for (const auto& [symbol, pips] : typicalFxCosts().spreadPips)
    {
        EXPECT_GT(pips, 0.0) << symbol;
        EXPECT_LT(pips, 10.0) << symbol;
    }
    EXPECT_GT(typicalFxCosts().defaultPips, 0.0);
}

// ---------- costs file ----------

class CostsFileTest : public ::testing::Test
{
protected:
    std::string write(const std::string& content)
    {
        path_ = fs::temp_directory_path() / (std::string("costs_test_") +
                ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".csv");
        std::ofstream(path_) << content;
        return path_.string();
    }
    void TearDown() override { fs::remove(path_); }

private:
    fs::path path_;
};

TEST_F(CostsFileTest, ReadsSymbolAndPips)
{
    const CostModel m = loadCostsCsv(write("symbol,spread_pips\nEURUSD,0.9\nUSDJPY,1.1\n"), CostModel{});

    EXPECT_DOUBLE_EQ(m.spreadPips.at("EURUSD"), 0.9);
    EXPECT_DOUBLE_EQ(m.spreadPips.at("USDJPY"), 1.1);
}

TEST_F(CostsFileTest, ListedPairsOverrideAndOthersKeepTheBaseValue)
{
    CostModel base;
    base.defaultPips = 2.5;
    base.spreadPips["EURUSD"] = 1.0;
    base.spreadPips["GBPAUD"] = 3.0;

    const CostModel m = loadCostsCsv(write("symbol,spread_pips\nEURUSD,0.5\n"), base);

    EXPECT_DOUBLE_EQ(m.spreadPips.at("EURUSD"), 0.5);
    EXPECT_DOUBLE_EQ(m.spreadPips.at("GBPAUD"), 3.0);
    EXPECT_DOUBLE_EQ(m.defaultPips, 2.5);
}

TEST_F(CostsFileTest, ADefaultRowSetsTheFallback)
{
    const CostModel m = loadCostsCsv(write("symbol,spread_pips\ndefault,4\n"), CostModel{});
    EXPECT_DOUBLE_EQ(m.defaultPips, 4.0);
    EXPECT_TRUE(m.spreadPips.empty());
}

TEST_F(CostsFileTest, SkipsBlankLinesAndTrimsSpaces)
{
    const CostModel m = loadCostsCsv(write("symbol,spread_pips\n\n EURUSD , 0.9 \n\n"), CostModel{});
    EXPECT_DOUBLE_EQ(m.spreadPips.at("EURUSD"), 0.9);
}

TEST_F(CostsFileTest, HeaderIsOptional)
{
    const CostModel m = loadCostsCsv(write("EURUSD,0.9\n"), CostModel{});
    EXPECT_DOUBLE_EQ(m.spreadPips.at("EURUSD"), 0.9);
}

TEST_F(CostsFileTest, RejectsNegativeSpread)
{
    EXPECT_THROW(loadCostsCsv(write("symbol,spread_pips\nEURUSD,-1\n"), CostModel{}), std::invalid_argument);
}

TEST_F(CostsFileTest, RejectsNonNumbers)
{
    EXPECT_THROW(loadCostsCsv(write("symbol,spread_pips\nEURUSD,abc\n"), CostModel{}), std::invalid_argument);
    EXPECT_THROW(loadCostsCsv(write("symbol,spread_pips\nEURUSD,1.2x\n"), CostModel{}), std::invalid_argument);
    EXPECT_THROW(loadCostsCsv(write("symbol,spread_pips\nEURUSD,nan\n"), CostModel{}), std::invalid_argument);
}

TEST_F(CostsFileTest, RejectsMissingColumnOrEmptySymbol)
{
    EXPECT_THROW(loadCostsCsv(write("symbol,spread_pips\nEURUSD\n"), CostModel{}), std::invalid_argument);
    EXPECT_THROW(loadCostsCsv(write("symbol,spread_pips\n,1.0\n"), CostModel{}), std::invalid_argument);
}

TEST_F(CostsFileTest, ThrowsWhenTheFileDoesNotExist)
{
    EXPECT_THROW(loadCostsCsv("/no/such/costs.csv", CostModel{}), std::runtime_error);
}
