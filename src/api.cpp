#include "api.hpp"

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
        json j = {{"id", r.id}, {"status", r.status}, {"createdAt", r.createdAt}, {"spec", r.spec}};
        if (!r.result.is_null())
            j["result"] = r.result;
        if (!r.error.empty())
            j["error"] = r.error;
        return j;
    }

    // The record without the equity curve, timestamps and trades: only the headline metrics.
    json summaryJson(const RunRecord& r)
    {
        json j = {{"id", r.id}, {"status", r.status}, {"createdAt", r.createdAt}, {"spec", r.spec}};
        if (r.result.is_object() && r.result.contains("metrics"))
            j["metrics"] = r.result.at("metrics");
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
        const std::string id = path.substr(runsPrefix.size());
        if (!RunStore::isValidId(id))
            return error(404, "no such run");
        if (method != "GET")
            return error(405, "use GET");
        const auto record = store_.load(id);
        if (!record)
            return error(404, "no such run");
        return reply(200, recordJson(*record));
    }

    return error(404, "no such path");
}
