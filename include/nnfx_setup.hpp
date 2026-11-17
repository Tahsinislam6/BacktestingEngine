#pragma once

#include "cli.hpp"
#include "components_registry.hpp"
#include "nnfx_strategy.hpp"

// The strategy options for a run: the named components looked up in the registry (with their default parameters)
// and the other nnfx settings copied from the command line. The cross-back exit is on unless --no-cross-exit, and
// always off for the screen.
// Throws std::invalid_argument for an unknown component or one of the wrong kind (confirmation and exit
// indicator need a State, the filter needs a Gate); the message lists what is available.
NnfxOptions makeNnfxOptions(const CliOptions& cli);
