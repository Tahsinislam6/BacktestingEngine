#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "strategy.hpp"

// One setting of a strategy, described well enough for a web form to be built from it: a name, a default, the allowed
// range, whether it must be a whole number, and a short help text.
struct ParamSpec
{
    std::string name;
    double defaultValue{};
    double min{};
    double max{};
    bool integer{false};
    std::string description;
};

// What a caller may know about a strategy without running it.
struct StrategyInfo
{
    std::string name;           // the id used in requests, e.g. "buy_and_hold"
    std::string description;
    std::vector<ParamSpec> params;
};

// Parameter name -> value. Every parameter is a number (a whole number is a double with no fraction).
using NamedParams = std::map<std::string, double>;

// Builds a FRESH strategy from a COMPLETE set of parameters (the registry has already checked them and filled in defaults).
using StrategyBuilder = std::function<std::unique_ptr<Strategy>(const NamedParams&)>;

// The table of strategies a job can name. The engine ships one (buy_and_hold, see buy_and_hold.hpp); a private project
// adds its own strategies to a registry in its own main() and hands the registry to the server.
class StrategyRegistry
{
    public:
        // Throws std::invalid_argument for an empty or repeated strategy name, a missing builder, an empty or repeated
        // parameter name, a parameter with min > max or a default outside min..max, or a non-finite limit or default.
        void add(StrategyInfo info, StrategyBuilder builder);

        const std::vector<StrategyInfo>& list() const {return infos_;}
        bool has(const std::string& name) const;

        // The full set of parameter values: what the caller gave plus the default of everything left out.
        // Throws std::invalid_argument for an unknown strategy, an unknown parameter name (a typo is an error, not
        // ignored), a value that is not finite, outside min..max, or fractional for an integer parameter.
        NamedParams resolveParams(const std::string& strategy, const NamedParams& given) const;

        // resolveParams, then the builder. Throws std::runtime_error if the builder returns no strategy.
        std::unique_ptr<Strategy> create(const std::string& strategy, const NamedParams& given) const;

    private:
        const StrategyInfo& find(const std::string& name) const;

        std::vector<StrategyInfo> infos_;
        std::vector<StrategyBuilder> builders_;     // same order as infos_
};
