#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "job.hpp"
#include "layouts.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

// A layout is a named description of a data file's columns. "auto", "simple" and "stooq" are built in; more come from a
// layouts.json file in the data folder (see layouts.hpp).
//
// Rules:
//   1. listLayouts gives auto, simple, stooq, then the names in layouts.json sorted. No file means only the built-ins.
//   2. requireLayout accepts a listed name and throws std::invalid_argument for any other (including a different case).
//   3. "simple" and "stooq" are the original layout and the Stooq layout; a custom entry is read field by field with the
//      "simple" value for every field it leaves out.
//   4. A layouts.json that is not valid JSON, not an object, has an entry that is not an object, a built-in name, an unknown
//      key, a field of the wrong type, a delimiter that is not one character or an unknown timeFormat is a
//      std::runtime_error whose text names the file (and the entry).
//   5. loadDataset(dir, name, layout) reads the file with that layout; "auto" decides from the header as before; a layout
//      that does not fit the file fails with the loader's error.
//   6. layoutsToJson lists name, builtin and description.

class LayoutsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir_ = fs::temp_directory_path() / (std::string("layouts_test_") + info->name());
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    void write(const std::string& name, const std::string& content) const { std::ofstream(dir_ / name) << content; }
    void layouts(const std::string& content) const { write("layouts.json", content); }
    std::string dir() const { return dir_.string(); }

    // Two daily bars, Stooq layout.
    void writeStooq(const std::string& name = "st.txt") const
    {
        write(name,
              "<TICKER>,<PER>,<DATE>,<TIME>,<OPEN>,<HIGH>,<LOW>,<CLOSE>,<VOL>,<OPENINT>\n"
              "X,D,20200101,000000,100,101,99,100,5,0\n"
              "X,D,20200102,000000,110,111,109,110,6,0\n");
    }
    // Two daily bars, the simple layout.
    void writeSimple(const std::string& name = "si.csv") const
    {
        write(name,
              "timestamp,open,high,low,close\n"
              "1577836800000,100,101,99,100\n"
              "1577923200000,110,111,109,110\n");
    }
    // Two bars in another exchange's style: semicolons, ISO dates, capitalised names, other column order.
    void writeOther(const std::string& name = "ot.csv") const
    {
        write(name,
              "Date;Close;Open;High;Low;Volume\n"
              "2020-01-01;100;98;101;97;7\n"
              "2020-01-02;110;108;111;107;8\n");
    }
    static const char* otherLayout()
    {
        return R"({ "other": { "description": "semicolons and ISO dates", "delimiter": ";", "timeFormat": "isoDate",
                   "time": "Date", "open": "Open", "high": "High", "low": "Low", "close": "Close", "volume": "Volume" } })";
    }

    fs::path dir_;
};

// ---- the list ----

TEST_F(LayoutsTest, WithoutAFileOnlyTheBuiltInsAreListed)
{
    EXPECT_EQ(listLayouts(dir()), (std::vector<std::string>{"auto", "simple", "stooq"}));
}

TEST_F(LayoutsTest, YourOwnNamesFollowTheBuiltInsSorted)
{
    layouts(R"({ "zeta": {}, "alpha": {}, "mid": {} })");
    EXPECT_EQ(listLayouts(dir()), (std::vector<std::string>{"auto", "simple", "stooq", "alpha", "mid", "zeta"}));
}

TEST_F(LayoutsTest, AFileWithNoEntriesIsFine)
{
    layouts("{}");
    EXPECT_EQ(listLayouts(dir()).size(), 3u);
}

TEST_F(LayoutsTest, TheFileIsReadAgainOnEveryCall)
{
    EXPECT_EQ(listLayouts(dir()).size(), 3u);
    layouts(R"({ "a": {} })");
    EXPECT_EQ(listLayouts(dir()).size(), 4u);
    layouts(R"({ "a": {}, "b": {} })");
    EXPECT_EQ(listLayouts(dir()).size(), 5u);
}

TEST_F(LayoutsTest, RequireAcceptsListedNamesOnly)
{
    layouts(R"({ "mine": {} })");
    for (const char* ok : {"auto", "simple", "stooq", "mine"})
        EXPECT_NO_THROW(requireLayout(dir(), ok)) << ok;
    for (const char* bad : {"", "Stooq", "AUTO", "nope", "mine "})
        EXPECT_THROW(requireLayout(dir(), bad), std::invalid_argument) << bad;
}

TEST_F(LayoutsTest, TheUnknownNameErrorListsTheKnownOnes)
{
    try
    {
        requireLayout(dir(), "nope");
        FAIL();
    }
    catch (const std::invalid_argument& e)
    {
        const std::string text = e.what();
        EXPECT_NE(text.find("nope"), std::string::npos) << text;
        EXPECT_NE(text.find("stooq"), std::string::npos) << text;
    }
}

TEST_F(LayoutsTest, ToJsonHasNameBuiltinAndDescription)
{
    layouts(otherLayout());
    const json doc = layoutsToJson(dir());
    ASSERT_EQ(doc.size(), 4u);
    EXPECT_EQ(doc[0].at("name"), "auto");
    EXPECT_TRUE(doc[0].at("builtin").get<bool>());
    EXPECT_FALSE(doc[0].at("description").get<std::string>().empty());
    EXPECT_EQ(doc[3].at("name"), "other");
    EXPECT_FALSE(doc[3].at("builtin").get<bool>());
    EXPECT_EQ(doc[3].at("description"), "semicolons and ISO dates");
}

TEST_F(LayoutsTest, ACustomEntryWithoutADescriptionGetsAnEmptyOne)
{
    layouts(R"({ "a": {} })");
    EXPECT_EQ(layoutsToJson(dir())[3].at("description"), "");
}

// ---- what a name means ----

TEST_F(LayoutsTest, SimpleIsTheOriginalLayoutAndStooqTheStooqOne)
{
    const CsvLayout simple = layoutByName(dir(), "simple");
    EXPECT_EQ(simple.delimiter, ',');
    EXPECT_TRUE(simple.hasHeader);
    EXPECT_EQ(simple.timeFormat, TimeFormat::Raw);
    EXPECT_EQ(simple.time, "0");

    const CsvLayout stooq = layoutByName(dir(), "stooq");
    EXPECT_EQ(stooq.timeFormat, TimeFormat::CompactDate);
    EXPECT_EQ(stooq.time, "date");
    EXPECT_EQ(stooq.timeOfDay, "time");
    EXPECT_EQ(stooq.volume, "vol");
}

TEST_F(LayoutsTest, AutoIsNotAFixedLayout)
{
    EXPECT_THROW(layoutByName(dir(), "auto"), std::invalid_argument);
    EXPECT_THROW(layoutByName(dir(), "nope"), std::invalid_argument);
}

TEST_F(LayoutsTest, ACustomEntryIsReadFieldByField)
{
    layouts(R"({ "x": { "delimiter": "\t", "hasHeader": false, "timeFormat": "epochSeconds", "time": "2", "timeOfDay": "9",
                        "open": "3", "high": "4", "low": "5", "close": "6", "volume": "7" } })");
    const CsvLayout l = layoutByName(dir(), "x");
    EXPECT_EQ(l.delimiter, '\t');
    EXPECT_FALSE(l.hasHeader);
    EXPECT_EQ(l.timeFormat, TimeFormat::EpochSeconds);
    EXPECT_EQ(l.time, "2");
    EXPECT_EQ(l.timeOfDay, "9");
    EXPECT_EQ(l.open, "3");
    EXPECT_EQ(l.high, "4");
    EXPECT_EQ(l.low, "5");
    EXPECT_EQ(l.close, "6");
    EXPECT_EQ(l.volume, "7");
}

TEST_F(LayoutsTest, FieldsLeftOutKeepTheSimpleValues)
{
    layouts(R"({ "x": { "delimiter": ";" } })");
    const CsvLayout l = layoutByName(dir(), "x");
    const CsvLayout simple = layoutByName(dir(), "simple");
    EXPECT_EQ(l.delimiter, ';');
    EXPECT_EQ(l.hasHeader, simple.hasHeader);
    EXPECT_EQ(l.timeFormat, simple.timeFormat);
    EXPECT_EQ(l.time, simple.time);
    EXPECT_EQ(l.open, simple.open);
    EXPECT_EQ(l.close, simple.close);
    EXPECT_EQ(l.volume, simple.volume);
}

TEST_F(LayoutsTest, EveryTimeFormatNameIsUnderstood)
{
    layouts(R"({ "a": {"timeFormat":"raw"}, "b": {"timeFormat":"epochSeconds"}, "c": {"timeFormat":"compactDate"},
                 "d": {"timeFormat":"isoDate"} })");
    EXPECT_EQ(layoutByName(dir(), "a").timeFormat, TimeFormat::Raw);
    EXPECT_EQ(layoutByName(dir(), "b").timeFormat, TimeFormat::EpochSeconds);
    EXPECT_EQ(layoutByName(dir(), "c").timeFormat, TimeFormat::CompactDate);
    EXPECT_EQ(layoutByName(dir(), "d").timeFormat, TimeFormat::IsoDate);
}

// ---- a bad file ----

// The fixture's helpers are protected; this subclass opens them to the free function below.
class LayoutsFileTest : public LayoutsTest
{
public:
    using LayoutsTest::dir;
    using LayoutsTest::write;
};

static void expectBad(const LayoutsFileTest& t, const std::string& content, const std::string& mention)
{
    t.write("layouts.json", content);
    try
    {
        listLayouts(t.dir());
        ADD_FAILURE() << "accepted: " << content;
    }
    catch (const std::runtime_error& e)
    {
        const std::string text = e.what();
        EXPECT_NE(text.find("layouts.json"), std::string::npos) << text;
        EXPECT_NE(text.find(mention), std::string::npos) << text << " (wanted: " << mention << ")";
    }
    catch (...)
    {
        ADD_FAILURE() << "wrong exception type for: " << content;
    }
}


TEST_F(LayoutsFileTest, ABadFileIsARuntimeErrorNamingTheFileAndTheEntry)
{
    expectBad(*this, "{not json", "JSON");
    expectBad(*this, "[]", "object");
    expectBad(*this, R"({ "a": 5 })", "\"a\"");
    expectBad(*this, R"({ "auto": {} })", "built in");
    expectBad(*this, R"({ "simple": {} })", "built in");
    expectBad(*this, R"({ "stooq": {} })", "built in");
    expectBad(*this, R"({ "a": { "colour": "red" } })", "colour");
    expectBad(*this, R"({ "a": { "open": 3 } })", "open");
    expectBad(*this, R"({ "a": { "hasHeader": "yes" } })", "hasHeader");
    expectBad(*this, R"({ "a": { "description": 1 } })", "description");
    expectBad(*this, R"({ "a": { "delimiter": "" } })", "delimiter");
    expectBad(*this, R"({ "a": { "delimiter": ";;" } })", "delimiter");
    expectBad(*this, R"({ "a": { "timeFormat": "banana" } })", "timeFormat");
    expectBad(*this, R"({ "": {} })", "name");
}

TEST_F(LayoutsFileTest, ABadFileAlsoStopsRequireAndLoad)
{
    write("layouts.json", "{not json");
    EXPECT_THROW(requireLayout(dir(), "stooq"), std::runtime_error);
    write("st.txt", "<DATE>,<OPEN>,<HIGH>,<LOW>,<CLOSE>\n20200101,1,1,1,1\n");
    EXPECT_THROW(loadDataset(dir(), "st.txt", "stooq"), std::runtime_error);
}

// ---- loading with a layout ----

TEST_F(LayoutsTest, LoadingWithTheMatchingBuiltInLayoutWorks)
{
    writeStooq();
    writeSimple();
    const BarSeries a = loadDataset(dir(), "st.txt", "stooq");
    ASSERT_EQ(a.bars.size(), 2u);
    EXPECT_EQ(a.bars[0].timestamp, 1577836800000);
    EXPECT_DOUBLE_EQ(a.bars[1].close, 110.0);
    EXPECT_DOUBLE_EQ(a.bars[0].volume, 5.0);

    const BarSeries b = loadDataset(dir(), "si.csv", "simple");
    ASSERT_EQ(b.bars.size(), 2u);
    EXPECT_EQ(b.bars[1].timestamp, 1577923200000);
}

TEST_F(LayoutsTest, AutoGivesTheSameBarsAsTheRightFixedLayout)
{
    writeStooq();
    writeSimple();
    EXPECT_EQ(loadDataset(dir(), "st.txt", "auto").bars.size(), 2u);
    EXPECT_EQ(loadDataset(dir(), "st.txt", "auto").bars[1].timestamp, loadDataset(dir(), "st.txt", "stooq").bars[1].timestamp);
    EXPECT_EQ(loadDataset(dir(), "si.csv", "auto").bars[1].timestamp, loadDataset(dir(), "si.csv", "simple").bars[1].timestamp);
    EXPECT_EQ(loadDataset(dir(), "st.txt").bars.size(), 2u);                 // the layout argument is optional: auto
    EXPECT_EQ(loadDataset(dir(), "si.csv").bars.size(), 2u);
}

TEST_F(LayoutsTest, ALayoutThatDoesNotFitTheFileFailsWithAReadableError)
{
    writeStooq();
    writeSimple();
    EXPECT_THROW(loadDataset(dir(), "st.txt", "simple"), std::exception);    // the ticker is not a timestamp
    EXPECT_THROW(loadDataset(dir(), "si.csv", "stooq"), std::runtime_error); // no <DATE> column
}

TEST_F(LayoutsTest, ACustomLayoutReadsAnotherFormat)
{
    layouts(otherLayout());
    writeOther();
    const BarSeries s = loadDataset(dir(), "ot.csv", "other");
    ASSERT_EQ(s.bars.size(), 2u);
    EXPECT_EQ(s.bars[0].timestamp, 1577836800000);
    EXPECT_DOUBLE_EQ(s.bars[0].open, 98.0);          // not the second column: columns are found by name
    EXPECT_DOUBLE_EQ(s.bars[0].high, 101.0);
    EXPECT_DOUBLE_EQ(s.bars[0].low, 97.0);
    EXPECT_DOUBLE_EQ(s.bars[0].close, 100.0);
    EXPECT_DOUBLE_EQ(s.bars[1].volume, 8.0);
    EXPECT_EQ(s.symbol, "ot.csv");
}

TEST_F(LayoutsTest, AnUnknownLayoutNameWhenLoadingIsInvalidArgument)
{
    writeSimple();
    EXPECT_THROW(loadDataset(dir(), "si.csv", "nope"), std::invalid_argument);
}

TEST_F(LayoutsTest, AutoStillGuessesWrongForAnotherFormatSoALayoutIsNeeded)
{
    layouts(otherLayout());
    writeOther();
    EXPECT_THROW(loadDataset(dir(), "ot.csv", "auto"), std::exception);      // why the choice exists
    EXPECT_NO_THROW(loadDataset(dir(), "ot.csv", "other"));
}

TEST_F(LayoutsTest, LooksLikeStooqChecksTheHeaderForDate)
{
    writeStooq();
    writeSimple();
    writeOther();
    EXPECT_TRUE(looksLikeStooq((dir_ / "st.txt").string()));
    EXPECT_FALSE(looksLikeStooq((dir_ / "si.csv").string()));
    EXPECT_FALSE(looksLikeStooq((dir_ / "missing.csv").string()));
}
