#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "buy_and_hold.hpp"
#include "run_manager.hpp"
#include "portfolio_job.hpp"
#include "sweep_job.hpp"
#include "test_strategies.hpp"

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
        registerScaledHold(registry_);
        writeSixBars(root_ / "data" / "six.txt");
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

// ---- sweeps: queued like runs, with progress and the count of ideas tried ----

static SweepSpec sweepSpec()
{
    SweepSpec s;
    s.strategy = "scaled_hold";
    s.dataset = "six.txt";
    s.axes = {{"delay", 0, 2, 1}, {"size", 1, 2, 1}};
    s.metric = "total-return";
    s.threshold = 0.055;
    s.radius = 1;
    s.minTrades = 0;
    s.nullRuns = 3;
    s.block = 1;
    s.cash = 1000;
    s.note = "pass rule";
    return s;
}

TEST_F(RunManagerTest, ASweepEndsDoneWithItsResultKindIdeasAndProgress)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    const RunRecord r = waitFinished(store, manager.submitSweep(sweepSpec()));

    EXPECT_EQ(r.status, RunStatus::Done);
    EXPECT_EQ(r.kind, "sweep");
    EXPECT_EQ(r.ideas, 24);                                  // 6 cells x (1 real + 3 shuffled)
    EXPECT_EQ(r.spec.at("note"), "pass rule");
    EXPECT_EQ(r.result.at("cells").get<int>(), 6);
    EXPECT_EQ(r.result.at("ideasTried").get<int>(), 24);
    EXPECT_EQ(r.progress.at("done"), r.progress.at("total"));
    EXPECT_EQ(r.progress.at("total").get<int>(), 4);
}

TEST_F(RunManagerTest, ARunIsKindRunWithOneIdea)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());
    const RunRecord r = waitFinished(store, manager.submit(spec()));
    EXPECT_EQ(r.kind, "run");
    EXPECT_EQ(r.ideas, 1);
}

TEST_F(RunManagerTest, ABadSweepThrowsAndLeavesNothingBehind)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    SweepSpec noNote = sweepSpec();
    noNote.note = "";
    EXPECT_THROW(manager.submitSweep(noNote), std::invalid_argument);
    SweepSpec bad = sweepSpec();
    bad.axes[0].to = 9;
    EXPECT_THROW(manager.submitSweep(bad), std::invalid_argument);

    EXPECT_TRUE(store.list().empty());
    EXPECT_EQ(manager.submitSweep(sweepSpec()), "000001");
}

TEST_F(RunManagerTest, ASweepThatFailsWhileRunningEndsFailedAndStillCountsItsIdeas)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());
    SweepSpec s = sweepSpec();
    s.dataset = "empty.csv";

    const RunRecord r = waitFinished(store, manager.submitSweep(s));

    EXPECT_EQ(r.status, RunStatus::Failed);
    EXPECT_NE(r.error.find("no bars"), std::string::npos) << r.error;
    EXPECT_EQ(r.ideas, 24);                                  // an idea started is an idea tried
}

TEST_F(RunManagerTest, RunsAndSweepsShareTheQueueAndTheIds)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    const std::string a = manager.submit(spec());
    const std::string b = manager.submitSweep(sweepSpec());
    const std::string c = manager.submit(spec());

    EXPECT_EQ(a, "000001");
    EXPECT_EQ(b, "000002");
    EXPECT_EQ(c, "000003");
    for (const auto& id : {a, b, c})
        EXPECT_EQ(waitFinished(store, id).status, RunStatus::Done) << id;
}

// ---- portfolios: queued like runs; one idea each ----

static PortfolioSpec portfolioSpec()
{
    PortfolioSpec p;
    p.sleeves = {{"a", "six.txt", "scaled_hold", {{"size", 2}}, 1000}, {"b", "six.txt", "scaled_hold", {{"size", 1}}, 500}};
    return p;
}

TEST_F(RunManagerTest, APortfolioEndsDoneWithItsResultAndKind)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    const RunRecord r = waitFinished(store, manager.submitPortfolio(portfolioSpec()));

    EXPECT_EQ(r.status, RunStatus::Done);
    EXPECT_EQ(r.kind, "portfolio");
    EXPECT_EQ(r.ideas, 1);
    EXPECT_EQ(r.spec.at("sleeves").size(), 2u);
    EXPECT_NEAR(r.result.at("combined").at("finalEquity").get<double>(), 1000 + 2 * 50 + 500 + 50, 1e-9);
}

TEST_F(RunManagerTest, ABadPortfolioThrowsAndLeavesNothingBehind)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());

    PortfolioSpec bad = portfolioSpec();
    bad.sleeves[1].name = "a";
    EXPECT_THROW(manager.submitPortfolio(bad), std::invalid_argument);
    PortfolioSpec none;
    EXPECT_THROW(manager.submitPortfolio(none), std::invalid_argument);

    EXPECT_TRUE(store.list().empty());
    EXPECT_EQ(manager.submitPortfolio(portfolioSpec()), "000001");
}

TEST_F(RunManagerTest, APortfolioThatFailsWhileRunningEndsFailed)
{
    RunStore store(runsDir());
    RunManager manager(store, registry_, dataDir());
    PortfolioSpec p = portfolioSpec();
    p.sleeves[0].dataset = "empty.csv";

    const RunRecord r = waitFinished(store, manager.submitPortfolio(p));

    EXPECT_EQ(r.status, RunStatus::Failed);
    EXPECT_NE(r.error.find("no bars"), std::string::npos) << r.error;
}
