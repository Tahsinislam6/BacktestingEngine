#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>

#include "cli.hpp"
#include "dataset.hpp"
#include "flip_strategy.hpp"
#include "study.hpp"

// ---------------------------------------------------------------------------------------------
// Baselines
//
// A baseline is any class with update(const Bar&), ready() and value() (the price of its line).
// To optimise your own: write the class, then add ONE entry to registry() below with
//   - the parameter ranges to search
//   - a factory that builds the strategy for one set of parameters
//   - how many bars of history one set needs before it is ready (its warm-up)
// ---------------------------------------------------------------------------------------------

// STAND-IN baseline so the program runs end to end: a plain moving average of the close.
// Replace it with your own indicators.
class SimpleMovingAverage
{
public:
    explicit SimpleMovingAverage(int period) : period_(period) {}

    void update(const Bar& bar)
    {
        window_.push_back(bar.close);
        sum_ += bar.close;
        if (static_cast<int>(window_.size()) > period_)
        {
            sum_ -= window_.front();
            window_.pop_front();
        }
    }
    bool ready() const { return static_cast<int>(window_.size()) >= period_; }
    double value() const { return sum_ / static_cast<double>(window_.size()); }

private:
    int period_;
    std::deque<double> window_;
    double sum_{0.0};
};

struct BaselineSpec
{
    std::vector<ParamRange> ranges;
    StrategyFactory factory;
    std::function<std::size_t(const Params&)> warmupBars;
};

static std::map<std::string, BaselineSpec> registry()
{
    std::map<std::string, BaselineSpec> r;

    r["sma"] = {
        {{"period", 5, 100, 5}},
        [](const Params& p, std::size_t assets) -> std::unique_ptr<MultiStrategy>
        {
            return std::make_unique<FlipStrategy<SimpleMovingAverage>>(assets, SimpleMovingAverage(p.getInt("period")));
        },
        [](const Params& p) { return static_cast<std::size_t>(p.getInt("period")); }};

    return r;
}

int main(int argc, char** argv)
{
    try
    {
        const CliOptions cli = parseArgs(std::vector<std::string>(argv + 1, argv + argc));
        if (cli.help)
        {
            std::cout << usage();
            return 0;
        }

        const std::map<std::string, BaselineSpec> baselines = registry();
        auto spec = baselines.find(cli.baseline);
        if (spec == baselines.end())
        {
            std::string names;
            for (const auto& entry : baselines)
                names += (names.empty() ? "" : ", ") + entry.first;
            throw std::invalid_argument("unknown baseline '" + cli.baseline + "' (available: " + names + ")");
        }

        StudyOptions study;
        study.validation.optimise.objective = cli.objective;
        study.validation.optimise.minTrades = cli.minTrades;
        study.validation.optimise.grid.threads = cli.threads;
        study.validation.optimise.grid.startingCash = cli.startingCash;
        study.validation.warmupBars = spec->second.warmupBars;
        study.trainFraction = cli.trainFraction;
        study.walkTrainBars = cli.walkTrain;
        study.walkTestBars = cli.walkTest;

        const AlignedData inSample = loadFolder(cli.inSampleDir);
        std::cout << "In-sample: " << inSample.symbols.size() << " assets, " << inSample.snapshots.size() << " days\n";

        AlignedData outOfSample;
        if (!cli.outSampleDir.empty())
        {
            outOfSample = loadFolder(cli.outSampleDir);
            std::cout << "Out-of-sample: " << outOfSample.symbols.size() << " assets, "
                      << outOfSample.snapshots.size() << " days\n";
        }
        std::cout << "\n";

        const StudyResult result = runStudy(inSample, cli.outSampleDir.empty() ? nullptr : &outOfSample,
                                            spec->second.ranges, spec->second.factory, study);
        std::cout << formatStudy(result, study, cli.top);

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
