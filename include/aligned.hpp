#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bar.hpp"

struct Snapshot
{
    std::int64_t timestamp{};
    std::vector<std::optional<Bar>> bars;
};

struct AlignedData
{
    std::vector<std::string> symbols;
    std::vector<Snapshot> snapshots;
};

AlignedData alignByDay(const std::vector<BarSeries>& series)