#include "result_log.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "report.hpp"

namespace
{
    std::string num(double v)
    {
        if (std::isnan(v))
            return "nan";
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%.6g", v);
        return buffer;
    }

    // Mean of the finite values; NaN when there are none.
    double meanExpectancy(const std::vector<ArmResult>& arms)
    {
        double sum = 0;
        int count = 0;
        for (const ArmResult& a : arms)
            if (std::isfinite(a.expectancyR))
            {
                sum += a.expectancyR;
                ++count;
            }
        return count > 0 ? sum / count : std::nan("");
    }

    const char* contextColumns() { return "timestamp,note,setup,costs,insample,outsample"; }

    std::string contextFields(const LogContext& c)
    {
        return csvField(c.timestamp) + "," + csvField(c.note) + "," + csvField(c.setup) + "," + csvField(c.costs) + ","
               + csvField(c.inSample) + "," + csvField(c.outSample);
    }

    std::string armFields(const ArmResult& a)
    {
        return num(a.sharpe) + "," + num(a.expectancyR) + "," + num(a.totalR) + "," + std::to_string(a.trades);
    }
}

std::string logTimestamp(std::time_t when)
{
    std::tm parts{};
    gmtime_r(&when, &parts);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &parts);
    return buffer;
}

std::string setupText(const CliOptions& cli)
{
    std::string s = "nnfx";
    if (cli.noCrossExit)
        s += " no-cross-exit";
    if (!cli.confirm.empty())
        s += " confirm=" + cli.confirm + " confirm-window=" + std::to_string(cli.confirmWindow);
    if (!cli.exitIndicator.empty())
        s += " exit-indicator=" + cli.exitIndicator;
    if (!cli.filter.empty())
        s += " filter=" + cli.filter;
    if (cli.oneCandle)
        s += " one-candle";
    if (cli.continuation)
        s += " continuation";
    if (cli.maxOpenRisk)
        s += " max-open-risk=" + num(*cli.maxOpenRisk);
    if (cli.maxPerCurrency)
        s += " max-per-currency=" + std::to_string(*cli.maxPerCurrency);
    if (!cli.gridFile.empty())
    {
        s += " grid=" + cli.gridFile;
        if (!cli.holdoutDir.empty())
            s += " holdout=" + cli.holdoutDir;
        s += " prior-tries=" + std::to_string(cli.priorTries) + " min-beats=" + num(cli.minBeats) +
             " min-expectancy=" + num(cli.minExpectancy);
    }
    // Breakeven and trailing exits are real stop orders (they fill at the stop, not at a later close). Log rows from
    // before that change have no such marker.
    return s + " stops=orders";
}

std::string costsText(const CliOptions& cli)
{
    const std::string swap = cli.ratesFile.empty() ? "no swap" : "swap " + cli.ratesFile + " markup " + num(cli.swapMarkup);
    if (cli.noCosts)
        return "no spread, " + swap;
    return (cli.costsFile.empty() ? "typical" : cli.costsFile) + " x" + num(cli.spreadMultiplier) + ", " + swap;
}

LogContext makeLogContext(const CliOptions& cli, std::time_t now)
{
    LogContext c;
    c.timestamp = logTimestamp(now);
    c.note = cli.note;
    c.setup = setupText(cli);
    c.costs = costsText(cli);
    c.inSample = cli.inSampleDir;
    c.outSample = cli.outSampleDir;
    return c;
}

void appendLog(const std::string& dir, const std::string& file, const std::string& header,
               const std::vector<std::string>& rows)
{
    if (rows.empty())
        return;
    namespace fs = std::filesystem;
    const fs::path path = fs::path(dir) / file;
    std::error_code error;
    fs::create_directories(dir, error);
    if (error)
        throw std::runtime_error("Cannot create " + dir + ": " + error.message());

    bool needHeader = true;
    if (std::ifstream existing{path})
    {
        std::string first;
        if (std::getline(existing, first))
        {
            if (first + "\n" != header)
                throw std::runtime_error(path.string() + " has different columns from this version of the program; "
                                         "move or rename it so a new log can start");
            needHeader = false;
        }
    }

    std::ofstream out(path, std::ios::app);
    if (!out)
        throw std::runtime_error("Cannot write " + path.string());
    if (needHeader)
        out << header;
    for (const std::string& r : rows)
        out << r;
    out.close();
    if (!out)
        throw std::runtime_error("Failed while writing " + path.string());
}

std::string directionLogHeader()
{
    return std::string(contextColumns()) + ",baseline,params,data,random_runs,sharpe,expectancy_r,total_r,trades,"
                                           "random_mean_expectancy_r,beats_sharpe,beats_expectancy,beats_total_r\n";
}

std::string directionLogRow(const LogContext& context, const std::string& baseline, const std::string& params,
                            const std::string& data, int runs, const ControlResult& result)
{
    return contextFields(context) + "," + csvField(baseline) + "," + csvField(params) + "," + csvField(data) + ","
           + std::to_string(runs) + "," + armFields(result.baseline) + "," + num(meanExpectancy(result.random)) + ","
           + num(result.sharpeRank) + "," + num(result.expectancyRank) + "," + num(result.totalRRank) + "\n";
}

std::string componentLogHeader()
{
    return std::string(contextColumns()) +
           ",baseline,params,role,component,data,random_runs,with_sharpe,with_expectancy_r,with_total_r,with_trades,"
           "without_sharpe,without_expectancy_r,without_total_r,without_trades,random_mean_expectancy_r,"
           "beats_sharpe,beats_expectancy,beats_total_r,behaviour\n";
}

std::string componentLogRow(const LogContext& context, const std::string& baseline, const std::string& params,
                            ComponentRole role, const std::string& component, const std::string& data, int runs,
                            const ComponentControlResult& result)
{
    return contextFields(context) + "," + csvField(baseline) + "," + csvField(params) + "," + roleName(role) + ","
           + csvField(component) + "," + csvField(data) + "," + std::to_string(runs) + "," + armFields(result.real) + ","
           + armFields(result.without) + "," + num(meanExpectancy(result.random)) + "," + num(result.sharpeRank) + ","
           + num(result.expectancyRank) + "," + num(result.totalRRank) + "," + csvField(result.behaviour) + "\n";
}

std::string entryStudyLogHeader()
{
    std::string h = std::string(contextColumns()) + ",baseline,params,data,random_runs,events";
    for (int hz : entryHorizons())
    {
        const std::string p = ",h" + std::to_string(hz) + "_";
        h += p + "mean_atr" + p + "net_atr" + p + "beats";
    }
    return h + ",target_first,stop_first,neither,random_target_first,target_beats,race_r,random_race_r,"
               "mfe_atr,mae_atr,random_mfe_atr,random_mae_atr,excursion_beats\n";
}

std::string entryStudyLogRow(const LogContext& context, const std::string& baseline, const std::string& params,
                             const std::string& data, int runs, const EntryStudyResult& result)
{
    std::string row = contextFields(context) + "," + csvField(baseline) + "," + csvField(params) + "," + csvField(data) +
                      "," + std::to_string(runs) + "," + std::to_string(result.events);
    for (const HorizonResult& h : result.horizons)
        row += "," + num(h.meanAtr) + "," + num(h.netMeanAtr) + "," + num(h.beats);
    for (double v : {result.targetFirst, result.stopFirst, result.neither, result.randomTargetFirst, result.targetBeats,
                     result.raceR, result.randomRaceR, result.mfeAtr, result.maeAtr, result.randomMfeAtr,
                     result.randomMaeAtr, result.excursionBeats})
        row += "," + num(v);
    return row + "\n";
}

std::string entryControlLogHeader()
{
    std::string h = std::string(contextColumns()) +
                    ",baseline,params,role,component,data,random_runs,with_events,without_events,random_mean_events";
    for (int hz : entryHorizons())
    {
        const std::string p = ",h" + std::to_string(hz) + "_";
        h += p + "with" + p + "without" + p + "random" + p + "beats";
    }
    return h + ",race_with,race_without,race_random,race_beats,balance_with,balance_without,balance_random,balance_beats,"
               "behaviour\n";
}

std::string entryControlLogRow(const LogContext& context, const std::string& baseline, const std::string& params,
                               ComponentRole role, const std::string& component, const std::string& data, int runs,
                               const EntryControlResult& result)
{
    auto finiteMean = [](const std::vector<double>& v)
    {
        double sum = 0;
        int n = 0;
        for (double x : v)
            if (std::isfinite(x)) { sum += x; ++n; }
        return n > 0 ? sum / n : std::nan("");
    };
    auto column = [&](auto member)
    {
        std::vector<double> out;
        for (const EntryArm& a : result.random)
            out.push_back(member(a));
        return out;
    };

    std::vector<double> events;
    for (const EntryArm& a : result.random)
        events.push_back(a.events);

    std::string row = contextFields(context) + "," + csvField(baseline) + "," + csvField(params) + "," + roleName(role) + "," +
                      csvField(component) + "," + csvField(data) + "," + std::to_string(runs) + "," +
                      std::to_string(result.real.events) + "," + std::to_string(result.without.events) + "," +
                      num(finiteMean(events));
    for (std::size_t h = 0; h < result.real.meanAtr.size(); ++h)
        row += "," + num(result.real.meanAtr[h]) + "," + num(result.without.meanAtr[h]) + "," +
               num(finiteMean(column([h](const EntryArm& a) { return a.meanAtr[h]; }))) + "," + num(result.meanBeats[h]);
    row += "," + num(result.real.raceR) + "," + num(result.without.raceR) + "," +
           num(finiteMean(column([](const EntryArm& a) { return a.raceR; }))) + "," + num(result.raceBeats);
    row += "," + num(result.real.balance) + "," + num(result.without.balance) + "," +
           num(finiteMean(column([](const EntryArm& a) { return a.balance; }))) + "," + num(result.balanceBeats);
    return row + "," + csvField(result.behaviour) + "\n";
}

std::string gridLogHeader()
{
    return std::string(contextColumns()) + ",random_runs," + gridCsvHeader();
}

std::vector<std::string> gridLogRows(const LogContext& context, const std::vector<GridRow>& rows, int controlRuns)
{
    std::vector<std::string> out;
    for (const GridRow& r : rows)
        out.push_back(contextFields(context) + "," + std::to_string(controlRuns) + "," + gridCsvRow(r));
    return out;
}

namespace
{
    std::string ledgerCell(std::string text)
    {
        for (char& c : text)
            if (c == '|')
                c = '/';
            else if (c == '\n' || c == '\r')
                c = ' ';
        return text;
    }
}

std::string ledgerRow(const LogContext& c, const std::string& what, const std::string& result)
{
    return "| " + ledgerCell(c.timestamp) + " | " + ledgerCell(what) + " | " +
           ledgerCell("in: " + c.inSample + ", out: " + c.outSample) + " | " + ledgerCell(c.note) + " | " +
           ledgerCell(result) + " |\n";
}

void appendLedger(const std::string& path, const std::string& row)
{
    namespace fs = std::filesystem;
    const fs::path file(path);
    if (file.has_parent_path())
    {
        std::error_code error;
        fs::create_directories(file.parent_path(), error);
        if (error)
            throw std::runtime_error("Cannot create " + file.parent_path().string() + ": " + error.message());
    }
    const bool existed = fs::exists(file);
    std::ofstream out(file, std::ios::app);
    if (!out)
        throw std::runtime_error("Cannot write " + path);
    if (!existed)
        out << "# Tries ledger\n\nOne row per test run. M = the number of pairs tried.\n\n"
               "| when | what | data | pass rule (written before the run) | result |\n|---|---|---|---|---|\n";
    out << row;
    out.close();
    if (!out)
        throw std::runtime_error("Failed while writing " + path);
}
