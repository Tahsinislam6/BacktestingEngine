#pragma once

#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "cli.hpp"
#include "component_control.hpp"
#include "control.hpp"
#include "entry_study.hpp"
#include "grid.hpp"

// The results log: one csv per type of test, rows only ever appended, so every test you run stays on record.
//
//   grid_log.csv               one row per baseline x C1 pair per grid run
//   direction_control_log.csv  one row per data set (in-sample, out-of-sample) per direction control
//   component_control_log.csv  one row per component per data set per component control
//   entry_study_log.csv        one row per data set per entry study
//   entry_control_log.csv      one row per component per data set per entry control
//
// Every row starts with the same six columns that say when and how the test was run (LogContext), then the
// numbers. A file that already exists with different columns is never appended to (appendLog throws).

inline constexpr const char* kGridLogFile = "grid_log.csv";
inline constexpr const char* kDirectionLogFile = "direction_control_log.csv";
inline constexpr const char* kComponentLogFile = "component_control_log.csv";
inline constexpr const char* kEntryStudyLogFile = "entry_study_log.csv";
inline constexpr const char* kEntryControlLogFile = "entry_control_log.csv";

struct LogContext
{
    std::string timestamp;      // UTC, see logTimestamp
    std::string note;           // free text from --note: why this test was run
    std::string setup;          // the nnfx switches (setupText)
    std::string costs;          // where the spreads came from (costsText)
    std::string inSample;       // the data folders
    std::string outSample;
};

std::string logTimestamp(std::time_t when);              // "2023-11-14T22:13:20Z"
std::string setupText(const CliOptions& cli);            // e.g. "nnfx no-cross-exit confirm=rsi stops=orders"
std::string costsText(const CliOptions& cli);            // "no spread, no swap", "typical x1, no swap" or "costs.csv x2, swap rates.csv markup 0.5"
LogContext makeLogContext(const CliOptions& cli, std::time_t now);

// Appends `rows` (each already a full csv line ending in "\n") to dir/file, creating the folder and the file with
// `header` (a line ending in "\n") when needed. Throws std::runtime_error if the file exists with another first
// line, or cannot be written. Nothing is written when `rows` is empty.
void appendLog(const std::string& dir, const std::string& file, const std::string& header,
               const std::vector<std::string>& rows);

std::string directionLogHeader();
// `data` names the data set ("in-sample" / "out-of-sample"); `runs` is the number of random-direction runs.
std::string directionLogRow(const LogContext& context, const std::string& baseline, const std::string& params,
                            const std::string& data, int runs, const ControlResult& result);

std::string componentLogHeader();
std::string componentLogRow(const LogContext& context, const std::string& baseline, const std::string& params,
                            ComponentRole role, const std::string& component, const std::string& data, int runs,
                            const ComponentControlResult& result);

std::string entryStudyLogHeader();
std::string entryStudyLogRow(const LogContext& context, const std::string& baseline, const std::string& params,
                             const std::string& data, int runs, const EntryStudyResult& result);

std::string entryControlLogHeader();
std::string entryControlLogRow(const LogContext& context, const std::string& baseline, const std::string& params,
                               ComponentRole role, const std::string& component, const std::string& data, int runs,
                               const EntryControlResult& result);

std::string gridLogHeader();
std::vector<std::string> gridLogRows(const LogContext& context, const std::vector<GridRow>& rows, int controlRuns);

// The tries ledger is a markdown table that stays when the csv logs are cleared: one row per test run.
// ledgerRow: "| timestamp | what | data | rule (the note) | result |\n" (pipes and line breaks in the texts are replaced).
std::string ledgerRow(const LogContext& context, const std::string& what, const std::string& result);

// Appends `row` to the file, creating it (and its folder) with a header first if it does not exist.
// Throws std::runtime_error if it cannot be written.
void appendLedger(const std::string& path, const std::string& row);
