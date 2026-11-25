#include "sweep_job_json.hpp"

#include <cmath>
#include <stdexcept>

using nlohmann::json;

namespace
{
    // A number that is not finite becomes null, so the text never has "nan" or "inf" and two equal results compare equal.
    json number(double x)
    {
        return std::isfinite(x) ? json(x) : json(nullptr);
    }

    json numbers(const std::vector<double>& xs)
    {
        json list = json::array();
        for (double x : xs)
            list.push_back(number(x));
        return list;
    }

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

    double real(const json& object, const char* key)
    {
        const json& v = need(object, key);
        if (!v.is_number())
            throw std::invalid_argument(std::string("\"") + key + "\" must be a number");
        return v.get<double>();
    }

    int whole(const json& object, const char* key)
    {
        const json& v = need(object, key);
        if (!v.is_number_integer())
            throw std::invalid_argument(std::string("\"") + key + "\" must be a whole number");
        return v.get<int>();
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

    json densityJson(const std::vector<double>& values, double median, double p95, std::size_t atLeast)
    {
        return {{"median", number(median)}, {"p95", number(p95)}, {"atLeastReal", atLeast}, {"values", numbers(values)}};
    }
}

SweepSpec sweepSpecFromJson(const json& request)
{
    if (!request.is_object())
        throw std::invalid_argument("a request must be a JSON object");
    onlyKeys(request, {"strategy", "dataset", "params", "axes", "metric", "threshold", "radius", "minTrades", "nullRuns",
                       "block", "cash", "periodsPerYear", "note"}, "request");

    SweepSpec spec;
    spec.strategy = text(request, "strategy");
    spec.dataset = text(request, "dataset");
    spec.metric = text(request, "metric");
    spec.note = text(request, "note");
    spec.threshold = real(request, "threshold");
    spec.radius = whole(request, "radius");
    spec.minTrades = whole(request, "minTrades");
    spec.nullRuns = whole(request, "nullRuns");
    spec.block = whole(request, "block");
    if (request.contains("cash"))
        spec.cash = real(request, "cash");
    if (request.contains("periodsPerYear"))
        spec.periodsPerYear = real(request, "periodsPerYear");

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

    const json& axes = need(request, "axes");
    if (!axes.is_array())
        throw std::invalid_argument("\"axes\" must be a list");
    for (const json& axis : axes)
    {
        if (!axis.is_object())
            throw std::invalid_argument("each axis must be an object");
        onlyKeys(axis, {"param", "from", "to", "step"}, "an axis");
        spec.axes.push_back({text(axis, "param"), real(axis, "from"), real(axis, "to"), real(axis, "step")});
    }
    return spec;
}

json toJson(const SweepSpec& spec)
{
    json axes = json::array();
    for (const AxisSpec& a : spec.axes)
        axes.push_back({{"param", a.param}, {"from", a.from}, {"to", a.to}, {"step", a.step}});
    json params = json::object();
    for (const auto& [name, value] : spec.params)
        params[name] = value;
    return {{"strategy", spec.strategy}, {"dataset", spec.dataset}, {"params", params}, {"axes", axes},
            {"metric", spec.metric}, {"threshold", number(spec.threshold)}, {"radius", spec.radius},
            {"minTrades", spec.minTrades}, {"nullRuns", spec.nullRuns}, {"block", spec.block}, {"cash", spec.cash},
            {"periodsPerYear", spec.periodsPerYear}, {"note", spec.note}};
}

json toJson(const SweepOutput& out)
{
    json axes = json::array();
    for (std::size_t a = 0; a < out.axisValues.size(); ++a)
        axes.push_back({{"param", out.spec.axes[a].param}, {"values", numbers(out.axisValues[a])}});

    json valid = json::array();
    for (bool v : out.valid)
        valid.push_back(v);

    json j;
    j["kind"] = "sweep";
    j["spec"] = toJson(out.spec);
    j["bars"] = out.bars;
    j["cells"] = out.cells;
    j["ideasTried"] = out.ideasTried;
    j["axes"] = axes;
    j["grid"] = {{"trades", out.trades}, {"values", numbers(out.values)}, {"valid", valid}};
    j["real"] = {{"validCells", out.validCells}, {"passDensity", number(out.passDensity)},
                 {"plateauDensity", number(out.plateauDensity)}, {"peakOverNeighbours", number(out.peakOverNeighbours)}};
    j["shuffled"] = {{"runs", out.spec.nullRuns}, {"block", out.spec.block},
                     {"passDensity", densityJson(out.nullPass, out.nullPassMedian, out.nullPass95, out.nullPassAtLeast)},
                     {"plateauDensity", densityJson(out.nullPlateau, out.nullPlateauMedian, out.nullPlateau95, out.nullPlateauAtLeast)}};
    return j;
}

std::string runSweepJson(const StrategyRegistry& registry, const std::string& dataDir, const std::string& requestText)
{
    const json request = json::parse(requestText, nullptr, false);
    if (request.is_discarded())
        throw std::invalid_argument("the request is not valid JSON");
    return toJson(runSweep(registry, dataDir, sweepSpecFromJson(request))).dump();
}
