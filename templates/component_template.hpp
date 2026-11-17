// TEMPLATE for a new NNFX component (confirmation, exit indicator or filter). Copy to
// include/components/<name>.hpp (lowercase, digits, underscores) and change the marked places. Nothing else needs
// editing: CMake finds the file, `--confirm <name>` / `--exit-indicator <name>` / `--filter <name>` work, and
// tests/components_test.cpp checks it automatically.
//
// Pick ONE kind:
//   State  state() returns +1 (bullish), -1 (bearish) or 0 (neutral)  -> confirmation or exit indicator
//   Gate   allows() returns true to trade, false to stand aside        -> filter (volatility, volume, ...)
// Component parameters are FIXED (defaults()); they are not searched.
#pragma once

#include <stdexcept>

#include "../components.hpp"

namespace components::NAME            // <- 1. the same as the file name, e.g. components::macd
{
class MyComponent                     // <- 2. the indicator (any class name)
{
public:
    explicit MyComponent(int period) : period_(period)
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

    // State kind: return -1, 0 or +1. (Gate kind: replace with `bool allows() const`.)
    // Throw std::logic_error if not ready. Must not change any state.
    int state() const
    {
        if (!ready())
            throw std::logic_error("not ready yet");
        return 0;
    }

private:
    int period_;
    int seen_{0};
};

struct Spec
{
    using Line = MyComponent;
    // 3. the fixed parameters, how to build one from them, and how many bars until ready()
    static Params defaults() { return {{"period", 14}}; }
    static Line make(const Params& p) { return Line(p.getInt("period")); }
    static std::size_t warmup(const Params& p) { return static_cast<std::size_t>(p.getInt("period")); }
};
}
