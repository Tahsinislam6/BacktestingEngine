#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <httplib.h>

#include "buy_and_hold.hpp"
#include "http_server.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

// One end-to-end check through a real socket: the server answers on localhost, the API paths reach the handler, and
// files from the web folder are served at the root. The handler's own rules are tested in api_test.cpp.
//
//   1. The server listens on the given host only (tests use 127.0.0.1) and port 0 means "any free port".
//   2. A web folder that does not exist is std::invalid_argument; no web folder means no static files.
//   3. stop() makes serve() return.

class HttpServerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        root_ = fs::temp_directory_path() / (std::string("http_server_test_") + info->name());
        fs::remove_all(root_);
        fs::create_directories(root_ / "data");
        fs::create_directories(root_ / "web");
        registerBuiltins(registry_);
        std::ofstream(root_ / "web" / "index.html") << "<html>hello web app</html>";
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
        server_.reset();
        api_.reset();
        manager_.reset();
        store_.reset();
        fs::remove_all(root_);
    }

    void start(const std::string& webDir)
    {
        server_ = std::make_unique<HttpServer>(*api_, webDir);
        port_ = server_->bind("127.0.0.1", 0);
        ASSERT_GT(port_, 0);
        thread_ = std::thread([this] { server_->serve(); });
    }
    // A stop that arrives before the server is listening is ignored by cpp-httplib, so wait until it answers.
    void waitUntilAnswering()
    {
        httplib::Client client("127.0.0.1", port_);
        for (int i = 0; i < 500; ++i)
        {
            if (client.Get("/api/datasets"))
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        FAIL() << "the server never answered";
    }
    void stop()
    {
        waitUntilAnswering();
        server_->stop();
        thread_.join();
    }

    StrategyRegistry registry_;
    fs::path root_;
    std::unique_ptr<RunStore> store_;
    std::unique_ptr<RunManager> manager_;
    std::unique_ptr<ApiHandler> api_;
    std::unique_ptr<HttpServer> server_;
    std::thread thread_;
    int port_{0};
};

TEST_F(HttpServerTest, ApiPathsReachTheHandler)
{
    start("");
    httplib::Client client("127.0.0.1", port_);

    auto strategies = client.Get("/api/strategies");
    ASSERT_TRUE(strategies);
    EXPECT_EQ(strategies->status, 200);
    EXPECT_EQ(json::parse(strategies->body)[0].at("name"), "buy_and_hold");

    auto posted = client.Post("/api/runs", R"({"strategy":"buy_and_hold","dataset":"x.txt"})", "application/json");
    ASSERT_TRUE(posted);
    EXPECT_EQ(posted->status, 202);
    const std::string id = json::parse(posted->body).at("id");

    std::string status;
    for (int i = 0; i < 1000 && status != "done"; ++i)
    {
        auto got = client.Get("/api/runs/" + id);
        ASSERT_TRUE(got);
        status = json::parse(got->body).at("status");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(status, "done");

    auto bad = client.Post("/api/runs", "{not json", "application/json");
    ASSERT_TRUE(bad);
    EXPECT_EQ(bad->status, 400);

    auto missing = client.Get("/api/nothing");
    ASSERT_TRUE(missing);
    EXPECT_EQ(missing->status, 404);

    auto wrongMethod = client.Delete("/api/runs");
    ASSERT_TRUE(wrongMethod);
    EXPECT_EQ(wrongMethod->status, 405);

    stop();
}

TEST_F(HttpServerTest, WebFolderIsServedAtTheRoot)
{
    start((root_ / "web").string());
    httplib::Client client("127.0.0.1", port_);

    auto index = client.Get("/");
    ASSERT_TRUE(index);
    EXPECT_EQ(index->status, 200);
    EXPECT_NE(index->body.find("hello web app"), std::string::npos);

    auto api = client.Get("/api/datasets");
    ASSERT_TRUE(api);
    EXPECT_EQ(api->status, 200);                       // the API still works next to the static files

    stop();
}

TEST_F(HttpServerTest, WithoutAWebFolderTheRootIs404)
{
    start("");
    httplib::Client client("127.0.0.1", port_);
    auto index = client.Get("/");
    ASSERT_TRUE(index);
    EXPECT_EQ(index->status, 404);
    stop();
}

TEST_F(HttpServerTest, MissingWebFolderIsRejected)
{
    EXPECT_THROW(HttpServer(*api_, (root_ / "nope").string()), std::invalid_argument);
}

TEST_F(HttpServerTest, StopMakesServeReturn)
{
    start("");
    stop();      // join() would hang if serve() did not return
    SUCCEED();
}
