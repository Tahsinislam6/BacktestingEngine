#include "run_manager.hpp"

#include <chrono>
#include <ctime>
#include <exception>

#include "job_json.hpp"

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

std::string RunManager::submit(const RunSpec& spec)
{
    validateSpec(registry_, dataDir_, spec);

    RunSpec resolved = spec;
    resolved.params = registry_.resolveParams(spec.strategy, spec.params);

    RunRecord record;
    record.id = store_.nextId();
    record.status = RunStatus::Queued;
    record.createdAt = nowUtc();
    record.spec = toJson(resolved);
    store_.save(record);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.emplace_back(record.id, resolved);
    }
    wake_.notify_one();
    return record.id;
}

void RunManager::workerLoop()
{
    for (;;)
    {
        std::pair<std::string, RunSpec> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_)
                return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        execute(job.first, job.second);
    }
}

void RunManager::execute(const std::string& id, const RunSpec& spec)
{
    RunRecord record;
    try
    {
        record = *store_.load(id);
        record.status = RunStatus::Running;
        store_.save(record);

        record.result = toJson(runJob(registry_, dataDir_, spec));
        record.status = RunStatus::Done;
    }
    catch (const std::exception& e)
    {
        record.id = id;
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
