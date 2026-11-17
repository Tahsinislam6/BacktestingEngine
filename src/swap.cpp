#include "swap.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "timestamp.hpp"

namespace
{
    bool isCurrency(const std::string& s)
    {
        return s.size() == 3 && std::all_of(s.begin(), s.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
    }

    std::string trimmed(const std::string& s)
    {
        const std::size_t a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos)
            return "";
        return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
    }

    [[noreturn]] void bad(int line, const std::string& why)
    {
        throw std::invalid_argument("rates file, line " + std::to_string(line) + ": " + why);
    }
}

std::optional<std::pair<std::string, std::string>> pairCurrencies(const std::string& symbol)
{
    if (symbol.size() != 6)
        return std::nullopt;
    const std::string base = symbol.substr(0, 3), quote = symbol.substr(3);
    if (!isCurrency(base) || !isCurrency(quote))
        return std::nullopt;
    return std::make_pair(base, quote);
}

double rateAt(const SwapModel& model, const std::string& currency, std::int64_t time)
{
    const auto it = model.rates.find(currency);
    if (it == model.rates.end() || it->second.empty())
        return std::nan("");
    const auto& obs = it->second;
    const auto next = std::upper_bound(obs.begin(), obs.end(), time,
                                       [](std::int64_t t, const std::pair<std::int64_t, double>& o) { return t < o.first; });
    if (next == obs.begin())
        return std::nan("");
    return std::prev(next)->second;
}

double swapAmount(Direction direction, double quantity, double price, double differentialPct, double markupPct, double days)
{
    const double difference = std::isnan(differentialPct) ? 0.0 : differentialPct;
    return quantity * price * (sign(direction) * difference - markupPct) / 100.0 / 365.0 * days;
}

SwapModel parseRatesCsv(const std::string& text)
{
    SwapModel model;
    std::istringstream in(text);
    std::string raw;
    int line = 0;
    bool haveHeader = false;
    std::set<std::pair<std::string, std::int64_t>> seen;
    while (std::getline(in, raw))
    {
        ++line;
        const std::string content = trimmed(raw);
        if (content.empty() || content[0] == '#')
            continue;

        std::vector<std::string> fields;
        std::istringstream cells(content);
        for (std::string f; std::getline(cells, f, ',');)
            fields.push_back(trimmed(f));
        if (!content.empty() && content.back() == ',')
            fields.push_back("");

        if (!haveHeader)
        {
            if (fields != std::vector<std::string>{"currency", "date", "rate"})
                bad(line, "the first line must be 'currency,date,rate'");
            haveHeader = true;
            continue;
        }
        if (fields.size() != 3)
            bad(line, "expected 3 fields (currency,date,rate), got " + std::to_string(fields.size()));
        if (!isCurrency(fields[0]))
            bad(line, "the currency must be three capital letters, got '" + fields[0] + "'");

        std::int64_t time = 0;
        try
        {
            time = parseTimestamp(fields[1]) * 1000;
        }
        catch (const std::exception&)
        {
            bad(line, "bad date '" + fields[1] + "'");
        }
        std::size_t used = 0;
        double rate = 0;
        try { rate = std::stod(fields[2], &used); }
        catch (const std::exception&) { used = 0; }
        if (used == 0 || used != fields[2].size() || !std::isfinite(rate))
            bad(line, "bad rate '" + fields[2] + "'");
        if (!seen.insert({fields[0], time}).second)
            bad(line, "'" + fields[0] + "' has two rates for " + fields[1]);
        model.rates[fields[0]].emplace_back(time, rate);
    }
    if (!haveHeader)
        throw std::invalid_argument("rates file: it is empty (the first line must be 'currency,date,rate')");
    if (model.rates.empty())
        throw std::invalid_argument("rates file: no rows");
    for (auto& entry : model.rates)
        std::sort(entry.second.begin(), entry.second.end());
    return model;
}

SwapModel loadRatesCsv(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot read " + path);
    std::ostringstream text;
    text << file.rdbuf();
    return parseRatesCsv(text.str());
}

SwapSchedule makeSwapSchedule(const SwapModel& model, const std::vector<std::string>& symbols, const AlignedData& data)
{
    SwapSchedule schedule;
    schedule.markupPct = model.markupPct;
    if (model.rates.empty())
        return schedule;                    // nothing to look up: inactive, or markup only

    schedule.differentialPct.assign(symbols.size(), std::vector<double>(data.snapshots.size(), std::nan("")));
    for (std::size_t a = 0; a < symbols.size(); ++a)
    {
        const auto currencies = pairCurrencies(symbols[a]);
        if (!currencies)
            continue;
        for (std::size_t i = 0; i < data.snapshots.size(); ++i)
        {
            const std::int64_t t = data.snapshots[i].timestamp;
            schedule.differentialPct[a][i] = rateAt(model, currencies->first, t) - rateAt(model, currencies->second, t);
        }
    }
    return schedule;
}

std::vector<std::string> pairsWithoutRates(const SwapModel& model, const std::vector<std::string>& symbols)
{
    std::vector<std::string> out;
    for (const std::string& s : symbols)
    {
        const auto c = pairCurrencies(s);
        if (c && (model.rates.count(c->first) == 0 || model.rates.count(c->second) == 0))
            out.push_back(s);
    }
    return out;
}
