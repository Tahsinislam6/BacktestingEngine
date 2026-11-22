#include "loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>


namespace
{
    // The whole text must be a whole number: "1451779200000" is fine, "2025-01-02" and "123abc" are not.
    std::int64_t toInteger(const std::string& text)
    {
        std::size_t used = 0;
        const long long value = std::stoll(text, &used);      // throws std::invalid_argument if not a number
        if (used != text.size())
            throw std::invalid_argument("Timestamp must be a whole number, got '" + text + "'");
        return value;
    }
}

BarSeries loadCsv(const std::string& path, const std::string& symbol)
{
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open " + path);

    BarSeries series;
    series.symbol = symbol;

    std::string line;
    std::getline(file, line);   // the header: it says which column (if any) holds the volume

    int volumeColumn = -1;
    {
        std::stringstream header(line);
        std::string name;
        for (int column = 0; std::getline(header, name, ','); ++column)
        {
            const std::size_t first = name.find_first_not_of(" \t\r");
            const std::size_t last = name.find_last_not_of(" \t\r");
            name = first == std::string::npos ? "" : name.substr(first, last - first + 1);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
            if (name == "volume" && column >= 5)
                volumeColumn = column;
        }
    }
    const std::size_t needed = volumeColumn >= 0 ? static_cast<std::size_t>(volumeColumn) + 1 : 5;

    while (std::getline(file, line))
    {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        while (fields.size() < needed && std::getline(ss, field, ','))
        {
            fields.push_back(field);
        }

        if(fields.size() < needed)
            throw std::runtime_error("Bad row in " + path + ": " + line);

        Bar bar;
        bar.timestamp = toInteger(fields[0]);
        bar.open = std::stod(fields[1]);
        bar.high = std::stod(fields[2]);
        bar.low = std::stod(fields[3]);
        bar.close = std::stod(fields[4]);
        if (volumeColumn >= 0)
            bar.volume = std::stod(fields[static_cast<std::size_t>(volumeColumn)]);
        series.bars.push_back(bar);
    
    }

    std::sort(series.bars.begin(), series.bars.end(),
        [](const Bar& a, const Bar& b) {return a.timestamp < b.timestamp;});
    
    return series;
}