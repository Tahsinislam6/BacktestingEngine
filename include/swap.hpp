#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aligned.hpp"
#include "bar.hpp"
#include "trade.hpp"

// Swap (financing): holding a position over a night earns the interest rate of the currency you hold and pays the rate of
// the currency you owe. For a pair BASEQUOTE a long holds the base and owes the quote:
//     per night, in quote currency:  notional * (+- (rate base - rate quote) - markup) / 100 / 365
// with + for a long and - for a short. The markup is the broker's cut, always against the trader. Rates are annual
// percentages per currency and date, read from a csv (see parseRatesCsv); no rates and no markup means no swap.

// The two currencies of a symbol: exactly six capital letters, "EURUSD" -> {"EUR", "USD"}. Anything else: no value.
std::optional<std::pair<std::string, std::string>> pairCurrencies(const std::string& symbol);

struct SwapModel
{
    // currency -> (timestamp in ms, annual rate in percent), sorted by time
    std::map<std::string, std::vector<std::pair<std::int64_t, double>>> rates;
    double markupPct{0.0};                  // percentage points a year, charged on every position whatever its direction
};

// The rate in force at `time`: the last observation at or before it (the last one holds after the end of the list).
// NaN before the first observation and for a currency without any.
double rateAt(const SwapModel& model, const std::string& currency, std::int64_t time);

// One night's swap of a position (positive = earned). `differentialPct` is base rate minus quote rate; NaN means the rates
// are unknown, which counts as no interest (the markup still applies). `days` is the number of nights.
double swapAmount(Direction direction, double quantity, double price, double differentialPct, double markupPct, double days);

// "currency,date,rate" with one observation per line: USD,2016-01-01,0.5 (annual percent; the date is read by
// parseTimestamp, and the rate holds from that day). Blank lines and lines starting with # are skipped. Throws
// std::invalid_argument naming the line for a wrong header, a field missing or extra, a currency that is not three capital
// letters, a bad date or rate, or a date given twice for a currency; and when there are no rows at all.
SwapModel parseRatesCsv(const std::string& text);

// Reads the file and parses it. Throws std::runtime_error if it cannot be read.
SwapModel loadRatesCsv(const std::string& path);

// What the backtester needs: for every asset and snapshot, the rate difference (base - quote, percent) in force at that
// snapshot, NaN where unknown; and the markup. Inactive (nothing is charged) when the model has no rates and no markup.
struct SwapSchedule
{
    std::vector<std::vector<double>> differentialPct;       // [asset][snapshot]; may be empty (markup only)
    double markupPct{0.0};

    bool active() const { return !differentialPct.empty() || markupPct != 0.0; }
};

SwapSchedule makeSwapSchedule(const SwapModel& model, const std::vector<std::string>& symbols, const AlignedData& data);

// The pair symbols among `symbols` for which a currency has no rates at all (their swap is zero): to warn the user.
std::vector<std::string> pairsWithoutRates(const SwapModel& model, const std::vector<std::string>& symbols);
