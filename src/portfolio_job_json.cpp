#include "portfolio_job_json.hpp"

#include <stdexcept>

#include "job_json.hpp"

using nlohmann::json;

namespace
{
    const json& need(const json& object, const char* key)
    {
        const auto it = object.find(key);
        if (it == object.end())
            throw std::invalid_argument(std::string("missing \"") + key + "\"");
        return *it;
    }

    std::string text(const json& object, const char* key)
    {
        const json& v = need(object, key);
        if (!v.is_string())
            throw std::invalid_argument(std::string("\"") + key + "\" must be text");
        return v.get<std::string>();
    }

    void onlyKeys(const json& object, std::initializer_list<const char*> allowed, const std::string& where)
    {
        for (const auto& item : object.items())
        {
            bool known = false;
            for (const char* key : allowed)
                known = known || item.key() == key;
            if (!known)
                throw std::invalid_argument("unknown key in " + where + ": " + item.key());
        }
    }

    json paramsJson(const NamedParams& params)
    {
        json j = json::object();
        for (const auto& [name, value] : params)
            j[name] = value;
        return j;
    }

    json curveJson(const std::vector<double>& curve)
    {
        return json(curve);
    }
}

PortfolioSpec portfolioSpecFromJson(const json& request)
{
    if (!request.is_object())
        throw std::invalid_argument("a request must be a JSON object");
    onlyKeys(request, {"sleeves", "commonPeriodOnly", "periodsPerYear"}, "request");

    PortfolioSpec spec;
    if (const auto it = request.find("commonPeriodOnly"); it != request.end())
    {
        if (!it->is_boolean())
            throw std::invalid_argument("\"commonPeriodOnly\" must be true or false");
        spec.commonPeriodOnly = it->get<bool>();
    }
    if (const auto it = request.find("periodsPerYear"); it != request.end())
    {
        if (!it->is_number())
            throw std::invalid_argument("\"periodsPerYear\" must be a number");
        spec.periodsPerYear = it->get<double>();
    }

    const json& sleeves = need(request, "sleeves");
    if (!sleeves.is_array())
        throw std::invalid_argument("\"sleeves\" must be a list");
    for (const json& s : sleeves)
    {
        if (!s.is_object())
            throw std::invalid_argument("each sleeve must be an object");
        onlyKeys(s, {"name", "dataset", "strategy", "params", "cash", "layout"}, "a sleeve");

        PortfolioSleeve sleeve;
        sleeve.name = text(s, "name");
        sleeve.dataset = text(s, "dataset");
        sleeve.strategy = text(s, "strategy");
        if (s.contains("layout"))
            sleeve.layout = text(s, "layout");
        const json& cash = need(s, "cash");
        if (!cash.is_number())
            throw std::invalid_argument("\"cash\" must be a number");
        sleeve.cash = cash.get<double>();
        if (const auto it = s.find("params"); it != s.end())
        {
            if (!it->is_object())
                throw std::invalid_argument("\"params\" must be an object");
            for (const auto& item : it->items())
            {
                if (!item.value().is_number())
                    throw std::invalid_argument("parameter " + item.key() + " must be a number");
                sleeve.params[item.key()] = item.value().get<double>();
            }
        }
        spec.sleeves.push_back(std::move(sleeve));
    }
    return spec;
}

json toJson(const PortfolioSpec& spec)
{
    json sleeves = json::array();
    for (const PortfolioSleeve& s : spec.sleeves)
        sleeves.push_back({{"name", s.name}, {"dataset", s.dataset}, {"strategy", s.strategy},
                           {"params", paramsJson(s.params)}, {"cash", s.cash}, {"layout", s.layout}});
    return {{"sleeves", sleeves}, {"commonPeriodOnly", spec.commonPeriodOnly}, {"periodsPerYear", spec.periodsPerYear}};
}

json toJson(const PortfolioOutput& out)
{
    json j;
    j["kind"] = "portfolio";
    j["spec"] = toJson(out.spec);
    j["bars"] = out.bars;
    j["totalCash"] = out.totalCash;
    j["timestamps"] = out.timestamps;
    j["combined"] = {{"cash", out.totalCash}, {"finalEquity", out.result.combined.finalEquity},
                     {"equity", curveJson(out.result.combined.equityCurve)}, {"metrics", toJson(out.metrics.combined)}};

    j["sleeves"] = json::array();
    for (std::size_t k = 0; k < out.result.sleeves.size(); ++k)
    {
        const SleeveRun& run = out.result.sleeves[k];
        const PortfolioSleeve& spec = out.spec.sleeves[k];
        j["sleeves"].push_back({{"name", run.name}, {"dataset", spec.dataset}, {"strategy", spec.strategy},
                                {"params", paramsJson(spec.params)}, {"cash", run.cash},
                                {"finalEquity", run.result.finalEquity}, {"equity", curveJson(run.result.equityCurve)},
                                {"metrics", toJson(out.metrics.sleeves[k].metrics)}});
    }

    j["trades"] = json::array();
    for (std::size_t i = 0; i < out.result.combined.closedTrades.size(); ++i)
    {
        json t = toJson(out.result.combined.closedTrades[i]);
        t["sleeve"] = out.result.closedTradeSleeve[i];
        j["trades"].push_back(t);
    }
    j["openTrades"] = json::array();
    for (std::size_t i = 0; i < out.result.combined.openTrades.size(); ++i)
    {
        json t = toJson(out.result.combined.openTrades[i]);
        t["sleeve"] = out.result.openTradeSleeve[i];
        j["openTrades"].push_back(t);
    }
    return j;
}

std::string runPortfolioJson(const StrategyRegistry& registry, const std::string& dataDir, const std::string& requestText)
{
    const json request = json::parse(requestText, nullptr, false);
    if (request.is_discarded())
        throw std::invalid_argument("the request is not valid JSON");
    return toJson(runPortfolio(registry, dataDir, portfolioSpecFromJson(request))).dump();
}
