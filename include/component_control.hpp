#pragma once

#include <string>
#include <vector>

#include "control.hpp"
#include "random_component.hpp"

// The component control: does a real confirmation / exit indicator / filter add anything over a random one that
// behaves just as often and just as persistently (random_component.hpp)? One role at a time; the other
// components stay as they are. Over the same data, with the same costs:
//
//   real      the options as given
//   without   the same options without this role's component
//   random    this role's component replaced by a random stand-in, one run per seed (1, 2, ...)
//
// The real arm is ranked among the random arms. The frequencies and persistence of the stand-in are measured on
// the data being tested, which uses no prices, only what the component said.

enum class ComponentRole { Confirmation, ExitIndicator, Filter };

const char* roleName(ComponentRole role);       // "confirmation", "exit indicator", "filter"

// The building blocks of the control, on a bare set of components (so other tools can reuse them):
//   hasRole / withoutRole   is there a component in this role / the same set without it
//   measureStandIn          what a random stand-in for the role's component must copy (frequencies and persistence,
//                           measured on `data`), plus a text describing it
//   withStandIn             the set with the role's component replaced by a fresh stand-in of that seed
struct StandIn
{
    ChainStats stats;
    std::size_t warmup{};
    bool isGate{false};
    std::string behaviour;              // e.g. "+1 48%, -1 47%, neutral 5%; keeps its state 93% of bars"
};

bool hasRole(const NnfxComponents& components, ComponentRole role);
NnfxComponents withoutRole(NnfxComponents components, ComponentRole role);
StandIn measureStandIn(const NnfxComponents& components, ComponentRole role, const AlignedData& data);   // throws std::invalid_argument if there is no such component
NnfxComponents withStandIn(NnfxComponents components, ComponentRole role, const StandIn& stand, std::uint64_t seed);

struct ComponentControlResult
{
    ArmResult real;
    ArmResult without;
    std::vector<ArmResult> random;
    double sharpeRank{};                // share of random runs the real component beats
    double expectancyRank{};
    double totalRRank{};
    std::string behaviour;              // what the stand-ins matched, e.g. "+1 48%, -1 47%, neutral 5%; keeps its state 93%"
};

// Throws std::invalid_argument if seeds < 1 or `options` has no component in `role`.
// The numbers do not depend on grid.threads.
ComponentControlResult runComponentControl(const AlignedData& data, const Params& params, const NnfxFactoryMaker& make,
                                           const NnfxOptions& options, ComponentRole role, int seeds,
                                           const GridOptions& grid = GridOptions{});

// `label` names the data (e.g. "out-of-sample"), `componentName` the component.
std::string formatComponentControl(const std::string& label, const std::string& componentName, ComponentRole role,
                                   const ComponentControlResult& result);
