#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "metrics.hpp"
#include "registry.hpp"
#include "sleeve.hpp"

// A portfolio job: several sleeves side by side, each one asset (a dataset) with its own strategy, settings and bucket of
// cash. Nothing moves between sleeves. It is the job version of the research program `portfolio_run`.
//
//  - A sleeve's name is also its asset's symbol, so two sleeves may use the same dataset under different names (two
//    strategies on one asset, say). Names are unique.
//  - Each sleeve is judged against its own cash; the portfolio against the sum of the cash, on the sum of the curves.
//  - commonPeriodOnly starts everything on the first day every asset has data; otherwise an asset whose data starts years
//    later sits in cash until then and dilutes the portfolio.
//  - There are no spreads or swap here (as in portfolio_run).

struct PortfolioSleeve
{
    std::string name;               // 1 to 40 characters, not blank, unique among the sleeves
    std::string dataset;            // a file name in the data folder
    std::string strategy;
    NamedParams params;             // may be partial; defaults fill the rest
    double cash{};                  // this sleeve's own starting cash, above 0
    std::string layout{"auto"};     // how this sleeve's dataset file is read: a name from listLayouts (layouts.hpp)
};

struct PortfolioSpec
{
    std::vector<PortfolioSleeve> sleeves;       // 1 to 50
    bool commonPeriodOnly{false};
    double periodsPerYear{260.0};               // bars per year, for CAGR / Sharpe / Sortino / MAR (365 for crypto)
};

struct PortfolioOutput
{
    PortfolioSpec spec;                         // as run: every sleeve's params are complete
    std::size_t bars{};                         // snapshots in the period that was run
    std::vector<std::int64_t> timestamps;       // one per snapshot
    double totalCash{};
    SleeveResult result;                        // each sleeve, and the whole portfolio
    PortfolioMetrics metrics;
};

// Everything that can be checked without reading the data: 1 to 50 sleeves with unique, non-blank names of at most 40
// characters; for each a known strategy with valid parameters, a dataset name that is a file in the folder, and cash above 0;
// periodsPerYear above 0. Throws std::invalid_argument. runPortfolio calls it first; the run manager calls it at submit time.
void validatePortfolioSpec(const StrategyRegistry& registry, const std::string& dataDir, const PortfolioSpec& spec);

// The spec with every sleeve's params completed with the defaults.
PortfolioSpec resolvePortfolioSpec(const StrategyRegistry& registry, const PortfolioSpec& spec);

// Throws what validatePortfolioSpec throws, and std::runtime_error if a dataset cannot be read or has no bars.
PortfolioOutput runPortfolio(const StrategyRegistry& registry, const std::string& dataDir, const PortfolioSpec& spec);
