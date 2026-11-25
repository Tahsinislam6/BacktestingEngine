#include <cstdlib>
#include <iostream>
#include <string>

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
    if (argc < 3)
    {
        std::cerr << "Usage: server <data folder> <runs folder> [--web <folder>] [--port <n>]\n";
        return 2;
    }
    options.dataDir = argv[1];
    options.runsDir = argv[2];
    for (int i = 3; i < argc; ++i)
    {
        const std::string flag = argv[i];
        if (flag == "--web" && i + 1 < argc)
            options.webDir = argv[++i];
        else if (flag == "--port" && i + 1 < argc)
            options.port = std::atoi(argv[++i]);
        else
        {
            std::cerr << "unknown or incomplete option: " << flag << "\n";
            return 2;
        }
    }

    StrategyRegistry registry;
    registerBuiltins(registry);
    return runServer(registry, options);
}
