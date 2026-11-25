#include "http_server.hpp"

#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include <httplib.h>

struct HttpServer::Impl
{
    httplib::Server server;
};

HttpServer::HttpServer(const ApiHandler& api, const std::string& webDir) : impl_(std::make_unique<Impl>())
{
    httplib::Server& server = impl_->server;
    server.set_payload_max_length(1024 * 1024);     // a request is a few hundred bytes; refuse anything big

    if (!webDir.empty())
    {
        if (!std::filesystem::is_directory(webDir) || !server.set_mount_point("/", webDir))
            throw std::invalid_argument("web folder not found: " + webDir);
    }

    auto forward = [&api](const httplib::Request& req, httplib::Response& res)
    {
        const HttpResponse out = api.handle(req.method, req.path, req.body);
        res.status = out.status;
        res.set_header("Cache-Control", "no-store");
        res.set_content(out.body, out.contentType);
    };
    const std::string anyApiPath = R"(/api/.*)";
    server.Get(anyApiPath, forward);
    server.Post(anyApiPath, forward);
    server.Put(anyApiPath, forward);
    server.Patch(anyApiPath, forward);
    server.Delete(anyApiPath, forward);
}

HttpServer::~HttpServer() = default;

int HttpServer::bind(const std::string& host, int port)
{
    if (port == 0)
        return impl_->server.bind_to_any_port(host);
    return impl_->server.bind_to_port(host, port) ? port : -1;
}

void HttpServer::serve()
{
    impl_->server.listen_after_bind();
}

void HttpServer::stop()
{
    impl_->server.stop();
}

namespace
{
    HttpServer* g_running = nullptr;

    void onInterrupt(int)
    {
        if (g_running)
            g_running->stop();      // httplib's stop() only clears a flag and closes the listening socket
    }
}

int runServer(const StrategyRegistry& registry, const ServerOptions& options)
{
    try
    {
        listDatasets(options.dataDir);      // fail early and clearly if the data folder is wrong

        RunStore store(options.runsDir);
        RunManager manager(store, registry, options.dataDir);
        ApiHandler api(registry, options.dataDir, store, manager);
        HttpServer server(api, options.webDir);

        const int port = server.bind(options.host, options.port);
        if (port < 0)
        {
            std::cerr << "error: cannot listen on " << options.host << ":" << options.port << "\n";
            return 1;
        }

        g_running = &server;
        std::signal(SIGINT, onInterrupt);
        std::signal(SIGTERM, onInterrupt);

        std::cout << "listening on http://" << options.host << ":" << port << "  (Ctrl-C to stop)\n"
                  << "data: " << options.dataDir << "   runs: " << options.runsDir << "\n";
        if (!options.webDir.empty())
            std::cout << "web app: " << options.webDir << "\n";
        server.serve();
        g_running = nullptr;
        std::cout << "stopped\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        g_running = nullptr;
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
