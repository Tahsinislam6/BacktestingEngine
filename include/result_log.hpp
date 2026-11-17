#pragma once

#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "cli.hpp"
#include "component_control.hpp"
#include "control.hpp"
#include "entry_study.hpp"
#include "screen.hpp"

// The results log: one csv per type of test, rows only ever appended, so every test you run stays on record.
//
//   screen_log.csv             one row per baseline per screen
//   direction_control_log.csv  one row per data set (in-sample, out-of-sample) per direction control
//   component_control_log.csv  one row per component per data set per component control
//   entry_study_log.csv        one row per data set per entry study
//   entry_control_log.csv      one row per component per data set per entry control
//
// Every row starts with the same six columns that say when and how the test was run (LogContext), then the
// numbers. A file that already exists with different columns is never appended to (appendLog throws).

inline constexpr const char* kScreenLogFile = "screen_log.csv";
inline constexpr const char* kDirectionLogFile = "direction_control_log.csv";
inline constexpr const char* kComponentLogFile = "component_control_log.csv";
inline constexpr const char* kEntryStudyLogFile = "entry_study_log.csv";
inline constexpr const char* kEntryControlLogFile = "entry_control_log.csv";

struct LogContext
{
    std::string timestamp;      // UTC, see logTimestamp
    std::string note;           // free text from --note: why this test was run
    std::string setup;          // the nnfx switches, objective and minimum trades (setupText)
    std::string costs;          // where the spreads came from (costsText)
    std::string inSample;       // the data folders
    std::string outSample;
};

std::string logTimestamp(std::time_t when);              // "2023-11-14T22:13:20Z"
std::string setupText(const CliOptions& cli);            // e.g. "nnfx no-cross-exit confirm=rsi objective=sharpe min-trades=30"
std::string costsText(const CliOptions& cli);            // "none", "typical x1" or "costs.csv x2"
LogContext makeLogContext(const CliOptions& cli, std::time_t now);

// Appends `rows` (each already a full csv line ending in "\n") to dir/file, creating the folder and the file with
// `header` (a line ending in "\n") when needed. Throws std::runtime_error if the file exists with another first
// line, or cannot be written. Nothing is written when `rows` is empty.
void appendLog(const std::string& dir, const std::string& file, const std::string& header,
               const std::vector<std::string>& rows);

std::string screenLogHeader();
std::vector<std::string> screenLogRows(const LogContext& context, const std::vector<ScreenRow>& rows, int controlRuns);

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
