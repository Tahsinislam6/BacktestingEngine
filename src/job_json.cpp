#include "job_json.hpp"

#include <stdexcept>

using nlohmann::json;

namespace
{
    const char* directionName(Direction d) { return d == Direction::Long ? "long" : "short"; }

    const char* reasonName(ExitReason r)
    {
        switch (r)
        {
            case ExitReason::Signal:     return "signal";
            case ExitReason::StopLoss:   return "stopLoss";
            case ExitReason::TakeProfit: return "takeProfit";
            case ExitReason::Breakeven:  return "breakeven";
            case ExitReason::Trailing:   return "trailing";
            case ExitReason::Indicator:  return "indicator";
        }
        return "signal";
    }

    json tradeFields(const Trade& t)
    {
        json j;
        j["id"] = t.id;
        j["asset"] = t.asset;
        j["direction"] = directionName(t.direction);
        j["quantity"] = t.quantity;
        j["entryTime"] = t.entryTime;
        j["entryPrice"] = t.entryPrice;
        j["stopLoss"] = t.stopLoss ? json(*t.stopLoss) : json(nullptr);
        j["takeProfit"] = t.takeProfit ? json(*t.takeProfit) : json(nullptr);
        j["initialStop"] = t.initialStop ? json(*t.initialStop) : json(nullptr);
        j["swap"] = t.swap;
        return j;
    }

    // Reads a number member; throws if it is there but not a number.
    double numberMember(const json& request, const char* key, double fallback)
    {
        const auto it = request.find(key);
        if (it == request.end())
            return fallback;
        if (!it->is_number())
            throw std::invalid_argument(std::string("\"") + key + "\" must be a number");
        return it->get<double>();
    }
}

RunSpec runSpecFromJson(const json& request)
{
    if (!request.is_object())
        throw std::invalid_argument("a request must be a JSON object");

    for (const auto& item : request.items())
    {
        const std::string& key = item.key();
        if (key != "strategy" && key != "dataset" && key != "params" && key != "cash" && key != "periodsPerYear" && key != "layout")
            throw std::invalid_argument("unknown key in request: " + key);
    }

    RunSpec spec;
    for (const char* key : {"strategy", "dataset"})
    {
        const auto it = request.find(key);
        if (it == request.end())
            throw std::invalid_argument(std::string("missing \"") + key + "\"");
        if (!it->is_string())
            throw std::invalid_argument(std::string("\"") + key + "\" must be text");
    }
    spec.strategy = request.at("strategy").get<std::string>();
    spec.dataset = request.at("dataset").get<std::string>();

    if (const auto it = request.find("params"); it != request.end())
    {
        if (!it->is_object())
            throw std::invalid_argument("\"params\" must be an object");
        for (const auto& item : it->items())
        {
            if (!item.value().is_number())
                throw std::invalid_argument("parameter " + item.key() + " must be a number");
            spec.params[item.key()] = item.value().get<double>();
        }
    }
    spec.cash = numberMember(request, "cash", spec.cash);
    spec.periodsPerYear = numberMember(request, "periodsPerYear", spec.periodsPerYear);
    if (const auto it = request.find("layout"); it != request.end())
    {
        if (!it->is_string())
            throw std::invalid_argument("\"layout\" must be text");
        spec.layout = it->get<std::string>();
    }
    return spec;
}

json toJson(const RunSpec& spec)
{
    json j;
    j["strategy"] = spec.strategy;
    j["dataset"] = spec.dataset;
    j["params"] = json::object();
    for (const auto& [name, value] : spec.params)
        j["params"][name] = value;
    j["cash"] = spec.cash;
    j["periodsPerYear"] = spec.periodsPerYear;
    j["layout"] = spec.layout;
    return j;
}

json toJson(const Metrics& m)
{
    json j;
    j["totalReturn"] = m.totalReturn;
    j["cagr"] = m.cagr;
    j["maxDrawdown"] = m.maxDrawdown;
    j["sharpe"] = m.sharpe;
    j["sortino"] = m.sortino;
    j["mar"] = m.mar;
    j["trades"] = m.trades;
    j["winRate"] = m.winRate;
    j["profitFactor"] = m.profitFactor;
    j["expectancy"] = m.expectancy;
    j["expectancyR"] = m.expectancyR;
    j["tradesWithRisk"] = m.tradesWithRisk;
    j["avgHoldingTime"] = m.avgHoldingTime;
    return j;
}

json toJson(const RunOutput& out)
{
    json j;
    j["spec"] = toJson(out.spec);
    j["metrics"] = toJson(out.metrics);
    j["timestamps"] = out.timestamps;
    j["equity"] = out.result.equityCurve;
    j["finalEquity"] = out.result.finalEquity;

    j["trades"] = json::array();
    for (const ClosedTrade& c : out.result.closedTrades)
        j["trades"].push_back(toJson(c));
    j["openTrades"] = json::array();
    for (const Trade& t : out.result.openTrades)
        j["openTrades"].push_back(toJson(t));
    return j;
}

json toJson(const Trade& trade)
{
    return tradeFields(trade);
}

json toJson(const ClosedTrade& c)
{
    json t = tradeFields(c.trade);
    t["exitTime"] = c.exitTime;
    t["exitPrice"] = c.exitPrice;
    t["reason"] = reasonName(c.reason);
    t["pnl"] = c.pnl;
    return t;
}

json strategiesToJson(const StrategyRegistry& registry)
{
    json list = json::array();
    for (const StrategyInfo& info : registry.list())
    {
        json params = json::array();
        for (const ParamSpec& p : info.params)
            params.push_back({{"name", p.name}, {"default", p.defaultValue}, {"min", p.min}, {"max", p.max},
                              {"integer", p.integer}, {"description", p.description}});
        list.push_back({{"name", info.name}, {"description", info.description}, {"params", params}});
    }
    return list;
}

std::string runJobJson(const StrategyRegistry& registry, const std::string& dataDir, const std::string& requestText)
{
    const json request = json::parse(requestText, nullptr, false);
    if (request.is_discarded())
        throw std::invalid_argument("the request is not valid JSON");
    return toJson(runJob(registry, dataDir, runSpecFromJson(request))).dump();
}
