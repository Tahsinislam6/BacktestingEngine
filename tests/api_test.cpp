#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "api.hpp"
#include "buy_and_hold.hpp"

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
    ASSERT_EQ(doc.size(), 1u);
    EXPECT_EQ(doc[0].at("name"), "buy_and_hold");
    EXPECT_EQ(doc[0].at("params")[0].at("name"), "fraction");
}

TEST_F(ApiTest, DatasetsListsTheFileNames)
{
    const HttpResponse r = call("GET", "/api/datasets");
    ASSERT_EQ(r.status, 200);
    EXPECT_EQ(json::parse(r.body), json::parse(R"(["x.txt"])"));
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
