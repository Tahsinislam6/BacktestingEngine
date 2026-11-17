#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

// One set of named parameter values, e.g. {period: 20, atrMultiple: 1.5}.
class Params
{
public:
    Params() = default;
    Params(std::initializer_list<std::pair<std::string, double>> init);

    bool has(const std::string& name) const;

    // Throws std::invalid_argument if the name is not there.
    double get(const std::string& name) const;

    // Same, rounded to the nearest integer (for periods and other whole numbers).
    int getInt(const std::string& name) const;

    void set(const std::string& name, double value);

    const std::map<std::string, double>& values() const { return values_; }

    bool operator==(const Params&) const = default;

private:
    std::map<std::string, double> values_;
};

// A named range: from, from + step, from + 2*step, ... up to and including `to` when it lands on a step.
struct ParamRange
{
    std::string name;
    double from{};
    double to{};
    double step{1.0};
};

// Every combination of the ranges, in the order of nested loops: the FIRST range changes slowest and the
// LAST one fastest. No ranges gives a single empty Params (one run with nothing to tune).
// Throws std::invalid_argument for an empty or repeated name, a step <= 0 (or NaN), or to < from.
std::vector<Params> expandGrid(const std::vector<ParamRange>& ranges);

// The parameters of one run given as text: "period=26,mult=2.5" (spaces are fine, the order does not matter).
// Every name in `allowed` must be given exactly once, no other name may appear, and each value must be a plain
// number inside its range (from..to, both ends allowed; the step is not enforced). Text for a baseline without
// parameters must be empty. Throws std::invalid_argument naming the problem.
Params parseParams(const std::string& text, const std::vector<ParamRange>& allowed);
