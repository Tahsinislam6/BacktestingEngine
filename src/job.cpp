#include "job.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "loader.hpp"

namespace fs = std::filesystem;

namespace
{
    // The layout of a Stooq daily file: <TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>.
    CsvLayout stooqLayout()
    {
        CsvLayout layout;
        layout.timeFormat = TimeFormat::CompactDate;
        layout.time = "date";
        layout.timeOfDay = "time";
        layout.open = "open";
        layout.high = "high";
        layout.low = "low";
        layout.close = "close";
        layout.volume = "vol";
        return layout;
    }

    bool isStooqFile(const std::string& path)
    {
        std::ifstream in(path);
        std::string header;
        std::getline(in, header);
        std::transform(header.begin(), header.end(), header.begin(), [](unsigned char c) { return std::tolower(c); });
        return header.find("<date>") != std::string::npos;
    }
}

std::vector<std::string> listDatasets(const std::string& dataDir)
{
    std::error_code ec;
    if (!fs::is_directory(dataDir, ec))
        throw std::runtime_error("data folder not found: " + dataDir);

    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dataDir))
    {
        const std::string ext = entry.path().extension().string();
        if (entry.is_regular_file() && (ext == ".csv" || ext == ".txt"))
            names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string resolveDataset(const std::string& dataDir, const std::string& name)
{
    if (name.empty())
        throw std::invalid_argument("dataset name is empty");
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
        name.find("..") != std::string::npos)
        throw std::invalid_argument("dataset must be a file name inside the data folder: " + name);

    const fs::path path = fs::path(dataDir) / name;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec))
        throw std::invalid_argument("dataset not found: " + name);
    return path.string();
}

BarSeries loadDataset(const std::string& dataDir, const std::string& name)
{
    const std::string path = resolveDataset(dataDir, name);
    return isStooqFile(path) ? loadCsv(path, name, stooqLayout()) : loadCsv(path, name);
}

void validateSpec(const StrategyRegistry& registry, const std::string& dataDir, const RunSpec& spec)
{
    if (!std::isfinite(spec.cash) || spec.cash <= 0.0)
        throw std::invalid_argument("cash must be above 0");
    if (!std::isfinite(spec.periodsPerYear) || spec.periodsPerYear <= 0.0)
        throw std::invalid_argument("periodsPerYear must be above 0");
    registry.resolveParams(spec.strategy, spec.params);
    resolveDataset(dataDir, spec.dataset);
}

RunOutput runJob(const StrategyRegistry& registry, const std::string& dataDir, const RunSpec& spec)
{
    validateSpec(registry, dataDir, spec);

    RunOutput out;
    out.spec = spec;
    out.spec.params = registry.resolveParams(spec.strategy, spec.params);    // also checks the strategy name

    const BarSeries series = loadDataset(dataDir, spec.dataset);
    if (series.bars.empty())
        throw std::runtime_error("dataset has no bars: " + spec.dataset);

    std::unique_ptr<Strategy> strategy = registry.create(spec.strategy, out.spec.params);
    out.result = runBacktest(series, *strategy, spec.cash);
    out.metrics = computeMetrics(out.result, spec.cash, spec.periodsPerYear);
    for (const Bar& bar : series.bars)
        out.timestamps.push_back(bar.timestamp);
    return out;
}
