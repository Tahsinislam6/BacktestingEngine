#pragma once

#include <string>

#include "job.hpp"
#include "job_json.hpp"
#include "registry.hpp"
#include "run_manager.hpp"
#include "run_store.hpp"

struct HttpResponse
{
    int status{200};
    std::string body;
    std::string contentType{"application/json"};
};

// The whole HTTP API as one function, so it can be tested without sockets:
//
//   GET  /api/strategies    200  the strategies with their parameter schemas
//   GET  /api/datasets      200  ["file.txt", ...]
//   POST /api/runs          202  {"id": "000001"}      the body is a run request (see job_json.hpp)
//   POST /api/sweeps        202  {"id": "000002"}      a density sweep request (see sweep_job_json.hpp); the note is required
//   POST /api/portfolios    202  {"id": "000003"}      a portfolio request (see portfolio_job_json.hpp)
//   GET  /api/runs/{id}     200  the run record: id, status, createdAt, spec, and result or error
//   GET  /api/runs          200  one summary per run or sweep, newest first (no equity curves, trades or grids)
//
// Every error is {"error": "text"}: 400 bad request, 404 unknown path or run, 405 wrong method, 500 anything unexpected.
// The registry, store and manager must outlive the handler.
class ApiHandler
{
    public:
        ApiHandler(const StrategyRegistry& registry, std::string dataDir, RunStore& store, RunManager& manager);

        // Never throws: every failure becomes an error response.
        HttpResponse handle(const std::string& method, const std::string& path, const std::string& body) const;

    private:
        HttpResponse route(const std::string& method, const std::string& path, const std::string& body) const;

        const StrategyRegistry& registry_;
        std::string dataDir_;
        RunStore& store_;
        RunManager& manager_;
};
