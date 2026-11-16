#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "capture.hpp"
#include "optimiser.hpp"

// "period=20, atrMultiple=1.5" (names in alphabetical order); "(no parameters)" when empty.
std::string paramsText(const Params& params);

// "sharpe", "sortino", "calmar", "return" or "capture".
std::string objectiveName(Objective objective);

// One row per ranked run, best first. Columns:
//   rank, score, <one column per parameter>, trades, total_return, cagr, max_drawdown, sharpe, sortino,
//   calmar, win_rate, profit_factor, expectancy, avg_holding_days, capture_efficiency, capture_legs, right_share
// Returns are fractions (0.1 = 10%). Infinity is written "inf" and NaN "nan". Lines end with "\n".
// The parameter columns come from the first row; an empty list gives just the header.
std::string rankedCsv(const std::vector<Ranked>& ranked);

// Writes the text to a file, replacing it. Throws std::runtime_error if it cannot be written.
void writeTextFile(const std::string& path, const std::string& text);

// The best `top` rows as a readable table.
std::string rankedTable(const std::vector<Ranked>& ranked, std::size_t top);

// How one asset fared in a run.
struct AssetReport
{
    std::string symbol;
    int trades{};              // closed + still open
    double pnl{};              // closed trades' profit, plus open trades valued at the asset's last close
    CaptureResult capture;
};

// pivots[a] belong to asset a (zigzagAll). Throws std::invalid_argument if pivots.size() != symbols.size().
std::vector<AssetReport> perAssetReport(const AlignedData& data, const BacktestResult& result,
                                        const std::vector<std::vector<Pivot>>& pivots);

std::string assetTable(const std::vector<AssetReport>& assets);
