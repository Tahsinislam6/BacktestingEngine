#include "api.hpp"
#include "portfolio_job_json.hpp"
#include "sweep_job_json.hpp"

#include <exception>
#include <stdexcept>

using nlohmann::json;

namespace
{
    HttpResponse reply(int status, const json& doc)
    {
        HttpResponse r;
        r.status = status;
        r.body = doc.dump();
        return r;
    }

    HttpResponse error(int status, const std::string& text)
    {
        return reply(status, json{{"error", text}});
    }

    json recordJson(const RunRecord& r)
    {
        json j = {{"id", r.id}, {"status", r.status}, {"createdAt", r.createdAt}, {"spec", r.spec},
                  {"kind", r.kind}, {"ideas", r.ideas}};
        if (!r.result.is_null())
            j["result"] = r.result;
        if (!r.progress.is_null())
            j["progress"] = r.progress;
        if (!r.error.empty())
            j["error"] = r.error;
        return j;
    }

    // The numbers a sweep is judged by, without its grid or its shuffled values.
    json headlineOf(const json& result)
    {
        json h;
        const json& real = result.at("real");
        const json& plateau = result.at("shuffled").at("plateauDensity");
        const json& pass = result.at("shuffled").at("passDensity");
        h["cells"] = result.at("cells");
        h["validCells"] = real.at("validCells");
        h["passDensity"] = real.at("passDensity");
        h["plateauDensity"] = real.at("plateauDensity");
        h["peakOverNeighbours"] = real.at("peakOverNeighbours");
        h["shuffledRuns"] = result.at("shuffled").at("runs");
        h["shuffledPassMedian"] = pass.at("median");
        h["shuffledPass95"] = pass.at("p95");
        h["shuffledPassAtLeastReal"] = pass.at("atLeastReal");
        h["shuffledPlateauMedian"] = plateau.at("median");
        h["shuffledPlateau95"] = plateau.at("p95");
        h["shuffledPlateauAtLeastReal"] = plateau.at("atLeastReal");
        return h;
    }

    // The record without the equity curve, timestamps and trades: only the headline metrics.
    // The numbers a portfolio is read by, without its curves or trades.
    json portfolioHeadlineOf(const json& result)
    {
        const json& m = result.at("combined").at("metrics");
        json h;
        h["sleeves"] = result.at("sleeves").size();
        h["cash"] = result.at("combined").at("cash");
        h["finalEquity"] = result.at("combined").at("finalEquity");
        for (const char* key : {"totalReturn", "cagr", "maxDrawdown", "mar", "sharpe", "trades"})
            h[key] = m.at(key);
        return h;
    }

    json summaryJson(const RunRecord& r)
    {
        json j = {{"id", r.id}, {"status", r.status}, {"createdAt", r.createdAt}, {"spec", r.spec},
                  {"kind", r.kind}, {"ideas", r.ideas}};
        if (r.result.is_object() && r.result.contains("metrics"))
            j["metrics"] = r.result.at("metrics");
        if (r.kind == "sweep" && r.result.is_object() && r.result.contains("real"))
            j["headline"] = headlineOf(r.result);
        if (r.kind == "portfolio" && r.result.is_object() && r.result.contains("combined"))
            j["headline"] = portfolioHeadlineOf(r.result);
        if (!r.progress.is_null() && r.status != RunStatus::Done)
            j["progress"] = r.progress;
        if (!r.error.empty())
            j["error"] = r.error;
        return j;
    }
}

ApiHandler::ApiHandler(const StrategyRegistry& registry, std::string dataDir, RunStore& store, RunManager& manager)
    : registry_(registry), dataDir_(std::move(dataDir)), store_(store), manager_(manager)
{
}

HttpResponse ApiHandler::handle(const std::string& method, const std::string& path, const std::string& body) const
{
    try
    {
        return route(method, path, body);
    }
    catch (const std::invalid_argument& e)
    {
        return error(400, e.what());
    }
    catch (const std::exception& e)
    {
        return error(500, e.what());
    }
}

HttpResponse ApiHandler::route(const std::string& method, const std::string& path, const std::string& body) const
{
    if (path == "/api/strategies")
        return method == "GET" ? reply(200, strategiesToJson(registry_)) : error(405, "use GET");

    if (path == "/api/datasets")
        return method == "GET" ? reply(200, listDatasets(dataDir_)) : error(405, "use GET");

    if (path == "/api/sweeps")
    {
        if (method != "POST")
            return error(405, "use POST");
        const json request = json::parse(body, nullptr, false);
        if (request.is_discarded())
            throw std::invalid_argument("the request is not valid JSON");
        return reply(202, json{{"id", manager_.submitSweep(sweepSpecFromJson(request))}});
    }

    if (path == "/api/portfolios")
    {
        if (method != "POST")
            return error(405, "use POST");
        const json request = json::parse(body, nullptr, false);
        if (request.is_discarded())
            throw std::invalid_argument("the request is not valid JSON");
        return reply(202, json{{"id", manager_.submitPortfolio(portfolioSpecFromJson(request))}});
    }

    if (path == "/api/runs")
    {
        if (method == "POST")
        {
            const json request = json::parse(body, nullptr, false);
            if (request.is_discarded())
                throw std::invalid_argument("the request is not valid JSON");
            const std::string id = manager_.submit(runSpecFromJson(request));
            return reply(202, json{{"id", id}});
        }
        if (method == "GET")
        {
            json list = json::array();
            for (const RunRecord& r : store_.list())
                list.push_back(summaryJson(r));
            return reply(200, list);
        }
        return error(405, "use GET or POST");
    }

    const std::string runsPrefix = "/api/runs/";
    if (path.rfind(runsPrefix, 0) == 0)
    {
        std::string id = path.substr(runsPrefix.size());
        const std::string cancelSuffix = "/cancel";
        const bool isCancel = id.size() > cancelSuffix.size() &&
                              id.compare(id.size() - cancelSuffix.size(), cancelSuffix.size(), cancelSuffix) == 0;
        if (isCancel)
            id.erase(id.size() - cancelSuffix.size());
        if (!RunStore::isValidId(id))
            return error(404, "no such run");
        if (isCancel)
        {
            if (method != "POST")
                return error(405, "use POST");
            switch (manager_.cancel(id))
            {
                case CancelResult::Cancelled:
                    return reply(200, json{{"id", id}, {"status", RunStatus::Cancelled}});
                case CancelResult::Stopping:
                    return reply(202, json{{"id", id}, {"status", "stopping"}});
                case CancelResult::NotCancellable:
                    return error(409, "this run cannot be cancelled: it has finished, or it is a single run or portfolio already running");
                case CancelResult::NotFound:
                    break;
            }
            return error(404, "no such run");
        }
        if (method != "GET")
            return error(405, "use GET");
        const auto record = store_.load(id);
        if (!record)
            return error(404, "no such run");
        return reply(200, recordJson(*record));
    }

    return error(404, "no such path");
}
