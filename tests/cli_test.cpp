#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "cli.hpp"

// parseArgs turns the words after the program name into options and explains anything wrong.
// A run is a SYSTEM: a baseline with fixed parameters (--params) and at least one component. A baseline is never run
// alone, and nothing is searched or tuned. The one other kind of run is --signal, which has no baseline at all.

namespace
{
CliOptions parse(std::vector<std::string> args)
{
    return parseArgs(args);
}

// the shortest valid system run
std::vector<std::string> base()
{
    return {"--insample", "data/in", "--baseline", "kijun", "--params", "period=26", "--confirm", "rsi"};
}

std::vector<std::string> with(std::vector<std::string> extra)
{
    std::vector<std::string> args = base();
    args.insert(args.end(), extra.begin(), extra.end());
    return args;
}

// a system run whose only component is the one given (so the "at least one component" rule is met by it alone)
std::vector<std::string> bareWith(std::vector<std::string> extra)
{
    std::vector<std::string> args = {"--insample", "data/in", "--baseline", "kijun", "--params", "period=26"};
    args.insert(args.end(), extra.begin(), extra.end());
    return args;
}
}

// ---------- the basics ----------

TEST(CliTest, ReadsTheSystemAndItsDefaults)
{
    const CliOptions o = parse(base());

    EXPECT_EQ(o.inSampleDir, "data/in");
    EXPECT_EQ(o.baseline, "kijun");
    EXPECT_EQ(o.params, "period=26");
    EXPECT_EQ(o.confirm, "rsi");
    EXPECT_TRUE(o.outSampleDir.empty());
    EXPECT_EQ(o.threads, 0);
    EXPECT_DOUBLE_EQ(o.startingCash, 100000.0);
    EXPECT_FALSE(o.help);
    EXPECT_TRUE(o.costsFile.empty());
    EXPECT_DOUBLE_EQ(o.spreadMultiplier, 1.0);
    EXPECT_FALSE(o.noCosts);
    EXPECT_FALSE(o.keepSundays);
}

TEST(CliTest, ReadsEveryBasicOption)
{
    const CliOptions o = parse({"--insample", "a", "--outsample", "b", "--baseline", "kama", "--params", "period=15,slow=30",
                                "--filter", "atr_expansion", "--threads", "4", "--cash", "5e4"});

    EXPECT_EQ(o.inSampleDir, "a");
    EXPECT_EQ(o.outSampleDir, "b");
    EXPECT_EQ(o.baseline, "kama");
    EXPECT_EQ(o.params, "period=15,slow=30");
    EXPECT_EQ(o.threads, 4);
    EXPECT_DOUBLE_EQ(o.startingCash, 50000.0);
}

TEST(CliTest, KeepSundaysIsAFlagWithoutAValue)
{
    EXPECT_FALSE(parse(base()).keepSundays);
    EXPECT_TRUE(parse(with({"--keep-sundays"})).keepSundays);
    EXPECT_TRUE(parse({"--keep-sundays", "--insample", "x", "--baseline", "kijun", "--params", "period=26", "--confirm", "rsi"}).keepSundays);
}

TEST(CliTest, OptionOrderDoesNotMatter)
{
    const CliOptions o = parse({"--confirm", "rsi", "--params", "period=26", "--insample", "x", "--baseline", "kijun"});

    EXPECT_EQ(o.inSampleDir, "x");
    EXPECT_EQ(o.baseline, "kijun");
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
    EXPECT_THROW(parse({"--baseline", "kijun", "--params", "period=26", "--confirm", "rsi"}), std::invalid_argument);
}

TEST(CliTest, UnknownOptionsAreRejected)
{
    EXPECT_THROW(parse(with({"--wat"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"stray"})), std::invalid_argument);
}

TEST(CliTest, AMissingValueIsRejected)
{
    EXPECT_THROW(parse({"--insample"}), std::invalid_argument);
    EXPECT_THROW(parse({"--insample", "--baseline", "kijun"}), std::invalid_argument);     // the next word is a flag
    EXPECT_THROW(parse(with({"--note"})), std::invalid_argument);
}

TEST(CliTest, AFlagIsNeverTakenAsTheValueOfAnotherFlag)
{
    EXPECT_THROW(parse(with({"--log-dir", "--note"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--baseline", "--help"})), std::invalid_argument);
}

TEST(CliTest, TheErrorNamesTheProblem)
{
    try
    {
        parse(with({"--threads", "lots"}));
        FAIL() << "should have thrown";
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("--threads"), std::string::npos);
        EXPECT_NE(std::string(e.what()).find("lots"), std::string::npos);
    }
}

TEST(CliTest, NumbersMustBeWholeNumbersWhereTheyShouldBe)
{
    EXPECT_THROW(parse(with({"--threads", "12abc"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--threads", "1.5"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--threads", ""})), std::invalid_argument);
}

TEST(CliTest, DecimalNumbersWithTrailingJunkAreRejected)
{
    EXPECT_THROW(parse(with({"--cash", "100abc"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--cash", "1 000"})), std::invalid_argument);
}

TEST(CliTest, NegativeCountsAreRejectedAndZeroMeansNone)
{
    EXPECT_THROW(parse(with({"--threads", "-2"})), std::invalid_argument);
    EXPECT_EQ(parse(with({"--threads", "0"})).threads, 0);
}

TEST(CliTest, TheStartingCashMustBePositive)
{
    EXPECT_THROW(parse(with({"--cash", "0"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--cash", "-5"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--cash", "ten"})), std::invalid_argument);
}

TEST(CliTest, TheUsageTextMentionsEveryOption)
{
    const std::string u = usage();

    for (const char* flag : {"--insample", "--outsample", "--baseline", "--params", "--threads", "--cash", "--keep-sundays",
                             "--help"})
        EXPECT_NE(u.find(flag), std::string::npos) << flag;
}

// ---------- what is gone: nothing is searched or tuned, and nothing runs without a component ----------

TEST(CliTest, TheSearchAndScreenOptionsAreGone)
{
    for (const std::vector<std::string>& gone : std::vector<std::vector<std::string>>{
             {"--mode", "nnfx"}, {"--mode", "flip"}, {"--screen"}, {"--screen-csv", "s.csv"}, {"--objective", "sharpe"},
             {"--min-trades", "30"}, {"--train-fraction", "0.7"}, {"--walk-train", "100"}, {"--walk-test", "50"},
             {"--top", "5"}, {"--csv", "out.csv"}})
        EXPECT_THROW(parse(with(gone)), std::invalid_argument) << gone.front();
}

TEST(CliTest, TheUsageTextNoLongerMentionsThem)
{
    for (const char* flag : {"--mode", "--screen", "--objective", "--min-trades", "--train-fraction", "--walk-train",
                             "--walk-test", "--top", "--csv"})
        EXPECT_EQ(usage().find(flag), std::string::npos) << flag;
}

TEST(CliTest, ABaselineIsNeverRunAlone)
{
    EXPECT_THROW(parse({"--insample", "a", "--baseline", "kijun", "--params", "period=26"}), std::invalid_argument);
    for (const char* flag : {"--confirm", "--exit-indicator", "--filter"})
        EXPECT_NO_THROW(parse(bareWith({flag, "rsi"}))) << flag;
}

TEST(CliTest, TheErrorForAMissingComponentSaysWhy)
{
    try
    {
        parse({"--insample", "a", "--baseline", "kijun", "--params", "period=26"});
        FAIL() << "should have thrown";
    }
    catch (const std::invalid_argument& e)
    {
        EXPECT_NE(std::string(e.what()).find("--confirm"), std::string::npos);
    }
}

TEST(CliTest, TheBaselineIsRequired)
{
    EXPECT_THROW(parse({"--insample", "a", "--params", "period=26", "--confirm", "rsi"}), std::invalid_argument);
}

TEST(CliTest, TheParametersAreRequiredButMayBeEmptyForABaselineWithNone)
{
    EXPECT_THROW(parse({"--insample", "a", "--baseline", "kijun", "--confirm", "rsi"}), std::invalid_argument);
    EXPECT_NO_THROW(parse({"--insample", "a", "--baseline", "kijun", "--params", "", "--confirm", "rsi"}));
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

// ---------- control ----------

TEST(CliTest, ControlAndNoCrossExitAreOffByDefault)
{
    const CliOptions o = parse(base());
    EXPECT_EQ(o.control, 0);
    EXPECT_FALSE(o.noCrossExit);
}

TEST(CliTest, ReadsTheControlOptions)
{
    const CliOptions o = parse(with({"--no-cross-exit", "--control", "100"}));

    EXPECT_EQ(o.control, 100);
    EXPECT_TRUE(o.noCrossExit);
}

TEST(CliTest, TheControlWorksWithOrWithoutTheCrossBackExit)
{
    EXPECT_NO_THROW(parse(with({"--control", "50"})));
    EXPECT_NO_THROW(parse(with({"--no-cross-exit", "--control", "50"})));
}

TEST(CliTest, TheControlCountMustBeAWholeNumber)
{
    EXPECT_THROW(parse(with({"--no-cross-exit", "--control", "-1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--no-cross-exit", "--control", "x"})), std::invalid_argument);
}

TEST(CliTest, UsageMentionsTheControlOptions)
{
    EXPECT_NE(usage().find("--control"), std::string::npos);
    EXPECT_NE(usage().find("--no-cross-exit"), std::string::npos);
}

// ---------- components and limits ----------

TEST(CliTest, TheComponentOptionsAreOffByDefault)
{
    const CliOptions o = parse(bareWith({"--filter", "atr_expansion"}));
    EXPECT_TRUE(o.confirm.empty());
    EXPECT_TRUE(o.exitIndicator.empty());
    EXPECT_EQ(o.filter, "atr_expansion");
    EXPECT_EQ(o.confirmWindow, 7);
    EXPECT_FALSE(o.oneCandle);
    EXPECT_FALSE(o.continuation);
    EXPECT_FALSE(o.maxOpenRisk.has_value());
    EXPECT_FALSE(o.maxPerCurrency.has_value());
}

TEST(CliTest, ReadsTheComponentOptions)
{
    const CliOptions o = parse(with({"--exit-indicator", "chandelier_exit", "--filter", "atr_expansion",
                                     "--confirm-window", "5", "--one-candle", "--continuation", "--max-open-risk", "0.06",
                                     "--max-per-currency", "2"}));

    EXPECT_EQ(o.confirm, "rsi");
    EXPECT_EQ(o.exitIndicator, "chandelier_exit");
    EXPECT_EQ(o.filter, "atr_expansion");
    EXPECT_EQ(o.confirmWindow, 5);
    EXPECT_TRUE(o.oneCandle);
    EXPECT_TRUE(o.continuation);
    ASSERT_TRUE(o.maxOpenRisk.has_value());
    EXPECT_DOUBLE_EQ(*o.maxOpenRisk, 0.06);
    ASSERT_TRUE(o.maxPerCurrency.has_value());
    EXPECT_EQ(*o.maxPerCurrency, 2);
}

TEST(CliTest, TheLimitsAreChecked)
{
    EXPECT_THROW(parse(with({"--max-open-risk", "0"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--max-open-risk", "-0.1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--max-open-risk", "x"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--max-per-currency", "0"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--confirm-window", "0"})), std::invalid_argument);
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
    EXPECT_EQ(parse(with({"--component-control", "100"})).componentControl, 100);
}

TEST(CliTest, TheComponentControlCountMustBeAWholeNumber)
{
    EXPECT_THROW(parse(with({"--component-control", "-1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--component-control", "x"})), std::invalid_argument);
}

TEST(CliTest, UsageMentionsTheComponentControl)
{
    EXPECT_NE(usage().find("--component-control"), std::string::npos);
}

// ---------- the log ----------

TEST(CliTest, TheLogIsOffByDefault)
{
    const CliOptions o = parse(base());
    EXPECT_TRUE(o.logDir.empty());
    EXPECT_TRUE(o.note.empty());
}

TEST(CliTest, ReadsTheLogFolderAndNote)
{
    const CliOptions o = parse(with({"--entry-study", "100", "--log-dir", "results", "--note", "first try"}));
    EXPECT_EQ(o.logDir, "results");
    EXPECT_EQ(o.note, "first try");
}

TEST(CliTest, TheLogFolderNeedsATestThatIsLogged)
{
    EXPECT_THROW(parse(with({"--log-dir", "results"})), std::invalid_argument);
    EXPECT_NO_THROW(parse(with({"--no-cross-exit", "--control", "50", "--log-dir", "results"})));
    EXPECT_NO_THROW(parse(with({"--component-control", "50", "--log-dir", "results"})));
    EXPECT_NO_THROW(parse(with({"--entry-study", "50", "--log-dir", "results"})));
    EXPECT_NO_THROW(parse(with({"--entry-control", "50", "--log-dir", "results"})));
}

TEST(CliTest, TheNoteNeedsTheLogFolder)
{
    EXPECT_THROW(parse(with({"--note", "why"})), std::invalid_argument);
}

// ---------- entry study and entry control ----------

TEST(CliTest, TheEntryStudyIsOffByDefault)
{
    EXPECT_EQ(parse(base()).entryStudy, 0);
}

TEST(CliTest, ReadsTheEntryStudyRunCount)
{
    EXPECT_EQ(parse(with({"--entry-study", "300"})).entryStudy, 300);
}

TEST(CliTest, TheEntryStudyCountMustBeAWholeNumber)
{
    EXPECT_THROW(parse(with({"--entry-study", "many"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--entry-study"})), std::invalid_argument);
}

TEST(CliTest, TheEntryControlIsOffByDefault)
{
    EXPECT_EQ(parse(base()).entryControl, 0);
}

TEST(CliTest, ReadsTheEntryControlCount)
{
    EXPECT_EQ(parse(with({"--entry-control", "200"})).entryControl, 200);
}

TEST(CliTest, TheEntryControlNeedsAConfirmationOrAFilter)
{
    EXPECT_THROW(parse(bareWith({"--exit-indicator", "rsi", "--entry-control", "100"})), std::invalid_argument);   // an exit is not an entry
    EXPECT_NO_THROW(parse(bareWith({"--filter", "atr_expansion", "--entry-control", "100"})));
    EXPECT_NO_THROW(parse(bareWith({"--confirm", "rsi", "--entry-control", "100"})));
}

TEST(CliTest, TheEntryControlCountMustBeAWholeNumber)
{
    EXPECT_THROW(parse(with({"--entry-control", "lots"})), std::invalid_argument);
}

// ---------- --params ----------

TEST(CliTest, TheParametersAreKeptAsTextForTheBaselineToCheck)
{
    EXPECT_EQ(parse(with({})).params, "period=26");
    EXPECT_EQ(parse({"--insample", "a", "--baseline", "kijun", "--params", "x=1, y=2", "--confirm", "rsi"}).params, "x=1, y=2");
}

// ---------- --signal: the entry study of a signal that is not a baseline ----------

namespace
{
std::vector<std::string> signalRun(std::vector<std::string> extra = {})
{
    std::vector<std::string> args = {"--insample", "data/in", "--entry-study", "100"};
    args.insert(args.end(), extra.begin(), extra.end());
    return args;
}
}

TEST(CliTest, TheSignalIsOffByDefault)
{
    const CliOptions o = parse(base());
    EXPECT_TRUE(o.signalKind.empty());
    EXPECT_EQ(o.signalLookback, 0);
}

TEST(CliTest, ReadsTheSignalKindAndLookback)
{
    CliOptions o = parse(signalRun({"--signal", "breakout:20"}));
    EXPECT_EQ(o.signalKind, "breakout");
    EXPECT_EQ(o.signalLookback, 20);
    o = parse(signalRun({"--signal", "momentum:60"}));
    EXPECT_EQ(o.signalKind, "momentum");
    EXPECT_EQ(o.signalLookback, 60);
}

TEST(CliTest, ABadSignalIsRefused)
{
    for (const char* bad : {"cross:20", "trend:20", "breakout", "breakout:", ":20", "breakout:x", "breakout:0", "breakout:-5", "breakout:2.5"})
        EXPECT_THROW(parse(signalRun({"--signal", bad})), std::invalid_argument) << bad;
}

TEST(CliTest, TheSignalNeedsTheEntryStudy)
{
    EXPECT_THROW(parse({"--insample", "data/in", "--signal", "breakout:20"}), std::invalid_argument);
}

TEST(CliTest, TheSignalNeedsNoBaselineOrComponent)
{
    EXPECT_NO_THROW(parse(signalRun({"--signal", "breakout:20"})));
}

TEST(CliTest, TheSignalHasNoBaselineParametersOrOtherControls)
{
    for (const std::vector<std::string>& extra : std::vector<std::vector<std::string>>{
             {"--baseline", "kijun"}, {"--params", "period=26"}, {"--control", "10", "--no-cross-exit"},
             {"--component-control", "10", "--confirm", "rsi"}, {"--entry-control", "10", "--confirm", "rsi"}})
    {
        std::vector<std::string> args = {"--signal", "breakout:20"};
        args.insert(args.end(), extra.begin(), extra.end());
        EXPECT_THROW(parse(signalRun(args)), std::invalid_argument) << extra.front();
    }
}

TEST(CliTest, TheSignalStudyAcceptsComponentsAnOutsampleAndALog)
{
    EXPECT_NO_THROW(parse(signalRun({"--signal", "momentum:60", "--confirm", "rsi", "--outsample", "data/out",
                                     "--log-dir", "results"})));
}

// ---------- --grid: every baseline x C1 pair in one batch ----------

namespace
{
// a grid run needs the plan file, both selection folders, a log folder and a note with the pass rule
std::vector<std::string> gridRun(std::vector<std::string> extra = {})
{
    std::vector<std::string> args = {"--insample", "data/in", "--outsample", "data/out", "--grid", "plan.txt",
                                     "--log-dir", "results", "--note", "rule: ..."};
    args.insert(args.end(), extra.begin(), extra.end());
    return args;
}
}

TEST(CliTest, TheGridIsOffByDefault)
{
    const CliOptions o = parse(base());
    EXPECT_TRUE(o.gridFile.empty());
    EXPECT_TRUE(o.holdoutDir.empty());
    EXPECT_TRUE(o.ledgerFile.empty());
    EXPECT_EQ(o.priorTries, 0);
    EXPECT_DOUBLE_EQ(o.minBeats, 0.9);
    EXPECT_DOUBLE_EQ(o.minExpectancy, 0.03);
}

TEST(CliTest, ReadsTheGridOptions)
{
    const CliOptions o = parse(gridRun({"--holdout", "data/old", "--prior-tries", "40", "--min-beats", "0.95",
                                        "--min-expectancy", "0.05", "--ledger", "ledger.md", "--control", "100"}));
    EXPECT_EQ(o.gridFile, "plan.txt");
    EXPECT_EQ(o.holdoutDir, "data/old");
    EXPECT_EQ(o.priorTries, 40);
    EXPECT_DOUBLE_EQ(o.minBeats, 0.95);
    EXPECT_DOUBLE_EQ(o.minExpectancy, 0.05);
    EXPECT_EQ(o.ledgerFile, "ledger.md");
    EXPECT_EQ(o.control, 100);
}

TEST(CliTest, TheGridNeedsNoBaselineParametersOrComponents)
{
    EXPECT_NO_THROW(parse(gridRun()));
}

TEST(CliTest, TheGridNeedsBothSelectionFoldersALogFolderAndANote)
{
    EXPECT_THROW(parse({"--insample", "a", "--grid", "plan.txt", "--log-dir", "r", "--note", "n"}), std::invalid_argument);     // no outsample
    EXPECT_THROW(parse({"--insample", "a", "--outsample", "b", "--grid", "plan.txt", "--note", "n"}), std::invalid_argument);   // no log folder
    EXPECT_THROW(parse({"--insample", "a", "--outsample", "b", "--grid", "plan.txt", "--log-dir", "r"}), std::invalid_argument); // no note
}

TEST(CliTest, TheGridCannotBeCombinedWithASystemOrOtherTests)
{
    for (const std::vector<std::string>& extra : std::vector<std::vector<std::string>>{
             {"--baseline", "kijun"}, {"--params", "period=26"}, {"--confirm", "rsi"}, {"--exit-indicator", "rsi"},
             {"--filter", "atr_expansion"}, {"--signal", "breakout:20"}, {"--entry-study", "100"},
             {"--entry-control", "100", "--confirm", "rsi"}, {"--component-control", "100", "--confirm", "rsi"}})
        EXPECT_THROW(parse(gridRun(extra)), std::invalid_argument) << extra.front();
}

TEST(CliTest, TheGridAcceptsTheSharedNnfxSwitches)
{
    EXPECT_NO_THROW(parse(gridRun({"--one-candle", "--continuation", "--max-per-currency", "2", "--max-open-risk", "0.06",
                                   "--confirm-window", "5", "--no-cross-exit", "--costs", "c.csv", "--spread-multiplier", "2"})));
}

TEST(CliTest, TheGridOnlyOptionsNeedTheGrid)
{
    for (const std::vector<std::string>& extra : std::vector<std::vector<std::string>>{
             {"--holdout", "h"}, {"--prior-tries", "3"}, {"--min-beats", "0.9"}, {"--min-expectancy", "0.03"}, {"--ledger", "l.md"}})
        EXPECT_THROW(parse(with(extra)), std::invalid_argument) << extra.front();
}

TEST(CliTest, TheGridThresholdsAreChecked)
{
    EXPECT_THROW(parse(gridRun({"--min-beats", "1.5"})), std::invalid_argument);
    EXPECT_THROW(parse(gridRun({"--min-beats", "-0.1"})), std::invalid_argument);
    EXPECT_THROW(parse(gridRun({"--min-beats", "x"})), std::invalid_argument);
    EXPECT_THROW(parse(gridRun({"--min-expectancy", "x"})), std::invalid_argument);
    EXPECT_THROW(parse(gridRun({"--prior-tries", "-1"})), std::invalid_argument);
    EXPECT_THROW(parse(gridRun({"--prior-tries", "1.5"})), std::invalid_argument);
    EXPECT_NO_THROW(parse(gridRun({"--prior-tries", "0"})));
}

TEST(CliTest, UsageMentionsTheGridOptions)
{
    for (const char* flag : {"--grid", "--holdout", "--prior-tries", "--min-beats", "--min-expectancy", "--ledger"})
        EXPECT_NE(usage().find(flag), std::string::npos) << flag;
}

// ---------- --rates and --swap-markup: swap (financing) ----------

TEST(CliTest, SwapIsOffByDefault)
{
    const CliOptions o = parse(base());
    EXPECT_TRUE(o.ratesFile.empty());
    EXPECT_DOUBLE_EQ(o.swapMarkup, 0.0);
}

TEST(CliTest, ReadsTheRatesFileAndTheMarkup)
{
    const CliOptions o = parse(with({"--rates", "../data/rates.csv", "--swap-markup", "0.5"}));
    EXPECT_EQ(o.ratesFile, "../data/rates.csv");
    EXPECT_DOUBLE_EQ(o.swapMarkup, 0.5);
}

TEST(CliTest, TheMarkupNeedsTheRatesFileAndCannotBeNegative)
{
    EXPECT_THROW(parse(with({"--swap-markup", "0.5"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--rates", "r.csv", "--swap-markup", "-0.1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--rates", "r.csv", "--swap-markup", "x"})), std::invalid_argument);
    EXPECT_NO_THROW(parse(with({"--rates", "r.csv", "--swap-markup", "0"})));
}

TEST(CliTest, SwapWorksWithSystemsSignalsAndTheGrid)
{
    EXPECT_NO_THROW(parse(with({"--rates", "r.csv"})));
    EXPECT_NO_THROW(parse(gridRun({"--rates", "r.csv", "--swap-markup", "1"})));
    EXPECT_NO_THROW(parse({"--insample", "a", "--signal", "breakout:20", "--entry-study", "100", "--rates", "r.csv"}));
}

TEST(CliTest, UsageMentionsTheSwapOptions)
{
    for (const char* flag : {"--rates", "--swap-markup"})
        EXPECT_NE(usage().find(flag), std::string::npos) << flag;
}

// ---------- no scaling out ----------

TEST(CliTest, ScalingOutIsOnByDefault)
{
    EXPECT_FALSE(parse(bareWith({"--filter", "atr_expansion"})).noScaling);
}

TEST(CliTest, ReadsNoScaling)
{
    EXPECT_TRUE(parse(with({"--no-scaling"})).noScaling);
}

TEST(CliTest, UsageMentionsNoScaling)
{
    EXPECT_NE(usage().find("--no-scaling"), std::string::npos);
}

TEST(CliTest, ReadsTrailAfter)
{
    EXPECT_FALSE(parse(bareWith({"--filter", "atr_expansion"})).trailAfter.has_value());
    const CliOptions o = parse(with({"--trail-after", "2"}));
    ASSERT_TRUE(o.trailAfter.has_value());
    EXPECT_DOUBLE_EQ(*o.trailAfter, 2.0);
}

TEST(CliTest, TrailAfterMustBePositive)
{
    EXPECT_THROW(parse(with({"--trail-after", "0"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--trail-after", "-1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--trail-after", "x"})), std::invalid_argument);
}

TEST(CliTest, UsageMentionsTrailAfter)
{
    EXPECT_NE(usage().find("--trail-after"), std::string::npos);
}

// ---------- stop, ATR period, risk, trailing and the grid's exit ----------

TEST(CliTest, TheStopAndSizeOptionsAreOffByDefault)
{
    const CliOptions o = parse(bareWith({"--filter", "atr_expansion"}));
    EXPECT_FALSE(o.stopAtr.has_value());
    EXPECT_FALSE(o.atrPeriod.has_value());
    EXPECT_FALSE(o.risk.has_value());
    EXPECT_FALSE(o.noTrail);
    EXPECT_FALSE(o.crossExit);
}

TEST(CliTest, TheChaseLimitOptions)
{
    const CliOptions none = parse(with({}));
    EXPECT_FALSE(none.maxDistance.has_value());
    EXPECT_FALSE(none.noChaseLimit);

    const CliOptions v = parse(with({"--max-distance", "3.5"}));
    ASSERT_TRUE(v.maxDistance.has_value());
    EXPECT_DOUBLE_EQ(*v.maxDistance, 3.5);
    EXPECT_TRUE(parse(with({"--no-chase-limit"})).noChaseLimit);

    EXPECT_THROW(parse(with({"--max-distance", "0"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--max-distance", "-1"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--max-distance", "2", "--no-chase-limit"})), std::invalid_argument);
}

TEST(CliTest, ReadsTheStopAndSizeOptions)
{
    const CliOptions o = parse(with({"--stop-atr", "2.5", "--atr-period", "20", "--risk", "0.0025", "--no-trail"}));
    ASSERT_TRUE(o.stopAtr.has_value());
    EXPECT_DOUBLE_EQ(*o.stopAtr, 2.5);
    ASSERT_TRUE(o.atrPeriod.has_value());
    EXPECT_EQ(*o.atrPeriod, 20);
    ASSERT_TRUE(o.risk.has_value());
    EXPECT_DOUBLE_EQ(*o.risk, 0.0025);
    EXPECT_TRUE(o.noTrail);
}

TEST(CliTest, TheStopAndSizeOptionsAreChecked)
{
    for (const char* bad : {"0", "-1", "x"})
    {
        EXPECT_THROW(parse(with({"--stop-atr", bad})), std::invalid_argument) << bad;
        EXPECT_THROW(parse(with({"--risk", bad})), std::invalid_argument) << bad;
    }
    EXPECT_THROW(parse(with({"--risk", "0.6"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--atr-period", "0"})), std::invalid_argument);
    EXPECT_THROW(parse(with({"--atr-period", "x"})), std::invalid_argument);
}

TEST(CliTest, UsageMentionsTheStopAndSizeOptions)
{
    for (const char* flag : {"--stop-atr", "--atr-period", "--risk", "--no-trail", "--cross-exit"})
        EXPECT_NE(usage().find(flag), std::string::npos) << flag;
}

TEST(CliTest, TheC1ExitSwitchNeedsTheGrid)
{
    EXPECT_FALSE(parse(gridRun({})).c1Exit);
    EXPECT_THROW(parse(with({"--c1-exit"})), std::invalid_argument);
    EXPECT_THROW(parse(gridRun({"--exit-indicator", "rsi"})), std::invalid_argument);    // the grid already refuses its own exit indicator
    EXPECT_TRUE(parse(gridRun({"--c1-exit"})).c1Exit);
}

TEST(CliTest, TheGridsCrossExitSwitchNeedsTheGridAndExcludesNoCrossExit)
{
    EXPECT_THROW(parse(with({"--cross-exit"})), std::invalid_argument);
    EXPECT_THROW(parse(gridRun({"--cross-exit", "--no-cross-exit"})), std::invalid_argument);
    const CliOptions o = parse(gridRun({"--cross-exit"}));
    EXPECT_TRUE(o.crossExit);
}
