#pragma once

#include <memory>
#include <string>
#include <vector>

#include "api.hpp"

// The socket layer: it hands every /api/ request to the ApiHandler and, if a web folder is given, serves the files in it
// at the root (the built web app). It holds no rules of its own.
class HttpServer
{
    public:
        // webDir may be empty (no static files). Throws std::invalid_argument if it is given but is not a folder.
        // The handler must outlive the server.
        HttpServer(const ApiHandler& api, const std::string& webDir);
        ~HttpServer();

        // Starts listening on host:port (port 0 = any free port). Returns the port, or -1 if it could not bind.
        // Use host "127.0.0.1" so only this computer can connect.
        int bind(const std::string& host, int port);

        // Answers requests until stop() is called. Call bind() first.
        void serve();

        // Makes serve() return. Safe to call from another thread or a signal-handling thread.
        void stop();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
};

struct ServerOptions
{
    std::string dataDir;
    std::string runsDir;
    std::string webDir;                 // empty: API only
    std::string host{"127.0.0.1"};
    int port{8080};
};

// Reads the command line of a server program (without the program name):
//   <data folder> <runs folder> [--web <folder>] [--port <n>]
// into `options` and returns an empty string, or returns the message to print (and leaves `options` as it was). The port
// must be a whole number from 1 to 65535. Shared by the engine's server and by private projects' servers.
std::string parseServerArgs(const std::vector<std::string>& args, ServerOptions& options);

// Builds the store, the manager, the handler and the server, prints the address and serves until Ctrl-C. Returns the exit
// code. A private project calls this with a registry that has its own strategies added.
int runServer(const StrategyRegistry& registry, const ServerOptions& options);
