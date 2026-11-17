#include "nnfx_setup.hpp"

#include <stdexcept>

namespace
{
    const ComponentInfo& lookup(const std::string& name, ComponentInfo::Kind kind, const char* role)
    {
        const ComponentInfo* c = findComponent(name);
        if (!c)
            throw std::invalid_argument("unknown component '" + name + "' for " + role + " (available: " + componentNames() + ")");
        if (c->kind != kind)
            throw std::invalid_argument("component '" + name + "' cannot be used as " + role + ": it is a " +
                                        (c->kind == ComponentInfo::Kind::State ? "state" : "gate") +
                                        " component, and " + role + " needs a " +
                                        (kind == ComponentInfo::Kind::State ? "state" : "gate") + " component");
        return *c;
    }
}

NnfxOptions makeNnfxOptions(const CliOptions& cli)
{
    NnfxOptions o;
    o.exitOnCross = !cli.noCrossExit && !cli.screen;
    o.confirmationWindow = cli.confirmWindow;
    o.oneCandleRule = cli.oneCandle;
    o.continuation = cli.continuation;
    o.maxOpenRisk = cli.maxOpenRisk;
    o.maxPerCurrency = cli.maxPerCurrency;

    if (!cli.confirm.empty())
    {
        const ComponentInfo& c = lookup(cli.confirm, ComponentInfo::Kind::State, "--confirm");
        o.components.confirmation = c.makeState(c.defaults);
    }
    if (!cli.exitIndicator.empty())
    {
        const ComponentInfo& c = lookup(cli.exitIndicator, ComponentInfo::Kind::State, "--exit-indicator");
        o.components.exitIndicator = c.makeState(c.defaults);
    }
    if (!cli.filter.empty())
    {
        const ComponentInfo& c = lookup(cli.filter, ComponentInfo::Kind::Gate, "--filter");
        o.components.filter = c.makeGate(c.defaults);
    }
    return o;
}
