#include "portfolio_job.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>

#include "aligned.hpp"
#include "job.hpp"
#include "single_asset.hpp"

namespace
{
    constexpr std::size_t kMaxSleeves = 50;
    constexpr std::size_t kMaxNameLength = 40;

    bool blank(const std::string& text)
    {
        return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    }

    // The data from the first snapshot at which EVERY asset has started (has had its first bar), to the end.
    AlignedData trimToCommonStart(const AlignedData& data)
    {
        std::size_t start = 0;
        for (std::size_t a = 0; a < data.symbols.size(); ++a)
        {
            std::optional<std::size_t> first;
            for (std::size_t i = 0; i < data.snapshots.size() && !first; ++i)
                if (data.snapshots[i].bars[a])
                    first = i;
            if (!first)
                throw std::runtime_error("no data at all for " + data.symbols[a]);
            start = std::max(start, *first);
        }
        AlignedData out;
        out.symbols = data.symbols;
        out.snapshots.assign(data.snapshots.begin() + static_cast<std::ptrdiff_t>(start), data.snapshots.end());
        return out;
    }
}

void validatePortfolioSpec(const StrategyRegistry& registry, const std::string& dataDir, const PortfolioSpec& spec)
{
    if (spec.sleeves.empty())
        throw std::invalid_argument("a portfolio needs at least one sleeve");
    if (spec.sleeves.size() > kMaxSleeves)
        throw std::invalid_argument("a portfolio can have at most 50 sleeves");
    if (!std::isfinite(spec.periodsPerYear) || spec.periodsPerYear <= 0.0)
        throw std::invalid_argument("periodsPerYear must be above 0");

    std::set<std::string> names;
    for (const PortfolioSleeve& s : spec.sleeves)
    {
        if (blank(s.name))
            throw std::invalid_argument("every sleeve needs a name");
        if (s.name.size() > kMaxNameLength)
            throw std::invalid_argument("a sleeve name can have at most 40 characters: " + s.name);
        if (!names.insert(s.name).second)
            throw std::invalid_argument("two sleeves are named " + s.name);
        if (!std::isfinite(s.cash) || s.cash <= 0.0)
            throw std::invalid_argument("sleeve " + s.name + ": cash must be above 0");
        registry.resolveParams(s.strategy, s.params);           // known strategy, known parameters, inside the limits
        resolveDataset(dataDir, s.dataset);
    }
}

PortfolioSpec resolvePortfolioSpec(const StrategyRegistry& registry, const PortfolioSpec& spec)
{
    PortfolioSpec out = spec;
    for (PortfolioSleeve& s : out.sleeves)
        s.params = registry.resolveParams(s.strategy, s.params);
    return out;
}

PortfolioOutput runPortfolio(const StrategyRegistry& registry, const std::string& dataDir, const PortfolioSpec& request)
{
    validatePortfolioSpec(registry, dataDir, request);

    PortfolioOutput out;
    out.spec = resolvePortfolioSpec(registry, request);

    // One series per sleeve, its asset named after the sleeve.
    std::vector<BarSeries> series;
    for (const PortfolioSleeve& s : out.spec.sleeves)
    {
        BarSeries bars = loadDataset(dataDir, s.dataset);
        if (bars.bars.empty())
            throw std::runtime_error("dataset has no bars: " + s.dataset);
        bars.symbol = s.name;
        series.push_back(std::move(bars));
    }
    AlignedData data = align(series);
    if (out.spec.commonPeriodOnly)
        data = trimToCommonStart(data);

    std::vector<Sleeve> sleeves;
    for (const PortfolioSleeve& s : out.spec.sleeves)
    {
        Sleeve sleeve;
        sleeve.name = s.name;
        sleeve.symbols = {s.name};
        sleeve.strategy = std::make_unique<SingleAssetAdapter>(registry.create(s.strategy, s.params));
        sleeve.cash = s.cash;
        sleeves.push_back(std::move(sleeve));
        out.totalCash += s.cash;
    }

    out.result = runSleeves(data, sleeves);
    out.metrics = computeSleeveMetrics(out.result, out.spec.periodsPerYear);
    out.bars = data.snapshots.size();
    for (const Snapshot& snapshot : data.snapshots)
        out.timestamps.push_back(snapshot.timestamp);
    return out;
}
