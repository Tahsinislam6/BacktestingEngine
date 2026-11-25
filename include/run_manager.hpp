#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "job.hpp"
#include "run_store.hpp"
#include "sweep_job.hpp"

// Runs backtests in the background. submit() returns an id at once; one worker thread then runs the queued requests
// one at a time, in order, saving every state change in the store, so a caller just polls the store for the result.
//
// The store, the registry and the data folder must outlive the manager.
class RunManager
{
    public:
        // Marks every run the store has as queued or running (cut off by a stop) as failed, then starts the worker.
        RunManager(RunStore& store, const StrategyRegistry& registry, std::string dataDir);

        // Stops the worker after the run it is on; runs still waiting stay "queued" in the store (and become "failed" the
        // next time a manager starts on that store).
        ~RunManager();

        RunManager(const RunManager&) = delete;
        RunManager& operator=(const RunManager&) = delete;

        // Checks the request (validateSpec; throws std::invalid_argument, and nothing is recorded), writes a "queued"
        // record and queues the run. Returns the run's id.
        std::string submit(const RunSpec& spec);

        // The same for a density sweep (validateSweepSpec; throws std::invalid_argument, and nothing is recorded). The record
        // has kind "sweep" and says how many ideas it tries, and its progress is saved while it runs.
        std::string submitSweep(const SweepSpec& spec);

    private:
        // One queued piece of work: it returns the result document and may report progress.
        struct Job
        {
            std::string id;
            std::function<nlohmann::json(const ProgressFn&)> work;
        };

        std::string enqueue(RunRecord record, std::function<nlohmann::json(const ProgressFn&)> work);
        void workerLoop();
        void execute(const Job& job);

        RunStore& store_;
        const StrategyRegistry& registry_;
        std::string dataDir_;

        std::mutex mutex_;
        std::condition_variable wake_;
        std::deque<Job> queue_;
        bool stopping_{false};
        std::thread worker_;        // last, so everything above exists before it starts
};
