#pragma once

#include <string>
#include "bar.hpp"

// How the time of a bar is written in the file. Every format except Raw gives milliseconds since 1970-01-01 UTC
// (dates and times are read as UTC).
enum class TimeFormat
{
    Raw,            // a whole number, stored exactly as written (the default; the current data files hold milliseconds)
    EpochSeconds,   // a whole number of seconds since 1970, stored as milliseconds
    CompactDate,    // YYYYMMDD, e.g. 20100717, midnight UTC
    IsoDate         // YYYY-MM-DD, or YYYY-MM-DD HH:MM:SS (a 'T' instead of the space is fine too)
};

// Where each value is in the file and how it is written. The default layout is the original one: a header line, commas,
// columns timestamp, open, high, low, close in that order, a raw timestamp, and the volume taken from a column whose
// header is "volume" (when there is one after the first five columns).
//
// A column is named by its header text, or by its 0-based position when the text is only digits ("3"). Header text is
// compared without capitals, spaces and angle brackets, so "<DATE>", "Date" and " date " are the same name. Names need
// a header line (hasHeader = true); positions work without one.
struct CsvLayout
{
    char delimiter{','};
    bool hasHeader{true};           // false: the first line is already data
    TimeFormat timeFormat{TimeFormat::Raw};

    std::string time{"0"};          // the timestamp, or the date for CompactDate / IsoDate
    std::string timeOfDay{};        // optional second column with the time of day (093000, 0930, 09:30 or 09:30:15),
                                    // added to the date. Only for the date formats.
    std::string open{"1"};
    std::string high{"2"};
    std::string low{"3"};
    std::string close{"4"};
    std::string volume{};           // empty: use a header named "volume" if there is one after the first five columns,
                                    // otherwise the volume is 0. Non-empty: that column (it must exist).
};

// Reads the file with the default layout: timestamp,open,high,low,close in the first 5 columns.
// The timestamp must be a whole number (milliseconds since 1970 in the current data files). It is stored
// exactly as written, with no conversion. Throws std::invalid_argument if it is anything else.
// `symbol` is stored in the returned series so several series can be told apart.
BarSeries loadCsv(const std::string& path, const std::string& symbol = "");

// Reads the file with the given layout. Throws std::runtime_error if the file cannot be opened, a named column is not in
// the header (or there is no header), or a row is too short; std::invalid_argument for a time or date that does not fit
// the time format. The bars come back sorted by timestamp.
BarSeries loadCsv(const std::string& path, const std::string& symbol, const CsvLayout& layout);
