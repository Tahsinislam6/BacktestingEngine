#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "http_server.hpp"

// parseServerArgs reads the command line of a server program (without the program name):
//   <data folder> <runs folder> [--web <folder>] [--port <n>]
// and returns an empty string on success, otherwise the message to print. It is shared by the engine's own server and by
// private projects' servers, so every one of them takes the same options.
//
// Rules:
//   1. Exactly two positional arguments: the data folder, then the runs folder. Defaults: no web folder, host 127.0.0.1,
//      port 8080.
//   2. --web and --port may come in any order, each followed by its value.
//   3. Anything else is an error: too few or too many folders, an unknown option, an option with no value, a port that is
//      not a whole number from 1 to 65535.
//   4. On an error the options are left as they were passed in.

namespace
{
std::string parse(const std::vector<std::string>& args, ServerOptions& out)
{
    return parseServerArgs(args, out);
}
}

TEST(ServerArgs, TwoFoldersGiveTheDefaults)
{
    ServerOptions o;
    EXPECT_EQ(parse({"../data", "runs"}, o), "");
    EXPECT_EQ(o.dataDir, "../data");
    EXPECT_EQ(o.runsDir, "runs");
    EXPECT_EQ(o.webDir, "");
    EXPECT_EQ(o.host, "127.0.0.1");
    EXPECT_EQ(o.port, 8080);
}

TEST(ServerArgs, WebAndPortInAnyOrder)
{
    ServerOptions a, b;
    EXPECT_EQ(parse({"d", "r", "--web", "w", "--port", "9000"}, a), "");
    EXPECT_EQ(parse({"d", "r", "--port", "9000", "--web", "w"}, b), "");
    EXPECT_EQ(a.webDir, "w");
    EXPECT_EQ(a.port, 9000);
    EXPECT_EQ(b.webDir, "w");
    EXPECT_EQ(b.port, 9000);
}

TEST(ServerArgs, OptionsMayComeBeforeTheFolders)
{
    ServerOptions o;
    EXPECT_EQ(parse({"--port", "9000", "d", "r"}, o), "");
    EXPECT_EQ(o.dataDir, "d");
    EXPECT_EQ(o.runsDir, "r");
    EXPECT_EQ(o.port, 9000);
}

TEST(ServerArgs, WrongNumberOfFoldersIsAnErrorThatShowsTheUsage)
{
    ServerOptions o;
    for (const std::vector<std::string>& args : std::vector<std::vector<std::string>>{
             {}, {"d"}, {"d", "r", "extra"}, {"--port", "9000"}})
    {
        const std::string error = parse(args, o);
        EXPECT_NE(error, "");
        EXPECT_NE(error.find("Usage"), std::string::npos) << error;
    }
}

TEST(ServerArgs, UnknownOrIncompleteOptionsAreErrors)
{
    ServerOptions o;
    EXPECT_NE(parse({"d", "r", "--wat"}, o), "");
    EXPECT_NE(parse({"d", "r", "--web"}, o), "");
    EXPECT_NE(parse({"d", "r", "--port"}, o), "");
}

TEST(ServerArgs, BadPortsAreErrors)
{
    ServerOptions o;
    for (const char* port : {"abc", "", "0", "-1", "65536", "80.5", "9000x", " 9000"})
        EXPECT_NE(parse({"d", "r", "--port", port}, o), "") << port;
    EXPECT_EQ(parse({"d", "r", "--port", "1"}, o), "");
    EXPECT_EQ(parse({"d", "r", "--port", "65535"}, o), "");
}

TEST(ServerArgs, AnErrorLeavesTheOptionsUntouched)
{
    ServerOptions o;
    o.dataDir = "keep";
    o.runsDir = "keep";
    o.port = 1234;
    EXPECT_NE(parse({"d", "r", "--port", "nope"}, o), "");
    EXPECT_EQ(o.dataDir, "keep");
    EXPECT_EQ(o.runsDir, "keep");
    EXPECT_EQ(o.port, 1234);
}

TEST(ServerArgs, AnErrorAfterSomethingWasReadStillLeavesTheOptionsUntouched)
{
    ServerOptions o;
    o.webDir = "keep";
    o.port = 1234;
    EXPECT_NE(parse({"d", "r", "--web", "changed", "--port", "nope"}, o), "");      // the port is read after the web folder
    EXPECT_EQ(o.webDir, "keep");
    EXPECT_NE(parse({"--web", "changed", "--port", "9000", "only-one-folder"}, o), "");   // too few folders is found last
    EXPECT_EQ(o.webDir, "keep");
    EXPECT_EQ(o.port, 1234);
}
