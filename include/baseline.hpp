#pragma once

#include <concepts>
#include <cstddef>
#include <vector>

#include "indicators/core.hpp"
#include "param_space.hpp"

// A baseline is a line that follows price: update(bar), ready(), value() (the price of the line).
template <typename T>
concept BaselineLine = BarIndicator<T> && requires(const T& t)
{
    {t.value()} -> std::convertible_to<double>;
};

// What a file in include/baselines/ must provide, as a struct called `Spec` in namespace
// baselines::<file name>. See CLAUDE.md and templates/baseline_template.hpp.
template <typename S>
concept BaselineDefinition = BaselineLine<typename S::Line> && requires(const Params& p)
{
    {S::ranges()} -> std::convertible_to<std::vector<ParamRange>>;    // what to search by default
    {S::make(p)} -> std::same_as<typename S::Line>;                    // build the indicator from one set
    {S::warmup(p)} -> std::convertible_to<std::size_t>;               // bars until it is ready
};
