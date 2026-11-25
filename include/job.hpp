#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "backtester.hpp"
#include "bar.hpp"
#include "metrics.hpp"
#include "registry.hpp"

// ---- datasets: a dataset is a file NAME inside one data folder; a request can never name a path ----

// The .csv and .txt file names in the folder, sorted. Throws std::runtime_error if the folder does not exist.
std::vector<std::string> listDatasets(const std::string& dataDir);

// The full path of a dataset. Throws std::invalid_argument for an empty name, a name with a slash, a backslash or "..",
// or a file that is not in the folder.
std::string resolveDataset(const std::string& dataDir, const std::string& name);

// Loads a dataset with the named layout (see layouts.hpp). "auto", the default, reads a file whose header has <DATE> (a
// Stooq daily file, dates like 20100717) as "stooq" and any other file as "simple" (timestamp,open,high,low,close).
// The series' symbol is the file name. Throws std::invalid_argument for an unknown layout or dataset name, and
// std::runtime_error / std::invalid_argument from the loader when the layout does not fit the file.
BarSeries loadDataset(const std::string& dataDir, const std::string& name, const std::string& layout = "auto");

// ---- one run ----

// A description of one backtest, as a request carries it.
struct RunSpec
{
    std::string strategy;
    NamedParams params;             // may be partial; defaults fill the rest
    std::string dataset;            // a file name, see listDatasets
    double cash{10000.0};           // starting cash, must be above 0
    double periodsPerYear{260.0};   // bars per year, for CAGR / Sharpe / Sortino; must be above 0 (260 for daily FX, 365 for crypto)
    std::string layout{"auto"};     // how the dataset file is read: a name from listLayouts (layouts.hpp)
};

struct RunOutput
{
    RunSpec spec;                           // as run: params holds the complete set, defaults included
    std::vector<std::int64_t> timestamps;   // one per bar (the equity curve's x axis)
    BacktestResult result;
    Metrics metrics;
};

// Everything about a request that can be checked without reading the dataset: cash and periodsPerYear above 0, a known
// strategy with valid parameters, a dataset name that is a file in the folder, a known layout. Throws std::invalid_argument. runJob calls
// it first; the run manager calls it at submit time so a bad request is refused at once instead of failing later.
void validateSpec(const StrategyRegistry& registry, const std::string& dataDir, const RunSpec& spec);

// Fills in the default parameters, loads the dataset, runs the strategy and measures it.
// Throws std::invalid_argument for an unknown strategy or parameter, a bad parameter value, a bad dataset name, or cash /
// periodsPerYear that is not above 0; std::runtime_error if the dataset cannot be read or has no bars.
RunOutput runJob(const StrategyRegistry& registry, const std::string& dataDir, const RunSpec& spec);
