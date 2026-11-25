#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "run_store.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

// The run store keeps one JSON file per run in a folder: the log of everything that was run, which also shows how many
// ideas were tried.
//
// Rules:
//   1. The folder is created if it is missing.
//   2. nextId() gives "000001", "000002", ...; a store opened on a folder that already has runs continues after the
//      highest id, so an id is never used twice.
//   3. save() then load() gives the same record back; saving again with the same id replaces it. No temp file is left.
//   4. load() of an id that does not exist gives nothing; an id that is not 1 to 12 digits is std::invalid_argument
//      (an id becomes part of a file name, so it must never contain a path).
//   5. list() gives the newest run first and skips files that are not runs or are damaged.

class RunStoreTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("run_store_test_") + info->name());
        fs::remove_all(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    static RunRecord record(const std::string& id, const std::string& status = RunStatus::Done)
    {
        RunRecord r;
        r.id = id;
        r.status = status;
        r.createdAt = "2026-11-25T10:00:00Z";
        r.spec = {{"strategy", "buy_and_hold"}, {"dataset", "x.txt"}};
        return r;
    }

    fs::path dir_;
};

TEST_F(RunStoreTest, CreatesTheFolder)
{
    RunStore store((dir_ / "a" / "b").string());
    EXPECT_TRUE(fs::is_directory(dir_ / "a" / "b"));
}

TEST_F(RunStoreTest, NextIdCountsUpFromOne)
{
    RunStore store(dir_.string());
    EXPECT_EQ(store.nextId(), "000001");
    EXPECT_EQ(store.nextId(), "000002");
}

TEST_F(RunStoreTest, NextIdContinuesAfterTheHighestExistingRun)
{
    {
        RunStore store(dir_.string());
        store.save(record("000001"));
        store.save(record("000007"));
    }
    RunStore again(dir_.string());
    EXPECT_EQ(again.nextId(), "000008");
}

TEST_F(RunStoreTest, SaveThenLoadGivesTheSameRecord)
{
    RunStore store(dir_.string());
    RunRecord r = record("000001");
    r.result = {{"equity", {1, 2, 3}}};
    r.error = "";
    store.save(r);

    const auto loaded = store.load("000001");

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->id, "000001");
    EXPECT_EQ(loaded->status, "done");
    EXPECT_EQ(loaded->createdAt, "2026-11-25T10:00:00Z");
    EXPECT_EQ(loaded->spec, r.spec);
    EXPECT_EQ(loaded->result, r.result);
    EXPECT_EQ(loaded->error, "");
}

TEST_F(RunStoreTest, SavingAgainReplacesTheRecordAndLeavesNoTempFile)
{
    RunStore store(dir_.string());
    store.save(record("000001", RunStatus::Queued));
    store.save(record("000001", RunStatus::Done));

    EXPECT_EQ(store.load("000001")->status, "done");
    int files = 0;
    for (const auto& e : fs::directory_iterator(dir_))
    {
        ++files;
        EXPECT_EQ(e.path().extension(), ".json") << e.path();
    }
    EXPECT_EQ(files, 1);
}

TEST_F(RunStoreTest, ErrorTextIsKept)
{
    RunStore store(dir_.string());
    RunRecord r = record("000001", RunStatus::Failed);
    r.error = "dataset has no bars";
    store.save(r);
    EXPECT_EQ(store.load("000001")->error, "dataset has no bars");
}

TEST_F(RunStoreTest, LoadOfAMissingRunGivesNothing)
{
    RunStore store(dir_.string());
    EXPECT_FALSE(store.load("000042").has_value());
}

TEST_F(RunStoreTest, BadIdsAreRejected)
{
    RunStore store(dir_.string());
    for (const char* bad : {"", "abc", "../000001", "00 01", "1234567890123", "000001.json", "a/b"})
    {
        EXPECT_FALSE(RunStore::isValidId(bad)) << bad;
        EXPECT_THROW(store.load(bad), std::invalid_argument) << bad;
        EXPECT_THROW(store.save(record(bad)), std::invalid_argument) << bad;
    }
    EXPECT_TRUE(RunStore::isValidId("000001"));
    EXPECT_TRUE(RunStore::isValidId("1"));
}

TEST_F(RunStoreTest, ListGivesNewestFirst)
{
    RunStore store(dir_.string());
    store.save(record("000002"));
    store.save(record("000010"));
    store.save(record("000001"));

    const auto all = store.list();

    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].id, "000010");
    EXPECT_EQ(all[1].id, "000002");
    EXPECT_EQ(all[2].id, "000001");
}

TEST_F(RunStoreTest, ListSkipsOtherFilesAndDamagedRuns)
{
    RunStore store(dir_.string());
    store.save(record("000001"));
    std::ofstream(dir_ / "notes.json") << "{}";
    std::ofstream(dir_ / "readme.txt") << "hello";
    std::ofstream(dir_ / "000002.json") << "{not json";

    const auto all = store.list();

    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].id, "000001");
}

TEST_F(RunStoreTest, LoadOfADamagedRunThrows)
{
    RunStore store(dir_.string());
    std::ofstream(dir_ / "000001.json") << "{not json";
    EXPECT_THROW(store.load("000001"), std::runtime_error);
}
