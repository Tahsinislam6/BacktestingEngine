#include "param_space.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
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

namespace
{
    std::string trimmed(const std::string& s)
    {
        const std::size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos)
            return "";
        return s.substr(a, s.find_last_not_of(" \t") - a + 1);
    }

    std::string shown(double v)
    {
        std::ostringstream out;
        out << v;
        return out.str();
    }

    std::string namesOf(const std::vector<ParamRange>& ranges)
    {
        std::string out;
        for (const ParamRange& r : ranges)
            out += (out.empty() ? "" : ", ") + r.name;
        return out;
    }
}

Params parseParams(const std::string& text, const std::vector<ParamRange>& allowed)
{
    Params out;
    std::size_t start = 0;
    if (!trimmed(text).empty())
        while (true)
        {
            const std::size_t comma = text.find(',', start);
            const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            const std::size_t eq = item.find('=');
            const std::string name = eq == std::string::npos ? "" : trimmed(item.substr(0, eq));
            const std::string value = eq == std::string::npos ? "" : trimmed(item.substr(eq + 1));
            if (name.empty() || value.empty())
                throw std::invalid_argument("parameters are written name=value, separated by commas (got '" + trimmed(item) + "')");

            const auto range = std::find_if(allowed.begin(), allowed.end(), [&](const ParamRange& r) { return r.name == name; });
            if (range == allowed.end())
                throw std::invalid_argument("unknown parameter '" + name + "' (this baseline has: " +
                                            (allowed.empty() ? "none" : namesOf(allowed)) + ")");
            if (out.has(name))
                throw std::invalid_argument("parameter '" + name + "' is given twice");

            std::size_t used = 0;
            double v = 0;
            try { v = std::stod(value, &used); }
            catch (const std::exception&) { used = 0; }
            if (used != value.size() || !std::isfinite(v))
                throw std::invalid_argument("parameter '" + name + "' needs a number (got '" + value + "')");
            if (v < range->from || v > range->to)
                throw std::invalid_argument("parameter '" + name + "' must be between " + shown(range->from) +
                                            " and " + shown(range->to) + " (got " + value + ")");
            out.set(name, v);

            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }

    std::string missing;
    for (const ParamRange& r : allowed)
        if (!out.has(r.name))
            missing += (missing.empty() ? "" : ", ") + r.name;
    if (!missing.empty())
        throw std::invalid_argument("missing parameter(s): " + missing);
    return out;
}
