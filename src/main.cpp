#include <algorithm>
#include <ctime>
#include <iostream>

#include "baseline_registry.hpp"
#include "cli.hpp"
#include "component_control.hpp"
#include "control.hpp"
#include "costs.hpp"
#include "dataset.hpp"
#include "entry_study.hpp"
#include "grid.hpp"
#include "nnfx_setup.hpp"
#include "report.hpp"
#include "result_log.hpp"

// Baselines are not listed here: every file in include/baselines/ is found automatically
// (see baseline_registry.hpp and CLAUDE.md).

namespace
{
    EntryStudyOptions entryOptionsOf(const NnfxOptions& nnfx)
    {
        EntryStudyOptions entry;
        entry.atrPeriod = nnfx.atrPeriod;
        entry.maxDistanceAtr = nnfx.maxDistanceAtr;
        entry.stopAtr = nnfx.entry.stopAtr;
        entry.targetAtr = nnfx.entry.takeProfitAtr;
        entry.components = nnfx.components;
        entry.confirmationWindow = nnfx.confirmationWindow;
        return entry;
    }
}

int main(int argc, char** argv)
{
    try
    {
        const CliOptions cli = parseArgs(std::vector<std::string>(argv + 1, argv + argc));
        if (cli.help)
        {
            std::cout << usage() << "\nBaselines: " << baselineNames() << "\nComponents: " << componentNames() << "\n";
            return 0;
        }

        const bool gridRun = !cli.gridFile.empty();
        const bool signalOnly = !cli.signalKind.empty();
        const BaselineInfo* baseline = signalOnly || gridRun ? nullptr : findBaseline(cli.baseline);
        if (!signalOnly && !gridRun && !baseline)
            throw std::invalid_argument("unknown baseline '" + cli.baseline + "' (available: " + baselineNames() + ")");

        const NnfxOptions nnfx = makeNnfxOptions(cli);
        RunOptions run;
        run.threads = cli.threads;
        run.startingCash = cli.startingCash;
        if (!cli.noCosts)
        {
            CostModel costs = typicalFxCosts();
            if (!cli.costsFile.empty())
                costs = loadCostsCsv(cli.costsFile, costs);
            costs.multiplier = cli.spreadMultiplier;
            run.costs = costs;
        }
        if (!cli.ratesFile.empty())
        {
            run.swap = loadRatesCsv(cli.ratesFile);
            run.swap.markupPct = cli.swapMarkup;
        }

        const AlignedData inSample = loadFolder(cli.inSampleDir, !cli.keepSundays);
        const LogContext logContext = makeLogContext(cli, std::time(nullptr));
        if (!cli.ratesFile.empty())
        {
            std::cout << "Swap on: rates from " << cli.ratesFile << ", markup " << cli.swapMarkup << " points a year";
            if (!cli.signalKind.empty() || cli.entryStudy > 0 || cli.entryControl > 0)
                std::cout << " (the entry study and the entry control measure price moves and do not charge swap)";
            std::cout << "\n";
            const std::vector<std::string> missing = pairsWithoutRates(run.swap, inSample.symbols);
            if (!missing.empty())
            {
                std::cout << "WARNING: no interest rate for a currency of";
                for (const std::string& s : missing)
                    std::cout << " " << s;
                std::cout << ": those pairs are charged no interest (only the markup)\n";
            }
        }
        else
            std::cout << "Swap off: no --rates file, so no financing is charged.\n";

        if (gridRun)
        {
            // every baseline x C1 pair of the plan, with its parameters fixed in the plan: nothing is searched
            const ResolvedGrid resolved = resolveGridPlan(loadGridPlan(cli.gridFile));
            const AlignedData outOfSample = loadFolder(cli.outSampleDir, !cli.keepSundays);
            AlignedData holdout;
            if (!cli.holdoutDir.empty())
                holdout = loadFolder(cli.holdoutDir, !cli.keepSundays);

            GridOptions grid;
            grid.run = run;
            grid.nnfx = nnfx;
            grid.nnfx.exitOnCross = false;               // the random-direction control needs it off
            grid.controlRuns = cli.control > 0 ? cli.control : 200;
            grid.minBeats = cli.minBeats;
            grid.minExpectancy = cli.minExpectancy;
            grid.priorTries = cli.priorTries;

            std::cout << "Plan " << cli.gridFile << ": " << resolved.baselines.size() << " baselines x " << resolved.c1s.size()
                      << " C1s = " << resolved.baselines.size() * resolved.c1s.size() << " pairs\n";
            std::cout << "In-sample: " << inSample.symbols.size() << " assets, " << inSample.snapshots.size()
                      << " days; out-of-sample: " << outOfSample.symbols.size() << " assets, " << outOfSample.snapshots.size()
                      << " days\n";
            if (!cli.holdoutDir.empty())
                std::cout << "Holdout: " << holdout.symbols.size() << " assets, " << holdout.snapshots.size()
                          << " days (only finalists are run on it, once)\n";
            std::cout << "\n";

            const std::vector<GridRow> rows = runGrid(resolved.baselines, resolved.c1s, inSample, outOfSample, grid,
                                                      cli.holdoutDir.empty() ? nullptr : &holdout);
            std::cout << gridTable(rows, grid);

            appendLog(cli.logDir, kGridLogFile, gridLogHeader(), gridLogRows(logContext, rows, grid.controlRuns));
            std::cout << "\nLogged " << rows.size() << " rows to " << cli.logDir << "/" << kGridLogFile << "\n";

            if (!cli.ledgerFile.empty())
            {
                int finalists = 0, candidates = 0;
                for (const GridRow& r : rows)
                {
                    finalists += r.passedStageOne ? 1 : 0;
                    candidates += r.candidate ? 1 : 0;
                }
                appendLedger(cli.ledgerFile,
                             ledgerRow(logContext,
                                       "grid " + cli.gridFile + ": " + std::to_string(resolved.baselines.size()) + " baselines x " +
                                           std::to_string(resolved.c1s.size()) + " C1 = " + std::to_string(rows.size()) + " pairs" +
                                           (cli.priorTries > 0 ? " (+ " + std::to_string(cli.priorTries) + " earlier)" : ""),
                                       std::to_string(finalists) + " finalists, " + std::to_string(candidates) +
                                           " candidates; rows in " + cli.logDir + "/" + kGridLogFile));
                std::cout << "Ledger row added to " << cli.ledgerFile << "\n";
            }
            return 0;
        }

        if (signalOnly)
        {
            EntryStudyOptions entry = entryOptionsOf(nnfx);
            entry.kind = cli.signalKind == "breakout" ? SignalKind::Breakout : SignalKind::Momentum;
            entry.lookback = cli.signalLookback;
            const std::string paramText = "lookback=" + std::to_string(cli.signalLookback);

            std::cout << "SIGNAL STUDY " << cli.signalKind << ":" << cli.signalLookback
                      << " (nothing is tuned, so the in-sample and the out-of-sample results are both fair tests)\n\n";
            std::vector<std::string> logRows;
            const auto study = [&](const char* label, const AlignedData& data)
            {
                const EntryStudyResult r = runSignalStudy(data, entry, cli.entryStudy, spreadsFor(run.costs, data.symbols));
                std::cout << formatEntryStudy(label, r) << "\n";
                logRows.push_back(entryStudyLogRow(logContext, cli.signalKind, paramText, label, cli.entryStudy, r));
            };
            study("in-sample", inSample);
            if (!cli.outSampleDir.empty())
                study("out-of-sample", loadFolder(cli.outSampleDir, !cli.keepSundays));
            if (!cli.logDir.empty())
            {
                appendLog(cli.logDir, kEntryStudyLogFile, entryStudyLogHeader(), logRows);
                std::cout << "Logged to " << cli.logDir << "/" << kEntryStudyLogFile << "\n";
            }
            return 0;
        }

        // A system: the baseline with its parameters fixed in advance, plus the chosen components. Nothing is searched.
        const Params params = parseParams(cli.params, baseline->ranges);
        const std::string label = baseline->name + " (" + paramsText(params) + ")";
        std::cout << "System: baseline " << label << (cli.noCrossExit ? ", no cross-back exit" : "") << "\n";
        std::cout << "Components: confirmation " << (cli.confirm.empty() ? "-" : cli.confirm) << ", exit indicator "
                  << (cli.exitIndicator.empty() ? "-" : cli.exitIndicator) << ", filter "
                  << (cli.filter.empty() ? "-" : cli.filter) << (cli.continuation ? ", continuation" : "")
                  << (cli.oneCandle ? ", one-candle rule" : "") << "\n";
        std::cout << "In-sample: " << inSample.symbols.size() << " assets, " << inSample.snapshots.size() << " days\n";

        AlignedData outOfSample;
        if (!cli.outSampleDir.empty())
        {
            outOfSample = loadFolder(cli.outSampleDir, !cli.keepSundays);
            std::cout << "Out-of-sample: " << outOfSample.symbols.size() << " assets, "
                      << outOfSample.snapshots.size() << " days\n";
        }
        std::cout << "\n";

        const StrategyFactory factory = baseline->nnfx(nnfx);
        std::cout << formatSystemRun("in-sample", runSystem(inSample, params, factory, run));
        if (!cli.outSampleDir.empty())
            std::cout << "\n" << formatSystemRun("out-of-sample", runSystem(outOfSample, params, factory, run));

        // the same two data sets, one after the other, for each test below
        struct Data { const char* label; const AlignedData* data; };
        std::vector<Data> sets{{"in-sample", &inSample}};
        if (!cli.outSampleDir.empty())
            sets.push_back({"out-of-sample", &outOfSample});

        if (cli.control > 0)
        {
            std::cout << "\nDIRECTION CONTROL (" << label << ")\n";
            std::vector<std::string> logRows;
            for (const Data& d : sets)
            {
                const ControlResult control = runDirectionControl(*d.data, params, baseline->nnfx, nnfx, cli.control, run);
                std::cout << formatControl(d.label, control);
                logRows.push_back(directionLogRow(logContext, baseline->name, paramsText(params), d.label, cli.control, control));
            }
            if (!cli.logDir.empty())
            {
                appendLog(cli.logDir, kDirectionLogFile, directionLogHeader(), logRows);
                std::cout << "Logged to " << cli.logDir << "/" << kDirectionLogFile << "\n";
            }
        }

        if (cli.entryStudy > 0)
        {
            const EntryStudyOptions entry = entryOptionsOf(nnfx);
            std::cout << "\nENTRY STUDY (" << label << ")\n";
            std::vector<std::string> logRows;
            for (const Data& d : sets)
            {
                const EntryStudyResult r = runEntryStudy(*d.data, *baseline, params, entry, cli.entryStudy,
                                                         spreadsFor(run.costs, d.data->symbols));
                std::cout << formatEntryStudy(d.label, r);
                logRows.push_back(entryStudyLogRow(logContext, baseline->name, paramsText(params), d.label, cli.entryStudy, r));
            }
            if (!cli.logDir.empty())
            {
                appendLog(cli.logDir, kEntryStudyLogFile, entryStudyLogHeader(), logRows);
                std::cout << "Logged to " << cli.logDir << "/" << kEntryStudyLogFile << "\n";
            }
        }

        struct Role { ComponentRole role; const std::string& name; };

        if (cli.entryControl > 0)
        {
            const EntryStudyOptions entry = entryOptionsOf(nnfx);
            std::cout << "\nENTRY CONTROL (" << label << ")\n";
            std::vector<std::string> logRows;
            for (const Role& r : {Role{ComponentRole::Confirmation, cli.confirm}, Role{ComponentRole::Filter, cli.filter}})
            {
                if (r.name.empty())
                    continue;
                for (const Data& d : sets)
                {
                    const EntryControlResult control = runEntryComponentControl(*d.data, *baseline, params, entry, r.role,
                                                           cli.entryControl, spreadsFor(run.costs, d.data->symbols));
                    std::cout << formatEntryControl(d.label, r.name, r.role, control);
                    logRows.push_back(entryControlLogRow(logContext, baseline->name, paramsText(params), r.role, r.name,
                                                         d.label, cli.entryControl, control));
                }
            }
            if (!cli.logDir.empty())
            {
                appendLog(cli.logDir, kEntryControlLogFile, entryControlLogHeader(), logRows);
                std::cout << "Logged to " << cli.logDir << "/" << kEntryControlLogFile << "\n";
            }
        }

        if (cli.componentControl > 0)
        {
            std::cout << "\nCOMPONENT CONTROL (" << label << ")\n";
            std::vector<std::string> logRows;
            for (const Role& r : {Role{ComponentRole::Confirmation, cli.confirm}, Role{ComponentRole::ExitIndicator, cli.exitIndicator},
                                  Role{ComponentRole::Filter, cli.filter}})
            {
                if (r.name.empty())
                    continue;
                for (const Data& d : sets)
                {
                    const ComponentControlResult control =
                        runComponentControl(*d.data, params, baseline->nnfx, nnfx, r.role, cli.componentControl, run);
                    std::cout << formatComponentControl(d.label, r.name, r.role, control);
                    logRows.push_back(componentLogRow(logContext, baseline->name, paramsText(params), r.role, r.name, d.label,
                                                      cli.componentControl, control));
                }
            }
            if (!cli.logDir.empty())
            {
                appendLog(cli.logDir, kComponentLogFile, componentLogHeader(), logRows);
                std::cout << "Logged to " << cli.logDir << "/" << kComponentLogFile << "\n";
            }
        }
        return 0;
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << "error: " << e.what() << "\n\n" << usage();
        return 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
