#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "aligned.hpp"
#include "components.hpp"

// A random stand-in for a real component, for the component control (does the real one add anything over a
// random one that behaves just as often and just as persistently?).
//
// The stand-in is a Markov chain: its states occur with the same frequencies as the real component's did on the
// data, and it moves between them with the same probabilities, so it flips as seldom or as often as the real one.
// It knows nothing about prices.

// Frequencies and transitions of a component, read off a run over some data.
//   State component: 3 states, index 0 = -1, 1 = 0, 2 = +1.   Gate: 2 states, index 0 = blocked, 1 = allowed.
// transition[i][j] = share of the bars in state i that were followed (on the same asset) by state j; a state never
// seen leaving falls back to the frequencies. Only bars after the warm-up count.
struct ChainStats
{
    std::size_t observations{0};
    std::vector<double> frequency;                        // adds up to 1
    std::vector<std::vector<double>> transition;          // each row adds up to 1
};

// Runs a copy of `prototype` over every asset of `data` (the prototype itself is not touched). Never ready on the
// data (no observations) gives uniform statistics.
ChainStats measureState(const AnyState& prototype, const AlignedData& data);
ChainStats measureGate(const AnyGate& prototype, const AlignedData& data);

// The stand-ins. Ready after `warmupBars` bars like the real one; the chain starts from the frequencies on the
// first ready bar. Each clone() is its own stream, derived from `seed` and the number of clones made so far, so a
// prototype gives repeatable but different streams to different assets. Use a fresh prototype per run.
std::shared_ptr<AnyState> randomState(const ChainStats& stats, std::size_t warmupBars, std::uint64_t seed);
std::shared_ptr<AnyGate> randomGate(const ChainStats& stats, std::size_t warmupBars, std::uint64_t seed);
