#include "loader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "timestamp.hpp"

namespace
{
    // The whole text must be a whole number: "1451779200000" is fine, "2025-01-02" and "123abc" are not.
    std::int64_t toInteger(const std::string& text)
    {
        std::size_t used = 0;
        long long value = 0;
        try
        {
            value = std::stoll(text, &used);
        }
        catch (const std::logic_error&)     // not a number at all, or too big
        {
            used = 0;
        }
        if (used == 0 || used != text.size())
            throw std::invalid_argument("Timestamp must be a whole number, got '" + text + "'");
        return value;
    }

    // Header text for comparing: no capitals, spaces, tabs, quotes, angle brackets or line ends.
    std::string normalized(const std::string& text)
    {
        std::string out;
        for (unsigned char c : text)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '<' || c == '>')
                continue;
            out += static_cast<char>(std::tolower(c));
        }
        return out;
    }

    bool onlyDigits(const std::string& text)
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    }

    std::vector<std::string> split(const std::string& line, char delimiter, std::size_t maxFields)
    {
        std::stringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        while (fields.size() < maxFields && std::getline(ss, field, delimiter))
            fields.push_back(field);
        return fields;
    }

    // Turns a column setting ("date", "<DATE>" or "3") into a 0-based position.
    std::size_t columnIndex(const std::string& setting, const std::vector<std::string>& header, bool hasHeader,
                            const std::string& path)
    {
        if (onlyDigits(setting))
            return static_cast<std::size_t>(std::stoul(setting));

        if (!hasHeader)
            throw std::runtime_error("Column '" + setting + "' is given by name but " + path + " has no header line");

        const std::string wanted = normalized(setting);
        for (std::size_t i = 0; i < header.size(); ++i)
        {
            if (normalized(header[i]) == wanted)
                return i;
        }
        throw std::runtime_error("Column '" + setting + "' is not in the header of " + path);
    }

    std::int64_t toMilliseconds(const std::string& text, TimeFormat format)
    {
        switch (format)
        {
            case TimeFormat::Raw:
                return toInteger(text);
            case TimeFormat::EpochSeconds:
                return toInteger(text) * 1000;
            case TimeFormat::CompactDate:
                return parseCompactDate(text) * 1000;
            case TimeFormat::IsoDate:
                // parseTimestamp would also accept a plain number as epoch seconds; here that is a mistake.
                if (text.size() < 10 || text[4] != '-' || text[7] != '-')
                    throw std::invalid_argument("Expected a date like 2010-07-17, got '" + text + "'");
                return parseTimestamp(text) * 1000;
        }
        throw std::invalid_argument("Unknown time format");
    }
}

BarSeries loadCsv(const std::string& path, const std::string& symbol)
{
    return loadCsv(path, symbol, CsvLayout{});
}

BarSeries loadCsv(const std::string& path, const std::string& symbol, const CsvLayout& layout)
{
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open " + path);

    BarSeries series;
    series.symbol = symbol;

    std::string line;
    std::vector<std::string> header;
    if (layout.hasHeader)
    {
        std::getline(file, line);
        if (line.compare(0, 3, "\xEF\xBB\xBF") == 0)    // a byte order mark some programs put at the start of a file
            line.erase(0, 3);
        header = split(line, layout.delimiter, line.size() + 1);
    }

    const std::size_t timeColumn = columnIndex(layout.time, header, layout.hasHeader, path);
    const std::size_t openColumn = columnIndex(layout.open, header, layout.hasHeader, path);
    const std::size_t highColumn = columnIndex(layout.high, header, layout.hasHeader, path);
    const std::size_t lowColumn = columnIndex(layout.low, header, layout.hasHeader, path);
    const std::size_t closeColumn = columnIndex(layout.close, header, layout.hasHeader, path);

    bool hasTimeOfDay = !layout.timeOfDay.empty();
    std::size_t timeOfDayColumn = 0;
    if (hasTimeOfDay)
        timeOfDayColumn = columnIndex(layout.timeOfDay, header, layout.hasHeader, path);

    // The volume: a named column must exist; with no setting, the old rule applies (a header named "volume", only
    // counted after the first five columns).
    bool hasVolume = false;
    std::size_t volumeColumn = 0;
    if (!layout.volume.empty())
    {
        volumeColumn = columnIndex(layout.volume, header, layout.hasHeader, path);
        hasVolume = true;
    }
    else
    {
        for (std::size_t i = 5; i < header.size(); ++i)
        {
            if (normalized(header[i]) == "volume")
            {
                volumeColumn = i;
                hasVolume = true;
                break;
            }
        }
    }

    std::size_t needed = std::max({timeColumn, openColumn, highColumn, lowColumn, closeColumn}) + 1;
    if (hasTimeOfDay)
        needed = std::max(needed, timeOfDayColumn + 1);
    if (hasVolume)
        needed = std::max(needed, volumeColumn + 1);

    while (std::getline(file, line))
    {
        if (line.empty()) continue;

        const std::vector<std::string> fields = split(line, layout.delimiter, needed);
        if (fields.size() < needed)
            throw std::runtime_error("Bad row in " + path + ": " + line);

        Bar bar;
        bar.timestamp = toMilliseconds(fields[timeColumn], layout.timeFormat);
        if (hasTimeOfDay)
            bar.timestamp += parseTimeOfDay(fields[timeOfDayColumn]) * 1000;
        bar.open = std::stod(fields[openColumn]);
        bar.high = std::stod(fields[highColumn]);
        bar.low = std::stod(fields[lowColumn]);
        bar.close = std::stod(fields[closeColumn]);
        if (hasVolume)
            bar.volume = std::stod(fields[volumeColumn]);
        series.bars.push_back(bar);
    }

    std::sort(series.bars.begin(), series.bars.end(),
        [](const Bar& a, const Bar& b) {return a.timestamp < b.timestamp;});

    return series;
}
