#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

// The states of a run. A run is queued, then running, then done or failed.
namespace RunStatus
{
    inline constexpr const char* Queued = "queued";
    inline constexpr const char* Running = "running";
    inline constexpr const char* Done = "done";
    inline constexpr const char* Failed = "failed";
}

// Everything kept about one run.
struct RunRecord
{
    std::string id;                 // "000001", ...
    std::string status;             // see RunStatus
    std::string createdAt;          // UTC, e.g. 2026-11-25T10:00:00Z
    nlohmann::json spec;            // the request as run (with every parameter filled in)
    nlohmann::json result;          // the result document (null until the run is done)
    std::string error;              // why it failed (empty otherwise)
    std::string kind{"run"};        // "run" (one backtest) or "sweep" (a density sweep)
    int ideas{1};                   // ideas tried: 1 for a run; cells x (1 + shuffled copies) for a sweep. Counted when the job is queued
    nlohmann::json progress;        // {"done": n, "total": m} while a long job runs (null otherwise)
};

// The log of every run: one JSON file per run (<id>.json) in one folder. Writing is atomic (a temp file, then a rename), so
// a crash never leaves half a file. Safe to use from several threads.
class RunStore
{
    public:
        // Creates the folder if it is missing. Throws std::runtime_error if it cannot be created.
        explicit RunStore(std::string dir);

        // The next unused id, zero-padded to 6 digits: "000001", "000002", ... After a restart it continues after the
        // highest id already in the folder, so an id is never used twice.
        std::string nextId();

        // Writes (or replaces) the run's file. Throws std::invalid_argument for a bad id.
        void save(const RunRecord& record);

        // Throws std::invalid_argument for a bad id and std::runtime_error for a damaged file; gives nothing if there is no such run.
        std::optional<RunRecord> load(const std::string& id) const;

        // Every readable run, newest first. Files that are not runs, and damaged ones, are skipped.
        std::vector<RunRecord> list() const;

        // 1 to 12 digits. An id becomes part of a file name, so nothing else is ever accepted.
        static bool isValidId(const std::string& id);

    private:
        std::string pathOf(const std::string& id) const;

        std::string dir_;
        mutable std::mutex mutex_;
        long long next_{1};
};
