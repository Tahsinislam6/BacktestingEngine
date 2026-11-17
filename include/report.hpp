#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "capture.hpp"
#include "rstats.hpp"
#include "system_run.hpp"

// "period=20, atrMultiple=1.5" (names in alphabetical order); "(no parameters)" when empty.
std::string paramsText(const Params& params);

// A csv field: quoted (with "" for a quote) when it holds a comma, a quote or a line break, else as it is.
std::string csvField(const std::string& s);

// Writes the text to a file, replacing it. Throws std::runtime_error if it cannot be written.
void writeTextFile(const std::string& path, const std::string& text);

// How one asset fared in a run.
struct AssetReport
{
    std::string symbol;
    int trades{};              // closed + still open
    double pnl{};              // closed trades' profit, plus open trades valued at the asset's last close
    CaptureResult capture;
    double costPaid{};         // spread paid: full spread x quantity per closed trade, half for a still-open one
    double swap{};             // financing earned (positive) or paid (negative), already inside `pnl`
};

// pivots[a] belong to asset a (zigzagAll). Throws std::invalid_argument if pivots.size() != symbols.size().
// spreads[a] (price units, as given to runMultiBacktest) is only used for costPaid; empty = no cost.
std::vector<AssetReport> perAssetReport(const AlignedData& data, const BacktestResult& result,
                                        const std::vector<std::vector<Pivot>>& pivots,
                                        const std::vector<double>& spreads = {});

std::string assetTable(const std::vector<AssetReport>& assets);

// A system run once with fixed parameters, with everything it is judged on.
struct SystemRun
{
    Evaluation evaluation;                  // the parameters, the metrics and the capture efficiency
    std::vector<AssetReport> assets;        // per asset, in the data's order
    std::optional<RReport> r;               // the R-multiples; empty if there are no closed trades with a stop
};

// Runs the strategy once over `data` (costs, cash and capture settings from `options`).
// Throws std::invalid_argument if the factory returns no strategy.
SystemRun runSystem(const AlignedData& data, const Params& params, const StrategyFactory& factory,
                    const RunOptions& options = RunOptions{});

// The run as readable text under the heading `label` ("out-of-sample"): return, Sharpe, drawdown, trades and capture,
// the per-asset table and the R-multiple table.
std::string formatSystemRun(const std::string& label, const SystemRun& run);
