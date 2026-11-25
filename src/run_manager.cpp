#include "run_manager.hpp"

#include <chrono>
#include <ctime>
#include <exception>

#include "job_json.hpp"
#include "portfolio_job_json.hpp"
#include "sweep_job_json.hpp"

namespace
{
    // The current time in UTC, e.g. 2026-11-25T10:00:00Z.
    std::string nowUtc()
    {
        const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm parts{};
        gmtime_r(&t, &parts);
        char text[32];
        std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &parts);
        return text;
    }
}

RunManager::RunManager(RunStore& store, const StrategyRegistry& registry, std::string dataDir)
    : store_(store), registry_(registry), dataDir_(std::move(dataDir))
{
    for (RunRecord r : store_.list())
    {
        if (r.status == RunStatus::Queued || r.status == RunStatus::Running)
        {
            r.status = RunStatus::Failed;
            r.error = "the server stopped before this run finished";
            store_.save(r);
        }
    }
    worker_ = std::thread([this] { workerLoop(); });
}

RunManager::~RunManager()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

std::string RunManager::enqueue(RunRecord record, Work work, bool stoppable)
{
    record.id = store_.nextId();
    record.status = RunStatus::Queued;
    record.createdAt = nowUtc();
    store_.save(record);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({record.id, std::move(work), stoppable});
    }
    wake_.notify_one();
    return record.id;
}

std::string RunManager::submit(const RunSpec& spec)
{
    validateSpec(registry_, dataDir_, spec);

    RunSpec resolved = spec;
    resolved.params = registry_.resolveParams(spec.strategy, spec.params);

    RunRecord record;
    record.kind = "run";
    record.ideas = 1;
    record.spec = toJson(resolved);
    return enqueue(std::move(record), [this, resolved](const ProgressFn&, const CancelFn&)
    {
        return toJson(runJob(registry_, dataDir_, resolved));
    }, false);
}

std::string RunManager::submitSweep(const SweepSpec& spec)
{
    validateSweepSpec(registry_, dataDir_, spec);

    const SweepSpec resolved = resolveSweepSpec(registry_, spec);

    RunRecord record;
    record.kind = "sweep";
    record.ideas = static_cast<int>(sweepIdeas(registry_, resolved));
    record.spec = toJson(resolved);
    return enqueue(std::move(record), [this, resolved](const ProgressFn& progress, const CancelFn& cancelled)
    {
        return toJson(runSweep(registry_, dataDir_, resolved, progress, 0, cancelled));
    }, true);
}

std::string RunManager::submitPortfolio(const PortfolioSpec& spec)
{
    validatePortfolioSpec(registry_, dataDir_, spec);

    const PortfolioSpec resolved = resolvePortfolioSpec(registry_, spec);

    RunRecord record;
    record.kind = "portfolio";
    record.ideas = 1;
    record.spec = toJson(resolved);
    return enqueue(std::move(record), [this, resolved](const ProgressFn&, const CancelFn&)
    {
        return toJson(runPortfolio(registry_, dataDir_, resolved));
    }, false);
}

CancelResult RunManager::cancel(const std::string& id)
{
    if (!RunStore::isValidId(id))
        return CancelResult::NotFound;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = queue_.begin(); it != queue_.end(); ++it)
    {
        if (it->id != id)
            continue;
        queue_.erase(it);
        if (auto record = store_.load(id))
        {
            record->status = RunStatus::Cancelled;
            record->error = "cancelled before it started";
            store_.save(*record);
        }
        return CancelResult::Cancelled;
    }
    if (runningId_ == id)
    {
        if (!runningStoppable_)
            return CancelResult::NotCancellable;
        cancelRequested_ = true;
        return CancelResult::Stopping;
    }
    return store_.load(id) ? CancelResult::NotCancellable : CancelResult::NotFound;
}

void RunManager::workerLoop()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_)
                return;
            job = std::move(queue_.front());
            queue_.pop_front();
            runningId_ = job.id;
            runningStoppable_ = job.stoppable;
            cancelRequested_ = false;
        }
        execute(job);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            runningId_.clear();
        }
    }
}

void RunManager::execute(const Job& job)
{
    RunRecord record;
    try
    {
        record = *store_.load(job.id);
        record.status = RunStatus::Running;
        store_.save(record);

        // Progress is saved at most every quarter of a second (and always at the end), so a long job does not spend its
        // time writing files. The job never calls this from two threads at once.
        auto lastSave = std::chrono::steady_clock::now();
        const ProgressFn progress = [&](std::size_t done, std::size_t total)
        {
            record.progress = {{"done", done}, {"total", total}};
            const auto now = std::chrono::steady_clock::now();
            if (done >= total || now - lastSave >= std::chrono::milliseconds(250))
            {
                store_.save(record);
                lastSave = now;
            }
        };

        const CancelFn cancelled = [this]
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return cancelRequested_ || stopping_;
        };
        record.result = job.work(progress, cancelled);
        record.status = RunStatus::Done;
    }
    catch (const JobCancelled&)
    {
        record.id = job.id;
        std::lock_guard<std::mutex> lock(mutex_);
        record.status = RunStatus::Cancelled;
        record.error = stopping_ ? "the server stopped before this run finished" : "cancelled by you";
    }
    catch (const std::exception& e)
    {
        record.id = job.id;
        record.status = RunStatus::Failed;
        record.error = e.what();
    }
    try
    {
        store_.save(record);
    }
    catch (const std::exception&)
    {
        // nothing more can be done if even the failure cannot be written
    }
}
