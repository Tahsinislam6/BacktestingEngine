// TEMPLATE for a new baseline. Copy to include/baselines/<name>.hpp (lowercase, digits, underscores) and
// change the three marked places. Nothing else needs editing: CMake finds the file, `--baseline <name>` works,
// and tests/baselines_test.cpp checks it automatically.
#pragma once

#include <stdexcept>

#include "../baseline.hpp"

namespace baselines::NAME            // <- 1. the same as the file name, e.g. baselines::hma
{
class MyLine                         // <- 2. the indicator (any class name)
{
public:
    explicit MyLine(int period) : period_(period)
    {
        if (period <= 0)
            throw std::invalid_argument("period must be positive");
    }

    // Called once per bar, oldest first. Only use this bar and earlier ones.
    void update(const Bar& bar)
    {
        (void)bar;
        ++seen_;
    }

    bool ready() const { return seen_ >= period_; }

    // The price of the line. Throw std::logic_error if not ready. Must not change any state.
    double value() const
    {
        if (!ready())
            throw std::logic_error("not ready yet");
        return 0.0;
    }

private:
    int period_;
    int seen_{0};
};

struct Spec
{
    using Line = MyLine;
    // 3. the search ranges (names must match what make() reads), how to build one from a parameter set,
    //    and how many bars until ready()
    static std::vector<ParamRange> ranges() { return {{"period", 5, 100, 1}}; }
    static Line make(const Params& p) { return Line(p.getInt("period")); }
    static std::size_t warmup(const Params& p) { return static_cast<std::size_t>(p.getInt("period")); }
};
}
