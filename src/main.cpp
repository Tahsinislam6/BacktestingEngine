#include <algorithm>
#include <iostream>

#include "baseline_registry.hpp"
#include "cli.hpp"
#include "control.hpp"
#include "costs.hpp"
#include "dataset.hpp"
#include "screen.hpp"
#include "study.hpp"

// Baselines are not listed here: every file in include/baselines/ is found automatically
// (see baseline_registry.hpp and CLAUDE.md).

int main(int argc, char** argv)
{
    try
    {
        const CliOptions cli = parseArgs(std::vector<std::string>(argv + 1, argv + argc));
        if (cli.help)
        {
            std::cout << usage() << "\nBaselines: " << baselineNames() << "\n";
            return 0;
        }

        const BaselineInfo* baseline = cli.screen ? nullptr : findBaseline(cli.baseline);
        if (!cli.screen && !baseline)
            throw std::invalid_argument("unknown baseline '" + cli.baseline + "' (available: " + baselineNames() + ")");

        NnfxOptions nnfx;
        nnfx.exitOnCross = !cli.noCrossExit && !cli.screen;
        StudyOptions study;
        StrategyFactory factory;
        if (baseline)
        {
            factory = cli.mode == Mode::Nnfx ? baseline->nnfx(nnfx) : baseline->flip;
            study.validation.warmupBars = cli.mode == Mode::Nnfx ? nnfxWarmupBars(*baseline, nnfx) : baseline->warmupBars;
        }

        study.validation.optimise.objective = cli.objective;
        study.validation.optimise.minTrades = cli.minTrades;
        study.validation.optimise.grid.threads = cli.threads;
        study.validation.optimise.grid.startingCash = cli.startingCash;
        if (!cli.noCosts)
        {
            CostModel costs = typicalFxCosts();
            if (!cli.costsFile.empty())
                costs = loadCostsCsv(cli.costsFile, costs);
            costs.multiplier = cli.spreadMultiplier;
            study.validation.optimise.grid.costs = costs;
        }
        study.trainFraction = cli.trainFraction;
        study.walkTrainBars = cli.walkTrain;
        study.walkTestBars = cli.walkTest;

        const AlignedData inSample = loadFolder(cli.inSampleDir, !cli.keepSundays);

        if (cli.screen)
        {
            const AlignedData outOfSample = loadFolder(cli.outSampleDir, !cli.keepSundays);
            std::cout << "In-sample: " << inSample.symbols.size() << " assets, " << inSample.snapshots.size()
                      << " days; out-of-sample: " << outOfSample.symbols.size() << " assets, "
                      << outOfSample.snapshots.size() << " days\n\n";

            ScreenOptions screen;
            screen.study = study;
            screen.nnfx = nnfx;
            screen.controlRuns = cli.control > 0 ? cli.control : 200;
            screen.minBeats = cli.minBeats;
            screen.minExpectancy = cli.minExpectancy;

            const std::vector<ScreenRow> rows = screenBaselines(baselineRegistry(), inSample, outOfSample, screen);
            std::cout << screenTable(rows, screen);
            if (!cli.screenCsv.empty())
            {
                writeTextFile(cli.screenCsv, screenCsv(rows));
                std::cout << "\nScreen written to " << cli.screenCsv << "\n";
            }
            return 0;
        }

        std::cout << "Baseline: " << baseline->name << " ("
                  << (cli.mode == Mode::Nnfx ? (cli.noCrossExit ? "nnfx, no cross-back exit" : "nnfx") : "flip") << ")\n";
        std::cout << "In-sample: " << inSample.symbols.size() << " assets, " << inSample.snapshots.size() << " days\n";

        AlignedData outOfSample;
        if (!cli.outSampleDir.empty())
        {
            outOfSample = loadFolder(cli.outSampleDir, !cli.keepSundays);
            std::cout << "Out-of-sample: " << outOfSample.symbols.size() << " assets, "
                      << outOfSample.snapshots.size() << " days\n";
        }
        std::cout << "\n";

        const StudyResult result = runStudy(inSample, cli.outSampleDir.empty() ? nullptr : &outOfSample,
                                            baseline->ranges, factory, study);
        std::cout << formatStudy(result, study, cli.top);

        if (cli.control > 0 && !result.ranking.empty())
        {
            const Params& winner = result.ranking[0].evaluation.params;
            const GridOptions& grid = study.validation.optimise.grid;
            std::cout << "\nDIRECTION CONTROL (winner " << paramsText(winner) << "; the winner was tuned on the in-sample data, "
                         "so only the out-of-sample line is a fair test)\n";
            std::cout << formatControl("in-sample", runDirectionControl(inSample, winner, baseline->nnfx, nnfx, cli.control, grid));
            if (!cli.outSampleDir.empty())
                std::cout << formatControl("out-of-sample", runDirectionControl(outOfSample, winner, baseline->nnfx, nnfx, cli.control, grid));
        }

        if (!cli.csvOut.empty())
        {
            writeTextFile(cli.csvOut, rankedCsv(result.ranking));
            std::cout << "\nRanked runs written to " << cli.csvOut << "\n";
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
