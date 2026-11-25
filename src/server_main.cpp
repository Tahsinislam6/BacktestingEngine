#include <iostream>
#include <string>
#include <vector>

#include "buy_and_hold.hpp"
#include "http_server.hpp"

// The local web server: the API (and, with --web, the built web app).
//
//   server <data folder> <runs folder> [--web <folder>] [--port <n>]
//
// It listens on 127.0.0.1 only, so only this computer can reach it. Every run is saved as a JSON file in the runs folder.
// This program knows only the built-in strategies; a private project makes its own main() that adds its strategies to the
// registry and calls runServer the same way.
int main(int argc, char** argv)
{
    ServerOptions options;
    const std::string error = parseServerArgs(std::vector<std::string>(argv + 1, argv + argc), options);
    if (!error.empty())
    {
        std::cerr << error << "\n";
        return 2;
    }

    StrategyRegistry registry;
    registerBuiltins(registry);
    return runServer(registry, options);
}
