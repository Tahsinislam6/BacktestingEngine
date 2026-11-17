#pragma once

#include <concepts>
#include <cstddef>
#include <memory>

#include "indicators/core.hpp"
#include "param_space.hpp"

// NNFX components other than the baseline. A file in include/components/ is one of two kinds:
//
//   State  a direction state per bar: state() is +1 (bullish), -1 (bearish) or 0 (neutral).
//          Used as a CONFIRMATION (must agree with the trade's direction at entry) or as an EXIT indicator
//          (closes a trade when it turns to the opposite state).
//   Gate   allows() is true when trading is allowed (e.g. enough volatility or volume), false to stand aside.
//
// A component has FIXED parameters (defaults()): they are not searched, so a component adds no tuning freedom.
// See CLAUDE.md and templates/component_template.hpp.

template <typename T>
concept StateIndicator = BarIndicator<T> && requires(const T& t)
{
    {t.state()} -> std::convertible_to<int>;
};

template <typename T>
concept GateIndicator = BarIndicator<T> && requires(const T& t)
{
    {t.allows()} -> std::convertible_to<bool>;
};

// What a file in include/components/ provides, as `Spec` in namespace components::<file name>.
template <typename S>
concept StateDefinition = StateIndicator<typename S::Line> && requires(const Params& p)
{
    {S::defaults()} -> std::convertible_to<Params>;
    {S::make(p)} -> std::same_as<typename S::Line>;
    {S::warmup(p)} -> std::convertible_to<std::size_t>;
};

template <typename S>
concept GateDefinition = GateIndicator<typename S::Line> && requires(const Params& p)
{
    {S::defaults()} -> std::convertible_to<Params>;
    {S::make(p)} -> std::same_as<typename S::Line>;
    {S::warmup(p)} -> std::convertible_to<std::size_t>;
};

// The same, with the type hidden (so the strategy and the tools can use any of them).
class AnyState
{
public:
    virtual ~AnyState() = default;
    virtual void update(const Bar& bar) = 0;
    virtual bool ready() const = 0;
    virtual int state() const = 0;                       // -1, 0 or +1
    virtual std::size_t warmupBars() const = 0;
    virtual std::unique_ptr<AnyState> clone() const = 0;
};

class AnyGate
{
public:
    virtual ~AnyGate() = default;
    virtual void update(const Bar& bar) = 0;
    virtual bool ready() const = 0;
    virtual bool allows() const = 0;
    virtual std::size_t warmupBars() const = 0;
    virtual std::unique_ptr<AnyGate> clone() const = 0;
};

// The components a strategy uses (any may be empty). Prototypes are never updated, only cloned, so options
// holding them can be copied and shared between threads.
struct NnfxComponents
{
    std::shared_ptr<const AnyState> confirmation;
    std::shared_ptr<const AnyState> exitIndicator;
    std::shared_ptr<const AnyGate> filter;
};
