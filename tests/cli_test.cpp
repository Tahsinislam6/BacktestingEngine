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
    EXPECT_TRUE(o.costsFile.empty());
    EXPECT_DOUBLE_EQ(o.spreadMultiplier, 1.0);
    EXPECT_FALSE(o.noCosts);
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

// ---------- costs ----------

TEST(CliTest, ReadsTheCostOptions)
{
    const CliOptions o = parse(with({"--costs", "my.csv", "--spread-multiplier", "1.5"}));

    EXPECT_EQ(o.costsFile, "my.csv");
    EXPECT_DOUBLE_EQ(o.spreadMultiplier, 1.5);
}

TEST(CliTest, NoCostsIsAFlag)
{
    EXPECT_TRUE(parse(with({"--no-costs"})).noCosts);
}

TEST(CliTest, TheSpreadMultiplierCannotBeNegativeAndMustBeANumber)
{
    EXPECT_THROW(parse(with({"--spread-multiplier", "-1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--spread-multiplier", "abc"})), std::invalid_argument);
    EXPECT_NO_THROW(parse(with({"--spread-multiplier", "0"})));
}

TEST(CliTest, NoCostsCannotBeCombinedWithCostsOrAMultiplier)
{
    EXPECT_THROW(parse(with({"--no-costs", "--costs", "x.csv"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--no-costs", "--spread-multiplier", "2"})), std::invalid_argument);
}

TEST(CliTest, UsageMentionsTheCostOptions)
{
    for (const char* flag : {"--costs", "--spread-multiplier", "--no-costs"})
        EXPECT_NE(usage().find(flag), std::string::npos) << flag;
}

// ---------- mode ----------

TEST(CliTest, ModeDefaultsToFlipAndCanBeNnfx)
{
    EXPECT_EQ(parse(base()).mode, Mode::Flip);
    EXPECT_EQ(parse(with({"--mode", "nnfx"})).mode, Mode::Nnfx);
    EXPECT_EQ(parse(with({"--mode", "flip"})).mode, Mode::Flip);
}

TEST(CliTest, ModeMustBeKnown)
{
    EXPECT_THROW(parse(with({"--mode", "hold"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--mode"})), std::invalid_argument);
    EXPECT_NE(usage().find("--mode"), std::string::npos);
}

// ---------- control ----------

TEST(CliTest, ControlAndNoCrossExitAreOffByDefault)
{
    const CliOptions o = parse(base());
    EXPECT_EQ(o.control, 0);
    EXPECT_FALSE(o.noCrossExit);
}

TEST(CliTest, ReadsTheControlOptions)
{
    const CliOptions o = parse(with({"--mode", "nnfx", "--no-cross-exit", "--control", "100"}));

    EXPECT_EQ(o.control, 100);
    EXPECT_TRUE(o.noCrossExit);
}

TEST(CliTest, TheControlNeedsNnfxModeAndNoCrossExit)
{
    EXPECT_THROW(parse(with({"--control", "50"})), std::invalid_argument);                       // flip mode
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--control", "50"})), std::invalid_argument);     // cross-back exit on
    EXPECT_THROW(parse(with({"--no-cross-exit"})), std::invalid_argument);                       // flip mode
    EXPECT_NO_THROW(parse(with({"--mode", "nnfx", "--no-cross-exit"})));
}

TEST(CliTest, TheControlCountMustBeAWholeNumber)
{
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--no-cross-exit", "--control", "-1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--no-cross-exit", "--control", "x"})), std::invalid_argument);
}

TEST(CliTest, UsageMentionsTheControlOptions)
{
    EXPECT_NE(usage().find("--control"), std::string::npos);
    EXPECT_NE(usage().find("--no-cross-exit"), std::string::npos);
}

// ---------- screen ----------

TEST(CliTest, ScreenIsOffByDefaultWithTheUsualThresholds)
{
    const CliOptions o = parse(base());
    EXPECT_FALSE(o.screen);
    EXPECT_TRUE(o.screenCsv.empty());
    EXPECT_DOUBLE_EQ(o.minBeats, 0.9);
    EXPECT_DOUBLE_EQ(o.minExpectancy, 0.03);
}

TEST(CliTest, ReadsTheScreenOptions)
{
    const CliOptions o = parse({"--screen", "--insample", "a", "--outsample", "b", "--screen-csv", "s.csv",
                                "--min-beats", "0.8", "--min-expectancy", "0.05", "--control", "100"});

    EXPECT_TRUE(o.screen);
    EXPECT_EQ(o.screenCsv, "s.csv");
    EXPECT_DOUBLE_EQ(o.minBeats, 0.8);
    EXPECT_DOUBLE_EQ(o.minExpectancy, 0.05);
    EXPECT_EQ(o.control, 100);
}

TEST(CliTest, ScreenNeedsOutOfSampleData)
{
    EXPECT_THROW(parse({"--screen", "--insample", "a"}), std::invalid_argument);
}

TEST(CliTest, ScreenCannotBeCombinedWithABaselineOrAMode)
{
    EXPECT_THROW(parse({"--screen", "--insample", "a", "--outsample", "b", "--baseline", "sma"}), std::invalid_argument);
    EXPECT_THROW(parse({"--screen", "--insample", "a", "--outsample", "b", "--mode", "flip"}), std::invalid_argument);
}

TEST(CliTest, TheScreenThresholdsAreChecked)
{
    EXPECT_THROW(parse({"--screen", "--insample", "a", "--outsample", "b", "--min-beats", "1.5"}), std::invalid_argument);
    EXPECT_THROW(parse({"--screen", "--insample", "a", "--outsample", "b", "--min-beats", "-0.1"}), std::invalid_argument);
    EXPECT_THROW(parse({"--screen", "--insample", "a", "--outsample", "b", "--min-expectancy", "x"}), std::invalid_argument);
}

TEST(CliTest, ScreenCsvNeedsScreen)
{
    EXPECT_THROW(parse(with({"--screen-csv", "s.csv"})), std::invalid_argument);
}

TEST(CliTest, UsageMentionsTheScreenOptions)
{
    for (const char* flag : {"--screen", "--screen-csv", "--min-beats", "--min-expectancy"})
        EXPECT_NE(usage().find(flag), std::string::npos) << flag;
}

// ---------- components and limits ----------

TEST(CliTest, TheComponentOptionsAreOffByDefault)
{
    const CliOptions o = parse(base());
    EXPECT_TRUE(o.confirm.empty());
    EXPECT_TRUE(o.exitIndicator.empty());
    EXPECT_TRUE(o.filter.empty());
    EXPECT_EQ(o.confirmWindow, 7);
    EXPECT_FALSE(o.oneCandle);
    EXPECT_FALSE(o.continuation);
    EXPECT_FALSE(o.maxOpenRisk.has_value());
    EXPECT_FALSE(o.maxPerCurrency.has_value());
}

TEST(CliTest, ReadsTheComponentOptions)
{
    const CliOptions o = parse(with({"--mode", "nnfx", "--confirm", "rsi", "--exit-indicator", "rsi", "--filter", "atr_expansion",
                                     "--confirm-window", "5", "--one-candle", "--continuation", "--max-open-risk", "0.06",
                                     "--max-per-currency", "2"}));

    EXPECT_EQ(o.confirm, "rsi");
    EXPECT_EQ(o.exitIndicator, "rsi");
    EXPECT_EQ(o.filter, "atr_expansion");
    EXPECT_EQ(o.confirmWindow, 5);
    EXPECT_TRUE(o.oneCandle);
    EXPECT_TRUE(o.continuation);
    ASSERT_TRUE(o.maxOpenRisk.has_value());
    EXPECT_DOUBLE_EQ(*o.maxOpenRisk, 0.06);
    ASSERT_TRUE(o.maxPerCurrency.has_value());
    EXPECT_EQ(*o.maxPerCurrency, 2);
}

TEST(CliTest, TheComponentOptionsNeedNnfxModeOrTheScreen)
{
    for (const char* flag : {"--one-candle", "--continuation"})
        EXPECT_THROW(parse(with({flag})), std::invalid_argument) << flag;
    for (const char* flag : {"--confirm", "--exit-indicator", "--filter"})
        EXPECT_THROW(parse(with({flag, "rsi"})), std::invalid_argument) << flag;
    EXPECT_THROW(parse(with({"--max-open-risk", "0.05"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--max-per-currency", "1"})), std::invalid_argument);

    EXPECT_NO_THROW(parse(with({"--mode", "nnfx", "--one-candle"})));
    EXPECT_NO_THROW(parse({"--screen", "--insample", "a", "--outsample", "b", "--confirm", "rsi", "--continuation"}));
}

TEST(CliTest, TheLimitsAreChecked)
{
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--max-open-risk", "0"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--max-open-risk", "-0.1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--max-open-risk", "x"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--max-per-currency", "0"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--confirm-window", "0"})), std::invalid_argument);
}

TEST(CliTest, UsageMentionsTheComponentOptions)
{
    for (const char* flag : {"--confirm", "--exit-indicator", "--filter", "--confirm-window", "--one-candle",
                             "--continuation", "--max-open-risk", "--max-per-currency"})
        EXPECT_NE(usage().find(flag), std::string::npos) << flag;
}

// ---------- component control ----------

TEST(CliTest, TheComponentControlIsOffByDefault)
{
    EXPECT_EQ(parse(base()).componentControl, 0);
}

TEST(CliTest, ReadsTheComponentControlCount)
{
    const CliOptions o = parse(with({"--mode", "nnfx", "--confirm", "rsi", "--component-control", "100"}));

    EXPECT_EQ(o.componentControl, 100);
}

TEST(CliTest, TheComponentControlNeedsNnfxModeAndAComponent)
{
    EXPECT_THROW(parse(with({"--component-control", "50"})), std::invalid_argument);                      // flip mode
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--component-control", "50"})), std::invalid_argument);    // nothing to control
    EXPECT_NO_THROW(parse(with({"--mode", "nnfx", "--filter", "atr_expansion", "--component-control", "50"})));
    EXPECT_NO_THROW(parse(with({"--mode", "nnfx", "--exit-indicator", "rsi", "--component-control", "50"})));
}

TEST(CliTest, TheComponentControlCannotBeCombinedWithTheScreen)
{
    EXPECT_THROW(parse({"--screen", "--insample", "a", "--outsample", "b", "--confirm", "rsi", "--component-control", "50"}),
                 std::invalid_argument);
}

TEST(CliTest, TheComponentControlCountMustBeAWholeNumber)
{
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--confirm", "rsi", "--component-control", "-1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--mode", "nnfx", "--confirm", "rsi", "--component-control", "x"})), std::invalid_argument);
}

TEST(CliTest, UsageMentionsTheComponentControl)
{
    EXPECT_NE(usage().find("--component-control"), std::string::npos);
}

TEST(CliTest, TheLogIsOffByDefault)
{
    const CliOptions o = parse(base());
    EXPECT_TRUE(o.logDir.empty());
    EXPECT_TRUE(o.note.empty());
}

TEST(CliTest, ReadsTheLogFolderAndNote)
{
    const CliOptions o = parse(with({"--screen", "--outsample", "data/out", "--log-dir", "results", "--note", "first try"}));
    EXPECT_EQ(o.logDir, "results");
    EXPECT_EQ(o.note, "first try");
}

TEST(CliTest, TheLogFolderNeedsATestThatIsLogged)
{
    EXPECT_THROW(parse(with({"--log-dir", "results"})), std::invalid_argument);
    EXPECT_NO_THROW(parse(with({"--mode", "nnfx", "--no-cross-exit", "--control", "50", "--log-dir", "results"})));
    EXPECT_NO_THROW(parse(with({"--mode", "nnfx", "--confirm", "rsi", "--component-control", "50", "--log-dir", "results"})));
    EXPECT_NO_THROW(parse(with({"--screen", "--outsample", "data/out", "--log-dir", "results"})));
}

TEST(CliTest, TheNoteNeedsTheLogFolder)
{
    EXPECT_THROW(parse(with({"--screen", "--outsample", "data/out", "--note", "why"})), std::invalid_argument);
}
