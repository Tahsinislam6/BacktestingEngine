#pragma once

#include <map>
#include <string>
#include <vector>

// Trading costs: a bid/ask spread per pair, in pips.
//
// The engine fills every order at the close, worse by HALF the spread (buys pay more, sells receive less),
// so a round trip costs one full spread. See runMultiBacktest.

// Price distance of one pip: 0.01 for a pair quoted in JPY (symbol of 6 letters ending in "JPY"),
// 0.0001 for everything else.
double pipSize(const std::string& symbol);

struct CostModel
{
    std::map<std::string, double> spreadPips;   // symbol -> full spread in pips
    double defaultPips{0.0};                    // for symbols not listed
    double multiplier{1.0};                     // scales every spread, e.g. 2.0 to stress-test

    // The full spread in price units: pips * pipSize * multiplier.
    double spreadPrice(const std::string& symbol) const;
};

// One spread (price units) per symbol, in the same order as `symbols`.
std::vector<double> spreadsFor(const CostModel& model, const std::vector<std::string>& symbols);

// Typical retail spreads for the 20 pairs of this study and a default for other pairs.
// These are placeholders: replace them with your broker's numbers (see loadCostsCsv).
CostModel typicalFxCosts();

// Reads "symbol,spread_pips" rows (header optional, blank lines skipped, spaces trimmed) on top of `base`:
// listed symbols replace the base value, the rest are kept. A row whose symbol is "default" sets
// defaultPips. Throws std::runtime_error if the file cannot be read, and std::invalid_argument for a row
// with a missing symbol or a spread that is not a number or is negative.
CostModel loadCostsCsv(const std::string& path, const CostModel& base);
