#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "result_log.hpp"

// The results log appends one csv row per result, one file per type of test. These tests read the files back
// and check that every number lands in its own column.

namespace
{
namespace fs = std::filesystem;

// Splits one csv line into fields; understands "quoted, fields" and "" inside quotes.
std::vector<std::string> splitCsv(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    const std::size_t end = !line.empty() && line.back() == '\n' ? line.size() - 1 : line.size();
    for (std::size_t i = 0; i < end; ++i)
    {
        const char c = line[i];
        if (quoted)
        {
            if (c == '"' && i + 1 < end && line[i + 1] == '"') { cur += '"'; ++i; }
            else if (c == '"') quoted = false;
            else cur += c;
        }
        else if (c == '"') quoted = true;
        else if (c == ',') { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> lines(const std::string& path)
{
    std::ifstream in(path);
    std::vector<std::string> out;
    for (std::string l; std::getline(in, l);)
        out.push_back(l);
    return out;
}

std::string freshDir(const std::string& name)
{
    const fs::path p = fs::temp_directory_path() / ("result_log_test_" + name);
    fs::remove_all(p);
    return p.string();
}

LogContext context()
{
    LogContext c;
    c.timestamp = "2026-11-17T10:00:00Z";
    c.note = "first, trial";
    c.setup = "nnfx confirm=rsi";
    c.costs = "typical x1";
    c.inSample = "in";
    c.outSample = "out";
    return c;
}

ControlResult controlResult()
{
    ControlResult r;
    r.baseline = {0.5, 0.1, 0.04, 12.5, 300};
    r.random = {{0.2, 0.0, 0.01, 3.0, 290}, {0.4, 0.0, 0.03, 9.0, 310}};
    r.sharpeRank = 0.75;
    r.expectancyRank = 0.8;
    r.totalRRank = 0.85;
    return r;
}

ComponentControlResult componentResult()
{
    ComponentControlResult r;
    r.real = {0.36, 0.2, 0.059, 52.35, 888};
    r.without = {-0.03, -0.1, -0.006, -5.36, 842};
    r.random = {{0.1, 0, 0.01, 4, 700}, {0.3, 0, 0.02, 8, 780}};
    r.sharpeRank = 0.89;
    r.expectancyRank = 0.88;
    r.totalRRank = 0.94;
    r.behaviour = "+1 51%, -1 49%, neutral 0%; keeps its state 88% of bars";
    return r;
}

EntryControlResult entryControlResult()
{
    EntryControlResult r;
    auto arm = [](int events, double base)
    {
        EntryArm a;
        a.events = events;
        for (int i = 0; i < 7; ++i)
            a.meanAtr.push_back(base + 0.01 * i);
        a.raceR = base * 2;
        a.balance = base * 10;
        return a;
    };
    r.real = arm(300, 0.10);
    r.without = arm(400, 0.02);
    r.random = {arm(250, 0.04), arm(270, 0.06)};
    for (int i = 0; i < 7; ++i)
        r.meanBeats.push_back(0.5 + 0.05 * i);
    r.raceBeats = 0.9;
    r.balanceBeats = 0.8;
    r.behaviour = "+1 51%, -1 49%, neutral 0%; keeps its state 87% of bars";
    return r;
}

EntryStudyResult entryResult()
{
    EntryStudyResult r;
    r.events = 321;
    const int hs[] = {1, 2, 3, 5, 10, 20, 40};
    for (int i = 0; i < 7; ++i)
    {
        HorizonResult h;
        h.horizon = hs[i];
        h.events = 300 - i;
        h.meanAtr = 0.01 * (i + 1);
        h.netMeanAtr = 0.01 * (i + 1) - 0.002;
        h.beats = 0.5 + 0.05 * i;
        r.horizons.push_back(h);
    }
    r.targetFirst = 0.61;
    r.stopFirst = 0.37;
    r.neither = 0.02;
    r.randomTargetFirst = 0.58;
    r.targetBeats = 0.8;
    r.raceR = 0.05;
    r.randomRaceR = -0.01;
    r.mfeAtr = 2.5;
    r.maeAtr = 1.5;
    r.randomMfeAtr = 2.0;
    r.randomMaeAtr = 2.0;
    r.excursionBeats = 0.7;
    return r;
}
}

TEST(ResultLog, TimestampIsUtcIso)
{
    EXPECT_EQ(logTimestamp(0), "1970-01-01T00:00:00Z");
    EXPECT_EQ(logTimestamp(1700000000), "2023-11-14T22:13:20Z");
}

TEST(ResultLog, AppendCreatesTheFolderAndWritesTheHeaderOnce)
{
    const std::string dir = freshDir("append") + "/nested";
    appendLog(dir, "t.csv", "a,b\n", {"1,2\n", "3,4\n"});
    appendLog(dir, "t.csv", "a,b\n", {"5,6\n"});
    const auto l = lines(dir + "/t.csv");
    ASSERT_EQ(l.size(), 4u);
    EXPECT_EQ(l[0], "a,b");
    EXPECT_EQ(l[1], "1,2");
    EXPECT_EQ(l[2], "3,4");
    EXPECT_EQ(l[3], "5,6");
}

TEST(ResultLog, AppendWithNoRowsWritesNothing)
{
    const std::string dir = freshDir("empty");
    appendLog(dir, "t.csv", "a,b\n", {});
    EXPECT_FALSE(fs::exists(dir + "/t.csv"));
}

TEST(ResultLog, AFileWithOtherColumnsIsLeftAlone)
{
    const std::string dir = freshDir("mismatch");
    appendLog(dir, "t.csv", "a,b\n", {"1,2\n"});
    EXPECT_THROW(appendLog(dir, "t.csv", "a,b,c\n", {"1,2,3\n"}), std::runtime_error);
    EXPECT_EQ(lines(dir + "/t.csv").size(), 2u);
}

TEST(ResultLog, AnEmptyExistingFileGetsTheHeader)
{
    const std::string dir = freshDir("emptyfile");
    fs::create_directories(dir);
    std::ofstream(dir + "/t.csv").close();
    appendLog(dir, "t.csv", "a,b\n", {"1,2\n"});
    const auto l = lines(dir + "/t.csv");
    ASSERT_EQ(l.size(), 2u);
    EXPECT_EQ(l[0], "a,b");
}

TEST(ResultLog, EveryRowHasAsManyFieldsAsItsHeader)
{
    ScreenRow s;
    s.name = "kijun";
    s.params = "period=141";
    s.error = "boom, with a comma";
    const std::vector<std::string> screen = screenLogRows(context(), {s, s}, 200);
    ASSERT_EQ(screen.size(), 2u);
    const std::size_t screenCols = splitCsv(screenLogHeader()).size();
    for (const std::string& r : screen)
        EXPECT_EQ(splitCsv(r).size(), screenCols);
    EXPECT_EQ(splitCsv(directionLogRow(context(), "kijun", "period=141", "in-sample", 200, controlResult())).size(),
              splitCsv(directionLogHeader()).size());
    EXPECT_EQ(splitCsv(componentLogRow(context(), "kijun", "period=141", ComponentRole::Confirmation, "rsi",
                                       "out-of-sample", 200, componentResult())).size(),
              splitCsv(componentLogHeader()).size());
    EXPECT_EQ(splitCsv(entryStudyLogRow(context(), "kijun", "period=141", "in-sample", 200, entryResult())).size(),
              splitCsv(entryStudyLogHeader()).size());
    EXPECT_EQ(splitCsv(entryControlLogRow(context(), "kijun", "period=141", ComponentRole::Confirmation, "rsi",
                                          "out-of-sample", 2, entryControlResult())).size(),
              splitCsv(entryControlLogHeader()).size());
}

namespace
{
// The value in the column called `name` of a row.
std::string field(const std::string& header, const std::string& row, const std::string& name)
{
    const auto h = splitCsv(header);
    const auto r = splitCsv(row);
    for (std::size_t i = 0; i < h.size(); ++i)
        if (h[i] == name)
            return r.at(i);
    ADD_FAILURE() << "no column " << name;
    return "";
}
}

TEST(ResultLog, ContextColumnsComeFirstInEveryLog)
{
    const std::vector<std::string> expected = {"timestamp", "note", "setup", "costs", "insample", "outsample"};
    for (const std::string& header : {screenLogHeader(), directionLogHeader(), componentLogHeader(), entryStudyLogHeader(), entryControlLogHeader()})
    {
        const auto h = splitCsv(header);
        ASSERT_GE(h.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
            EXPECT_EQ(h[i], expected[i]);
    }
    const std::string row = directionLogRow(context(), "kijun", "period=141", "in-sample", 200, controlResult());
    const auto f = splitCsv(row);
    EXPECT_EQ(f[0], "2026-11-17T10:00:00Z");
    EXPECT_EQ(f[1], "first, trial");              // the comma survives quoting
    EXPECT_EQ(f[2], "nnfx confirm=rsi");
    EXPECT_EQ(f[3], "typical x1");
    EXPECT_EQ(f[4], "in");
    EXPECT_EQ(f[5], "out");
}

TEST(ResultLog, ScreenRowsCarryTheScreenNumbers)
{
    ScreenRow s;
    s.name = "kijun";
    s.params = "period=141";
    s.inSampleSharpe = 0.36;
    s.neighbourShare = 0.9;
    s.timeSplitScore = -0.21;
    s.oosSharpe = 0.07;
    s.oosExpectancyR = 0.012;
    s.oosTrades = 901;
    s.oosBeatsExpectancy = 0.48;
    s.oosBeatsSharpe = 0.46;
    s.inSampleBeatsExpectancy = 0.88;
    s.candidate = false;
    const std::string h = screenLogHeader();
    const std::string r = screenLogRows(context(), {s}, 200).at(0);
    EXPECT_EQ(field(h, r, "baseline"), "kijun");
    EXPECT_EQ(field(h, r, "params"), "period=141");
    EXPECT_EQ(field(h, r, "random_runs"), "200");
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "in_sample_sharpe")), 0.36);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "time_split_score")), -0.21);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "oos_sharpe")), 0.07);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "oos_expectancy_r")), 0.012);
    EXPECT_EQ(field(h, r, "oos_trades"), "901");
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "oos_beats_expectancy")), 0.48);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "oos_beats_sharpe")), 0.46);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "in_sample_beats_expectancy")), 0.88);
    EXPECT_EQ(field(h, r, "candidate"), "no");
}

TEST(ResultLog, DirectionRowCarriesTheControlNumbers)
{
    const std::string h = directionLogHeader();
    const std::string r = directionLogRow(context(), "kijun", "period=141", "out-of-sample", 2, controlResult());
    EXPECT_EQ(field(h, r, "baseline"), "kijun");
    EXPECT_EQ(field(h, r, "params"), "period=141");
    EXPECT_EQ(field(h, r, "data"), "out-of-sample");
    EXPECT_EQ(field(h, r, "random_runs"), "2");
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "sharpe")), 0.5);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "expectancy_r")), 0.04);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "total_r")), 12.5);
    EXPECT_EQ(field(h, r, "trades"), "300");
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "random_mean_expectancy_r")), 0.02);     // (0.01 + 0.03) / 2
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "beats_sharpe")), 0.75);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "beats_expectancy")), 0.8);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "beats_total_r")), 0.85);
}

TEST(ResultLog, ComponentRowCarriesBothArmsAndTheRanks)
{
    const std::string h = componentLogHeader();
    const std::string r = componentLogRow(context(), "kijun", "period=208", ComponentRole::ExitIndicator, "rsi",
                                          "out-of-sample", 2, componentResult());
    EXPECT_EQ(field(h, r, "role"), "exit indicator");
    EXPECT_EQ(field(h, r, "component"), "rsi");
    EXPECT_EQ(field(h, r, "data"), "out-of-sample");
    EXPECT_EQ(field(h, r, "random_runs"), "2");
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "with_sharpe")), 0.36);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "with_expectancy_r")), 0.059);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "with_total_r")), 52.35);
    EXPECT_EQ(field(h, r, "with_trades"), "888");
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "without_sharpe")), -0.03);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "without_expectancy_r")), -0.006);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "without_total_r")), -5.36);
    EXPECT_EQ(field(h, r, "without_trades"), "842");
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "random_mean_expectancy_r")), 0.015);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "beats_sharpe")), 0.89);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "beats_expectancy")), 0.88);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "beats_total_r")), 0.94);
    EXPECT_EQ(field(h, r, "behaviour"), "+1 51%, -1 49%, neutral 0%; keeps its state 88% of bars");
}

TEST(ResultLog, UndefinedNumbersAreNan)
{
    ControlResult c = controlResult();
    c.baseline.expectancyR = std::nan("");
    c.random.clear();
    const std::string r = directionLogRow(context(), "k", "p", "in-sample", 0, c);
    EXPECT_EQ(field(directionLogHeader(), r, "expectancy_r"), "nan");
    EXPECT_EQ(field(directionLogHeader(), r, "random_mean_expectancy_r"), "nan");
}

TEST(ResultLog, SetupTextNamesWhatWasSwitchedOn)
{
    CliOptions cli;
    cli.mode = Mode::Nnfx;
    cli.noCrossExit = true;
    cli.confirm = "rsi";
    cli.filter = "atr_expansion";
    cli.oneCandle = true;
    cli.maxOpenRisk = 0.06;
    cli.maxPerCurrency = 2;
    const std::string s = setupText(cli);
    for (const char* part : {"nnfx", "no-cross-exit", "confirm=rsi", "filter=atr_expansion", "one-candle",
                             "max-open-risk=0.06", "max-per-currency=2", "objective=sharpe", "min-trades=30"})
        EXPECT_NE(s.find(part), std::string::npos) << part << " missing from: " << s;
    EXPECT_NE(s.find("stops=orders"), std::string::npos) << s;    // marks rows made after breakeven/trailing became stop orders
    EXPECT_EQ(s.find("exit-indicator"), std::string::npos) << s;  // no exit indicator given
    EXPECT_EQ(s.find("continuation"), std::string::npos) << s;
}

TEST(ResultLog, CostsTextSaysWhichSpreadsWereUsed)
{
    CliOptions cli;
    EXPECT_EQ(costsText(cli), "typical x1");
    cli.costsFile = "costs.csv";
    cli.spreadMultiplier = 2;
    EXPECT_EQ(costsText(cli), "costs.csv x2");
    cli.noCosts = true;
    EXPECT_EQ(costsText(cli), "none");
}

TEST(ResultLog, ContextTakesTheFoldersAndNoteFromTheCommandLine)
{
    CliOptions cli;
    cli.inSampleDir = "in";
    cli.outSampleDir = "out";
    cli.note = "why";
    const LogContext c = makeLogContext(cli, 0);
    EXPECT_EQ(c.timestamp, "1970-01-01T00:00:00Z");
    EXPECT_EQ(c.note, "why");
    EXPECT_EQ(c.inSample, "in");
    EXPECT_EQ(c.outSample, "out");
}

TEST(ResultLog, AFlipRunHasNoStopsMarker)
{
    CliOptions cli;            // flip mode by default
    EXPECT_EQ(setupText(cli).find("stops=orders"), std::string::npos);
}

TEST(ResultLog, EntryStudyRowCarriesTheHorizonsTheRaceAndTheExcursions)
{
    const std::string h = entryStudyLogHeader();
    const std::string r = entryStudyLogRow(context(), "kijun", "period=141", "out-of-sample", 200, entryResult());

    EXPECT_EQ(field(h, r, "baseline"), "kijun");
    EXPECT_EQ(field(h, r, "params"), "period=141");
    EXPECT_EQ(field(h, r, "data"), "out-of-sample");
    EXPECT_EQ(field(h, r, "random_runs"), "200");
    EXPECT_EQ(field(h, r, "events"), "321");
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "h1_mean_atr")), 0.01);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "h5_mean_atr")), 0.04);       // the 4th horizon
    EXPECT_NEAR(std::stod(field(h, r, "h5_net_atr")), 0.038, 1e-12);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "h5_beats")), 0.65);          // 0.5 + 0.05 * 3
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "h40_mean_atr")), 0.07);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "h40_beats")), 0.8);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "target_first")), 0.61);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "stop_first")), 0.37);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "neither")), 0.02);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "random_target_first")), 0.58);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "target_beats")), 0.8);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "race_r")), 0.05);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "random_race_r")), -0.01);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "mfe_atr")), 2.5);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "mae_atr")), 1.5);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "random_mfe_atr")), 2.0);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "random_mae_atr")), 2.0);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "excursion_beats")), 0.7);
}

TEST(ResultLog, AnEntryStudyWithoutEventsIsAllNan)
{
    EntryStudyResult empty;
    for (int hz : {1, 2, 3, 5, 10, 20, 40})
    {
        HorizonResult h;
        h.horizon = hz;
        empty.horizons.push_back(h);
    }
    const std::string r = entryStudyLogRow(context(), "k", "p", "in-sample", 10, empty);

    EXPECT_EQ(field(entryStudyLogHeader(), r, "events"), "0");
    EXPECT_EQ(field(entryStudyLogHeader(), r, "h1_mean_atr"), "nan");
    EXPECT_EQ(field(entryStudyLogHeader(), r, "target_first"), "nan");
}

TEST(ResultLog, TheEntryStudyLogHasItsOwnFile)
{
    EXPECT_STREQ(kEntryStudyLogFile, "entry_study_log.csv");
}

TEST(ResultLog, EntryControlRowCarriesBothArmsTheRandomMeanAndTheRanks)
{
    const std::string h = entryControlLogHeader();
    const std::string r = entryControlLogRow(context(), "kijun", "period=141", ComponentRole::Filter, "atr_expansion",
                                             "out-of-sample", 2, entryControlResult());

    EXPECT_EQ(field(h, r, "role"), "filter");
    EXPECT_EQ(field(h, r, "component"), "atr_expansion");
    EXPECT_EQ(field(h, r, "data"), "out-of-sample");
    EXPECT_EQ(field(h, r, "random_runs"), "2");
    EXPECT_EQ(field(h, r, "with_events"), "300");
    EXPECT_EQ(field(h, r, "without_events"), "400");
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "random_mean_events")), 260.0);
    EXPECT_NEAR(std::stod(field(h, r, "h1_with")), 0.10, 1e-12);
    EXPECT_NEAR(std::stod(field(h, r, "h1_without")), 0.02, 1e-12);
    EXPECT_NEAR(std::stod(field(h, r, "h1_random")), 0.05, 1e-12);            // (0.04 + 0.06) / 2
    EXPECT_NEAR(std::stod(field(h, r, "h5_with")), 0.13, 1e-12);              // the 4th horizon: 0.10 + 0.03
    EXPECT_NEAR(std::stod(field(h, r, "h5_random")), 0.08, 1e-12);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "h5_beats")), 0.65);
    EXPECT_NEAR(std::stod(field(h, r, "h40_with")), 0.16, 1e-12);
    EXPECT_NEAR(std::stod(field(h, r, "h40_beats")), 0.8, 1e-12);
    EXPECT_NEAR(std::stod(field(h, r, "race_with")), 0.2, 1e-12);
    EXPECT_NEAR(std::stod(field(h, r, "race_without")), 0.04, 1e-12);
    EXPECT_NEAR(std::stod(field(h, r, "race_random")), 0.1, 1e-12);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "race_beats")), 0.9);
    EXPECT_NEAR(std::stod(field(h, r, "balance_with")), 1.0, 1e-12);
    EXPECT_NEAR(std::stod(field(h, r, "balance_without")), 0.2, 1e-12);
    EXPECT_NEAR(std::stod(field(h, r, "balance_random")), 0.5, 1e-12);
    EXPECT_DOUBLE_EQ(std::stod(field(h, r, "balance_beats")), 0.8);
    EXPECT_EQ(field(h, r, "behaviour"), "+1 51%, -1 49%, neutral 0%; keeps its state 87% of bars");
}

TEST(ResultLog, TheEntryControlLogHasItsOwnFile)
{
    EXPECT_STREQ(kEntryControlLogFile, "entry_control_log.csv");
}
