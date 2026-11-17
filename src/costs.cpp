#include "costs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>

double pipSize(const std::string& symbol)
{
    const bool jpyQuoted = symbol.size() == 6 && symbol.compare(3, 3, "JPY") == 0;
    return jpyQuoted ? 0.01 : 0.0001;
}

double CostModel::spreadPrice(const std::string& symbol) const
{
    auto it = spreadPips.find(symbol);
    const double pips = it == spreadPips.end() ? defaultPips : it->second;
    return pips * pipSize(symbol) * multiplier;
}

std::vector<double> spreadsFor(const CostModel& model, const std::vector<std::string>& symbols)
{
    std::vector<double> out;
    out.reserve(symbols.size());
    for (const std::string& s : symbols)
        out.push_back(model.spreadPrice(s));
    return out;
}

CostModel typicalFxCosts()
{
    CostModel m;
    m.defaultPips = 3.0;
    m.spreadPips = {
        // majors
        {"EURUSD", 0.8}, {"USDJPY", 1.0}, {"GBPUSD", 1.2}, {"USDCHF", 1.5}, {"USDCAD", 1.5},
        {"AUDUSD", 1.0}, {"NZDUSD", 1.5},
        // crosses
        {"EURGBP", 1.2}, {"EURJPY", 1.5}, {"EURCHF", 1.8}, {"EURCAD", 2.5}, {"EURAUD", 2.5}, {"EURNZD", 3.5},
        {"GBPJPY", 2.5}, {"GBPCHF", 3.0}, {"GBPAUD", 3.0}, {"GBPCAD", 3.5}, {"GBPNZD", 4.5},
        {"AUDJPY", 2.0}, {"CADJPY", 2.5}, {"CHFJPY", 2.5}, {"NZDJPY", 2.5},
        {"AUDCAD", 2.5}, {"AUDCHF", 2.5}, {"AUDNZD", 3.0}, {"NZDCAD", 3.0}, {"NZDCHF", 3.5}, {"CADCHF", 2.5},
        // other
        {"USDSGD", 3.0}};
    return m;
}

namespace
{
    std::string trim(const std::string& s)
    {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    double toPips(const std::string& symbol, const std::string& text)
    {
        std::size_t used = 0;
        double v = 0;
        try { v = std::stod(text, &used); }
        catch (const std::exception&) { used = 0; }
        if (used == 0 || used != text.size() || !std::isfinite(v))
            throw std::invalid_argument("costs file: spread for " + symbol + " is not a number: '" + text + "'");
        if (v < 0)
            throw std::invalid_argument("costs file: spread for " + symbol + " is negative");
        return v;
    }
}

CostModel loadCostsCsv(const std::string& path, const CostModel& base)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open costs file " + path);

    CostModel m = base;
    std::string line;
    bool first = true;
    while (std::getline(file, line))
    {
        if (trim(line).empty())
            continue;

        const std::size_t comma = line.find(',');
        const std::string symbol = trim(line.substr(0, comma));
        if (first)
        {
            first = false;
            if (lower(symbol) == "symbol")
                continue;           // header
        }
        if (symbol.empty())
            throw std::invalid_argument("costs file: a row has no symbol");
        if (comma == std::string::npos)
            throw std::invalid_argument("costs file: no spread for " + symbol);

        std::string rest = line.substr(comma + 1);
        const std::size_t next = rest.find(',');
        const double pips = toPips(symbol, trim(rest.substr(0, next)));

        if (lower(symbol) == "default")
            m.defaultPips = pips;
        else
            m.spreadPips[symbol] = pips;
    }
    return m;
}
