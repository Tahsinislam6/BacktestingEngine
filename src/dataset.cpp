#include "dataset.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <stdexcept>

#include "loader.hpp"
#include "sundays.hpp"

namespace fs = std::filesystem;

std::string symbolFromFilename(const std::string& path)
{
    std::string stem = fs::path(path).stem().string();
    const std::size_t cut = std::min(stem.find("_forex"), stem.find("_1d"));     // npos if neither is there
    if (cut != std::string::npos)
        stem.erase(cut);
    return stem;
}

std::vector<std::string> csvFilesIn(const std::string& folder)
{
    if (!fs::is_directory(folder))
        throw std::runtime_error("Not a folder: " + folder);

    std::vector<std::string> files;
    for (const fs::directory_entry& entry : fs::directory_iterator(folder))
    {
        if (!entry.is_regular_file())
            continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        if (ext == ".csv")
            files.push_back(entry.path().string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

AlignedData loadFolder(const std::string& folder, bool mergeSundays)
{
    const std::vector<std::string> files = csvFilesIn(folder);
    if (files.empty())
        throw std::runtime_error("No .csv files in " + folder);

    std::vector<BarSeries> series;
    std::set<std::string> seen;
    for (const std::string& file : files)
    {
        const std::string symbol = symbolFromFilename(file);
        if (!seen.insert(symbol).second)
            throw std::runtime_error("Two files give the symbol " + symbol + " in " + folder);
        BarSeries loaded = loadCsv(file, symbol);
        series.push_back(mergeSundays ? mergeSundayBars(loaded) : loaded);
    }
    return align(series);
}
