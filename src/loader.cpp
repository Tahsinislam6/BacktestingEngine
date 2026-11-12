#include "loader.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "timestamp.hpp"

BarSeries loadCsv(const std::string& path, const std::string& symbol)
{
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open " + path);

    BarSeries series;
    series.symbol = symbol;

    std::string line;
    std::getline(file, line);   // skip header

    while (std::getline(file, line))
    {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        while (fields.size() < 5 && std::getline(ss, field, ','))
        {
            fields.push_back(field);
        }

        if(fields.size() < 5)
            throw std::runtime_error("Bad row in " + path + ": " + line);

        Bar bar;
        bar.timestamp = parseTimestamp(fields[0]);
        bar.open = std::stod(fields[1]);
        bar.high = std::stod(fields[2]);
        bar.low = std::stod(fields[3]);
        bar.close = std::stod(fields[4]);
        series.bars.push_back(bar);
    
    }

    std::sort(series.bars.begin(), series.bars.end(),
        [](const Bar& a, const Bar& b) {return a.timestamp < b.timestamp;});
    
    return series;
}