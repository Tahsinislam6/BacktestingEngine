#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "cli.hpp"

// parseArgs turns the words after the program name into options and explains anything wrong.

namespace
{
CliOptions parse(std::vector<std::string> args)
{
    return parseArgs(args);
}

// the shortest valid command line
std::vector<std::string> base()
{
    return {"--insample", "data/in"};
}

std::vector<std::string> with(std::vector<std::string> extra)
{
    std::vector<std::string> args = base();
    args.insert(args.end(), extra.begin(), extra.end());
    return args;
}
}

TEST(CliTest, DefaultsWhenOnlyTheInSampleFolderIsGiven)
{
    const CliOptions o = parse(base());

    EXPECT_EQ(o.inSampleDir, "data/in");
    EXPECT_EQ(o.baseline, "sma");
    EXPECT_TRUE(o.outSampleDir.empty());
    EXPECT_TRUE(o.csvOut.empty());
    EXPECT_EQ(o.objective, Objective::Sharpe);
    EXPECT_EQ(o.minTrades, 30);
    EXPECT_EQ(o.threads, 0);
    EXPECT_DOUBLE_EQ(o.trainFraction, 0.7);
    EXPECT_EQ(o.walkTrain, 0u);
    EXPECT_EQ(o.walkTest, 0u);
    EXPECT_DOUBLE_EQ(o.startingCash, 100000.0);
    EXPECT_EQ(o.top, 15u);
    EXPECT_FALSE(o.help);
}

TEST(CliTest, ReadsEveryOption)
{
    const CliOptions o = parse({"--insample", "a", "--outsample", "b", "--baseline", "kama", "--csv", "out.csv",
                                "--objective", "capture", "--min-trades", "12", "--threads", "4",
                                "--train-fraction", "0.8", "--walk-train", "750", "--walk-test", "250",
                                "--cash", "5e4", "--top", "5"});

    EXPECT_EQ(o.inSampleDir, "a");
    EXPECT_EQ(o.outSampleDir, "b");
    EXPECT_EQ(o.baseline, "kama");
    EXPECT_EQ(o.csvOut, "out.csv");
    EXPECT_EQ(o.objective, Objective::CaptureEfficiency);
    EXPECT_EQ(o.minTrades, 12);
    EXPECT_EQ(o.threads, 4);
    EXPECT_DOUBLE_EQ(o.trainFraction, 0.8);
    EXPECT_EQ(o.walkTrain, 750u);
    EXPECT_EQ(o.walkTest, 250u);
    EXPECT_DOUBLE_EQ(o.startingCash, 50000.0);
    EXPECT_EQ(o.top, 5u);
}

TEST(CliTest, KeepSundaysIsAFlagWithoutAValue)
{
    EXPECT_FALSE(parse(base()).keepSundays);
    EXPECT_TRUE(parse(with({"--keep-sundays"})).keepSundays);
    EXPECT_TRUE(parse({"--keep-sundays", "--insample", "x", "--top", "3"}).keepSundays);
}

TEST(CliTest, OptionOrderDoesNotMatter)
{
    const CliOptions o = parse({"--top", "3", "--insample", "x", "--min-trades", "1"});

    EXPECT_EQ(o.inSampleDir, "x");
    EXPECT_EQ(o.top, 3u);
}

TEST(CliTest, EveryObjectiveName)
{
    EXPECT_EQ(parse(with({"--objective", "sharpe"})).objective, Objective::Sharpe);
    EXPECT_EQ(parse(with({"--objective", "sortino"})).objective, Objective::Sortino);
    EXPECT_EQ(parse(with({"--objective", "calmar"})).objective, Objective::Calmar);
    EXPECT_EQ(parse(with({"--objective", "return"})).objective, Objective::TotalReturn);
    EXPECT_EQ(parse(with({"--objective", "capture"})).objective, Objective::CaptureEfficiency);
}

TEST(CliTest, HelpNeedsNothingElse)
{
    EXPECT_TRUE(parse({"--help"}).help);
    EXPECT_TRUE(parse({"-h"}).help);
    EXPECT_TRUE(parse(with({"--help"})).help);
}

TEST(CliTest, TheInSampleFolderIsRequired)
{
    EXPECT_THROW(parse({}), std::invalid_argument);
    EXPECT_THROW(parse({"--top", "3"}), std::invalid_argument);
}

TEST(CliTest, UnknownOptionsAreRejected)
{
    EXPECT_THROW(parse(with({"--wat"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"stray"})), std::invalid_argument);
}

TEST(CliTest, AMissingValueIsRejected)
{
    EXPECT_THROW(parse({"--insample"}), std::invalid_argument);
    EXPECT_THROW(parse({"--insample", "--top", "3"}), std::invalid_argument);     // the next word is a flag
    EXPECT_THROW(parse(with({"--csv"})), std::invalid_argument);
}

TEST(CliTest, AFlagIsNeverTakenAsTheValueOfAnotherFlag)
{
    // with nothing after it to stop the mistake, "--top" would silently become the csv file name
    EXPECT_THROW(parse(with({"--csv", "--top"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--baseline", "--help"})), std::invalid_argument);
}

TEST(CliTest, TheErrorNamesTheProblem)
{
    try
    {
        parse(with({"--min-trades", "lots"}));
        FAIL() << "should have thrown";
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("--min-trades"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("lots"), std::string::npos);
    }
}

TEST(CliTest, NumbersMustBeWholeNumbersWhereTheyShouldBe)
{
    EXPECT_THROW(parse(with({"--min-trades", "12abc"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--min-trades", "1.5"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--threads", ""})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--walk-train", "x", "--walk-test", "1"})), std::invalid_argument);
}

TEST(CliTest, DecimalNumbersWithTrailingJunkAreRejected)
{
    EXPECT_THROW(parse(with({"--cash", "100abc"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--train-fraction", "0.5x"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--cash", "1 000"})), std::invalid_argument);
}

TEST(CliTest, NegativeCountsAreRejected)
{
    EXPECT_THROW(parse(with({"--min-trades", "-1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--threads", "-2"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--top", "-1"})), std::invalid_argument);
}

TEST(CliTest, ZeroIsFineWhereItMeansNone)
{
    EXPECT_EQ(parse(with({"--min-trades", "0"})).minTrades, 0);
    EXPECT_EQ(parse(with({"--top", "0"})).top, 0u);
    EXPECT_EQ(parse(with({"--threads", "0"})).threads, 0);
}

TEST(CliTest, TheTrainFractionMustBeBetweenZeroAndOne)
{
    for (const char* bad : {"0", "1", "-0.2", "1.5", "abc", "nan"})
        EXPECT_THROW(parse(with({"--train-fraction", bad})), std::invalid_argument) << bad;
    EXPECT_DOUBLE_EQ(parse(with({"--train-fraction", "0.5"})).trainFraction, 0.5);
}

TEST(CliTest, TheStartingCashMustBePositive)
{
    EXPECT_THROW(parse(with({"--cash", "0"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--cash", "-5"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--cash", "ten"})), std::invalid_argument);
}

TEST(CliTest, AnUnknownObjectiveIsRejected)
{
    EXPECT_THROW(parse(with({"--objective", "profit"})), std::invalid_argument);
}

TEST(CliTest, WalkForwardNeedsBothWindowSizes)
{
    EXPECT_THROW(parse(with({"--walk-train", "100"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--walk-test", "100"})), std::invalid_argument);
    EXPECT_NO_THROW(parse(with({"--walk-train", "100", "--walk-test", "50"})));
}

TEST(CliTest, TheUsageTextMentionsEveryOption)
{
    const std::string u = usage();

    for (const char* flag : {"--insample", "--outsample", "--baseline", "--objective", "--min-trades",
                             "--train-fraction", "--walk-train", "--walk-test", "--csv", "--threads",
                             "--cash", "--top", "--keep-sundays", "--help"})
        EXPECT_NE(u.find(flag), std::string::npos) << flag;
}
