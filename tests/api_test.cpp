#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "api.hpp"
#include "buy_and_hold.hpp"
#include "test_strategies.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

// ApiHandler is the whole HTTP API as a plain function: (method, path, body) -> (status, JSON text). No sockets are
// involved, so every rule can be tested directly. The socket layer (http_server.hpp) only passes requests through.
//
//   GET  /api/strategies    200  the strategies with their parameter schemas
//   GET  /api/datasets      200  ["file.txt", ...]
//   POST /api/runs          202  {"id": "000001"}; the body is a run request (see job_json.hpp)
//   GET  /api/runs/{id}     200  the run record: id, status, createdAt, spec, and result or error
//   GET  /api/runs          200  one summary per run, newest first: id, status, createdAt, spec, metrics, error
//                                (no equity curves or trades, so the list stays small)
//
// Errors are always {"error": "text"}: 400 for a bad request (bad JSON, unknown strategy or parameter, bad dataset),
// 404 for an unknown path or run, 405 for the wrong method on a known path, 500 for anything unexpected.

class ApiTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        root_ = fs::temp_directory_path() / (std::string("api_test_") + info->name());
        fs::remove_all(root_);
        fs::create_directories(root_ / "data");
        registerBuiltins(registry_);
        registerScaledHold(registry_);
        writeSixBars(root_ / "data" / "six.txt");
        std::ofstream(root_ / "data" / "x.txt") <<
            "<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>\n"
            "X,D,20200101,000000,100,100,100,100,0,0\n"
            "X,D,20200102,000000,110,110,110,110,0,0\n";
        store_ = std::make_unique<RunStore>((root_ / "runs").string());
        manager_ = std::make_unique<RunManager>(*store_, registry_, (root_ / "data").string());
        api_ = std::make_unique<ApiHandler>(registry_, (root_ / "data").string(), *store_, *manager_);
    }
    void TearDown() override
    {
        api_.reset();
        manager_.reset();
        store_.reset();
        fs::remove_all(root_);
    }

    HttpResponse call(const std::string& method, const std::string& path, const std::string& body = "") const
    {
        return api_->handle(method, path, body);
    }

    std::string submitGoodRun() const
    {
        const HttpResponse r = call("POST", "/api/runs", R"({"strategy":"buy_and_hold","dataset":"x.txt","cash":1000})");
        EXPECT_EQ(r.status, 202) << r.body;
        return json::parse(r.body).at("id").get<std::string>();
    }

    json waitFinished(const std::string& id) const
    {
        for (int i = 0; i < 1000; ++i)
        {
            const json doc = json::parse(call("GET", "/api/runs/" + id).body);
            if (doc.at("status") == "done" || doc.at("status") == "failed")
                return doc;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ADD_FAILURE() << "run " << id << " did not finish";
        return {};
    }

    StrategyRegistry registry_;
    fs::path root_;
    std::unique_ptr<RunStore> store_;
    std::unique_ptr<RunManager> manager_;
    std::unique_ptr<ApiHandler> api_;
};

TEST_F(ApiTest, StrategiesListsTheSchemas)
{
    const HttpResponse r = call("GET", "/api/strategies");
    ASSERT_EQ(r.status, 200);
    EXPECT_EQ(r.contentType, "application/json");
    const json doc = json::parse(r.body);
    ASSERT_EQ(doc.size(), 2u);                                   // buy_and_hold and the test strategy scaled_hold
    EXPECT_EQ(doc[0].at("name"), "buy_and_hold");
    EXPECT_EQ(doc[0].at("params")[0].at("name"), "fraction");
    EXPECT_EQ(doc[1].at("name"), "scaled_hold");
    EXPECT_EQ(doc[1].at("params").size(), 2u);
}

TEST_F(ApiTest, DatasetsListsTheFileNames)
{
    const HttpResponse r = call("GET", "/api/datasets");
    ASSERT_EQ(r.status, 200);
    EXPECT_EQ(json::parse(r.body), json::parse(R"(["six.txt","x.txt"])"));
}

TEST_F(ApiTest, PostingARunGivesAnIdAndTheRunFinishes)
{
    const std::string id = submitGoodRun();
    EXPECT_EQ(id, "000001");

    const json doc = waitFinished(id);

    EXPECT_EQ(doc.at("status"), "done");
    EXPECT_EQ(doc.at("id"), "000001");
    EXPECT_EQ(doc.at("spec").at("strategy"), "buy_and_hold");
    EXPECT_NEAR(doc.at("result").at("equity").back().get<double>(), 1100.0, 1e-9);
    EXPECT_FALSE(doc.contains("error"));
}

TEST_F(ApiTest, ABadRunRequestIs400WithAnErrorText)
{
    for (const char* body : {
        "{not json",
        R"({"dataset":"x.txt"})",
        R"({"strategy":"nope","dataset":"x.txt"})",
        R"({"strategy":"buy_and_hold","dataset":"missing.txt"})",
        R"({"strategy":"buy_and_hold","dataset":"../x.txt"})",
        R"({"strategy":"buy_and_hold","dataset":"x.txt","params":{"bogus":1}})",
        R"({"strategy":"buy_and_hold","dataset":"x.txt","cash":-1})",
        ""})
    {
        const HttpResponse r = call("POST", "/api/runs", body);
        EXPECT_EQ(r.status, 400) << body;
        const json doc = json::parse(r.body);
        EXPECT_TRUE(doc.at("error").is_string() && !doc.at("error").get<std::string>().empty()) << body;
    }
    EXPECT_TRUE(store_->list().empty());
}

TEST_F(ApiTest, AFailedRunShowsItsError)
{
    std::ofstream(root_ / "data" / "empty.csv") << "timestamp,open,high,low,close\n";
    const HttpResponse r = call("POST", "/api/runs", R"({"strategy":"buy_and_hold","dataset":"empty.csv"})");
    ASSERT_EQ(r.status, 202);

    const json doc = waitFinished(json::parse(r.body).at("id"));

    EXPECT_EQ(doc.at("status"), "failed");
    EXPECT_NE(doc.at("error").get<std::string>().find("no bars"), std::string::npos);
}

TEST_F(ApiTest, UnknownRunIs404)
{
    EXPECT_EQ(call("GET", "/api/runs/000099").status, 404);
    EXPECT_EQ(call("GET", "/api/runs/abc").status, 404);
    EXPECT_EQ(call("GET", "/api/runs/..%2F..%2Fetc").status, 404);
    EXPECT_EQ(call("GET", "/api/runs/000001/extra").status, 404);
}

TEST_F(ApiTest, RunListHasSummariesNewestFirstWithoutTheBigArrays)
{
    const std::string a = submitGoodRun();
    const std::string b = submitGoodRun();
    waitFinished(a);
    waitFinished(b);

    const HttpResponse r = call("GET", "/api/runs");
    ASSERT_EQ(r.status, 200);
    const json list = json::parse(r.body);

    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].at("id"), "000002");
    EXPECT_EQ(list[1].at("id"), "000001");
    EXPECT_EQ(list[0].at("status"), "done");
    EXPECT_EQ(list[0].at("spec").at("dataset"), "x.txt");
    EXPECT_NEAR(list[0].at("metrics").at("totalReturn").get<double>(), 0.1, 1e-9);
    EXPECT_FALSE(list[0].contains("result"));
    EXPECT_EQ(r.body.find("equity"), std::string::npos);
}

TEST_F(ApiTest, EmptyRunListIsAnEmptyArray)
{
    const HttpResponse r = call("GET", "/api/runs");
    ASSERT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "[]");
}

TEST_F(ApiTest, UnknownPathsAre404AndWrongMethodsAre405)
{
    EXPECT_EQ(call("GET", "/api/nothing").status, 404);
    EXPECT_EQ(call("GET", "/elsewhere").status, 404);
    EXPECT_EQ(call("POST", "/api/strategies").status, 405);
    EXPECT_EQ(call("DELETE", "/api/runs").status, 405);
    EXPECT_EQ(call("PUT", "/api/runs/000001").status, 405);
    EXPECT_EQ(call("POST", "/api/datasets").status, 405);
    const json doc = json::parse(call("GET", "/api/nothing").body);
    EXPECT_TRUE(doc.contains("error"));
}

TEST_F(ApiTest, ADamagedRunFileIs500NotACrash)
{
    std::ofstream(root_ / "runs" / "000005.json") << "{not json";
    EXPECT_EQ(call("GET", "/api/runs/000005").status, 500);
}

// ---- sweeps: POST /api/sweeps 202 {"id"}; the run record and the list then say kind "sweep" ----

static const char* kSweepBody = R"({
    "strategy": "scaled_hold", "dataset": "six.txt",
    "axes": [{"param": "delay", "from": 0, "to": 2, "step": 1}, {"param": "size", "from": 1, "to": 2, "step": 1}],
    "metric": "total-return", "threshold": 0.055, "radius": 1, "minTrades": 0, "nullRuns": 3, "block": 1,
    "cash": 1000, "note": "my pass rule" })";

TEST_F(ApiTest, PostingASweepGivesAnIdAndTheSweepFinishes)
{
    const HttpResponse r = call("POST", "/api/sweeps", kSweepBody);
    ASSERT_EQ(r.status, 202) << r.body;
    const std::string id = json::parse(r.body).at("id");

    const json doc = waitFinished(id);

    EXPECT_EQ(doc.at("status"), "done");
    EXPECT_EQ(doc.at("kind"), "sweep");
    EXPECT_EQ(doc.at("ideas").get<int>(), 24);
    EXPECT_EQ(doc.at("spec").at("note"), "my pass rule");
    EXPECT_EQ(doc.at("result").at("cells").get<int>(), 6);
    EXPECT_EQ(doc.at("result").at("grid").at("values").size(), 6u);
}

TEST_F(ApiTest, ABadSweepIs400AndSaysWhy)
{
    json noNote = json::parse(kSweepBody);
    noNote["note"] = "";
    HttpResponse r = call("POST", "/api/sweeps", noNote.dump());
    EXPECT_EQ(r.status, 400);
    EXPECT_NE(json::parse(r.body).at("error").get<std::string>().find("note"), std::string::npos) << r.body;

    json missingMetric = json::parse(kSweepBody);
    missingMetric.erase("metric");
    EXPECT_EQ(call("POST", "/api/sweeps", missingMetric.dump()).status, 400);

    json outOfRange = json::parse(kSweepBody);
    outOfRange["axes"][0]["to"] = 9;
    EXPECT_EQ(call("POST", "/api/sweeps", outOfRange.dump()).status, 400);

    EXPECT_EQ(call("POST", "/api/sweeps", "{not json").status, 400);
    EXPECT_EQ(call("POST", "/api/sweeps", "").status, 400);
    EXPECT_TRUE(store_->list().empty());
}

TEST_F(ApiTest, SweepsOnlyAcceptPost)
{
    EXPECT_EQ(call("GET", "/api/sweeps").status, 405);
    EXPECT_EQ(call("DELETE", "/api/sweeps").status, 405);
}

TEST_F(ApiTest, TheListSummarisesASweepWithoutItsGrid)
{
    const std::string sweepId = json::parse(call("POST", "/api/sweeps", kSweepBody).body).at("id");
    waitFinished(sweepId);
    waitFinished(submitGoodRun());

    const HttpResponse r = call("GET", "/api/runs");
    ASSERT_EQ(r.status, 200);
    const json list = json::parse(r.body);

    ASSERT_EQ(list.size(), 2u);
    const json& run = list[0];                               // newest first
    const json& sweep = list[1];
    EXPECT_EQ(run.at("kind"), "run");
    EXPECT_EQ(run.at("ideas").get<int>(), 1);
    EXPECT_TRUE(run.contains("metrics"));

    EXPECT_EQ(sweep.at("kind"), "sweep");
    EXPECT_EQ(sweep.at("ideas").get<int>(), 24);
    EXPECT_EQ(sweep.at("spec").at("dataset"), "six.txt");
    ASSERT_TRUE(sweep.contains("headline"));
    EXPECT_EQ(sweep.at("headline").at("cells").get<int>(), 6);
    EXPECT_DOUBLE_EQ(sweep.at("headline").at("plateauDensity").get<double>(), 0.0);
    EXPECT_TRUE(sweep.at("headline").contains("shuffledPlateau95"));
    EXPECT_FALSE(sweep.contains("result"));
    EXPECT_EQ(sweep.dump().find("\"grid\""), std::string::npos);
}

// ---- portfolios: POST /api/portfolios 202 {"id"}; kind "portfolio" with a small summary in the list ----

static const char* kPortfolioBody = R"({
    "sleeves": [
        {"name": "a", "dataset": "six.txt", "strategy": "scaled_hold", "params": {"size": 2}, "cash": 1000},
        {"name": "b", "dataset": "six.txt", "strategy": "scaled_hold", "params": {"size": 1}, "cash": 500}],
    "periodsPerYear": 365 })";

TEST_F(ApiTest, PostingAPortfolioGivesAnIdAndItFinishes)
{
    const HttpResponse r = call("POST", "/api/portfolios", kPortfolioBody);
    ASSERT_EQ(r.status, 202) << r.body;

    const json doc = waitFinished(json::parse(r.body).at("id"));

    EXPECT_EQ(doc.at("status"), "done");
    EXPECT_EQ(doc.at("kind"), "portfolio");
    EXPECT_EQ(doc.at("ideas").get<int>(), 1);
    EXPECT_EQ(doc.at("result").at("sleeves").size(), 2u);
    EXPECT_NEAR(doc.at("result").at("combined").at("finalEquity").get<double>(), 1650, 1e-9);
}

TEST_F(ApiTest, ABadPortfolioIs400AndSaysWhy)
{
    json duplicate = json::parse(kPortfolioBody);
    duplicate["sleeves"][1]["name"] = "a";
    HttpResponse r = call("POST", "/api/portfolios", duplicate.dump());
    EXPECT_EQ(r.status, 400);
    EXPECT_NE(json::parse(r.body).at("error").get<std::string>().find("a"), std::string::npos);

    json missingDataset = json::parse(kPortfolioBody);
    missingDataset["sleeves"][0]["dataset"] = "nope.txt";
    EXPECT_EQ(call("POST", "/api/portfolios", missingDataset.dump()).status, 400);
    EXPECT_EQ(call("POST", "/api/portfolios", R"({"sleeves": []})").status, 400);
    EXPECT_EQ(call("POST", "/api/portfolios", "{not json").status, 400);
    EXPECT_EQ(call("POST", "/api/portfolios", "").status, 400);
    EXPECT_TRUE(store_->list().empty());
}

TEST_F(ApiTest, PortfoliosOnlyAcceptPost)
{
    EXPECT_EQ(call("GET", "/api/portfolios").status, 405);
    EXPECT_EQ(call("DELETE", "/api/portfolios").status, 405);
}

TEST_F(ApiTest, TheListSummarisesAPortfolioWithoutItsCurves)
{
    waitFinished(json::parse(call("POST", "/api/portfolios", kPortfolioBody).body).at("id"));

    const json list = json::parse(call("GET", "/api/runs").body);

    ASSERT_EQ(list.size(), 1u);
    const json& p = list[0];
    EXPECT_EQ(p.at("kind"), "portfolio");
    ASSERT_TRUE(p.contains("headline"));
    EXPECT_EQ(p.at("headline").at("sleeves").get<int>(), 2);
    EXPECT_DOUBLE_EQ(p.at("headline").at("cash").get<double>(), 1500);
    EXPECT_NEAR(p.at("headline").at("finalEquity").get<double>(), 1650, 1e-9);
    EXPECT_NEAR(p.at("headline").at("totalReturn").get<double>(), 0.1, 1e-9);
    EXPECT_TRUE(p.at("headline").contains("maxDrawdown"));
    EXPECT_FALSE(p.contains("result"));
    EXPECT_EQ(p.dump().find("\"equity\""), std::string::npos);
}
