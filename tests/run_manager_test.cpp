#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "buy_and_hold.hpp"
#include "run_manager.hpp"

namespace fs = std::filesystem;

// The run manager takes a run request, gives back an id at once, and runs the backtest on a worker thread, saving
// every state change (queued, running, done or failed) in the run store.
//
// Rules:
//   1. submit() checks the request first (strategy, parameters, cash, dataset name); a bad request throws
//      std::invalid_argument and leaves no record and uses no id.
//   2. A good request gets a record "queued" at once, then "running", then "done" with the result document, or
//      "failed" with the error text (e.g. a dataset with no bars, which can only be found by reading it).
//   3. Runs are processed one at a time, in the order they were submitted.
//   4. A run found "queued" or "running" in the store when a manager starts was cut off by a stop: it becomes
//      "failed" with an error that says so.
//   5. Destroying a manager with work waiting does not hang.

class RunManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        root_ = fs::temp_directory_path() / (std::string("run_manager_test_") + info->name());
        fs::remove_all(root_);
        fs::create_directories(root_ / "data");
        registerBuiltins(registry_);
        std::ofstream(root_ / "data" / "x.txt") <<
            "<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>\n"
            "X,D,20200101,000000,100,100,100,100,0,0\n"
            "X,D,20200102,000000,110,110,110,110,0,0\n";
        std::ofstream(root_ / "data" / "empty.csv") << "timestamp,open,high,low,close\n";
    }
    void TearDown() override { fs::remove_all(root_); }

    std::string dataDir() const { return (root_ / "data").string(); }
    std::string runsDir() const { return (root_ / "runs").string(); }

    static RunSpec spec(const std::string& dataset = "x.txt")
    {
        RunSpec s;
        s.strategy = "buy_and_hold";
        s.dataset = dataset;
        s.cash = 1000;
        return s;
    }

    // Waits until the run is done or failed (at most 10 seconds).
    static RunRecord waitFinished(RunStore& store, const std::string& id)
    {
        for (int i = 0; i < 1000; ++i)
        {
            const auto r = store.load(id);
            if (r && (r->status == RunStatus::Done || r->status == RunStatus::Failed))
                return *r;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ADD_FAILURE() << "run " << id << " did not finish";
        return {};
    }

    StrategyRegistry registry_;
    fs::path root_;
};

TEST_F(RunManagerTest, AGoodRunEndsDoneWithAResult)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    const std::string id = manager.submit(spec());
    const RunRecord r = waitFinished(store, id);

    EXPECT_EQ(r.id, "000001");
    EXPECT_EQ(r.status, RunStatus::Done);
    EXPECT_EQ(r.error, "");
    EXPECT_EQ(r.spec.at("strategy"), "buy_and_hold");
    EXPECT_EQ(r.spec.at("params").at("fraction").get<double>(), 1.0);      // the resolved parameters are logged
    EXPECT_EQ(r.result.at("equity").size(), 2u);
    EXPECT_NEAR(r.result.at("equity").back().get<double>(), 1100.0, 1e-9);
    EXPECT_EQ(r.createdAt.size(), 20u);                                      // 2026-11-25T10:00:00Z
    EXPECT_EQ(r.createdAt.back(), 'Z');
}

TEST_F(RunManagerTest, ABadRequestThrowsAndLeavesNothingBehind)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    RunSpec bad = spec();
    bad.strategy = "nope";
    EXPECT_THROW(manager.submit(bad), std::invalid_argument);
    bad = spec("missing.txt");
    EXPECT_THROW(manager.submit(bad), std::invalid_argument);
    bad = spec();
    bad.cash = 0;
    EXPECT_THROW(manager.submit(bad), std::invalid_argument);

    EXPECT_TRUE(store.list().empty());
    EXPECT_EQ(manager.submit(spec()), "000001");     // the failed requests used no id
}

TEST_F(RunManagerTest, ARunThatFailsWhileRunningEndsFailedWithTheError)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    const RunRecord r = waitFinished(store, manager.submit(spec("empty.csv")));

    EXPECT_EQ(r.status, RunStatus::Failed);
    EXPECT_NE(r.error.find("no bars"), std::string::npos) << r.error;
}

TEST_F(RunManagerTest, SeveralRunsAllFinish)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    std::vector<std::string> ids;
    for (int i = 0; i < 5; ++i)
        ids.push_back(manager.submit(spec()));

    for (const auto& id : ids)
        EXPECT_EQ(waitFinished(store, id).status, RunStatus::Done) << id;
    EXPECT_EQ(ids.front(), "000001");
    EXPECT_EQ(ids.back(), "000005");
}

TEST_F(RunManagerTest, RunsCutOffByAStopBecomeFailed)
{
    {
        RunStore store(runsDir());
        RunRecord q;  q.id = "000001"; q.status = RunStatus::Queued;  q.createdAt = "2026-11-25T10:00:00Z";
        RunRecord r;  r.id = "000002"; r.status = RunStatus::Running; r.createdAt = "2026-11-25T10:00:00Z";
        RunRecord d;  d.id = "000003"; d.status = RunStatus::Done;    d.createdAt = "2026-11-25T10:00:00Z";
        store.save(q); store.save(r); store.save(d);
    }
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    EXPECT_EQ(store.load("000001")->status, RunStatus::Failed);
    EXPECT_NE(store.load("000001")->error.find("stopped"), std::string::npos);
    EXPECT_EQ(store.load("000002")->status, RunStatus::Failed);
    EXPECT_EQ(store.load("000003")->status, RunStatus::Done);        // finished runs are left alone
    EXPECT_EQ(manager.submit(spec()), "000004");
}

TEST_F(RunManagerTest, DestroyingAManagerWithWorkWaitingDoesNotHang)
{
    RunStore store(runsDir());
    {
        RunManager manager(store, registry_, dataDir());
        for (int i = 0; i < 50; ++i)
            manager.submit(spec());
    }
    SUCCEED();
}
