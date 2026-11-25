#include "registry.hpp"

#include <cmath>
#include <set>
#include <stdexcept>

void StrategyRegistry::add(StrategyInfo info, StrategyBuilder builder)
{
    if (info.name.empty())
        throw std::invalid_argument("strategy name is empty");
    if (has(info.name))
        throw std::invalid_argument("strategy already registered: " + info.name);
    if (!builder)
        throw std::invalid_argument("strategy " + info.name + " has no builder");

    std::set<std::string> seen;
    for (const ParamSpec& p : info.params)
    {
        if (p.name.empty())
            throw std::invalid_argument("strategy " + info.name + " has a parameter with no name");
        if (!seen.insert(p.name).second)
            throw std::invalid_argument("strategy " + info.name + " repeats parameter " + p.name);
        if (!std::isfinite(p.min) || !std::isfinite(p.max) || !std::isfinite(p.defaultValue))
            throw std::invalid_argument("parameter " + p.name + " has a limit or default that is not a number");
        if (p.min > p.max)
            throw std::invalid_argument("parameter " + p.name + " has min above max");
        if (p.defaultValue < p.min || p.defaultValue > p.max)
            throw std::invalid_argument("parameter " + p.name + " has a default outside min..max");
    }

    infos_.push_back(std::move(info));
    builders_.push_back(std::move(builder));
}

bool StrategyRegistry::has(const std::string& name) const
{
    for (const StrategyInfo& info : infos_)
        if (info.name == name)
            return true;
    return false;
}

const StrategyInfo& StrategyRegistry::find(const std::string& name) const
{
    for (const StrategyInfo& info : infos_)
        if (info.name == name)
            return info;
    throw std::invalid_argument("unknown strategy: " + name);
}

NamedParams StrategyRegistry::resolveParams(const std::string& strategy, const NamedParams& given) const
{
    const StrategyInfo& info = find(strategy);

    for (const auto& entry : given)
    {
        bool known = false;
        for (const ParamSpec& p : info.params)
            known = known || p.name == entry.first;
        if (!known)
            throw std::invalid_argument("strategy " + strategy + " has no parameter " + entry.first);
    }

    NamedParams full;
    for (const ParamSpec& p : info.params)
    {
        const auto it = given.find(p.name);
        const double value = it == given.end() ? p.defaultValue : it->second;
        if (!std::isfinite(value))
            throw std::invalid_argument("parameter " + p.name + " is not a number");
        if (value < p.min || value > p.max)
            throw std::invalid_argument("parameter " + p.name + " must be between " + std::to_string(p.min) + " and " +
                                        std::to_string(p.max));
        if (p.integer && value != std::floor(value))
            throw std::invalid_argument("parameter " + p.name + " must be a whole number");
        full[p.name] = value;
    }
    return full;
}

std::unique_ptr<Strategy> StrategyRegistry::create(const std::string& strategy, const NamedParams& given) const
{
    const NamedParams full = resolveParams(strategy, given);
    for (std::size_t i = 0; i < infos_.size(); ++i)
    {
        if (infos_[i].name != strategy)
            continue;
        std::unique_ptr<Strategy> made = builders_[i](full);
        if (!made)
            throw std::runtime_error("the builder of " + strategy + " returned no strategy");
        return made;
    }
    throw std::invalid_argument("unknown strategy: " + strategy);   // unreachable: resolveParams already checked
}
