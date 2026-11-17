#include "param_space.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

Params::Params(std::initializer_list<std::pair<std::string, double>> init)
{
    for (const auto& [name, value] : init)
        values_[name] = value;
}

bool Params::has(const std::string& name) const
{
    return values_.find(name) != values_.end();
}

double Params::get(const std::string& name) const
{
    auto it = values_.find(name);
    if (it == values_.end())
        throw std::invalid_argument("unknown parameter: " + name);
    return it->second;
}

int Params::getInt(const std::string& name) const
{
    return static_cast<int>(std::lround(get(name)));
}

void Params::set(const std::string& name, double value)
{
    values_[name] = value;
}

namespace
{
    std::vector<double> valuesOf(const ParamRange& r)
    {
        if (!(r.step > 0.0))
            throw std::invalid_argument("step must be positive for " + r.name);
        if (!(r.to >= r.from))
            throw std::invalid_argument("'to' must not be below 'from' for " + r.name);

        // The small epsilon keeps 0..1 step 0.1 from losing its last value to rounding.
        const std::size_t count = static_cast<std::size_t>(std::floor((r.to - r.from) / r.step + 1e-9)) + 1;

        std::vector<double> values;
        for (std::size_t i = 0; i < count; ++i)
            values.push_back(std::min(r.from + static_cast<double>(i) * r.step, r.to));
        return values;
    }
}

std::vector<Params> expandGrid(const std::vector<ParamRange>& ranges)
{
    std::set<std::string> seen;
    std::vector<std::vector<double>> options;
    for (const ParamRange& r : ranges)
    {
        if (r.name.empty())
            throw std::invalid_argument("a parameter needs a name");
        if (!seen.insert(r.name).second)
            throw std::invalid_argument("parameter named twice: " + r.name);
        options.push_back(valuesOf(r));
    }

    // Build the combinations one range at a time: each existing combination is extended by every value.
    std::vector<Params> grid{Params{}};
    for (std::size_t r = 0; r < ranges.size(); ++r)
    {
        std::vector<Params> next;
        next.reserve(grid.size() * options[r].size());
        for (const Params& base : grid)
        {
            for (double v : options[r])
            {
                Params p = base;
                p.set(ranges[r].name, v);
                next.push_back(p);
            }
        }
        grid = std::move(next);
    }
    return grid;
}
