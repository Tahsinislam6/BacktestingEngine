#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "job.hpp"
#include "portfolio_job.hpp"
#include "run_store.hpp"
#include "sweep_job.hpp"

// Runs backtests in the background. submit() returns an id at once; one worker thread then runs the queued requests
// one at a time, in order, saving every state change in the store, so a caller just polls the store for the result.
//
// What cancel() did.
enum class CancelResult
{
    Cancelled,          // it was waiting in the queue: it will never run and is "cancelled" now
    Stopping,           // a sweep that is running was told to stop; it becomes "cancelled" at its next step
    NotCancellable,     // finished already, or a single run or portfolio that is already running (they are short)
    NotFound,           // no run with that id
};

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

        // The same for a portfolio (validatePortfolioSpec; throws std::invalid_argument, and nothing is recorded). The record
        // has kind "portfolio" and counts as one idea.
        std::string submitPortfolio(const PortfolioSpec& spec);

        // Cancels a run that has not finished (see CancelResult). Never throws for a bad id.
        CancelResult cancel(const std::string& id);

    private:
        // One queued piece of work: it returns the result document and may report progress. A job that can be stopped
        // while it runs (a sweep) checks the cancel function it is given.
        using Work = std::function<nlohmann::json(const ProgressFn&, const CancelFn&)>;
        struct Job
        {
            std::string id;
            Work work;
            bool stoppable{false};
        };

        std::string enqueue(RunRecord record, Work work, bool stoppable);
        void workerLoop();
        void execute(const Job& job);

        RunStore& store_;
        const StrategyRegistry& registry_;
        std::string dataDir_;

        std::mutex mutex_;
        std::condition_variable wake_;
        std::deque<Job> queue_;
        std::string runningId_;             // the job the worker is on ("" if none)
        bool runningStoppable_{false};
        bool cancelRequested_{false};       // for the running job
        bool stopping_{false};
        std::thread worker_;        // last, so everything above exists before it starts
};
