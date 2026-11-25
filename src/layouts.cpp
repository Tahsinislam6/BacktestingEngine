#include "layouts.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{
    const char* const kFileName = "layouts.json";

    // The layout of a Stooq daily file: <TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>.
    CsvLayout stooq()
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

    struct Entry
    {
        std::string name;
        std::string description;
        bool builtin{};
        CsvLayout layout;       // not used for "auto"
    };

    std::vector<Entry> builtIns()
    {
        return {
            {"auto", "A file whose header has <DATE> is read as stooq, any other file as simple", true, {}},
            {"simple", "timestamp,open,high,low,close with a millisecond timestamp", true, CsvLayout{}},
            {"stooq", "<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>, dates like 20100717", true, stooq()},
        };
    }

    [[noreturn]] void bad(const std::string& entry, const std::string& why)
    {
        throw std::runtime_error(std::string(kFileName) + (entry.empty() ? "" : ", entry \"" + entry + "\"") + ": " + why);
    }

    std::string textField(const json& object, const char* key, const std::string& entry, const std::string& fallback)
    {
        const auto it = object.find(key);
        if (it == object.end())
            return fallback;
        if (!it->is_string())
            bad(entry, std::string("\"") + key + "\" must be text");
        return it->get<std::string>();
    }

    TimeFormat timeFormatByName(const std::string& name, const std::string& entry)
    {
        if (name == "raw") return TimeFormat::Raw;
        if (name == "epochSeconds") return TimeFormat::EpochSeconds;
        if (name == "compactDate") return TimeFormat::CompactDate;
        if (name == "isoDate") return TimeFormat::IsoDate;
        bad(entry, "timeFormat must be raw, epochSeconds, compactDate or isoDate, not \"" + name + "\"");
    }

    Entry parseEntry(const std::string& name, const json& body)
    {
        if (name.empty())
            bad("", "a layout needs a name");
        if (!body.is_object())
            bad(name, "must be an object");
        static const char* const allowed[] = {"description", "delimiter", "hasHeader", "timeFormat", "time", "timeOfDay",
                                              "open", "high", "low", "close", "volume"};
        for (const auto& item : body.items())
            if (std::find_if(std::begin(allowed), std::end(allowed), [&](const char* k) { return item.key() == k; }) == std::end(allowed))
                bad(name, "unknown key \"" + item.key() + "\"");

        Entry e;
        e.name = name;
        e.description = textField(body, "description", name, "");
        CsvLayout& l = e.layout;

        const std::string delimiter = textField(body, "delimiter", name, std::string(1, l.delimiter));
        if (delimiter.size() != 1)
            bad(name, "delimiter must be exactly one character");
        l.delimiter = delimiter[0];

        if (const auto it = body.find("hasHeader"); it != body.end())
        {
            if (!it->is_boolean())
                bad(name, "\"hasHeader\" must be true or false");
            l.hasHeader = it->get<bool>();
        }
        l.timeFormat = timeFormatByName(textField(body, "timeFormat", name, "raw"), name);
        l.time = textField(body, "time", name, l.time);
        l.timeOfDay = textField(body, "timeOfDay", name, l.timeOfDay);
        l.open = textField(body, "open", name, l.open);
        l.high = textField(body, "high", name, l.high);
        l.low = textField(body, "low", name, l.low);
        l.close = textField(body, "close", name, l.close);
        l.volume = textField(body, "volume", name, l.volume);
        return e;
    }

    // The built-ins, then the entries of layouts.json sorted by name (std::map keeps them sorted).
    std::vector<Entry> allEntries(const std::string& dataDir)
    {
        std::vector<Entry> entries = builtIns();
        const fs::path file = fs::path(dataDir) / kFileName;
        std::error_code ec;
        if (!fs::is_regular_file(file, ec))
            return entries;

        std::ifstream in(file);
        const json doc = json::parse(in, nullptr, false);
        if (doc.is_discarded())
            bad("", "this is not valid JSON");
        if (!doc.is_object())
            bad("", "the file must hold one JSON object with a layout per name");

        std::map<std::string, Entry> custom;
        for (const auto& item : doc.items())
        {
            const std::string& name = item.key();
            if (std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.name == name; }))
                bad(name, "this name is built in; choose another");
            custom[name] = parseEntry(name, item.value());
        }
        for (auto& [name, entry] : custom)
            entries.push_back(std::move(entry));
        return entries;
    }
}

std::vector<std::string> listLayouts(const std::string& dataDir)
{
    std::vector<std::string> names;
    for (const Entry& e : allEntries(dataDir))
        names.push_back(e.name);
    return names;
}

json layoutsToJson(const std::string& dataDir)
{
    json list = json::array();
    for (const Entry& e : allEntries(dataDir))
        list.push_back({{"name", e.name}, {"builtin", e.builtin}, {"description", e.description}});
    return list;
}

void requireLayout(const std::string& dataDir, const std::string& name)
{
    const std::vector<Entry> entries = allEntries(dataDir);
    if (std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.name == name; }))
        return;
    std::string known;
    for (const Entry& e : entries)
        known += (known.empty() ? "" : ", ") + e.name;
    throw std::invalid_argument("unknown layout: " + name + " (known: " + known + ")");
}

CsvLayout layoutByName(const std::string& dataDir, const std::string& name)
{
    requireLayout(dataDir, name);
    if (name == "auto")
        throw std::invalid_argument("\"auto\" is not one fixed layout");
    for (const Entry& e : allEntries(dataDir))
        if (e.name == name)
            return e.layout;
    throw std::invalid_argument("unknown layout: " + name);       // not reached
}

bool looksLikeStooq(const std::string& path)
{
    std::ifstream in(path);
    std::string header;
    std::getline(in, header);
    std::transform(header.begin(), header.end(), header.begin(), [](unsigned char c) { return std::tolower(c); });
    return header.find("<date>") != std::string::npos;
}
