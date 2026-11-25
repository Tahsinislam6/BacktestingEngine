#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "loader.hpp"

// Layout presets: a layout says which columns a data file has and how the time is written (see CsvLayout in loader.hpp).
// A request picks one by NAME for each dataset it uses.
//
// Built in:
//   "auto"    the default: a file whose header has <DATE> is read as "stooq", any other file as "simple"
//   "simple"  timestamp,open,high,low,close with a millisecond timestamp (the original layout)
//   "stooq"   <TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT> with dates like 20100717
//
// Your own: a file named layouts.json in the data folder, read again on every use (so an edit needs no restart):
//   { "my_exchange": { "description": "optional text", "delimiter": ";", "hasHeader": true, "timeFormat": "isoDate",
//                      "time": "Date", "timeOfDay": "", "open": "Open", "high": "High", "low": "Low", "close": "Close",
//                      "volume": "Volume" } }
// Every field is optional and defaults to the "simple" layout's value. timeFormat is "raw", "epochSeconds", "compactDate" or
// "isoDate". A name may not be one of the built-in names. Anything wrong in the file is an error naming the file and the entry.

// The built-in names first (auto, simple, stooq), then the names in layouts.json, sorted.
// Throws std::runtime_error if layouts.json exists but is not valid.
std::vector<std::string> listLayouts(const std::string& dataDir);

// [{"name": ..., "builtin": true|false, "description": ...}, ...] in the order of listLayouts.
nlohmann::json layoutsToJson(const std::string& dataDir);

// Throws std::invalid_argument if the name is not in listLayouts; std::runtime_error as listLayouts does.
void requireLayout(const std::string& dataDir, const std::string& name);

// The layout behind a name other than "auto". Throws what requireLayout throws, and std::invalid_argument for "auto".
CsvLayout layoutByName(const std::string& dataDir, const std::string& name);

// True if the file's header line has a <DATE> column, i.e. it is a Stooq daily file.
bool looksLikeStooq(const std::string& path);
